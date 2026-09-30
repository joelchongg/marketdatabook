#include "MmapLogger.h"

namespace recovery {


MmapLogger::MmapLogger(const char* filename) {
    int fd = open(filename, O_CREAT | O_RDWR | O_LARGEFILE | O_TRUNC, 0600);
    if (fd == -1) [[unlikely]] {
        throw std::runtime_error("MmapLogger(): unable to create new file. Error: " 
            + std::string(strerror(errno)));
    }

    int ret = fallocate(fd, 0, 0, FILE_SIZE);
    if (ret == -1) [[unlikely]] {
        close(fd);
        throw std::runtime_error("MmapLogger(): unable to fallocate data blocks for file. Error: " 
            + std::string(strerror(errno)));
    }
    
    void* addr = mmap(NULL, FILE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);
    if (addr == MAP_FAILED) [[unlikely]] {
        close(fd);
        throw std::runtime_error("MmapLogger(): unable to mmap file. Error: " 
            + std::string(strerror(errno)));
    }
    start_ = static_cast<char *>(addr);
    curr_ = start_;

    ret = madvise(start_, FILE_SIZE, MADV_SEQUENTIAL);
    if (ret == -1) [[unlikely]] {
        close(fd);
        munmap(start_, FILE_SIZE);
        throw std::runtime_error("MmapLogger(): unable to use madvise. Error: " 
            + std::string(strerror(errno)));
    }

    close(fd);
}

MmapLogger::~MmapLogger() {
    if (start_ != nullptr) munmap(start_, FILE_SIZE);
}

MmapLogger::MmapLogger(MmapLogger&& other)
    : start_ { std::exchange(other.start_, nullptr) }
    , curr_ { std::exchange(other.curr_, nullptr) }
{ }

MmapLogger& MmapLogger::operator=(MmapLogger&& other) {
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

WalFrame* MmapLogger::reserve_frame() {
    if (curr_ + sizeof(WalFrame) > start_ + FILE_SIZE) [[unlikely]] {
        return nullptr;
    }

    WalFrame* frame = reinterpret_cast<WalFrame*>(curr_);
    curr_ += sizeof(WalFrame);

    return frame;
}

void MmapLogger::flush_to_disk(int flag) {
    msync(start_, curr_ - start_, flag);
}

} // namespace utils