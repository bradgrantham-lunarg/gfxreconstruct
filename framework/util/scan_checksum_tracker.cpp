/*
** Copyright (c) 2025 LunarG, Inc.
**
** Permission is hereby granted, free of charge, to any person obtaining a
** copy of this software and associated documentation files (the "Software"),
** to deal in the Software without restriction, including without limitation
** the rights to use, copy, modify, merge, publish, distribute, sublicense,
** and/or sell copies of the Software, and to permit persons to whom the
** Software is furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in
** all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
** FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
** DEALINGS IN THE SOFTWARE.
*/

#include "util/scan_checksum_tracker.h"
#include "util/logging.h"
#include "util/platform.h"

#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cstring>

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(util)

ScanChecksumTracker* ScanChecksumTracker::instance_ = nullptr;

ScanChecksumTracker::TrackedRegion::TrackedRegion(void*        mm,
                                                  size_t       mo,
                                                  size_t       mr,
                                                  size_t       ps,
                                                  size_t       tp,
                                                  size_t       lss,
                                                  ChecksumFunc checksum_fn) :
    mapped_memory(mm),
    mapped_offset(mo), mapped_range(mr), page_size(ps), total_pages(tp), last_segment_size(lss),
    checksums(tp, 0), ref_count(0)
{
    // Compute initial checksums for all pages.
    const uint8_t* base = static_cast<const uint8_t*>(mapped_memory);
    for (size_t i = 0; i < total_pages; ++i)
    {
        size_t segment_size = (i == total_pages - 1) ? last_segment_size : page_size;
        checksums[i]        = checksum_fn(base + (i * page_size), segment_size);
    }
}

void ScanChecksumTracker::Create()
{
    if (instance_ == nullptr)
    {
        instance_ = new ScanChecksumTracker();
    }
}

void ScanChecksumTracker::Destroy()
{
    delete instance_;
    instance_ = nullptr;
}

void* ScanChecksumTracker::AddTrackedMemory(uint64_t memory_id,
                                            void*    mapped_memory,
                                            size_t   mapped_offset,
                                            size_t   mapped_range)
{
    std::lock_guard<std::mutex> guard(lock_);

    auto entry = tracked_regions_.find(memory_id);
    if (entry != tracked_regions_.end())
    {
        // Same memory mapped again (duplicate map). Increment ref count.
        ++entry->second.ref_count;
        return mapped_memory;
    }

    size_t total_pages      = (mapped_range + page_size_ - 1) / page_size_;
    size_t last_segment_size = mapped_range - ((total_pages - 1) * page_size_);

    auto result = tracked_regions_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(memory_id),
        std::forward_as_tuple(mapped_memory, mapped_offset, mapped_range, page_size_, total_pages, last_segment_size,
                              checksum_func_));
    result.first->second.ref_count = 1;

    GFXRECON_LOG_DEBUG("ScanChecksumTracker: Tracking memory %" PRIu64 " (%zu bytes, %zu pages)",
                       memory_id,
                       mapped_range,
                       total_pages);

    // Return the original pointer — no shadow memory.
    return mapped_memory;
}

void ScanChecksumTracker::RemoveTrackedMemory(uint64_t memory_id)
{
    std::lock_guard<std::mutex> guard(lock_);

    auto entry = tracked_regions_.find(memory_id);
    if (entry != tracked_regions_.end())
    {
        if (--entry->second.ref_count == 0)
        {
            tracked_regions_.erase(entry);
        }
    }
}

void ScanChecksumTracker::ProcessMemoryEntry(uint64_t memory_id, const ModifiedMemoryFunc& handle_modified)
{
    std::lock_guard<std::mutex> guard(lock_);

    auto entry = tracked_regions_.find(memory_id);
    if (entry != tracked_regions_.end())
    {
        ScanAndProcess(memory_id, entry->second, handle_modified);
    }
}

void ScanChecksumTracker::ProcessMemoryEntries(const ModifiedMemoryFunc& handle_modified)
{
    std::lock_guard<std::mutex> guard(lock_);

    for (auto& entry : tracked_regions_)
    {
        ScanAndProcess(entry.first, entry.second, handle_modified);
    }
}

size_t ScanChecksumTracker::GetPageSegmentSize(const TrackedRegion& region, size_t page_index) const
{
    if (page_index == region.total_pages - 1)
    {
        return region.last_segment_size;
    }
    return region.page_size;
}

void ScanChecksumTracker::ScanAndProcess(uint64_t              memory_id,
                                         TrackedRegion&        region,
                                         const ModifiedMemoryFunc& handle_modified)
{
    const uint8_t* base = static_cast<const uint8_t*>(region.mapped_memory);

    // Scan all pages, computing new checksums and finding dirty ranges.
    // Coalesce contiguous dirty pages into single callbacks, matching the behavior of PageGuardManager::ProcessEntry.
    size_t dirty_start = 0;
    bool   in_dirty    = false;

    for (size_t i = 0; i < region.total_pages; ++i)
    {
        size_t   segment_size = GetPageSegmentSize(region, i);
        uint32_t new_checksum = checksum_func_(base + (i * region.page_size), segment_size);

        if (new_checksum != region.checksums[i])
        {
            // Page is dirty.
            region.checksums[i] = new_checksum;

            if (!in_dirty)
            {
                dirty_start = i;
                in_dirty    = true;
            }
        }
        else
        {
            if (in_dirty)
            {
                // Emit the dirty range [dirty_start, i).
                size_t range_offset = dirty_start * region.page_size;
                size_t range_end    = (i - 1) * region.page_size + GetPageSegmentSize(region, i - 1);
                size_t range_size   = range_end - range_offset;

                handle_modified(
                    memory_id,
                    const_cast<void*>(static_cast<const void*>(base + range_offset)),
                    range_offset,
                    range_size);

                in_dirty = false;
            }
        }
    }

    // Handle trailing dirty range.
    if (in_dirty)
    {
        size_t range_offset = dirty_start * region.page_size;
        size_t range_size   = region.mapped_range - range_offset;

        handle_modified(
            memory_id,
            const_cast<void*>(static_cast<const void*>(base + range_offset)),
            range_offset,
            range_size);
    }
}

ScanChecksumTracker::ScanChecksumTracker() :
    page_size_(util::platform::GetSystemPageSize()), checksum_func_(SelectBestChecksum())
{}

GFXRECON_END_NAMESPACE(util)
GFXRECON_END_NAMESPACE(gfxrecon)
