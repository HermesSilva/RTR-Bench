// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - what every instrument is to the application: something that
// draws itself in its window, receives the probe events of the ports wired
// to it, and has numbered inputs (channels) the rack can wire.
#pragma once

#include <cstdint>
#include <vector>

#include "core/probe.h"

namespace ui {
class Window;
}

namespace app {

enum class Instrument { Scope, Logic, Generator, Supply, Multimeter };

const char *instrument_name(Instrument kind);   // "SCOPE"

class InstrumentBase {
public:
    virtual ~InstrumentBase() = default;
    virtual Instrument kind() const = 0;
    virtual int channel_count() const = 0;
    virtual void draw(ui::Window &window) = 0;
    // Events of this frame, all ports; the instrument keeps those it is wired to.
    virtual void feed(const std::vector<core::DigitalEvent> &events) = 0;
    virtual void feed_analog(const std::vector<core::AnalogBlock> &blocks) { (void)blocks; }
    // The wiring changed (a channel got or lost its port).
    virtual void wiring_changed() {}
};

}  // namespace app
