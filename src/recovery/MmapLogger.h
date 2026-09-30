#pragma once

#include "utils/WalFrame.h"

#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace recovery {

/*
* MmapLogger class is used to log all events that occurred in the order book.
* This allows for deterministic replay if orderbook crashes.
* Size of file for MmapLogger is currently fixed at 1GB, and should be tuned based on actual market data amount for the day.
*/
class MmapLogger {
public:
    MmapLogger(const char* filename);
    ~MmapLogger();

    // disable copy semantics
    MmapLogger(const MmapLogger&) = delete;
    MmapLogger& operator=(const MmapLogger&) = delete;

    // allow move semantics
    MmapLogger(MmapLogger&& other);
    MmapLogger& operator=(MmapLogger&& other);

    WalFrame* reserve_frame();

    /*
    * Allows user to flush current data in mmap to disk immediately
    * Takes in a flag for MS_SYNC or MS_ASYNC
    */
    void flush_to_disk(int flag);

private:
    constexpr static size_t FILE_SIZE = 1 << 30; // should be tuned based on actual market data for a day
    char* start_ { nullptr };
    char* curr_ { nullptr };
};

} // namespace utils