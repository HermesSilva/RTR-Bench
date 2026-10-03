// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the application: owns the probe, the port state, the wires,
// the windows and runs the frame loop.
#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "app/instrument.h"
#include "app/rack.h"
#include "core/port_state.h"
#include "core/probe.h"
#include "probes/demo_probe.h"
#include "probes/emulator_probe.h"
#include "ui/theme.h"
#include "ui/window.h"

namespace app {

// A wire from a port of the target to a channel of an instrument.
struct Wire {
    Instrument instrument;
    int channel;
    int port;
};

class App {
public:
    enum class ProbeKind { Emulator, Demo };

    App();
    ~App();

    // Runs until the rack closes. Returns the process exit code.
    int run();

    // Documentation screenshots: after `delay_seconds` of running, writes a
    // PNG of each named window ("rack", "scope", ...) and quits.
    void screenshot(const std::string &window_name, const std::string &path);
    void set_screenshot_delay(double delay_seconds) { screenshot_delay_ = delay_seconds; }
    // Start-up actions from the command line: probe, open an instrument, make a wire.
    void probe_at_start(ProbeKind kind) { probe_kind_ = kind; }
    void open_at_start(Instrument kind) { open_at_start_.push_back(kind); }
    void wire_at_start(Instrument kind, int channel, int port) { wires_at_start_.push_back(Wire{kind, channel, port}); }

    // For the rack and the instruments.
    core::Probe &probe() { return *probe_; }
    const core::PortState &ports() const { return ports_; }
    ProbeKind probe_kind() const { return probe_kind_; }
    void switch_probe(ProbeKind kind);   // deferred to between frames
    // The port of the current probe, by index; null when out of range.
    const core::PortInfo *port_info(int port) const;
    double events_per_second() const { return events_per_second_; }
    void open_instrument(Instrument kind);
    bool instrument_open(Instrument kind) const;
    void set_theme(ui::ThemeKind kind);

    // Wiring. A wire is made from either end: the rack selects a port, an
    // instrument offers a channel; when both are known the wire exists.
    // Clicking a wired end again removes the wire.
    const std::vector<Wire> &wires() const { return wires_; }
    int wired_port(Instrument instrument, int channel) const;      // -1 when none
    bool port_wired_to(int port, Instrument &instrument, int &channel) const;
    uint32_t port_wire_colour(int port) const;                      // 0 when none
    void select_port(int port);                                     // from the rack
    int selected_port() const { return selected_port_; }
    void offer_channel(Instrument instrument, int channel);         // from an instrument
    bool channel_offered(Instrument instrument, int channel) const;
    void unwire(Instrument instrument, int channel);

private:
    void open_rack();
    void open_pending();
    void create_instrument(Instrument kind);
    void pump_probe();
    void make_wire(Instrument instrument, int channel, int port);
    InstrumentBase *find_instrument(Instrument kind);

    void create_probe(ProbeKind kind);

    std::unique_ptr<core::Probe> probe_;
    ProbeKind probe_kind_ = ProbeKind::Emulator;
    bool switch_pending_ = false;
    ProbeKind switch_to_ = ProbeKind::Emulator;
    core::PortState ports_{28};
    std::vector<core::DigitalEvent> events_;
    std::vector<core::AnalogBlock> analog_;
    std::vector<probes::EmulatorProbe::FunctionChange> functions_;

    std::unique_ptr<ui::Window> rack_window_;
    Rack rack_{*this};
    struct OpenInstrument {
        std::unique_ptr<InstrumentBase> instrument;
        std::unique_ptr<ui::Window> window;
    };
    std::vector<OpenInstrument> instruments_;

    std::vector<Wire> wires_;
    int selected_port_ = -1;
    bool offered_ = false;
    Instrument offered_instrument_ = Instrument::Scope;
    int offered_channel_ = -1;

    struct ScreenshotTarget {
        std::string window;
        std::string path;
    };
    std::vector<ScreenshotTarget> screenshots_;
    double screenshot_delay_ = 5.0;
    std::vector<Instrument> open_at_start_;
    std::vector<Instrument> pending_open_;
    std::vector<Wire> wires_at_start_;

    uint64_t last_event_count_ = 0;
    std::chrono::steady_clock::time_point last_rate_time_;
    double events_per_second_ = 0.0;
};

}  // namespace app
