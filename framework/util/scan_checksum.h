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

#ifndef GFXRECON_UTIL_SCAN_CHECKSUM_H
#define GFXRECON_UTIL_SCAN_CHECKSUM_H

#include "util/defines.h"

#include <cstddef>
#include <cstdint>

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(util)

// Function pointer type for checksum computation.
typedef uint32_t (*ChecksumFunc)(const void* data, size_t size);

// Portable implementation using FNV-1a.  Always available on all platforms.
uint32_t ChecksumGeneric(const void* data, size_t size);

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
// SSE4.2 CRC32 hardware-accelerated implementation.
uint32_t ChecksumSSE42(const void* data, size_t size);
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
// ARM CRC32 hardware-accelerated implementation.
uint32_t ChecksumArmCRC32(const void* data, size_t size);
#endif

// Select the best available checksum implementation for the current CPU at runtime.
ChecksumFunc SelectBestChecksum();

GFXRECON_END_NAMESPACE(util)
GFXRECON_END_NAMESPACE(gfxrecon)

#endif // GFXRECON_UTIL_SCAN_CHECKSUM_H
