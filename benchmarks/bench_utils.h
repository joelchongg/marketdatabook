#include <cstdint>
#include <x86intrin.h>

namespace telemetry {
    
inline uint64_t get_tsc_start() {
    _mm_lfence();
    uint64_t tsc = __rdtsc();
    _mm_lfence();
    return tsc;
}

inline uint64_t get_tsc_end() {
    unsigned int aux;
    uint64_t tsc = __rdtscp(&aux);
    _mm_lfence();
    return tsc;
}

} // namespace telemetry
