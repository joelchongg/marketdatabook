#pragma once

#include <atomic>

namespace utils {

template <typename T, size_t Size>
class SPSCQueue {
    static_assert((Size & (Size - 1)) == 0, "SPSC Queue size must be a power of 2");

public:
    bool push(T& input) {
        size_t tail_idx = tail_.load(std::memory_order_relaxed);
        size_t next_tail_idx = (tail_idx + 1) & (Size - 1);
        if (head_.load(std::memory_order_acquire) == ((tail_idx + 1) & (Size - 1))) {
            return false;
        }

        data_[tail_idx] = input;
        tail_.store(next_tail_idx, std::memory_order_release);
        return true;
    }

    bool push(T&& input) {
        size_t tail_idx = tail_.load(std::memory_order_relaxed);
        size_t next_tail_idx = (tail_idx + 1) & (Size - 1);
        if (head_.load(std::memory_order_acquire) == next_tail_idx) {
            return false;
        }

        data_[tail_idx] = std::move(input);
        tail_.store(next_tail_idx, std::memory_order_release);
        return true;
    }

    bool pop(T& output) {
        size_t head_idx = head_.load(std::memory_order_relaxed);
        size_t next_head_idx = (head_idx + 1) & (Size - 1);
        if (head_idx == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        
        output = std::move(data_[head_idx]);
        head_.store(next_head_idx, std::memory_order_release);
        return true;
    }

private:
    alignas(64) std::atomic<size_t> head_ { 0 };
    alignas(64) std::atomic<size_t> tail_ { 0 };
    T data_[Size];
};

} // namespace utils