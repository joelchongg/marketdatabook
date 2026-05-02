#include "bench_utils.h"
#include "utils/MmapLogger.h"

#include <iostream>
#include <limits>
#include <random>

// rng may add overhead
utils::LogEvent create_dummy_event() {
    constexpr static uint64_t UINT64T_MAX = std::numeric_limits<uint64_t>::max();
    constexpr static uint32_t UINT32T_MAX = std::numeric_limits<uint32_t>::max();

    static std::mt19937 rng(42);
    static std::uniform_int_distribution<uint64_t> timestamp_dist(0, UINT64T_MAX);
    static std::uniform_int_distribution<uint64_t> order_id_dist(0, UINT64T_MAX);
    static std::uniform_int_distribution<uint32_t> price_dist(0, UINT32T_MAX);
    static std::uniform_int_distribution<uint32_t> quantity_dist(0, UINT32T_MAX);
    static std::vector<char> events { 'A', 'E', 'X' }; // contains all currently supported events, should be updated accordingly
    static std::uniform_int_distribution<size_t> event_type_dist(0, events.size());

    utils::LogEvent event;
    event.timestamp = timestamp_dist(rng);
    event.order_id = order_id_dist(rng);
    event.price = price_dist(rng);
    event.quantity = quantity_dist(rng);
    event.event_type = events[event_type_dist(rng)];

    return event;
}

int main() {
    constexpr size_t NUM_ITERATIONS = 10'000'000;

    const char* filepath = "./logs/mmap_logger_bench_results.txt";
    utils::MmapLogger logger(filepath);

    utils::LogEvent event{};
    // warm up cache
    for (size_t i = 0; i < 100'000; ++i) {
        event.order_id = i;
        logger.append(event);
    }

    // capture get_tsc_start and get_tsc_end overhead
    uint64_t overhead_start = telemetry::get_tsc_start();

    for (size_t i = 0; i < NUM_ITERATIONS; ++i) {
        asm volatile("" : : : "memory");
    }

    uint64_t overhead_end = telemetry::get_tsc_end();

    uint64_t start = telemetry::get_tsc_start();

    for (size_t i = 0; i < NUM_ITERATIONS; ++i) {
        event.order_id = i;
        logger.append(event);
        asm volatile("" : : : "memory");
    }

    uint64_t end = telemetry::get_tsc_end();

    double baseline_overhead_cycles = 1.0 * (overhead_end - overhead_start) / NUM_ITERATIONS;
    uint64_t num_cycles = end - start;
    double net_cycles_per_append = 1.0 * (num_cycles) / NUM_ITERATIONS - baseline_overhead_cycles;

    // ran on Intel Xeon w5-3423 which has a clock speed of
    double ns_per_append = net_cycles_per_append / 2.112;
    printf("Average Nanoseconds per append: %.2lf\n", ns_per_append);
}