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

#ifndef GFXRECON_UTIL_SCAN_CHECKSUM_TRACKER_H
#define GFXRECON_UTIL_SCAN_CHECKSUM_TRACKER_H

#include "util/defines.h"
#include "util/scan_checksum.h"
#include "util/page_status_tracker.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(util)

class ScanChecksumTracker
{
  public:
    // Callback for processing modified memory.  Parameters are: memory ID, pointer to start of modified range,
    // offset from the mapped memory pointer to the modified range, and size of the modified range.
    typedef std::function<void(uint64_t, void*, size_t, size_t)> ModifiedMemoryFunc;

  public:
    static void Create();

    static void Destroy();

    static ScanChecksumTracker* Get() { return instance_; }

    // Begin tracking a mapped memory region. Computes initial checksums for all pages.
    // Returns the mapped_memory pointer unchanged (no shadow memory is used).
    void* AddTrackedMemory(uint64_t memory_id, void* mapped_memory, size_t mapped_offset, size_t mapped_range);

    // Stop tracking a mapped memory region.
    void RemoveTrackedMemory(uint64_t memory_id);

    // Scan a single tracked memory region for changes.  For each contiguous range of dirty pages,
    // invokes handle_modified.  Updates stored checksums after scanning.
    void ProcessMemoryEntry(uint64_t memory_id, const ModifiedMemoryFunc& handle_modified);

    // Scan all tracked memory regions for changes.
    void ProcessMemoryEntries(const ModifiedMemoryFunc& handle_modified);

  private:
    struct TrackedRegion
    {
        TrackedRegion(void*   mm,
                      size_t  mo,
                      size_t  mr,
                      size_t  page_size,
                      size_t  tp,
                      size_t  lss,
                      ChecksumFunc checksum_fn);

        void*                 mapped_memory;    // Original driver-provided mapped pointer (includes mapped_offset).
        size_t                mapped_offset;    // Offset within the VkDeviceMemory allocation.
        size_t                mapped_range;     // Size of the mapped region.
        size_t                page_size;        // Page size used for scanning.
        size_t                total_pages;      // Number of pages covering the mapped range.
        size_t                last_segment_size;// Size of the last page (may be less than page_size).
        std::vector<uint32_t> checksums;        // One checksum per page.
        uint32_t              ref_count;        // For duplicate map tracking.
    };

    typedef std::unordered_map<uint64_t, TrackedRegion> TrackedRegionMap;

    void ScanAndProcess(uint64_t memory_id, TrackedRegion& region, const ModifiedMemoryFunc& handle_modified);

    size_t GetPageSegmentSize(const TrackedRegion& region, size_t page_index) const;

    ScanChecksumTracker();

    static ScanChecksumTracker* instance_;
    TrackedRegionMap            tracked_regions_;
    std::mutex                  lock_;
    size_t                      page_size_;
    ChecksumFunc                checksum_func_;
};

GFXRECON_END_NAMESPACE(util)
GFXRECON_END_NAMESPACE(gfxrecon)

#endif // GFXRECON_UTIL_SCAN_CHECKSUM_TRACKER_H
