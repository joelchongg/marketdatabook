#include "bench_utils.h"

#include "utils/MmapLogger.h"
#include <unistd.h>

struct alignas(32) LogEvent {
    uint64_t timestamp;
    uint64_t order_id;
    uint32_t price;
    uint32_t quantity;
    char event_type;
};

/*
* This class has the same functionality as MmapLogger, but uses write() syscalls for logging.
* This class is used to check the performance improvement of MmapLogger, showing the difference
* of having syscalls in the hotpath.
*/
class WriteLogger {
public:
    WriteLogger(const char* filename) {
        fd_ = open(filename, O_CREAT | O_RDWR | O_LARGEFILE | O_TRUNC, 0600);
        if (fd_ == -1) [[unlikely]] {
            throw std::runtime_error("WriteLogger(): unable to create new file. Error: " + std::string(strerror(errno)));
        }
    }

    ~WriteLogger() {
        if (fd_ != -1) {
            close(fd_);
        }
    }

    bool append(const LogEvent& event) {
        int ret = write(fd_, &event, sizeof(LogEvent));
        if (ret == -1) {
            throw std::runtime_error("WriteLogger::append(): Unable to append data to file. Error: " + std::string(strerror(errno)));
        }
        return true;
    }

    void flush_to_disk(int flag) {
        if (fd_ != -1) {
            fdatasync(fd_);
        }
    }
private:
    int fd_ { -1 };

};

int main() {
    constexpr size_t NUM_ITERATIONS = 10'000'000;
    const char* filepath = "./logs/write_logger_bench_results.txt";
    WriteLogger logger(filepath);

    LogEvent event{};
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