#include "bench_utils.h"
#include "book/LimitOrderBook.h"
#include "protocol/ItchOrderTypes.h"
#include "recovery/MmapLogger.h"
#include "recovery/RecoveryEngine.h"
#include "utils/spscqueue.h"

/*
* Current test is skewed towards branch prediction due to only processing Add Orders
*/
int main() {
    using Orders = protocol::NormalizedOrder;
    constexpr size_t NUM_ITERATIONS = 16'700'000;

    recovery::MmapLogger logger("logs/event_logs.txt");
    utils::SPSCQueue<Orders, 4096> queue;
    book::LimitOrderBook book(1000000);

    protocol::ItchParser parser {queue, logger};

    uint8_t raw_msg[36] = {0};
    raw_msg[0] = 'A';

    printf("Generating 1GB WAL file...\n");
    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        parser.parse_block(reinterpret_cast<char*>(raw_msg), 36);
    }

    logger.flush_to_disk(MS_SYNC);
    printf("WAL file flushed. Starting Recovery...\n\n");

    recovery::RecoveryEngine recovery_engine(book);
    uint64_t start = telemetry::get_tsc_start();
    recovery_engine.run_recovery("logs/event_logs.txt");
    uint64_t end = telemetry::get_tsc_end();

    // ran on Intel i7-7700
    uint64_t num_cycles = end - start;
    double total_ns = num_cycles / 3.6;
    double total_ms = total_ns / 1'000'000.0;
    double avg_ns_per_frame = total_ns / NUM_ITERATIONS;
    
    // million frames per second
    double mfps = (NUM_ITERATIONS / 1'000'000.0) / (total_ms / 1000.0);

    printf("--- RECOVERY BENCHMARK RESULTS ---\n");
    printf("Records Processed : %zu frames\n", NUM_ITERATIONS);
    printf("Total Time        : %.2lf ms\n", total_ms);
    printf("Average Latency   : %.2lf ns/frame\n", avg_ns_per_frame);
    printf("Throughput        : %.2lf Mfps\n", mfps);
    printf("----------------------------------\n");
}