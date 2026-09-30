#pragma once

#include "protocol/ItchParser.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "protocol/ItchOrderTypes.h"
#include "utils/HierarchicalBitset.h"
#include "utils/MagicBuffer.h"
#include "utils/StaticOrderMap.h"

namespace book {

enum class Side {
    Buy,
    Sell
};

// aligned to 32 bytes to avoid an order being partitioned between 2 cache lines
struct alignas(32) Order {
    uint64_t order_reference_number;
    uint32_t shares;
    uint32_t price;
    // index "pointers" to the next order in the same price level
    uint32_t next;
    uint32_t prev;
};

struct PriceLevel {
    uint32_t head;
    uint32_t tail;
};

class LimitOrderBook {
public:
    LimitOrderBook(size_t max_orders);

    void add_order(const protocol::NormalizedAddOrder& parsed_order);
    void cancel_order(uint64_t order_id);
    void execute_order(const protocol::NormalizedOrderExecuted& order);
    size_t get_best_bid() const { return bids_bitset_.get_best_bid(); }
    size_t get_best_offer() const { return asks_bitset_.get_best_offer(); }

private:
    constexpr static uint32_t NULL_INDEX = 0xFFFFFFFF; // signifies nullptr for the index "pointers"
    constexpr static int MAX_TICKS = 100000; // should be adjusted
    constexpr static uint32_t SIDE_MASK = 1U << 31;
    constexpr static uint32_t INDEX_MASK = ~SIDE_MASK;
    constexpr static size_t ORDER_MAP_SIZE = 2 << 21;

    Order* pool_; // free list of order objects
    std::array<PriceLevel, MAX_TICKS> bids_{}; // bid orders for each price level
    std::array<PriceLevel, MAX_TICKS> asks_{}; // sell orders for each price level
    alignas(64) utils::StaticOrderMap<ORDER_MAP_SIZE> orders_; // capacity of orders_ should be tuned to limit order book # of elements to enforce load factor of < 0.5
    alignas(64) utils::HierarchicalBitset<MAX_TICKS> bids_bitset_{};
    alignas(64) utils::HierarchicalBitset<MAX_TICKS> asks_bitset_{};
    uint32_t next_free_index_ = 0; // keeps track of the free order objects within the pool

    template <Side side>
    void add_order_impl(const protocol::NormalizedAddOrder& order);

    // Updates price level for adding new orders
    void add_update_price_level(PriceLevel& price_level, Order& new_order, uint32_t new_order_idx);
    void execute_update_price_level(PriceLevel& price_level, Order& order, uint32_t order_idx);

    template <Side side>
    void release_order_to_pool(Order& order, uint32_t order_idx);
};

} // namespace book