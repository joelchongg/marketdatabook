#pragma once

#include "protocol/ItchParser.h"

#include <array>
#include <cstdint>
#include <cstring>
#include<stdexcept>
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
    LimitOrderBook(size_t max_orders) {
        if (max_orders == 0) {
            throw std::runtime_error("LimitOrderBook(): Max Orders should be a positive value.");
        }

        size_t bytes_needed = max_orders * sizeof(Order);
        size_t page_aligned_bytes_needed = (bytes_needed + utils::PAGE_SIZE - 1) & ~(utils::PAGE_SIZE - 1);
        void* pool_addr = mmap(NULL, page_aligned_bytes_needed, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);

        if (pool_addr == MAP_FAILED) [[unlikely]] {
            throw std::runtime_error("LimitOrderBook(): Unable to mmap order pool. Error: " + std::string(strerror(errno)));
        }

        pool_ = static_cast<Order *>(pool_addr);

        for (size_t i = 1; i < max_orders; ++i) {
            pool_[i - 1].next = i;
        }
        pool_[max_orders - 1].next = NULL_INDEX;

        next_free_index_ = 0;

        // Initialize book
        bids_.fill({NULL_INDEX, NULL_INDEX});
        asks_.fill({NULL_INDEX, NULL_INDEX});
    }

    void add_order(const protocol::NormalizedAddOrder& parsed_order) {
        if (parsed_order.price >= MAX_TICKS) {
            // corrupted data
            return;
        }

        if (next_free_index_ == NULL_INDEX) {
            // can fall back to reallocation if needed during actual execution
            return;
        }

        if (parsed_order.indicator == 'B') {
            add_order_impl<Side::Buy>(parsed_order);
        } else {
            add_order_impl<Side::Sell>(parsed_order);
        }
    }

    void cancel_order(uint64_t order_id) {
        auto it = orders_.find(order_id);
        if (it == nullptr) {
            // order does not exist
            return;
        }

        uint32_t tagged_order_idx = it->pool_idx;
        uint32_t order_idx = tagged_order_idx & INDEX_MASK;
        bool is_sell = (tagged_order_idx & SIDE_MASK) != 0;

        Order& order = pool_[order_idx];
        if (is_sell) {
            release_order_to_pool<Side::Sell>(order, order_idx);
        } else {
            release_order_to_pool<Side::Buy>(order, order_idx);
        }
    }

    void execute_order(const protocol::NormalizedOrderExecuted& order) {
        auto it = orders_.find(order.order_reference_number);
        if (it == nullptr) {
            return;
        }

        uint32_t tagged_order_idx = it->pool_idx;
        uint32_t order_idx = tagged_order_idx & INDEX_MASK;
        bool is_sell = (tagged_order_idx & SIDE_MASK) != 0;
        Order& curr_order = pool_[order_idx];

        if (curr_order.shares < order.shares) {
            throw std::runtime_error("LimitOrderBook::execute_order(): Current order has less shares than required from execute order");
        }

        curr_order.shares -= order.shares;
        if (curr_order.shares == 0) {
            if (is_sell) {
                release_order_to_pool<Side::Sell>(curr_order, order_idx);
            } else {
                release_order_to_pool<Side::Buy>(curr_order, order_idx);
            }
        }
    }

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
    void add_order_impl(const protocol::NormalizedAddOrder& order) {
        // retrieve new order
        uint32_t new_order_idx = next_free_index_;
        next_free_index_ = pool_[next_free_index_].next;

        Order& new_order = pool_[new_order_idx];

        // set new order values from parsed order
        new_order.order_reference_number = order.order_reference_number;
        new_order.price = order.price;
        new_order.shares = order.shares;

        // add order to current orders
        // use the 31st bit to represent the side for cancel / execute orders
        constexpr uint32_t tagged_idx = side == Side::Buy ? 0 : (1U << 31);
        orders_[new_order.order_reference_number] = tagged_idx | new_order_idx;

        // Update Price level for new order based on side
        if constexpr (side == Side::Buy) {
            PriceLevel& price_level = bids_[new_order.price];
            add_update_price_level(price_level, new_order, new_order_idx);
            bids_bitset_.set_price_level(new_order.price); // update hierarchical bitset
        } else {
            PriceLevel& price_level = asks_[new_order.price];
            add_update_price_level(price_level, new_order, new_order_idx);
            asks_bitset_.set_price_level(new_order.price); // update hierarchical bitset
        }
    }

    // Updates price level for adding new orders
    void add_update_price_level(PriceLevel& price_level, Order& new_order, uint32_t new_order_idx) {
        new_order.prev = NULL_INDEX;
        new_order.next = NULL_INDEX;

        if (price_level.head == NULL_INDEX) {
            price_level.head = new_order_idx;
        }

        if (price_level.tail != NULL_INDEX) {
            pool_[price_level.tail].next = new_order_idx;
            new_order.prev = price_level.tail;
        }
        price_level.tail = new_order_idx;
    }

    template <Side side>
    void release_order_to_pool(Order& order, uint32_t order_idx) {
        // Update Neighbours at price level
        if (order.prev != NULL_INDEX) {
            pool_[order.prev].next = order.next;
        }

        if (order.next != NULL_INDEX) {
            pool_[order.next].prev = order.prev;
        }

        // Update Price Level
        if constexpr (side == Side::Buy) {
            PriceLevel& price_level = bids_[order.price];
            execute_update_price_level(price_level, order, order_idx);

            if (price_level.head == NULL_INDEX) {
                bids_bitset_.clear_price_level(order.price);
            }
        } else {
            PriceLevel& price_level = asks_[order.price];
            execute_update_price_level(price_level, order, order_idx);

            if (price_level.head == NULL_INDEX) {
                asks_bitset_.clear_price_level(order.price);
            }
        }
    
        // Return object back to pool
        orders_.erase(order.order_reference_number);
        order.next = next_free_index_;
        next_free_index_ = order_idx;
    }

    // Updates price level for release_order_to_pool
    void execute_update_price_level(PriceLevel& price_level, Order& order, uint32_t order_idx) {
        if (price_level.head == order_idx) {
            price_level.head = order.next;
        }

        if (price_level.tail == order_idx) {
            price_level.tail = order.prev;
        }
    }
};

} // namespace book