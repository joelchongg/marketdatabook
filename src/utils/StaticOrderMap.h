#pragma once

#include "book/LimitOrderBook.h"
#include "MagicBuffer.h"

#include <sys/mman.h>
#include <utility>

namespace utils {

enum class BucketState : uint32_t {
    Empty = 0,
    Occupied = 1,
    Tombstone = 2
};

// Alternative idea is to leverage the fact that the order_id is already in a register,
// and we can use order_id == 0 to signify empty, and pool_index 0xFFFFFFFF to signify tombstone
// so that it compiles down to 1 TEST instruction instead of an additional instruction to
// store state in a register. If this is a bottleneck, may want to use this optimization.

// defines the pair of values for a map entry
struct alignas(16) Bucket {
    uint64_t order_id;
    uint32_t pool_idx;
    BucketState state;
};

/*
* StaticOrderMap is an open-addressed hash table that uses linear probing.
* The idea is to leverage cache locality by using linear probing.
*/
template <size_t Capacity>
class StaticOrderMap {
public:
    // Enforce that our capacity is a power of 2
    static_assert(__builtin_popcount(Capacity) == 1);

    StaticOrderMap() {
        size_t bytes_needed = Capacity * sizeof(Bucket);
        size_t page_aligned_bytes_needed = (bytes_needed + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        void* map_addr = mmap(NULL, page_aligned_bytes_needed, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);

        if (map_addr == MAP_FAILED) [[unlikely]] {
            throw std::runtime_error("StaticOrderMap: Unable to allocate memory for StaticOrderMap.");
        }

        map_ = reinterpret_cast<Bucket *>(map_addr);
        end_ = reinterpret_cast<Bucket *>(static_cast<char *>(map_addr) + page_aligned_bytes_needed);
        capacity_ = Capacity;
        mapped_bytes_ = page_aligned_bytes_needed;
    }

    ~StaticOrderMap() {
        munmap(map_, mapped_bytes_);
    }

    // delete copy semantics
    StaticOrderMap(const StaticOrderMap&) = delete;
    StaticOrderMap& operator=(const StaticOrderMap&) = delete;

    // allow move semantics
    StaticOrderMap(StaticOrderMap&& other)
        : map_ { std::exchange(other.map_, nullptr) }, end_ { std::exchange(other.end_, nullptr) }, capacity_ { other.capacity_ }
    { }

    StaticOrderMap& operator=(StaticOrderMap&& other) {
        if (&other == this) {
            return *this;
        }

        if (map_) {
            munmap(map_, mapped_bytes_);
        }

        map_ = std::exchange(other.map_, nullptr);
        end_ = other.end_;
        capacity_ = other.capacity_;
        mapped_bytes_ = other.mapped_bytes_;

        return *this;
    }

    uint32_t& operator[](size_t order_id) {
        Bucket* found = find(order_id);
        if (found == nullptr) {
            auto p = insert(order_id, 0);
            if (p.second) [[likely]] {
                return p.first->pool_idx;
            }

            throw std::runtime_error("StaticOrderMap (operator[]): Map is full, unable to allocate any more elements.");
        }

        return found->pool_idx;
    }

    /*
    * Returns a pointer to the Bucket Element if it exists in the hash map
    * Else, returns a null pointer.
    */
    Bucket* find(uint64_t order_id) {
        size_t idx = order_id & (capacity_ - 1);
        size_t num_elements_checked = 0;
        Bucket* curr_location = map_ + idx;

        while (num_elements_checked < capacity_ && curr_location->state != BucketState::Empty) {
            if (curr_location->order_id == order_id && curr_location->state == BucketState::Occupied) {
                return curr_location;
            }

            ++num_elements_checked;
            curr_location++;

            // should be well predicted
            if (curr_location == end_) [[unlikely]] {
                curr_location = map_;
            }
        }

        return nullptr;
    }
    
    std::pair<Bucket*, bool> insert(uint64_t order_id, uint32_t pool_idx) {
        size_t idx = order_id & (capacity_ - 1);
        size_t num_elements_checked = 0;

        Bucket* curr_location = map_ + idx;
        while (num_elements_checked < capacity_ && curr_location->state == BucketState::Occupied && curr_location->order_id != order_id) {
            ++num_elements_checked;
            curr_location++;
    
            // should be well predicted
            if (curr_location == end_) [[unlikely]] {
                curr_location = map_;
            }
        }

        // found a spot / found the previous element
        // Note that this spot may be a tombstone. We do not check further to ensure we do not add a duplicate
        // as order ids are usually unique throughout the day and will not be duplicated.
        if (num_elements_checked < capacity_) {
            curr_location->order_id = order_id;
            curr_location->pool_idx = pool_idx;
            curr_location->state = BucketState::Occupied;

            return {curr_location, true};
        }
        return {end_, false};
    }

    void erase(Bucket* entry_ptr) {
        if (entry_ptr < map_ || entry_ptr >= end_) [[unlikely]] {
            throw std::runtime_error("StaticOrderMap: Unable to erase an element that is not from the map.");
        }

        entry_ptr->state = BucketState::Tombstone;
    }

    void erase(uint64_t order_id) {
        Bucket* found = find(order_id);
        if (found) {
            found->state = BucketState::Tombstone;
        }
    }

    Bucket* end() { return end_; }



private:
    Bucket* map_; // map will store contiguous pairs of <uint64_t, uint64_t> instead of uint32_t for the value in order to ensure alignment
    Bucket* end_;
    size_t capacity_;
    size_t mapped_bytes_;
};

} // namespace utils