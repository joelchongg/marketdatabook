#pragma once

#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace utils {

/*
* MmapLogger class is used to log all events that occurred in the order book.
* This allows for deterministic replay if orderbook crashes.
* Size of file for MmapLogger is currently fixed at 1GB, and should be tuned based on actual market data amount for the day.
*/
class MmapLogger {
public:
    MmapLogger(const char* filename) {
        int fd = open(filename, O_CREAT | O_RDWR | O_LARGEFILE | O_TRUNC, 0600);
        if (fd == -1) [[unlikely]] {
            throw std::runtime_error("MmapLogger: unable to create new file. Error: " + std::string(strerror(errno)));
        }

        int ret = fallocate(fd, 0, 0, FILE_SIZE);
        if (ret == -1) [[unlikely]] {
            close(fd);
            throw std::runtime_error("MmapLogger: unable to fallocate data blocks for file. Error: " + std::string(strerror(errno)));
        }
        
        void* addr = mmap(NULL, FILE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);
        if (addr == MAP_FAILED) [[unlikely]] {
            close(fd);
            throw std::runtime_error("MmapLogger: unable to mmap file. Error: " + std::string(strerror(errno)));
        }
        start_ = static_cast<char *>(addr);
        curr_ = start_;

        ret = madvise(start_, FILE_SIZE, MADV_SEQUENTIAL);
        if (ret == -1) [[unlikely]] {
            close(fd);
            munmap(start_, FILE_SIZE);
            throw std::runtime_error("MmapLogger: unable to use madvise. Error: " + std::string(strerror(errno)));
        }

        close(fd);
    }

    ~MmapLogger() {
        if (start_ != nullptr) munmap(start_, FILE_SIZE);
    }

    // disable copy semantics
    MmapLogger(const MmapLogger&) = delete;
    MmapLogger& operator=(const MmapLogger&) = delete;

    // allow move semantics
    MmapLogger(MmapLogger&& other)
        : start_ { std::exchange(other.start_, nullptr) }
        , curr_ { std::exchange(other.curr_, nullptr) }
    { }

    MmapLogger& operator=(MmapLogger&& other) {
        if (&other == this) {
            return *this;
        }

        if (start_ != nullptr) {
            munmap(start_, FILE_SIZE);
        }

        start_ = std::exchange(other.start_, nullptr);
        curr_ = std::exchange(other.curr_, nullptr);

        return *this;
    }

    /*
    * Append assumes that there is sufficient space for all market data logging.
    * Current space allocated is 1GB. If space is not enough, this function returns false.
    */
    bool append(void* buffer, size_t length) {
        char* next_curr = curr_ + length;
        if (next_curr > start_ + FILE_SIZE) [[unlikely]] {
            return false;
        }

        std::memcpy(curr_, buffer, length);
        curr_ = next_curr;
        return true;
    }

    /*
    * Allows user to flush current data in mmap to disk immediately
    * Takes in a flag for MS_SYNC or MS_ASYNC
    */
    void flush_to_disk(int flag) {
        msync(start_, curr_ - start_, flag);
    }

private:
    constexpr static size_t FILE_SIZE = 1 << 30; // should be tuned based on actual market data for a day
    char* start_ { nullptr };
    char* curr_ { nullptr };
};

} // namespace utils