// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the application: owns the probe, the port state, the windows
// and runs the frame loop.
#pragma once

#include <chrono>
#include <memory>
#include <vector>

#include "app/rack.h"
#include "core/port_state.h"
#include "core/probe.h"
#include "probes/emulator_probe.h"
#include "ui/theme.h"
#include "ui/window.h"

namespace app {

enum class Instrument { Scope, Logic, Generator, Supply, Multimeter };

class App {
public:
    App();
    ~App();

    // Runs until the rack closes. Returns the process exit code.
    int run();

    // For the rack and the instruments.
    core::Probe &probe() { return *probe_; }
    const core::PortState &ports() const { return ports_; }
    double events_per_second() const { return events_per_second_; }
    void open_instrument(Instrument kind);
    bool instrument_open(Instrument kind) const;
    void set_theme(ui::ThemeKind kind);

private:
    void open_rack();
    void pump_probe();

    std::unique_ptr<probes::EmulatorProbe> probe_;
    core::PortState ports_{28};
    std::vector<core::DigitalEvent> events_;
    std::vector<probes::EmulatorProbe::FunctionChange> functions_;

    std::unique_ptr<ui::Window> rack_window_;
    Rack rack_{*this};
    struct OpenInstrument {
        Instrument kind;
        std::unique_ptr<ui::Window> window;
    };
    std::vector<OpenInstrument> instruments_;

    uint64_t last_event_count_ = 0;
    std::chrono::steady_clock::time_point last_rate_time_;
    double events_per_second_ = 0.0;
};

}  // namespace app
