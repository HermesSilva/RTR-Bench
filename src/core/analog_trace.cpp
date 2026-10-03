// SPDX-License-Identifier: Apache-2.0
#include "core/analog_trace.h"

#include <algorithm>

namespace core {

AnalogTrace::AnalogTrace(size_t capacity) : slots_(capacity ? capacity : 1) {}

void AnalogTrace::clear()
{
    first_ = 0;
    count_ = 0;
    dt_ns_ = 0;
    generation_++;
}

void AnalogTrace::add(int64_t t0_ns, int64_t dt_ns, const float *volts, size_t count)
{
    if (count == 0 || dt_ns <= 0) {
        return;
    }
    int64_t expected = count_ ? start_ns_ + static_cast<int64_t>(count_) * dt_ns_ : t0_ns;
    int64_t gap = t0_ns - expected;
    int64_t half = dt_ns / 2;
    if (count_ == 0 || dt_ns != dt_ns_ || gap > half || gap < -half) {
        first_ = 0;
        count_ = 0;
        start_ns_ = t0_ns;
        dt_ns_ = dt_ns;
        generation_++;
    }
    size_t n = slots_.size();
    if (n == 0) {
        return;  // the constructor never allows this; keeps the analyzer sure
    }
    for (size_t i = 0; i < count; i++) {
        if (count_ == n) {
            first_ = (first_ + 1) % n;
            count_--;
            start_ns_ += dt_ns_;
        }
        slots_[(first_ + count_) % n] = volts[i];
        count_++;
    }
}

size_t AnalogTrace::index_at(int64_t ns) const
{
    if (count_ == 0 || dt_ns_ <= 0 || ns <= start_ns_) {
        return 0;
    }
    int64_t i = (ns - start_ns_ + dt_ns_ - 1) / dt_ns_;
    return i >= static_cast<int64_t>(count_) ? count_ : static_cast<size_t>(i);
}

bool AnalogTrace::value_at(int64_t ns, float &volts) const
{
    if (count_ == 0 || dt_ns_ <= 0 || ns < start_ns_ || ns > last_ns()) {
        return false;
    }
    int64_t offset = ns - start_ns_;
    size_t i = static_cast<size_t>(offset / dt_ns_);
    if (i + 1 >= count_) {
        volts = at(count_ - 1);
        return true;
    }
    float f = static_cast<float>(offset % dt_ns_) / static_cast<float>(dt_ns_);
    volts = at(i) + (at(i + 1) - at(i)) * f;
    return true;
}

bool AnalogTrace::min_max(size_t from, size_t to, float &lo, float &hi) const
{
    to = std::min(to, count_);
    if (from >= to) {
        return false;
    }
    lo = at(from);
    hi = lo;
    for (size_t i = from + 1; i < to; i++) {
        float v = at(i);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    return true;
}

}  // namespace core
