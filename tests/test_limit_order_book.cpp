#include <catch2/catch_test_macros.hpp>
#include "book/LimitOrderBook.h"
#include "protocol/OrderTypes.h"

#include <map>
#include <unordered_map>
#include <list>
#include <vector>
#include <random>

struct MockOrder {
    uint64_t order_reference_number;
    uint32_t shares;
    uint32_t price;
    book::Side side;
};

// Simple implementation of an order book
class MockOrderBook {
public:
    void add_order(const MockOrder& order) {
        orders_[order.order_reference_number] = order;
        if (order.side == book::Side::Buy) {
            bids_[order.price].push_back(order.order_reference_number);
        } else {
            asks_[order.price].push_back(order.order_reference_number);
        }
    }

    void cancel_order(uint64_t order_id) {
        auto it = orders_.find(order_id);
        if (it == orders_.end()) return;

        MockOrder order = it->second;

        if (order.side == book::Side::Buy) {
            auto& price_list = bids_[order.price];
            price_list.remove(order_id);
            if (price_list.empty()) {
                bids_.erase(order.price);
            }
        } else {
            auto& price_list = asks_[order.price];
            price_list.remove(order_id);
            if (price_list.empty()) {
                asks_.erase(order.price);
            }
        }

        orders_.erase(order_id);
    }

    void execute_order(uint64_t order_id, uint32_t shares) {
        auto it = orders_.find(order_id);
        if (it == orders_.end()) return;

        it->second.shares -= shares;
        if (it->second.shares == 0) {
            cancel_order(order_id);
        }
    }

    size_t get_best_bid() const {
        if (bids_.empty()) return static_cast<size_t>(-1);
        return bids_.rbegin()->first; 
    }

    size_t get_best_offer() const {
        if (asks_.empty()) return static_cast<size_t>(-1);
        return asks_.begin()->first; 
    }

    uint32_t get_shares(uint64_t id) {
        return orders_[id].shares;
    }

private:
    std::map<uint32_t, std::list<uint64_t>> bids_;
    std::map<uint32_t, std::list<uint64_t>> asks_;
    std::unordered_map<uint64_t, MockOrder> orders_;
};

TEST_CASE("Limit Order Book Test") {
    book::LimitOrderBook lob(2'000'000);
    MockOrderBook test_book;

    std::vector<uint64_t> active_ids;
    std::unordered_map<uint64_t, size_t> id_to_vector_idx;

    std::mt19937 rng(42); 
    std::uniform_int_distribution<int> op_dist(0, 99); // 0-49=Add, 50-74=Cancel, 75-99=Execute
    std::uniform_int_distribution<uint32_t> price_dist(0, 99999);
    std::uniform_int_distribution<uint32_t> shares_dist(100, 10000);
    std::uniform_int_distribution<int> side_dist(0, 1);

    uint64_t next_order_id = 1;

    SECTION("1,000,000 Random Operations") {
        for (int i = 0; i < 1'000'000; ++i) {
            int op = op_dist(rng);

            // force an add if book is empty so we have something to cancel/execute
            if (active_ids.empty()) op = 0; 

            if (op < 50) {
                uint64_t id = next_order_id++;
                uint32_t price = price_dist(rng);
                uint32_t shares = shares_dist(rng);
                char side = side_dist(rng) == 0 ? 'B' : 'S';
                book::Side enum_side = side == 'B' ? book::Side::Buy : book::Side::Sell;

                protocol::NormalizedAddOrder normalized_add{0, id, shares, price, 0, 0, "", side};
                lob.add_order(normalized_add);

                test_book.add_order({id, shares, price, enum_side});

                id_to_vector_idx[id] = active_ids.size();
                active_ids.push_back(id);
            } else {
                // Pick a random existing active order
                std::uniform_int_distribution<size_t> idx_dist(0, active_ids.size() - 1);
                size_t random_idx = idx_dist(rng);
                uint64_t target_id = active_ids[random_idx];

                if (op < 75) {
                    lob.cancel_order(target_id);
                    test_book.cancel_order(target_id);

                    uint64_t last_id = active_ids.back();
                    active_ids[random_idx] = last_id;
                    id_to_vector_idx[last_id] = random_idx;
                    active_ids.pop_back();
                    id_to_vector_idx.erase(target_id);

                } else {
                    uint32_t current_shares = test_book.get_shares(target_id);
                    std::uniform_int_distribution<uint32_t> exec_dist(1, current_shares);
                    uint32_t executed_shares = exec_dist(rng);

                    protocol::NormalizedOrderExecuted exec_event{0, target_id, 0, executed_shares, 0, 0};
                    lob.execute_order(exec_event);
                    test_book.execute_order(target_id, executed_shares);

                    if (current_shares == executed_shares) {
                        uint64_t last_id = active_ids.back();
                        active_ids[random_idx] = last_id;
                        id_to_vector_idx[last_id] = random_idx;
                        active_ids.pop_back();
                        id_to_vector_idx.erase(target_id);
                    }
                }
            }

            REQUIRE(lob.get_best_bid() == test_book.get_best_bid());
            REQUIRE(lob.get_best_offer() == test_book.get_best_offer());
        }
    }
}