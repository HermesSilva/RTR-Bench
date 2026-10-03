// SPDX-License-Identifier: Apache-2.0
#include "core/trace.h"

namespace core {

DigitalTrace::DigitalTrace(size_t capacity) : slots_(capacity ? capacity : 1) {}

void DigitalTrace::clear()
{
    first_ = 0;
    count_ = 0;
}

void DigitalTrace::add(int64_t ns, uint8_t level)
{
    size_t n = slots_.size();
    if (n == 0) {
        return;  // the constructor never allows this; keeps the analyzer sure
    }
    if (count_ == n) {
        first_ = (first_ + 1) % n;
        count_--;
    }
    slots_[(first_ + count_) % n] = Transition{ns, level};
    count_++;
}

void DigitalTrace::snapshot(int64_t ns, uint8_t level)
{
    if (count_ == 0 || at(count_ - 1).level != level) {
        add(ns, level);
    }
}

size_t DigitalTrace::lower_bound(int64_t ns) const
{
    size_t lo = 0;
    size_t hi = count_;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (at(mid).ns < ns) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

int DigitalTrace::level_at(int64_t ns) const
{
    size_t i = lower_bound(ns + 1);  // first transition strictly after ns
    if (i == 0) {
        return -1;
    }
    return at(i - 1).level;
}

}  // namespace core
