// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - a digital trace: the history of one port as a ring of
// (time, level) transitions, with binary search by time. One per scope
// channel; the memory depth is the ring capacity.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace core {

struct Transition {
    int64_t ns;
    uint8_t level;   // level after the transition
};

class DigitalTrace {
public:
    explicit DigitalTrace(size_t capacity = 1u << 20);

    void clear();
    // Appends a transition; the oldest one is dropped when the ring is full.
    // Times must not decrease.
    void add(int64_t ns, uint8_t level);
    // A snapshot sets the level without recording a transition when it
    // agrees with the last one; otherwise it is recorded.
    void snapshot(int64_t ns, uint8_t level);

    size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    size_t capacity() const { return slots_.size(); }
    const Transition &at(size_t i) const { return slots_[(first_ + i) % slots_.size()]; }
    int64_t first_ns() const { return count_ ? at(0).ns : -1; }
    int64_t last_ns() const { return count_ ? at(count_ - 1).ns : -1; }

    // Index of the first transition at or after `ns` (count when none).
    size_t lower_bound(int64_t ns) const;
    // Level at time `ns`: the level after the last transition before or at
    // `ns`; -1 when the trace knows nothing that early.
    int level_at(int64_t ns) const;
    // Current level, -1 when unknown.
    int level() const { return count_ ? at(count_ - 1).level : -1; }

private:
    std::vector<Transition> slots_;
    size_t first_ = 0;
    size_t count_ = 0;
};

}  // namespace core
