#pragma once

#include "TSC_Clock.h"

#include <cassert>
#include <cstring>
#include <linux/perf_event.h>
#include <stdexcept>
#include <sys/cdefs.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace telemetry {

class TelemetryEngine {
public:
#ifdef ENABLE_HW_TELEMETRY
    TelemetryEngine(enum perf_type_id type, uint64_t config) {
        struct perf_event_attr event_attr;
        memset(&event_attr, 0, sizeof(event_attr));

        // populate event_attr fields
        event_attr.exclude_kernel = 1;
        event_attr.exclude_hv = 1;
        event_attr.exclude_idle = 1;
        event_attr.type = type;
        event_attr.config = config;
        event_attr.size = sizeof(event_attr);

        int fd = syscall(SYS_perf_event_open, &event_attr, 0, -1, -1, PERF_FLAG_FD_CLOEXEC | PERF_FLAG_FD_NO_GROUP);
        if (fd == -1) [[unlikely]] {
            throw std::runtime_error("TelemetryEngine(): Unable to open new perf event for type: " 
                + std::to_string(type) + " and config: " + std::to_string(config) + ". Error: " + strerror(errno));
        }
        fd_ = fd;

        void* addr = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
        if (addr == MAP_FAILED) [[unlikely]] {
            close(fd_);
            throw std::runtime_error(
                "TelemetryEngine(): Unable to create mapping for perf_event_mmap_page. Error: " + std::string(strerror(errno)));
        }
        metadata_ = reinterpret_cast<perf_event_mmap_page*>(addr);
    }

    ~TelemetryEngine() {
        if (fd_ != -1) close(fd_);
        if (metadata_) munmap(metadata_, 4096);
    }

    // disable copy semantics
    TelemetryEngine(const TelemetryEngine&) = delete;
    TelemetryEngine& operator=(const TelemetryEngine&) = delete;

    // move semantics currently not implemented
    TelemetryEngine(TelemetryEngine&&) = delete;
    TelemetryEngine& operator=(TelemetryEngine&&) = delete;

    __always_inline uint64_t read_pmc() const {
        uint32_t id = metadata_->index;
        assert(id > 0);

        return __rdpmc(id - 1);
    }
#else
    TelemetryEngine([[maybe_unused]] enum perf_type_id type, [[maybe_unused]] uint64_t config) {}
    ~TelemetryEngine() {}

    __always_inline uint64_t read_pmc() const { return 0; }
#endif

private:
#ifdef ENABLE_HW_TELEMETRY
    perf_event_mmap_page* metadata_ { nullptr };
    int fd_ { -1 };
#endif
};

} // namespace telemetry