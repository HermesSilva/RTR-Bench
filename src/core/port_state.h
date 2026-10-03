// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the present state of every port of the target, kept by the
// interface thread from the events of the probe. The rack shows it; the
// instruments keep their own history.
#pragma once

#include <cstdint>
#include <vector>

#include "core/probe.h"

namespace core {

struct PortStatus {
    int level = -1;                       // -1 unknown
    PortDirection direction = PortDirection::Unknown;
    int64_t last_change_ns = -1;
    uint64_t transitions = 0;             // since the probe connected
    uint32_t recent = 0;                  // transitions in the last window
};

class PortState {
public:
    explicit PortState(size_t ports) : ports_(ports) {}

    void apply(const DigitalEvent &event);
    void set_direction(int port, PortDirection direction);
    void reset();

    // Call once per interface frame; `recent` counts the transitions since
    // the previous call, so the rack can flash an activity mark.
    void end_frame();

    const PortStatus &at(size_t port) const { return ports_[port]; }
    size_t size() const { return ports_.size(); }
    int64_t last_ns() const { return last_ns_; }

private:
    std::vector<PortStatus> ports_;
    std::vector<uint32_t> pending_;
    int64_t last_ns_ = -1;
};

}  // namespace core
