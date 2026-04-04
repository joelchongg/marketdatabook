#include <catch2/catch_test_macros.hpp>
#include "utils/HierarchicalBitset.h"

#include <set>
#include <random>

TEST_CASE("Hierarchical Bitset: Test") {
    // 1. Initialize your data structures
    utils::HierarchicalBitset<100000> bitset;
    std::set<size_t> st;

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> index_dist(0, 99999); // MAX_TICKS
    std::uniform_int_distribution<int> op_dist(0, 1); // 0 = clear, 1 = set

    SECTION("1,000,000 random operations") {
        for (int i = 0; i < 1000000; ++i) {
            size_t idx = index_dist(rng);
            int op = op_dist(rng);

            if (op == 0) {
                bitset.clear_price_level(idx);
                st.erase(idx);
            } else {
                bitset.set_price_level(idx);
                st.insert(idx);
            }

            if (st.empty()) {
                REQUIRE(bitset.get_best_bid() == static_cast<size_t>(-1));
                REQUIRE(bitset.get_best_offer() == static_cast<size_t>(-1));
            } else {
                REQUIRE(bitset.get_best_bid() == *st.rbegin());
                REQUIRE(bitset.get_best_offer() == *st.begin());
            }
        }
    }
}