/*
** Copyright (c) 2020 LunarG, Inc.
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

        
#define _CRT_SECURE_NO_WARNINGS

#include "file_optimizer.h"

#include "format/format_util.h"
#include "util/logging.h"
#include "util/platform.h"

#include <cassert>

GFXRECON_BEGIN_NAMESPACE(gfxrecon)

FileOptimizer::FileOptimizer(const std::unordered_set<format::HandleId>& unreferenced_ids) :
    unreferenced_ids_(unreferenced_ids)
{
    GFXRECON_LOG_INFO("unreferenced_ids.size() == %zd\n", unreferenced_ids.size());
}

FileOptimizer::FileOptimizer(std::unordered_set<format::HandleId>&& unreferenced_ids) :
    unreferenced_ids_(std::move(unreferenced_ids))
{
    GFXRECON_LOG_INFO("unreferenced_ids_.size() == %zd\n", unreferenced_ids_.size());
}

bool FileOptimizer::ProcessMetaData(const format::BlockHeader& block_header, format::MetaDataId meta_data_id)
{
    format::MetaDataType meta_data_type = format::GetMetaDataType(meta_data_id);
    if (meta_data_type == format::MetaDataType::kInitBufferCommand)
    {
        return FilterInitBufferMetaData(block_header, meta_data_id);
    }
    else if (meta_data_type == format::MetaDataType::kInitImageCommand)
    {
        return FilterInitImageMetaData(block_header, meta_data_id);
    }
    else
    {
        // Copy the meta data block, if it was not filtered.
        return FileTransformer::ProcessMetaData(block_header, meta_data_id);
    }
}

int which = 0;

bool FileOptimizer::FilterInitBufferMetaData(const format::BlockHeader& block_header, format::MetaDataId meta_data_id)
{
    assert(format::GetMetaDataType(meta_data_id) == format::MetaDataType::kInitBufferCommand);

    format::InitBufferCommandHeader header;

    bool success = ReadBytes(&header.thread_id, sizeof(header.thread_id));
    success      = success && ReadBytes(&header.device_id, sizeof(header.device_id));
    success      = success && ReadBytes(&header.buffer_id, sizeof(header.buffer_id));
    success      = success && ReadBytes(&header.data_size, sizeof(header.data_size));

    if (success)
    {
        // Total number of bytes remaining to be read for the current block.
        uint64_t unread_bytes = block_header.size - (sizeof(header) - sizeof(block_header));

        int startkeeping = atoi(getenv("START_KEEPING"));
        int endkeeping = atoi(getenv("END_KEEPING"));

        bool is_unreferenced = (unreferenced_ids_.find(header.buffer_id) != unreferenced_ids_.end());
        bool force_keep = (which >= startkeeping) && (which <= endkeeping);

        // If the buffer is in the unused list, omit its initialization data from the file.
        if (is_unreferenced && !force_keep)
        {
            if (!SkipBytes(unread_bytes))
            {
                HandleBlockReadError(kErrorSeekingFile, "Failed to skip init buffer data meta-data block data");
                    return false;
            }
        }
        else
        {
            if(is_unreferenced && (startkeeping == endkeeping)) {
                printf("culprit is buffer meta data ID %" PRIu64 "\n", header.buffer_id);
            }
            // Copy the block from the input file to the output file.
            header.meta_header.block_header   = block_header;
            header.meta_header.meta_data_id   = meta_data_id;

            if (!WriteBytes(&header, sizeof(header)))
            {
                HandleBlockWriteError(kErrorReadingBlockHeader,
                                      "Failed to write init buffer data meta-data block header");
                return false;
            }

            if (!CopyBytes(unread_bytes))
            {
                HandleBlockCopyError(kErrorCopyingBlockData, "Failed to copy init buffer data meta-data block data");
                return false;
            }
        }
        if (is_unreferenced) {
            which++;
        }
    }
    else
    {
        HandleBlockReadError(kErrorReadingBlockHeader, "Failed to read init buffer data meta-data block header");
        return false;
    }

    return true;
}

bool FileOptimizer::FilterInitImageMetaData(const format::BlockHeader& block_header, format::MetaDataId meta_data_id)
{
    assert(format::GetMetaDataType(meta_data_id) == format::MetaDataType::kInitImageCommand);

    format::InitImageCommandHeader header;
    std::vector<uint64_t>          level_sizes;

    bool success = ReadBytes(&header.thread_id, sizeof(header.thread_id));
    success      = success && ReadBytes(&header.device_id, sizeof(header.device_id));
    success      = success && ReadBytes(&header.image_id, sizeof(header.image_id));
    success      = success && ReadBytes(&header.data_size, sizeof(header.data_size));
    success      = success && ReadBytes(&header.aspect, sizeof(header.aspect));
    success      = success && ReadBytes(&header.layout, sizeof(header.layout));
    success      = success && ReadBytes(&header.level_count, sizeof(header.level_count));

    printf("InitImageCommand for image ID %" PRIu64 " (%" PRIx64 ")\n", header.image_id, header.image_id);

    if (success)
    {
        // Total number of bytes remaining to be read for the current block.
        uint64_t unread_bytes = block_header.size - (sizeof(header) - sizeof(block_header));

        int startkeeping = atoi(getenv("START_KEEPING"));
        int endkeeping = atoi(getenv("END_KEEPING"));

        bool is_unreferenced = (unreferenced_ids_.find(header.image_id) != unreferenced_ids_.end());
        bool force_keep = (which >= startkeeping) && (which <= endkeeping);

        // If the image is in the unused list, omit its initialization data from the file.
        if (is_unreferenced && !force_keep)
        {
            if (!SkipBytes(unread_bytes))
            {
                HandleBlockReadError(kErrorSeekingFile, "Failed to skip init image data meta-data block data");
                return false;
            }
        }
        else
        {
            if(is_unreferenced && (startkeeping == endkeeping)) {
                printf("culprit is image ID %" PRIu64 " (%" PRIx64 ")\n", header.image_id, header.image_id);
            }

            // Copy the block from the input file to the output file.
            header.meta_header.block_header   = block_header;
            header.meta_header.meta_data_id   = meta_data_id;

            if (!WriteBytes(&header, sizeof(header)))
            {
                HandleBlockWriteError(kErrorReadingBlockHeader,
                                      "Failed to write init image data meta-data block header");
                return false;
            }

            if (!CopyBytes(unread_bytes))
            {
                HandleBlockCopyError(kErrorCopyingBlockData, "Failed to copy init image data meta-data block data");
                return false;
            }
        }
        if (is_unreferenced) {
            which++;
        }
    }
    else
    {
        HandleBlockReadError(kErrorReadingBlockHeader, "Failed to read init image data meta-data block header");
        return false;
    }

    return true;
}

GFXRECON_END_NAMESPACE(gfxrecon)
