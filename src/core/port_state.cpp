// SPDX-License-Identifier: Apache-2.0
#include "core/port_state.h"

namespace core {

void PortState::apply(const DigitalEvent &event)
{
    if (event.port >= ports_.size()) {
        return;
    }
    PortStatus &p = ports_[event.port];
    if (pending_.size() != ports_.size()) {
        pending_.assign(ports_.size(), 0);
    }
    if (event.kind == DigitalEvent::Transition) {
        if (p.level != event.level) {
            p.transitions++;
            pending_[event.port]++;
            p.last_change_ns = event.ns;
        }
    }
    p.level = event.level;
    if (event.ns > last_ns_) {
        last_ns_ = event.ns;
    }
}

void PortState::set_direction(int port, PortDirection direction)
{
    if (port >= 0 && static_cast<size_t>(port) < ports_.size()) {
        ports_[static_cast<size_t>(port)].direction = direction;
    }
}

void PortState::reset()
{
    for (PortStatus &p : ports_) {
        p = PortStatus{};
    }
    pending_.assign(ports_.size(), 0);
    last_ns_ = -1;
}

void PortState::end_frame()
{
    if (pending_.size() != ports_.size()) {
        pending_.assign(ports_.size(), 0);
    }
    for (size_t i = 0; i < ports_.size(); i++) {
        ports_[i].recent = pending_[i];
        pending_[i] = 0;
    }
}

}  // namespace core
