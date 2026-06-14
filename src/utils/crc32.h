#pragma once

#include <cstdint>
#include <nmmintrin.h>

namespace utils {

inline uint32_t calculate_crc32(const void* data) {
        const uint64_t* ptr = static_cast<const uint64_t*>(data);
        uint64_t crc = 0;

        crc = _mm_crc32_u64(crc, ptr[0]);
        crc = _mm_crc32_u64(crc, ptr[1]);
        crc = _mm_crc32_u64(crc, ptr[2]);
        crc = _mm_crc32_u64(crc, ptr[3]);
        crc = _mm_crc32_u64(crc, ptr[4]);
        crc = _mm_crc32_u64(crc, ptr[5]);
        crc = _mm_crc32_u64(crc, ptr[6]);

        const uint32_t* ptr32 = reinterpret_cast<const uint32_t*>(ptr + 7);
        crc = _mm_crc32_u32(static_cast<uint32_t>(crc), *ptr32);

        return static_cast<uint32_t>(crc);
    }

} // namespace utils