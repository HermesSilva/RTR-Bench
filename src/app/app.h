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

// A wire from a port of the target to a channel of an instrument instance.
struct Wire {
    InstrumentId instrument;
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
    // PNG of each named window ("rack", "scope", "bench", ...) and quits.
    void screenshot(const std::string &window_name, const std::string &path);
    void set_screenshot_delay(double delay_seconds) { screenshot_delay_ = delay_seconds; }
    // Start-up actions from the command line: probe, open an instrument,
    // make a wire, place a window, presets.
    void probe_at_start(ProbeKind kind)
    {
        probe_kind_ = kind;
        probe_from_command_line_ = true;
    }
    void open_at_start(Instrument kind) { open_at_start_.push_back(kind); }
    void wire_at_start(Instrument kind, int channel, int port)
    {
        wires_at_start_.push_back(Wire{InstrumentId{kind, 0}, channel, port});
    }
    struct Placement {
        std::string window;   // "rack" or an instrument name as on the command line
        int x;
        int y;
    };
    void place_at_start(const std::string &window, int x, int y) { placements_.push_back(Placement{window, x, y}); }
    struct MathPreset {
        int channel;
        std::string formula;
    };
    void math_at_start(int channel, const std::string &formula) { math_presets_.push_back(MathPreset{channel, formula}); }
    const std::vector<MathPreset> &math_presets() const { return math_presets_; }
    void generator_on_at_start(int output) { generator_on_.push_back(output); }
    void theme_at_start(ui::ThemeKind kind)
    {
        theme_ = kind;
        theme_from_command_line_ = true;
    }
    const std::vector<int> &generator_outputs_on() const { return generator_on_; }

    // For the rack and the instruments.
    core::Probe &probe() { return *probe_; }
    const core::PortState &ports() const { return ports_; }
    ProbeKind probe_kind() const { return probe_kind_; }
    void switch_probe(ProbeKind kind);   // deferred to between frames
    const core::PortInfo *port_info(int port) const;
    // Opens an instrument: raises the existing one, or makes a new instance
    // when asked (ctrl+click on the rack key).
    void open_instrument(Instrument kind, bool new_instance = false);
    bool instrument_open(Instrument kind) const;
    void set_theme(ui::ThemeKind kind);

    // Wiring. A wire is made from either end: the rack selects a port, an
    // instrument offers a channel; when both are known the wire exists.
    // Clicking a wired end again removes the wire.
    const std::vector<Wire> &wires() const { return wires_; }
    int wired_port(InstrumentId instrument, int channel) const;   // -1 when none
    bool port_wired_to(int port, InstrumentId &instrument, int &channel) const;
    uint32_t port_wire_colour(int port) const;                    // 0 when none
    void select_port(int port);                                   // from the rack
    int selected_port() const { return selected_port_; }
    void offer_channel(InstrumentId instrument, int channel);     // from an instrument
    bool channel_offered(InstrumentId instrument, int channel) const;
    void unwire(InstrumentId instrument, int channel);
    // A wire made by an instrument itself (channels renumbered after a removal).
    void rewire(InstrumentId instrument, int channel, int port) { make_wire(instrument, channel, port); }
    void cancel_wiring();

    // Where the ends of the wires are on the desktop, reported every frame by
    // the rack (ports) and the instruments (channels) from their windows.
    void anchor_port(int port, ui::Window &window, float local_x, float local_y);
    void anchor_channel(InstrumentId instrument, int channel, ui::Window &window, float local_x, float local_y);
    bool wires_across_desktop() const { return overlays_enabled_; }
    bool wires_shown() const { return wires_shown_; }
    void show_wires(bool shown) { wires_shown_ = shown; }

private:
    void open_rack();
    void open_pending();
    void create_instrument(Instrument kind, int instance, int x, int y);
    int next_instance(Instrument kind) const;
    void pump_probe();
    void make_wire(InstrumentId instrument, int channel, int port);
    InstrumentBase *find_instrument(InstrumentId id);
    void create_probe(ProbeKind kind);

    std::unique_ptr<core::Probe> probe_;
    ProbeKind probe_kind_ = ProbeKind::Emulator;
    bool probe_from_command_line_ = false;
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
    struct PendingOpen {
        Instrument kind;
        bool new_instance;
    };
    std::vector<PendingOpen> pending_open_;

    std::vector<Wire> wires_;
    int selected_port_ = -1;
    bool offered_ = false;
    InstrumentId offered_instrument_{Instrument::Scope, 0};
    int offered_channel_ = -1;

    // Wires across the desktop: anchors in desktop coordinates and one
    // overlay window per wire (plus one for the cable being made).
    struct Anchor {
        bool valid = false;
        float x = 0.0f;
        float y = 0.0f;
    };
    std::vector<Anchor> port_anchors_;
    struct ChannelAnchor {
        InstrumentId instrument;
        int channel;
        Anchor anchor;
    };
    std::vector<ChannelAnchor> channel_anchors_;
    struct Overlay {
        std::unique_ptr<ui::Window> window;
        float from_x = 0.0f;
        float from_y = 0.0f;
        float to_x = 0.0f;
        float to_y = 0.0f;
        uint32_t colour = 0;
        bool dangling = false;
        bool used = false;
    };
    std::vector<Overlay> overlays_;
    bool overlays_enabled_ = false;
    bool wires_shown_ = true;
    bool force_wires_ = false;
    bool bench_was_focused_ = false;
    bool bench_focused() const;
    void begin_anchors();
    void update_overlays();
    Overlay &overlay_slot(size_t index);
    const Anchor *channel_anchor(InstrumentId instrument, int channel) const;

    // The composite screenshot: every window and cable at its place on the desktop.
    struct Layer {
        std::vector<uint8_t> rgba;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };
    std::vector<Layer> bench_layers_;
    void request_bench_capture();
    bool collect_bench_capture();
    void write_bench_capture(const std::string &path);

    // Settings: bench.json (theme, probe, windows, wires) and one file per
    // instrument instance, read at start and written on exit and every few seconds.
    void load_bench_settings();
    void save_bench_settings();
    static std::string settings_name(InstrumentId id);
    std::chrono::steady_clock::time_point last_save_time_;
    struct SavedWindow {
        InstrumentId id;
        int x;
        int y;
    };
    std::vector<SavedWindow> saved_instruments_;   // from bench.json, applied when opened
    int rack_x_ = 40;
    int rack_y_ = 40;

    struct ScreenshotTarget {
        std::string window;
        std::string path;
    };
    std::vector<ScreenshotTarget> screenshots_;
    double screenshot_delay_ = 5.0;
    std::vector<Instrument> open_at_start_;
    std::vector<Wire> wires_at_start_;
    std::vector<Placement> placements_;
    std::vector<MathPreset> math_presets_;
    std::vector<int> generator_on_;
    ui::ThemeKind theme_ = ui::ThemeKind::Dark;
    bool theme_from_command_line_ = false;

    uint64_t last_event_count_ = 0;
    std::chrono::steady_clock::time_point last_rate_time_;
    double events_per_second_ = 0.0;

public:
    double events_per_second() const { return events_per_second_; }
};

}  // namespace app
