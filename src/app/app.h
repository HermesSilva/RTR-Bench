// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the application: owns the probe, the port state, the wires,
// the windows and runs the frame loop.
#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <utility>
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

class AudioPorts;

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
    // "--tile on": once the windows have their sizes they are laid out in
    // columns, none over another (the composite screenshots use it: sizes
    // depend on the instruments and on the audio devices of the computer).
    void tile_at_start() { tile_ = true; }
    // "--circuit FILE": the circuit bench opens with the circuit of a file
    // (as saved in lab.json) instead of the one it had.
    void circuit_at_start(const std::string &path) { circuit_file_ = path; }
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
    // An output of one instrument into an input of another (--link gen:1=scope:1).
    void link_at_start(Instrument out, int out_channel, Instrument in, int in_channel)
    {
        pending_virtual_wires_.push_back(PendingVirtualWire{InstrumentId{in, 0}, in_channel, InstrumentId{out, 0}, out_channel});
    }
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
    // "--psu 1=3.8": output 1 of the supply starts on at 3.8 V.
    void supply_on_at_start(int output, float volts) { supply_on_.push_back({output, volts}); }
    const std::vector<std::pair<int, float>> &supply_outputs_on() const { return supply_on_; }
    // "--patch 1=15": audio jack 1 of the rack (a source) straight into audio jack 15 (an output).
    void audio_patch_at_start(int source, int sink) { audio_patches_.push_back({source, sink}); }
    // "--dmm 1=adc": the function tip 1 of the multimeter starts on, by its number.
    void multimeter_function_at_start(int tip, int function) { multimeter_functions_.push_back({tip, function}); }
    const std::vector<std::pair<int, int>> &multimeter_functions() const { return multimeter_functions_; }

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
    // The colour of the wire of a channel of an instrument.
    uint32_t wire_colour(InstrumentId instrument, int channel) const;
    const InstrumentBase *instrument(InstrumentId id) const;      // null when not open
    InstrumentBase *instrument(InstrumentId id) { return find_instrument(id); }
    // The audio of the computer: an instrument without a window, its jacks are on the rack.
    AudioPorts *audio() { return audio_.get(); }
    void select_port(int port);                                   // from the rack
    int selected_port() const { return selected_port_; }
    void offer_channel(InstrumentId instrument, int channel);     // from an instrument
    bool channel_offered(InstrumentId instrument, int channel) const;
    void unwire(InstrumentId instrument, int channel);
    // A wire made by an instrument itself (channels renumbered after a removal).
    void rewire(InstrumentId instrument, int channel, int port) { make_wire(instrument, channel, port); }
    void cancel_wiring();

    // Instrument to instrument. An output (generator, supply) publishes what
    // it produces every frame; the bench gives it a virtual port that any
    // input (scope, logic analyzer, multimeter) can be wired to, and
    // generates its events as the probe would. Returns the virtual port.
    int publish_output(InstrumentId instrument, int channel, const core::WaveSpec &spec, bool on);
    static constexpr int virtual_port_base = 10000;
    static bool is_virtual_port(int port) { return port >= virtual_port_base && port < circuit_port_base; }
    // What an output publishes; false when it has published nothing yet.
    bool output_spec(InstrumentId instrument, int channel, core::WaveSpec &spec, bool &on) const;
    // The current an output delivers into the circuit it is wired to; false elsewhere.
    bool output_current(InstrumentId instrument, int channel, float &amps) const;

    // Circuit ports: the points of the schematic a cable is plugged into.
    // The circuit bench declares one per cable; inputs read its voltage,
    // outputs drive it.
    static constexpr int circuit_port_base = 20000;
    static bool is_circuit_port(int port) { return port >= circuit_port_base && port < audio_port_base; }
    // Audio ports: the sources of the audio strip of the rack (inputs of the
    // computer and what it is playing), as ports any input of an instrument
    // can be wired to. 30000 + the channel of the jack.
    static constexpr int audio_port_base = 30000;
    static bool is_audio_port(int port) { return port >= audio_port_base; }
    void declare_circuit_port(int slot, const std::string &name);
    void remove_circuit_port(int slot);   // and the cable in it
    // The cable in hand, when it hangs from an instrument.
    bool cable_offered() const { return offered_; }
    // Puts in hand a cable that hangs from a channel, whatever that channel
    // is wired to (an instrument that keeps cables of its own uses it when
    // one of their ends is taken out).
    void hold_cable(InstrumentId instrument, int channel)
    {
        offered_ = true;
        offered_instrument_ = instrument;
        offered_channel_ = channel;
        selected_port_ = -1;
    }
    // Which channel that cable hangs from; false when there is none.
    bool offered(InstrumentId &instrument, int &channel) const
    {
        instrument = offered_instrument_;
        channel = offered_channel_;
        return offered_;
    }
    static bool is_output_instrument(Instrument kind) { return kind == Instrument::Generator || kind == Instrument::Supply; }

    // A click within `grab_radius` of a wire end (outside the jack itself)
    // grabs that end of the cable; called by every window after its widgets.
    void grab_near(ui::Window &window);
    static constexpr float grab_radius = 20.0f;

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
    std::unique_ptr<AudioPorts> audio_;
    nlohmann::json audio_settings_;   // from bench.json, for the audio ports once they exist
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

    // Virtual ports: one per published output of an instrument.
    struct VirtualPort {
        InstrumentId output;
        int channel;
        core::WaveSpec spec;
        bool on = false;
        core::PortInfo info;
        int level = 0;          // digital state
        double phase = 0.0;     // analog state
        int64_t edge_ns = 0;    // next digital edge
        bool seen = false;      // published this frame
    };
    std::vector<VirtualPort> virtual_ports_;
    int64_t virtual_clock_ns_ = 0;   // where the virtual generation got to
    std::chrono::steady_clock::time_point virtual_start_;
    int64_t probe_latest_ns_ = 0;        // the probe's latest timestamp
    int64_t probe_latest_wall_ns_ = 0;   // wall time (since virtual_start_) when it arrived
    bool probe_offset_known_ = false;
    void pump_virtual_ports();
    VirtualPort *virtual_port(int port);
    struct PendingVirtualWire {
        InstrumentId input;
        int channel;
        InstrumentId output;
        int output_channel;
    };
    std::vector<PendingVirtualWire> pending_virtual_wires_;   // from bench.json, resolved when published

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
    std::vector<std::pair<int, Anchor>> circuit_anchors_;
    const Anchor *port_anchor(int port);   // of any port: probe, output or circuit
    std::vector<core::PortInfo> circuit_ports_;   // by slot; an empty name is a free slot
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
        bool on_top;
    };
    std::vector<SavedWindow> saved_instruments_;   // from bench.json, applied when opened
    int rack_x_ = 40;
    int rack_y_ = 40;
    bool rack_on_top_ = false;

    struct ScreenshotTarget {
        std::string window;
        std::string path;
    };
    std::vector<ScreenshotTarget> screenshots_;
    double screenshot_delay_ = 5.0;
    std::vector<Instrument> open_at_start_;
    bool tile_ = false;
    bool tiled_ = false;
    void tile_windows();
    std::string circuit_file_;
    std::vector<Wire> wires_at_start_;
    std::vector<Placement> placements_;
    std::vector<MathPreset> math_presets_;
    std::vector<int> generator_on_;
    std::vector<std::pair<int, float>> supply_on_;
    std::vector<std::pair<int, int>> multimeter_functions_;
    std::vector<std::pair<int, int>> audio_patches_;
    ui::ThemeKind theme_ = ui::ThemeKind::Dark;
    bool theme_from_command_line_ = false;

    uint64_t last_event_count_ = 0;
    std::chrono::steady_clock::time_point last_rate_time_;
    double events_per_second_ = 0.0;

public:
    double events_per_second() const { return events_per_second_; }
};

}  // namespace app
