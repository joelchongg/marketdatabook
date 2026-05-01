#include <catch2/catch_test_macros.hpp>
#include <random>

#include "utils/StaticOrderMap.h"


TEST_CASE("Static Order Map Find Test") {
    constexpr int SIZE = 64;
    utils::StaticOrderMap<SIZE> order_map;

    // empty order map, find() should return nullptr
    REQUIRE(order_map.find(1) == nullptr);

    // insert an element into the order map and attempt to find it
    uint64_t order_id = 1;
    uint32_t pool_idx_0 = 1;
    order_map.insert(order_id, pool_idx_0);

    // check that bucket order id and pool index is correct
    REQUIRE(order_map.find(order_id)->order_id == order_id);
    REQUIRE(order_map.find(order_id)->pool_idx == pool_idx_0);

    // insert multiple elements that have the same hash value to test open addressing (hash value 3)
    uint32_t pool_idx = 10000;
    for (int i = 0; i < 10; ++i) {
        order_map.insert(3 + i * SIZE, pool_idx + i);
    }

    for (int i = 0; i < 10; ++i) {
        REQUIRE(order_map.find(3 + i * SIZE)->order_id == static_cast<uint64_t>(3 + i * SIZE));
        REQUIRE(order_map.find(3 + i * SIZE)->pool_idx == static_cast<uint32_t>(pool_idx + i));
    }

    // set elements in the middle of open addressed hashed buckets to a tombstone value, and test access after tombstone
    for (int i = 1; i < 10; i += 2) {
        order_map.erase(order_map.find(3 + i * SIZE));
    }

    REQUIRE(order_map.find(3 + 8 * SIZE) != nullptr);
}

TEST_CASE("Static Order Map Full Find Test") {
    // test for finding a nonexistent element when the order map is full
    constexpr int SIZE = 64;
    utils::StaticOrderMap<SIZE> order_map;

    uint64_t order_id = 1;
    uint32_t pool_idx = 1;
    for (int i = 0; i < SIZE; ++i) {
        order_map.insert(order_id + i * SIZE, pool_idx + i * SIZE);
    }

    REQUIRE(order_map.find(0) == nullptr);

    // ensure that order map was full
    auto result = order_map.insert(0, 0);
    REQUIRE(result.first == order_map.end());
    REQUIRE(result.second == false);
}

TEST_CASE("Static Order Map Open Addressing Wraparound") {
    // test that wrap around is valid and works for open addressing
    constexpr int SIZE = 4096; // exactly the same as the size of a page, to check if it triggers SIGSEGV. 
    // This test is better than a smaller capacity as a whole page is allocated even when capacity is smaller, which may not trigger SIGSEGV
    utils::StaticOrderMap<SIZE> order_map;

    uint64_t order_id = SIZE - 1;
    uint32_t pool_idx = 1;

    // insert starting at the last hash index which is 63, and hash repeated elements to hash value 63 to trigger wraparound
    for (int i = 0; i < 10; ++i) {
        REQUIRE(order_map.insert(order_id + i * SIZE, pool_idx + i * SIZE).second == true);
    }
}

TEST_CASE("Tombstone Overwrite Test") {
    // test that new orders will overwrite past tombstone values
    constexpr int SIZE = 64;
    utils::StaticOrderMap<SIZE> order_map;

    uint64_t order_id = 1;
    uint32_t pool_idx = 1;

    auto res1 = order_map.insert(order_id, pool_idx);
    order_map.erase(res1.first);
    auto res2 = order_map.insert(order_id + SIZE, pool_idx);
    REQUIRE(res1 == res2);
}

TEST_CASE("Differential Fuzzer Test") {
    constexpr int SIZE = 1 << 20;
    utils::StaticOrderMap<SIZE> order_map;
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint64_t> id_dist(0, std::numeric_limits<uint64_t>::max());
    std::uniform_int_distribution<uint32_t> pool_idx_dist(0, std::numeric_limits<uint32_t>::max());

    std::vector<uint64_t> all_order_ids;
    all_order_ids.reserve(500'000);

    // 500'000 random order_ids inserted (order ids should be unique)
    for (int i = 0; i < 500'000; ++i) {
        uint64_t order_id = id_dist(rng);
        uint32_t pool_idx = pool_idx_dist(rng);
        while (order_map.find(order_id) != nullptr) {
            order_id = id_dist(rng);
        }
        order_map.insert(order_id, pool_idx);
        all_order_ids.emplace_back(order_id);
    }

    // check that all 500'000 values are in the order map.
    for (uint64_t order_id : all_order_ids) {
        REQUIRE(order_map.find(order_id) != nullptr);
    }
}