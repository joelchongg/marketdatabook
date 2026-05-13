#pragma once

#include <cpuid.h>
#include <cstdint>
#include <x86intrin.h>

namespace telemetry {


/*
* Used as a utility class in order to obtain information regarding
* base frequency as well as start and end of RDTSC
*/
class TSC_Clock {
public:
    /*
    * Used to determine the base frequency of the CPU that the
    * current thread / process is running on.
    */
    unsigned int calibrate_frequency() {
        unsigned int eax, ebx, ecx, edx;
        __cpuid(0x16, eax, ebx, ecx, edx);

        unsigned int base_freq = eax & 0xFFFF;
        return base_freq;
    }

    /*
    * Retrieve the current count in the RDTSC.
    * RDTSC instruction is serialized with the use of _mm_lfence()
    */
    uint64_t rdtsc_start() {
        _mm_lfence();
        uint64_t tsc = __rdtsc();
        _mm_lfence();
        return tsc;
    }

    /*
    * Retrieve the current count in the RDTSC.
    * RDTSCP instruction is serialized with the use of _mm_lfence()
    */
    uint64_t rdtsc_end() {
        unsigned int aux;
        uint64_t tsc = __rdtscp(&aux);
        _mm_lfence();
        return tsc;
    }
};

} // namespace telemetry