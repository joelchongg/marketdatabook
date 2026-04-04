#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <iostream>

namespace utils {

/*
* Used for finding maximum price level in O(1) time (for bids)
* Intuition: Presence of orders for each price level can be represented by a bit
* At the lowest level, each bit represents each price level (MAX_TICKS / 64) total bits
* At the second level, each bit represents the presence of orders in a level 0 64-bit block
* At the third level, each bit represents the presence of orders in the level 2 64-bit block
* Since MAX_TICKS is often bounded to 100000, we can find the maximum price level with orders
* with at most 3 calls of __builtin_ctzll.
* Similarly, we can also find the minimum price level for offers
*/
template <size_t MAX_TICKS>
class HierarchicalBitset {
static_assert(MAX_TICKS <= 262144, "HierarchicalBitset cannot handle above 64^3 ticks");

public:
    HierarchicalBitset() = default;

    size_t get_best_offer() {
        if (third_level_ == 0) return -1;

        uint64_t second_level_idx = __builtin_clzll(third_level_);
        uint64_t first_level_idx = __builtin_clzll(second_level_[second_level_idx]);
        uint64_t actual_idx = __builtin_clzll(first_level_[first_level_idx + (second_level_idx << 6)]);

        return actual_idx + (first_level_idx << 6) + (second_level_idx << 12);
    }

    size_t get_best_bid() {
        if (third_level_ == 0) return -1;

        uint64_t second_level_idx = 63 - __builtin_ctzll(third_level_);
        uint64_t first_level_idx = 63 - __builtin_ctzll(second_level_[second_level_idx]);
        uint64_t actual_idx = 63 - __builtin_ctzll(first_level_[first_level_idx + (second_level_idx << 6)]);

        return actual_idx + (first_level_idx << 6) + (second_level_idx << 12);
    }

    void set_price_level(size_t tick_idx) {
        uint64_t first_level_idx = tick_idx >> 6;
        first_level_[first_level_idx] |= 1ULL << (63 - (tick_idx & 63));

        uint64_t second_level_idx = first_level_idx >> 6;
        second_level_[second_level_idx] |= 1ULL << (63 - (first_level_idx & 63));

        third_level_ |= 1ULL << (63 - (second_level_idx & 63));
    }

    void clear_price_level(size_t tick_idx) {
        uint64_t first_level_idx = tick_idx >> 6;
        first_level_[first_level_idx] &= ~(1ULL << (63 - (tick_idx & 63)));

        if (first_level_[first_level_idx] == 0) {
            uint64_t second_level_idx = first_level_idx >> 6;
            second_level_[second_level_idx] &= ~(1ULL << (63 - (first_level_idx & 63)));

            if (second_level_[second_level_idx] == 0) {
                third_level_ &= ~(1ULL << (63 - (second_level_idx & 63)));
            }
        }
    }

private:
    static constexpr size_t L1_SIZE = (MAX_TICKS + 63) / 64;
    static constexpr size_t L2_SIZE = (L1_SIZE + 63) / 64;

    std::array<uint64_t, L1_SIZE> first_level_{};
    std::array<uint64_t, L2_SIZE> second_level_{};
    uint64_t third_level_{};
};

} // namespace utils