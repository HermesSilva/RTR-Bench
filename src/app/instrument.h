// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - what every instrument is to the application: something that
// draws itself in its window, receives the probe events of the ports wired
// to it, and has numbered inputs (channels) the rack can wire. There may be
// several instances of a kind (ctrl+click on its rack key): an instrument
// is identified by its kind and its instance number.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/probe.h"

namespace ui {
class Window;
}

namespace app {

enum class Instrument { Scope, Logic, Generator, Supply, Multimeter };

const char *instrument_name(Instrument kind);   // "SCOPE"

struct InstrumentId {
    Instrument kind;
    int instance;   // 0 for the first of its kind
    bool operator==(const InstrumentId &o) const { return kind == o.kind && instance == o.instance; }
    bool operator!=(const InstrumentId &o) const { return !(*this == o); }
};

// "SCOPE", "SCOPE #2"
std::string instrument_label(InstrumentId id);

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
    // Settings of the instrument (its own JSON file, see app/settings.h).
    virtual void save(nlohmann::json &out) const { (void)out; }
    virtual void load(const nlohmann::json &in) { (void)in; }

    void set_instance(int instance) { instance_ = instance; }
    int instance() const { return instance_; }
    InstrumentId id() const { return InstrumentId{kind(), instance_}; }
    // "" for the first instance, "  #2" for the second...
    std::string title_suffix() const { return instance_ > 0 ? "  #" + std::to_string(instance_ + 1) : ""; }

private:
    int instance_ = 0;
};

}  // namespace app
