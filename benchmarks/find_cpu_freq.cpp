#include <stdio.h>
#include <stdint.h>
#include <time.h>

// Function to read the Time Stamp Counter
static inline uint64_t rdtsc() {
    unsigned int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

int main() {
    struct timespec t1, t2;
    uint64_t start_tsc, end_tsc;

    // Get exact starting time and TSC
    clock_gettime(CLOCK_MONOTONIC, &t1);
    start_tsc = rdtsc();
    
    // Busy-wait for exactly 1 second
    do {
        clock_gettime(CLOCK_MONOTONIC, &t2);
    } while (t2.tv_sec - t1.tv_sec < 1);

    // Get exact ending TSC
    end_tsc = rdtsc();

    // Calculate elapsed time in seconds (as a double)
    double elapsed = (t2.tv_sec - t1.tv_sec) + (t2.tv_nsec - t1.tv_nsec) / 1e9;
    
    // Frequency = Cycles / Seconds
    double freq = (end_tsc - start_tsc) / elapsed;

    printf("Measured TSC frequency: %.2f Hz (%.2f MHz)\n", freq, freq / 1000000.0);
    return 0;
}