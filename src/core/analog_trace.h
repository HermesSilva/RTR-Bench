// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - an analog trace: a ring of samples at a fixed interval, with
// the time of the first sample kept, so that any sample has a time and any
// time has a sample. A gap or a rate change in the incoming blocks restarts
// the ring (the screen is told through `generation`).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace core {

class AnalogTrace {
public:
    explicit AnalogTrace(size_t capacity = 1u << 20);

    void clear();
    // Appends a block of samples; restarts the ring if it does not continue
    // the previous one (within half a sample interval).
    void add(int64_t t0_ns, int64_t dt_ns, const float *volts, size_t count);

    bool empty() const { return count_ == 0; }
    size_t size() const { return count_; }
    int64_t dt_ns() const { return dt_ns_; }
    int64_t first_ns() const { return start_ns_; }
    int64_t last_ns() const { return count_ ? start_ns_ + static_cast<int64_t>(count_ - 1) * dt_ns_ : -1; }
    uint32_t generation() const { return generation_; }

    float at(size_t i) const { return slots_[(first_ + i) % slots_.size()]; }
    // Index of the sample at or after `ns` (count when past the end, 0 when
    // before the start).
    size_t index_at(int64_t ns) const;
    // Interpolated value at `ns`; false outside the trace.
    bool value_at(int64_t ns, float &volts) const;
    // Smallest and largest sample in [from, to) (indices); false when empty.
    bool min_max(size_t from, size_t to, float &lo, float &hi) const;

private:
    std::vector<float> slots_;
    size_t first_ = 0;
    size_t count_ = 0;
    int64_t start_ns_ = 0;
    int64_t dt_ns_ = 0;
    uint32_t generation_ = 0;
};

}  // namespace core
