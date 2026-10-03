// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - single-producer, single-consumer lock-free ring of events,
// between the acquisition thread of a probe and the interface thread.
#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace core {

template <typename T>
class EventQueue {
public:
    // Capacity is rounded up to a power of two; one slot stays empty.
    explicit EventQueue(size_t capacity)
    {
        size_t n = 2;
        while (n < capacity + 1) {
            n *= 2;
        }
        slots_.resize(n);
        mask_ = n - 1;
    }

    // Producer. Returns false (and drops the event) when the ring is full.
    bool push(const T &item)
    {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next = (head + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        slots_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer. Appends everything available to `out`; returns the count.
    size_t drain(std::vector<T> &out)
    {
        size_t tail = tail_.load(std::memory_order_relaxed);
        size_t head = head_.load(std::memory_order_acquire);
        size_t count = 0;
        while (tail != head) {
            out.push_back(slots_[tail]);
            tail = (tail + 1) & mask_;
            count++;
        }
        tail_.store(tail, std::memory_order_release);
        return count;
    }

    size_t capacity() const { return mask_; }

private:
    std::vector<T> slots_;
    size_t mask_ = 0;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};

}  // namespace core
