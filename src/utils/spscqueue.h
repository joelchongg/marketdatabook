#pragma once

#include <atomic>

namespace utils {

template <typename T, size_t Size>
class SPSCQueue {
    static_assert((Size & (Size - 1)) == 0, "SPSC Queue size must be a power of 2");

public:
    bool push(T& input) {
        size_t tail_idx = producer_.tail_.load(std::memory_order_relaxed);
        size_t next_tail_idx = (tail_idx + 1) & (Size - 1);
        if (producer_.cached_head_ == next_tail_idx) {
            producer_.cached_head_ = consumer_.head_.load(std::memory_order_acquire);

            if (producer_.cached_head_ == next_tail_idx) [[unlikely]] {
                return false;
            }
        }

        data_[tail_idx] = input;
        producer_.tail_.store(next_tail_idx, std::memory_order_release);
        return true;
    }

    bool push(T&& input) {
        size_t tail_idx = producer_.tail_.load(std::memory_order_relaxed);
        size_t next_tail_idx = (tail_idx + 1) & (Size - 1);
        if (producer_.cached_head_ == next_tail_idx) {
            producer_.cached_head_ = consumer_.head_.load(std::memory_order_acquire);
            
            if (producer_.cached_head_ == next_tail_idx) [[unlikely]] {
                return false;
            }
        }

        data_[tail_idx] = std::move(input);
        producer_.tail_.store(next_tail_idx, std::memory_order_release);
        return true;
    }

    bool pop(T& output) {
        size_t head_idx = consumer_.head_.load(std::memory_order_relaxed);
        size_t next_head_idx = (head_idx + 1) & (Size - 1);
        if (head_idx == consumer_.cached_tail_) {
            consumer_.cached_tail_ = producer_.tail_.load(std::memory_order_acquire);

            if (head_idx == consumer_.cached_tail_) [[unlikely]] {
                return false;
            }
        }
        
        output = std::move(data_[head_idx]);
        consumer_.head_.store(next_head_idx, std::memory_order_release);
        return true;
    }

private:
    struct alignas(64) ProducerState {
        std::atomic<size_t> tail_ { 0 };
        size_t cached_head_ { 0 };
    };

    struct alignas(64) ConsumerState {
        std::atomic<size_t> head_ { 0 };
        size_t cached_tail_ { 0 };
    };

    ProducerState producer_;
    ConsumerState consumer_;
    T data_[Size];
};

} // namespace utils