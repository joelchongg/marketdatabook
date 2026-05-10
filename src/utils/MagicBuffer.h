#pragma once

#include <cstddef>
#include <fcntl.h>
#include <stdexcept>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>

namespace utils {

inline const size_t PAGE_SIZE = sysconf(_SC_PAGESIZE);

/*
* The MagicBuffer Implementation makes use of virtualization in order to map two consecutive
* pages to the same physical page. This allows for writing of bytes that would go past the buffer
* to wrap around cleanly to the start of the buffer. Current implementation makes use of standard page
* sizes, but can be extended to use HugePages instead if it is a bottleneck.
* This class is currently built specific to the EpollReactor, but may be used for SPSCQueue if needed.
*/
class MagicBuffer {
public:
    MagicBuffer(size_t capacity) {
        if (capacity == 0) [[unlikely]] {
            throw std::logic_error("MagicBuffer(): Unable to create magic buffer of size 0");
        }

        fd_ = memfd_create("magicbuf", MFD_CLOEXEC);
        if (fd_ == -1) [[unlikely]] {
            throw std::runtime_error("MagicBuffer(): Unable to create file descriptor for magic buffer. Error Code: " 
                + std::string(strerror(errno)));
        }

        // capacity must be aligned to page size
        aligned_capacity_ = (capacity + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        if (ftruncate(fd_, aligned_capacity_) == -1) [[unlikely]] {
            close(fd_);
            throw std::runtime_error("MagicBuffer(): Unable to create magic buffer. Error Code: " 
                + std::string(strerror(errno)));
        }

        char* buffer = static_cast<char *>(mmap(NULL, aligned_capacity_ * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (buffer == MAP_FAILED) [[unlikely]] {
            close(fd_);
            throw std::runtime_error("MagicBuffer(): Unable to mmap, not enough memory. Error Code: " 
                + std::string(strerror(errno)));
        }

        start_ = static_cast<char *>(mmap(buffer, aligned_capacity_, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd_, 0));
        if (start_ == MAP_FAILED) [[unlikely]] {
            munmap(buffer, aligned_capacity_ * 2);
            close(fd_);
            throw std::runtime_error("MagicBuffer(): Unexpected error occurred when mapping start of buffer. Error Code: " 
                + std::string(strerror(errno)));
        }

        second_half_start_ = static_cast<char *>(mmap(buffer + aligned_capacity_, aligned_capacity_, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd_, 0));
        if (second_half_start_ == MAP_FAILED) [[unlikely]] {
            munmap(start_, aligned_capacity_ * 2);
            close(fd_);
            throw std::runtime_error(
                "MagicBuffer(): Unexpected error occurred when mapping second start of buffer. Error Code: " 
                + std::string(strerror(errno)));
        }

        write_head_ = start_;
        read_head_ = start_;
    }
    
    ~MagicBuffer() noexcept {
        if (start_ != nullptr) munmap(start_, aligned_capacity_ * 2);
        if (fd_ != -1) close(fd_);
    }

    // disable copy semantics
    MagicBuffer(const MagicBuffer&) = delete;
    MagicBuffer& operator=(const MagicBuffer&) = delete;

    // delete move semantics (for now, we only use it for SPSCQueue. If needed, enable move semantics)
    MagicBuffer(MagicBuffer&& other)
        : start_ { std::exchange(other.start_, nullptr) }
        , second_half_start_ { std::exchange(other.second_half_start_, nullptr) }
        , aligned_capacity_ { other.aligned_capacity_ }
    { }

    MagicBuffer& operator=(MagicBuffer&& other) {
        if (this == &other) {
            return *this;
        }

        if (start_) {
            munmap(start_, aligned_capacity_);
            munmap(second_half_start_, aligned_capacity_);
            close(fd_);
        }

        start_ = std::exchange(other.start_, nullptr);
        second_half_start_ = std::exchange(other.second_half_start_, nullptr);
        aligned_capacity_ = other.aligned_capacity_;

        return *this;
    }

    char* data() { return start_; }
    size_t capacity() { return aligned_capacity_; }
    
    size_t read_space_left() { 
        return write_head_ >= read_head_ 
            ? static_cast<size_t>(write_head_ - read_head_) 
            : aligned_capacity_ - static_cast<size_t>(read_head_ - write_head_);
    }

    size_t write_space_left() { return aligned_capacity_ - 1 - read_space_left(); }
    char* get_write_head() { return write_head_; }
    char* get_read_head() { return read_head_; }

    void advance_write(size_t bytes) {
        write_head_ += bytes;
        if (write_head_ >= second_half_start_) {
            write_head_ -= aligned_capacity_;
        }
    }

    void advance_read(size_t bytes) {
        read_head_ += bytes;
        if (read_head_ >= second_half_start_) {
            read_head_ -= aligned_capacity_;
        }
    }

    char* get_write_location() { return write_head_; }

    void reset() {
        // since we are dealing with trivially copyable types, we can just reset the pointers
        read_head_ = start_;
        write_head_ = start_;
    }

private:
    char* start_;
    char* second_half_start_;
    char* read_head_;
    char* write_head_;
    size_t aligned_capacity_;
    int fd_;
};

} // namespace utils