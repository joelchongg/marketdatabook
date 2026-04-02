#pragma once

#include "src/protocol/ItchParser.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream> // for debugging
#include<stdexcept>
#include <vector>

#include "absl/container/flat_hash_map.h"

namespace book {

struct Order {
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
            throw std::runtime_error("Max Orders should be a positive value.");
        }

        // Initialize pool objects
        pool_.resize(max_orders);

        pool_[0].prev = NULL_INDEX;
        for (size_t i = 1; i < max_orders; ++i) {
            pool_[i - 1].next = i;
        }
        pool_[max_orders - 1].next = NULL_INDEX;

        next_free_index_ = 0;

        // Initialize book
        book_.fill({NULL_INDEX, NULL_INDEX});
    }

    void add_order(const protocol::AddOrder& parsed_order) {
        if (next_free_index_ == NULL_INDEX) {
            // can fall back to reallocation if needed during actual execution
            std::cout << "Pool is fully utilized, unable to add more orders.\n";
        }

        // retrieve new order
        uint32_t new_order_idx = next_free_index_;
        next_free_index_ = pool_[next_free_index_].next;

        Order& new_order = pool_[new_order_idx];

        // set new order values from parsed order
        new_order.order_reference_number = parsed_order.order_reference_number;
        new_order.shares = parsed_order.shares;
        new_order.price = parsed_order.price;

        // insert order into current orders flat map
        orders_[new_order.order_reference_number] = new_order_idx;

        // update price level for new order
        PriceLevel& price_level = book_[new_order.price];
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

    void cancel_order(uint64_t order_id) {
        if (!orders_.contains(order_id)) {
            // order does not exist
            return;
        }

        uint32_t order_idx = orders_[order_id];

        // update list of orders at that price level in the pool
        Order& order = pool_[order_idx];
        if (order.prev != NULL_INDEX) {
            pool_[order.prev].next = order.next;
        }

        if (order.next != NULL_INDEX) {
            pool_[order.next].prev = order.prev;
        }

        // update head and tail of PriceLevel if necessary
        PriceLevel& price_level = book_[order.price];
        if (price_level.head == order_idx) {
            price_level.head = order.next;
        }
        if (price_level.tail == order_idx) {
            price_level.tail = order.prev;
        }

        // Remove order from current orders
        orders_.erase(order_id);

        // Insert order back into free list
        order.next = next_free_index_;
        next_free_index_ = order_idx;
    }

private:
    constexpr static uint32_t NULL_INDEX = 0xFFFFFFFF; // signifies nullptr for the index "pointers"
    constexpr static int MAX_TICKS = 1000; // should be adjusted

    std::vector<Order> pool_; // free list of order objects
    std::array<PriceLevel, MAX_TICKS> book_; // contains the orders for each price level
    absl::flat_hash_map<uint64_t, uint32_t> orders_;
    uint32_t next_free_index_ = 0; // keeps track of the free order objects within the pool
};

} // namespace book