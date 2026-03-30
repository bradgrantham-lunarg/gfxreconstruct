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

#include <cstring>
#include "util/scan_checksum.h"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#include <nmmintrin.h> // SSE4.2
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <arm_acle.h>
#endif
#endif

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(util)

uint32_t ChecksumGeneric(const void* data, size_t size)
{
    // FNV-1a hash, 32-bit.
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t       hash  = 0x811c9dc5u;

    // Process 4 bytes at a time for better throughput.
    size_t i = 0;
    for (; i + 4 <= size; i += 4)
    {
        uint32_t word;
        memcpy(&word, bytes + i, sizeof(word));
        hash ^= word;
        hash *= 0x01000193u;
    }

    // Handle remaining bytes.
    for (; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= 0x01000193u;
    }

    return hash;
}

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)

static bool HasSSE42()
{
#if defined(_MSC_VER)
    int cpu_info[4];
    __cpuid(cpu_info, 1);
    return (cpu_info[2] & (1 << 20)) != 0; // SSE4.2 bit in ECX
#else
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
    {
        return (ecx & (1 << 20)) != 0;
    }
    return false;
#endif
}

#if defined(__x86_64__) || defined(_M_X64)

// 64-bit x86: use _mm_crc32_u64 for 8 bytes at a time.
#if defined(_MSC_VER)
uint32_t ChecksumSSE42(const void* data, size_t size)
#elif defined(__clang__) || defined(__GNUC__)
__attribute__((target("sse4.2"))) uint32_t ChecksumSSE42(const void* data, size_t size)
#endif
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint64_t       crc   = 0xFFFFFFFFu;

    size_t i = 0;
    for (; i + 8 <= size; i += 8)
    {
        uint64_t word;
        memcpy(&word, bytes + i, sizeof(word));
        crc = _mm_crc32_u64(crc, word);
    }
    for (; i < size; ++i)
    {
        crc = _mm_crc32_u8(static_cast<uint32_t>(crc), bytes[i]);
    }

    return static_cast<uint32_t>(crc ^ 0xFFFFFFFFu);
}

#else // 32-bit x86

#if defined(_MSC_VER)
uint32_t ChecksumSSE42(const void* data, size_t size)
#elif defined(__clang__) || defined(__GNUC__)
__attribute__((target("sse4.2"))) uint32_t ChecksumSSE42(const void* data, size_t size)
#endif
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t       crc   = 0xFFFFFFFFu;

    size_t i = 0;
    for (; i + 4 <= size; i += 4)
    {
        uint32_t word;
        memcpy(&word, bytes + i, sizeof(word));
        crc = _mm_crc32_u32(crc, word);
    }
    for (; i < size; ++i)
    {
        crc = _mm_crc32_u8(crc, bytes[i]);
    }

    return crc ^ 0xFFFFFFFFu;
}

#endif // 64 vs 32-bit x86

#endif // x86

#if defined(__aarch64__) || defined(_M_ARM64)

uint32_t ChecksumArmCRC32(const void* data, size_t size)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t       crc   = 0xFFFFFFFFu;

    size_t i = 0;
    for (; i + 8 <= size; i += 8)
    {
        uint64_t word;
        memcpy(&word, bytes + i, sizeof(word));
        crc = __crc32d(crc, word);
    }
    for (; i + 4 <= size; i += 4)
    {
        uint32_t word;
        memcpy(&word, bytes + i, sizeof(word));
        crc = __crc32w(crc, word);
    }
    for (; i < size; ++i)
    {
        crc = __crc32b(crc, bytes[i]);
    }

    return crc ^ 0xFFFFFFFFu;
}

#endif // aarch64

ChecksumFunc SelectBestChecksum()
{
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    if (HasSSE42())
    {
        return ChecksumSSE42;
    }
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
    // ARM CRC32 instructions are mandatory on ARMv8.1+. For ARMv8.0 this may need a runtime check,
    // but in practice all AArch64 Android/Linux/Windows targets support it.
    return ChecksumArmCRC32;
#endif

    return ChecksumGeneric;
}

GFXRECON_END_NAMESPACE(util)
GFXRECON_END_NAMESPACE(gfxrecon)
