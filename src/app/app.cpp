// SPDX-License-Identifier: Apache-2.0
#include "app/app.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <random>

#include <imgui.h>

#include "app/settings.h"
#include "instruments/dmm/dmm.h"
#include "instruments/generator/generator.h"
#include "instruments/logic/logic.h"
#include "instruments/scope/scope.h"
#include "instruments/supply/supply.h"
#include "ui/png.h"
#include "ui/theme.h"
#include "ui/wire.h"

namespace app {

const char *instrument_name(Instrument kind)
{
    switch (kind) {
    case Instrument::Scope:
        return "SCOPE";
    case Instrument::Logic:
        return "LOGIC";
    case Instrument::Generator:
        return "GEN";
    case Instrument::Supply:
        return "PSU";
    case Instrument::Multimeter:
        return "DMM";
    }
    return "";
}

std::string instrument_label(InstrumentId id)
{
    std::string label = instrument_name(id.kind);
    if (id.instance > 0) {
        label += " #" + std::to_string(id.instance + 1);
    }
    return label;
}

namespace {

const char *probe_name(App::ProbeKind kind)
{
    return kind == App::ProbeKind::Demo ? "demo" : "emulator";
}

const char *theme_name(ui::ThemeKind kind)
{
    return kind == ui::ThemeKind::Light ? "light" : kind == ui::ThemeKind::Amber ? "amber" : "dark";
}

bool instrument_from_name(const std::string &name, Instrument &kind)
{
    const Instrument all[] = {Instrument::Scope, Instrument::Logic, Instrument::Generator, Instrument::Supply,
                              Instrument::Multimeter};
    for (Instrument i : all) {
        if (name == instrument_name(i)) {
            kind = i;
            return true;
        }
    }
    return false;
}

// The name used on the command line ("scope") for an instrument kind.
std::string command_line_name(Instrument kind)
{
    std::string name = instrument_name(kind);
    for (char &c : name) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return name;
}

}  // namespace

App::App()
{
    last_rate_time_ = std::chrono::steady_clock::now();
    virtual_start_ = last_rate_time_;
}

App::~App() = default;

void App::create_probe(ProbeKind kind)
{
    if (probe_) {
        probe_->disconnect();
    }
    switch (kind) {
    case ProbeKind::Demo:
        probe_ = std::make_unique<probes::DemoProbe>();
        break;
    case ProbeKind::Emulator:
        probe_ = std::make_unique<probes::EmulatorProbe>();
        break;
    }
    probe_kind_ = kind;
    ports_ = core::PortState(probe_->ports().size());
    // Ports have another meaning now: every wire is removed.
    wires_.clear();
    selected_port_ = -1;
    offered_ = false;
    for (OpenInstrument &i : instruments_) {
        i.instrument->wiring_changed();
    }
    last_event_count_ = 0;
    events_per_second_ = 0.0;
    probe_->connect();
}

void App::switch_probe(ProbeKind kind)
{
    if (kind == probe_kind_) {
        return;
    }
    switch_pending_ = true;
    switch_to_ = kind;
}

const core::PortInfo *App::port_info(int port) const
{
    if (is_virtual_port(port)) {
        size_t slot = static_cast<size_t>(port - virtual_port_base);
        return slot < virtual_ports_.size() ? &virtual_ports_[slot].info : nullptr;
    }
    const std::vector<core::PortInfo> &ports = probe_->ports();
    if (port < 0 || static_cast<size_t>(port) >= ports.size()) {
        return nullptr;
    }
    return &ports[static_cast<size_t>(port)];
}

void App::open_rack()
{
    ui::WindowSpec spec;
    spec.title = "RTR-Bench Rack";
    spec.width = 940;
    spec.height = 232;
    spec.x = rack_x_;
    spec.y = rack_y_;
    for (const Placement &p : placements_) {
        if (p.window == "rack") {
            spec.x = p.x;
            spec.y = p.y;
        }
    }
    rack_window_ = std::make_unique<ui::Window>(spec);
    if (!rack_window_->valid()) {
        std::fprintf(stderr, "rtr-bench: cannot create the rack window\n");
        rack_window_.reset();
        return;
    }
    rack_window_->set_draw([this](ui::Window &w) { rack_.draw(w); });
}

InstrumentBase *App::find_instrument(InstrumentId id)
{
    for (OpenInstrument &i : instruments_) {
        if (i.instrument->id() == id) {
            return i.instrument.get();
        }
    }
    return nullptr;
}

bool App::instrument_open(Instrument kind) const
{
    for (const OpenInstrument &i : instruments_) {
        if (i.instrument->kind() == kind) {
            return true;
        }
    }
    return false;
}

int App::next_instance(Instrument kind) const
{
    int instance = 0;
    for (bool taken = true; taken; instance++) {
        taken = false;
        for (const OpenInstrument &i : instruments_) {
            if (i.instrument->kind() == kind && i.instrument->instance() == instance) {
                taken = true;
            }
        }
        if (!taken) {
            return instance;
        }
    }
    return instance;
}

// Called from inside a frame (a key on the rack): creating a window there
// would switch the ImGui context under the drawing code, so it is queued
// and done between frames.
void App::open_instrument(Instrument kind, bool new_instance)
{
    if (!new_instance) {
        for (OpenInstrument &i : instruments_) {
            if (i.instrument->kind() == kind) {
                i.window->raise();
                return;
            }
        }
    }
    pending_open_.push_back(PendingOpen{kind, new_instance});
}

void App::open_pending()
{
    std::vector<PendingOpen> pending;
    pending.swap(pending_open_);
    for (const PendingOpen &p : pending) {
        if (!p.new_instance && instrument_open(p.kind)) {
            continue;
        }
        int instance = p.new_instance ? next_instance(p.kind) : 0;
        // A new instance opens a little below and to the right of the last one.
        int x = 40 + 30 * instance;
        int y = 280 + 30 * instance;
        create_instrument(p.kind, instance, x, y);
    }
}

void App::create_instrument(Instrument kind, int instance, int x, int y)
{
    if (find_instrument(InstrumentId{kind, instance})) {
        return;
    }
    std::unique_ptr<InstrumentBase> instrument;
    ui::WindowSpec spec;
    spec.x = x;
    spec.y = y;
    switch (kind) {
    case Instrument::Scope:
        instrument = std::make_unique<Scope>(*this);
        spec.title = "RTR-Bench Oscilloscope";
        spec.width = 940;
        spec.height = 560;
        break;
    case Instrument::Logic:
        instrument = std::make_unique<LogicAnalyzer>(*this);
        spec.title = "RTR-Bench Logic Analyzer";
        spec.width = 940;
        spec.height = 420;
        break;
    case Instrument::Generator:
        instrument = std::make_unique<Generator>(*this);
        spec.title = "RTR-Bench Generator";
        spec.width = 940;
        spec.height = 220;
        break;
    case Instrument::Supply:
        instrument = std::make_unique<Supply>(*this);
        spec.title = "RTR-Bench Power Supply";
        spec.width = 940;
        spec.height = 220;
        break;
    case Instrument::Multimeter:
        instrument = std::make_unique<Multimeter>(*this);
        spec.title = "RTR-Bench Multimeter";
        spec.width = 940;
        spec.height = 230;
        break;
    }
    instrument->set_instance(instance);
    if (instance > 0) {
        spec.title += " " + std::to_string(instance + 1);
    }
    auto window = std::make_unique<ui::Window>(spec);
    if (!window->valid()) {
        return;
    }
    if (screenshots_.empty()) {
        instrument->load(load_settings(settings_name(instrument->id())));   // screenshots: command line only
    }
    InstrumentBase *raw = instrument.get();
    window->set_draw([raw](ui::Window &w) { raw->draw(w); });
    instruments_.push_back(OpenInstrument{std::move(instrument), std::move(window)});
}

void App::set_theme(ui::ThemeKind kind)
{
    ui::set_theme(kind);
}

void App::screenshot(const std::string &window_name, const std::string &path)
{
    screenshots_.push_back(ScreenshotTarget{window_name, path});
}

// ---- wiring ---------------------------------------------------------------

int App::wired_port(InstrumentId instrument, int channel) const
{
    for (const Wire &w : wires_) {
        if (w.instrument == instrument && w.channel == channel) {
            return w.port;
        }
    }
    return -1;
}

bool App::port_wired_to(int port, InstrumentId &instrument, int &channel) const
{
    for (const Wire &w : wires_) {
        if (w.port == port) {
            instrument = w.instrument;
            channel = w.channel;
            return true;
        }
    }
    return false;
}

uint32_t App::port_wire_colour(int port) const
{
    InstrumentId instrument{Instrument::Scope, 0};
    int channel = 0;
    if (!port_wired_to(port, instrument, channel)) {
        return 0;
    }
    return ui::channel_colour(channel % ui::channel_count);
}

void App::make_wire(InstrumentId instrument, int channel, int port)
{
    bool real = port >= 0 && static_cast<size_t>(port) < probe_->ports().size();
    bool virt = is_virtual_port(port) && port_info(port) != nullptr;
    if ((!real && !virt) || channel < 0) {
        return;
    }
    // Pertinent only: a virtual port (an output) feeds an input instrument.
    if (virt && is_output_instrument(instrument.kind)) {
        return;
    }
    unwire(instrument, channel);
    wires_.push_back(Wire{instrument, channel, port});
    selected_port_ = -1;
    offered_ = false;
    if (InstrumentBase *i = find_instrument(instrument)) {
        i->wiring_changed();
    }
}

void App::unwire(InstrumentId instrument, int channel)
{
    bool removed = false;
    for (size_t i = 0; i < wires_.size(); i++) {
        if (wires_[i].instrument == instrument && wires_[i].channel == channel) {
            wires_.erase(wires_.begin() + static_cast<std::ptrdiff_t>(i));
            removed = true;
            break;
        }
    }
    if (removed) {
        if (InstrumentBase *i = find_instrument(instrument)) {
            i->wiring_changed();
        }
    }
}

void App::select_port(int port)
{
    if (offered_) {
        make_wire(offered_instrument_, offered_channel_, port);
        return;
    }
    // A wired jack: the click grabs that end of the cable, which then hangs
    // from the instrument until it is plugged somewhere else.
    InstrumentId wired{Instrument::Scope, 0};
    int channel = 0;
    if (selected_port_ < 0 && port_wired_to(port, wired, channel)) {
        unwire(wired, channel);
        offered_ = true;
        offered_instrument_ = wired;
        offered_channel_ = channel;
        return;
    }
    selected_port_ = selected_port_ == port ? -1 : port;
}

void App::offer_channel(InstrumentId instrument, int channel)
{
    if (selected_port_ >= 0) {
        make_wire(instrument, channel, selected_port_);
        return;
    }
    // A wired channel: the click grabs that end of the cable, which then
    // hangs from the rack port until it is plugged into another channel.
    int port = wired_port(instrument, channel);
    if (!offered_ && port >= 0) {
        unwire(instrument, channel);
        selected_port_ = port;
        return;
    }
    // Two instrument ends: an output into an input makes a wire through the
    // output's virtual port; any other pair just moves the offer.
    if (offered_ && offered_instrument_ != instrument) {
        bool this_out = is_output_instrument(instrument.kind);
        bool that_out = is_output_instrument(offered_instrument_.kind);
        if (this_out != that_out) {
            InstrumentId out = this_out ? instrument : offered_instrument_;
            int out_channel = this_out ? channel : offered_channel_;
            InstrumentId in = this_out ? offered_instrument_ : instrument;
            int in_channel = this_out ? offered_channel_ : channel;
            for (size_t i = 0; i < virtual_ports_.size(); i++) {
                if (virtual_ports_[i].output == out && virtual_ports_[i].channel == out_channel) {
                    make_wire(in, in_channel, virtual_port_base + static_cast<int>(i));
                    return;
                }
            }
        }
    }
    if (offered_ && offered_instrument_ == instrument && offered_channel_ == channel) {
        offered_ = false;
        return;
    }
    offered_ = true;
    offered_instrument_ = instrument;
    offered_channel_ = channel;
}

bool App::channel_offered(InstrumentId instrument, int channel) const
{
    return offered_ && offered_instrument_ == instrument && offered_channel_ == channel;
}

void App::cancel_wiring()
{
    selected_port_ = -1;
    offered_ = false;
}

// ---- virtual ports: instrument to instrument -----------------------------

App::VirtualPort *App::virtual_port(int port)
{
    if (!is_virtual_port(port)) {
        return nullptr;
    }
    size_t slot = static_cast<size_t>(port - virtual_port_base);
    return slot < virtual_ports_.size() ? &virtual_ports_[slot] : nullptr;
}

int App::publish_output(InstrumentId instrument, int channel, const core::WaveSpec &spec, bool on)
{
    for (size_t i = 0; i < virtual_ports_.size(); i++) {
        VirtualPort &v = virtual_ports_[i];
        if (v.output == instrument && v.channel == channel) {
            bool kind_changed = v.spec.kind != spec.kind || (v.on != on);
            v.spec = spec;
            v.on = on;
            v.seen = true;
            v.info.analog = core::waveform_is_analog(spec.kind);
            v.info.digital = !v.info.analog;
            if (kind_changed) {
                v.phase = 0.0;
                v.edge_ns = virtual_clock_ns_;
            }
            return virtual_port_base + static_cast<int>(i);
        }
    }
    VirtualPort v;
    v.output = instrument;
    v.channel = channel;
    v.spec = spec;
    v.on = on;
    v.seen = true;
    v.edge_ns = virtual_clock_ns_;
    v.info.index = virtual_port_base + static_cast<int>(virtual_ports_.size());
    v.info.name = instrument_label(instrument) + " OUT" + std::to_string(channel + 1);
    v.info.analog = core::waveform_is_analog(spec.kind);
    v.info.digital = !v.info.analog;
    v.info.drivable = false;
    virtual_ports_.push_back(v);
    return v.info.index;
}

// Generates the events of the virtual ports from the last frame to now, on
// the wall clock, into the frame's event and sample lists.
void App::pump_virtual_ports()
{
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> noise(-1.0, 1.0);
    int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - virtual_start_).count();
    int64_t from = virtual_clock_ns_;
    virtual_clock_ns_ = now;
    // Saved wires to an output get made as soon as it has been published.
    for (size_t p = 0; p < pending_virtual_wires_.size();) {
        const PendingVirtualWire &w = pending_virtual_wires_[p];
        bool done = false;
        for (size_t i = 0; i < virtual_ports_.size(); i++) {
            if (virtual_ports_[i].output == w.output && virtual_ports_[i].channel == w.output_channel) {
                make_wire(w.input, w.channel, virtual_port_base + static_cast<int>(i));
                done = true;
            }
        }
        if (done) {
            pending_virtual_wires_.erase(pending_virtual_wires_.begin() + static_cast<std::ptrdiff_t>(p));
        } else {
            p++;
        }
    }
    for (size_t i = 0; i < virtual_ports_.size(); i++) {
        VirtualPort &v = virtual_ports_[i];
        uint16_t port = static_cast<uint16_t>(0);
        (void)port;
        int index = virtual_port_base + static_cast<int>(i);
        bool wired = false;
        for (const Wire &w : wires_) {
            wired = wired || w.port == index;
        }
        if (!wired || !v.seen) {
            v.seen = false;
            continue;
        }
        v.seen = false;
        const core::WaveSpec &s = v.spec;
        core::Waveform kind = v.on ? s.kind : core::Waveform::Off;
        if (core::waveform_is_analog(kind)) {
            // 100 kS/s is plenty for the frequencies the knobs reach on screen.
            const int64_t dt = 10000;
            int64_t first = (from / dt + 1) * dt;
            if (first > now) {
                continue;
            }
            core::AnalogBlock block;
            block.t0_ns = first;
            block.dt_ns = dt;
            block.port = static_cast<uint16_t>(index & 0xFFFF);
            size_t count = static_cast<size_t>((now - first) / dt + 1);
            block.volts.resize(count);
            const double pi = 3.14159265358979323846;
            for (size_t k = 0; k < count; k++) {
                double t = static_cast<double>(first + static_cast<int64_t>(k) * dt) / 1e9;
                double mod = std::sin(2.0 * pi * s.mod_freq_hz * t);
                double f = kind == core::Waveform::FM ? s.freq_hz * (1.0 + s.mod_depth * mod) : s.freq_hz;
                double amp = kind == core::Waveform::AM ? s.amplitude_v * (1.0 + s.mod_depth * mod) / (1.0 + s.mod_depth)
                                                        : s.amplitude_v;
                v.phase += f * static_cast<double>(dt) / 1e9;
                v.phase -= std::floor(v.phase);
                double p = v.phase;
                if (kind == core::Waveform::PM) {
                    p += s.mod_depth * 0.5 * mod;
                    p -= std::floor(p);
                }
                double val = 0.0;
                switch (kind) {
                case core::Waveform::Sine:
                case core::Waveform::AM:
                case core::Waveform::FM:
                case core::Waveform::PM:
                    val = amp * std::sin(2.0 * pi * p);
                    break;
                case core::Waveform::Triangle:
                    val = amp * (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
                    break;
                case core::Waveform::Sawtooth:
                    val = amp * (2.0 * p - 1.0);
                    break;
                case core::Waveform::RampDown:
                    val = amp * (1.0 - 2.0 * p);
                    break;
                case core::Waveform::Noise:
                    val = amp * noise(rng);
                    break;
                case core::Waveform::Dc:
                    val = amp;
                    break;
                case core::Waveform::PwmMod:
                    val = p < 0.5 + 0.45 * s.mod_depth * mod ? amp : -amp;
                    break;
                default:
                    break;
                }
                block.volts[k] = static_cast<float>(val + s.offset_v);
            }
            analog_.push_back(std::move(block));
            continue;
        }
        // Digital: a level, or a pattern on its grid (burst and sweep as the demo probe).
        auto emit = [&](int64_t at, int level) {
            if (v.level != level) {
                v.level = level;
                events_.push_back(core::DigitalEvent{at, static_cast<uint16_t>(index & 0xFFFF), static_cast<uint8_t>(level),
                                                     core::DigitalEvent::Transition});
            }
        };
        if (kind == core::Waveform::Off || kind == core::Waveform::Low) {
            emit(now, 0);
            continue;
        }
        if (kind == core::Waveform::High || kind == core::Waveform::Dc) {
            emit(now, kind == core::Waveform::Dc ? (s.amplitude_v + s.offset_v > 0.0 ? 1 : 0) : 1);
            continue;
        }
        int64_t period = s.freq_hz > 0.0 ? static_cast<int64_t>(1e9 / s.freq_hz) : 0;
        if (period <= 0) {
            continue;
        }
        int duty = kind == core::Waveform::Pwm ? s.duty : 50;
        for (int64_t edge = (from / period) * period; edge <= now + period; edge += period) {
            int64_t p = period;
            int64_t high = p * duty / 100;
            if (kind == core::Waveform::Burst && (edge % 1000000000LL) / period >= s.burst_count) {
                continue;
            }
            if (kind == core::Waveform::Sweep) {
                double frac = static_cast<double>(edge % 1000000000LL) / 1e9;
                double f = s.freq_hz + (s.sweep_end_hz - s.freq_hz) * frac;
                p = static_cast<int64_t>(1e9 / std::max(f, 1.0));
                high = p / 2;
            }
            if (edge > from && edge <= now) {
                emit(edge, 1);
            }
            if (edge + high > from && edge + high <= now) {
                emit(edge + high, 0);
            }
        }
    }
}

// A click near a wire end grabs it: the end is unplugged and the cable
// follows the mouse from its other end.
void App::grab_near(ui::Window &window)
{
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsAnyItemHovered() || !ImGui::IsWindowHovered()) {
        return;
    }
    int cx = 0;
    int cy = 0;
    if (!ui::platform_cursor(cx, cy)) {
        return;
    }
    (void)window;
    float mx = static_cast<float>(cx);
    float my = static_cast<float>(cy);
    float r2 = grab_radius * grab_radius;
    for (const Wire &w : wires_) {
        // The port end.
        const Anchor *p = nullptr;
        if (is_virtual_port(w.port)) {
            if (VirtualPort *v = virtual_port(w.port)) {
                p = channel_anchor(v->output, v->channel);
            }
        } else if (w.port >= 0 && static_cast<size_t>(w.port) < port_anchors_.size()) {
            p = &port_anchors_[static_cast<size_t>(w.port)];
        }
        if (p && p->valid && (p->x - mx) * (p->x - mx) + (p->y - my) * (p->y - my) <= r2) {
            InstrumentId in = w.instrument;
            int ch = w.channel;
            unwire(in, ch);
            offered_ = true;
            offered_instrument_ = in;
            offered_channel_ = ch;
            selected_port_ = -1;
            return;
        }
        // The channel end.
        const Anchor *c = channel_anchor(w.instrument, w.channel);
        if (c && c->valid && (c->x - mx) * (c->x - mx) + (c->y - my) * (c->y - my) <= r2) {
            int port = w.port;
            unwire(w.instrument, w.channel);
            offered_ = false;
            if (is_virtual_port(port)) {
                if (VirtualPort *v = virtual_port(port)) {
                    offered_ = true;
                    offered_instrument_ = v->output;
                    offered_channel_ = v->channel;
                }
            } else {
                selected_port_ = port;
            }
            return;
        }
    }
}

// ---- wires across the desktop ---------------------------------------------

void App::begin_anchors()
{
    port_anchors_.assign(probe_->ports().size(), Anchor{});
    channel_anchors_.clear();
}

void App::anchor_port(int port, ui::Window &window, float local_x, float local_y)
{
    if (port < 0 || static_cast<size_t>(port) >= port_anchors_.size() || window.minimized()) {
        return;
    }
    int wx = 0;
    int wy = 0;
    window.position(wx, wy);
    port_anchors_[static_cast<size_t>(port)] = Anchor{true, static_cast<float>(wx) + local_x, static_cast<float>(wy) + local_y};
}

void App::anchor_channel(InstrumentId instrument, int channel, ui::Window &window, float local_x, float local_y)
{
    if (window.minimized()) {
        return;
    }
    int wx = 0;
    int wy = 0;
    window.position(wx, wy);
    channel_anchors_.push_back(ChannelAnchor{
        instrument, channel, Anchor{true, static_cast<float>(wx) + local_x, static_cast<float>(wy) + local_y}});
}

const App::Anchor *App::channel_anchor(InstrumentId instrument, int channel) const
{
    for (const ChannelAnchor &a : channel_anchors_) {
        if (a.instrument == instrument && a.channel == channel) {
            return &a.anchor;
        }
    }
    return nullptr;
}

App::Overlay &App::overlay_slot(size_t index)
{
    while (overlays_.size() <= index) {
        Overlay o;
        ui::WindowSpec spec;
        spec.title = "RTR-Bench Wire";
        spec.width = 64;
        spec.height = 64;
        spec.x = 0;
        spec.y = 0;
        spec.overlay = true;
        o.window = std::make_unique<ui::Window>(spec);
        overlays_.push_back(std::move(o));
        Overlay &made = overlays_.back();
        size_t slot = overlays_.size() - 1;
        made.window->set_draw([this, slot](ui::Window &w) {
            Overlay &self = overlays_[slot];
            ImGuiViewport *viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize(viewport->Size);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::Begin("##wire", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
            ImGui::PopStyleVar(2);
            ui::draw_wire(ImGui::GetWindowDrawList(), ImVec2(self.from_x, self.from_y), ImVec2(self.to_x, self.to_y),
                          self.colour, w.scale(), self.dangling);
            ImGui::End();
        });
    }
    return overlays_[index];
}

bool App::bench_focused() const
{
    if (rack_window_ && rack_window_->focused()) {
        return true;
    }
    for (const OpenInstrument &i : instruments_) {
        if (i.window->focused()) {
            return true;
        }
    }
    return false;
}

// Once per frame, after the rack and the instruments reported their anchors:
// place one overlay per wire over the two ends, plus one following the mouse
// while a wire is being made.
void App::update_overlays()
{
    if (!overlays_enabled_) {
        return;
    }
    for (Overlay &o : overlays_) {
        o.used = false;
    }
    // Cables only while the bench is in use: the WIRES key on the rack and
    // the focus on one of its windows (the overlays never take the focus).
    if (!wires_shown_ || (!bench_focused() && !force_wires_)) {
        for (Overlay &o : overlays_) {
            o.window->show(false);
        }
        return;
    }
    size_t index = 0;
    auto place = [&](float x0, float y0, float x1, float y1, uint32_t colour, bool dangling) {
        Overlay &o = overlay_slot(index++);
        float margin = ui::wire_margin(o.window->scale());
        float left = std::min(x0, x1) - margin;
        float top = std::min(y0, y1) - margin;
        float right = std::max(x0, x1) + margin;
        float bottom = std::max(y0, y1) + margin;
        o.window->set_bounds(static_cast<int>(left), static_cast<int>(top), static_cast<int>(right - left),
                             static_cast<int>(bottom - top));
        o.from_x = x0 - left;
        o.from_y = y0 - top;
        o.to_x = x1 - left;
        o.to_y = y1 - top;
        o.colour = colour;
        o.dangling = dangling;
        o.used = true;
    };

    for (const Wire &w : wires_) {
        const Anchor *p = nullptr;
        if (is_virtual_port(w.port)) {
            if (VirtualPort *v = virtual_port(w.port)) {
                p = channel_anchor(v->output, v->channel);
            }
        } else if (w.port >= 0 && static_cast<size_t>(w.port) < port_anchors_.size()) {
            p = &port_anchors_[static_cast<size_t>(w.port)];
        }
        const Anchor *c = channel_anchor(w.instrument, w.channel);
        if (!p || !p->valid || !c || !c->valid) {
            continue;
        }
        place(p->x, p->y, c->x, c->y, ui::channel_colour(w.channel % ui::channel_count), false);
    }
    int cx = 0;
    int cy = 0;
    if ((selected_port_ >= 0 || offered_) && ui::platform_cursor(cx, cy)) {
        const Anchor *from = nullptr;
        uint32_t colour = 0xFFFFFFFFu;
        if (selected_port_ >= 0 && static_cast<size_t>(selected_port_) < port_anchors_.size()) {
            from = &port_anchors_[static_cast<size_t>(selected_port_)];
            colour = ui::current_theme().led_warn;
        } else if (offered_) {
            from = channel_anchor(offered_instrument_, offered_channel_);
            colour = ui::channel_colour(offered_channel_ % ui::channel_count);
        }
        if (from && from->valid) {
            place(from->x, from->y, static_cast<float>(cx), static_cast<float>(cy), colour, true);
        }
    }
    for (Overlay &o : overlays_) {
        o.window->show(o.used);
        if (o.used) {
            o.window->frame();
        }
    }
}

// ---- settings -------------------------------------------------------------

std::string App::settings_name(InstrumentId id)
{
    std::string name = command_line_name(id.kind);
    if (id.instance > 0) {
        name += "-" + std::to_string(id.instance + 1);
    }
    return name;
}

void App::load_bench_settings()
{
    nlohmann::json j = load_settings("bench");
    std::string theme = j.value("theme", "dark");
    ui::set_theme(theme == "light" ? ui::ThemeKind::Light : theme == "amber" ? ui::ThemeKind::Amber : ui::ThemeKind::Dark);
    if (j.value("probe", "") == "demo") {
        probe_kind_ = ProbeKind::Demo;
    } else if (j.value("probe", "") == "emulator") {
        probe_kind_ = ProbeKind::Emulator;
    }
    wires_shown_ = j.value("wires_shown", true);
    if (j.contains("rack") && j["rack"].is_object()) {
        rack_x_ = j["rack"].value("x", rack_x_);
        rack_y_ = j["rack"].value("y", rack_y_);
    }
    if (j.contains("instruments") && j["instruments"].is_array()) {
        for (const nlohmann::json &w : j["instruments"]) {
            Instrument kind;
            if (w.is_object() && instrument_from_name(w.value("name", ""), kind)) {
                saved_instruments_.push_back(
                    SavedWindow{InstrumentId{kind, w.value("instance", 0)}, w.value("x", 40), w.value("y", 280)});
            }
        }
    }
    if (j.contains("wires") && j["wires"].is_array()) {
        for (const nlohmann::json &w : j["wires"]) {
            Instrument kind;
            if (w.is_object() && instrument_from_name(w.value("instrument", ""), kind)) {
                InstrumentId in{kind, w.value("instance", 0)};
                Instrument out_kind;
                if (w.contains("from") && w["from"].is_object() &&
                    instrument_from_name(w["from"].value("instrument", ""), out_kind)) {
                    pending_virtual_wires_.push_back(PendingVirtualWire{
                        in, w.value("channel", 0), InstrumentId{out_kind, w["from"].value("instance", 0)},
                        w["from"].value("channel", 0)});
                    continue;
                }
                // Before the command-line wires, so those win on a conflict.
                wires_at_start_.insert(wires_at_start_.begin(), Wire{in, w.value("channel", 0), w.value("port", -1)});
            }
        }
    }
}

void App::save_bench_settings()
{
    nlohmann::json j;
    j["theme"] = theme_name(ui::current_theme().kind);
    j["probe"] = probe_name(probe_kind_);
    j["wires_shown"] = wires_shown_;
    if (rack_window_) {
        int x = 0;
        int y = 0;
        rack_window_->position(x, y);
        j["rack"] = {{"x", x}, {"y", y}};
    }
    j["instruments"] = nlohmann::json::array();
    for (OpenInstrument &i : instruments_) {
        int x = 0;
        int y = 0;
        i.window->position(x, y);
        j["instruments"].push_back({{"name", instrument_name(i.instrument->kind())},
                                    {"instance", i.instrument->instance()},
                                    {"x", x},
                                    {"y", y}});
        nlohmann::json inst;
        i.instrument->save(inst);
        save_settings(settings_name(i.instrument->id()), inst);
    }
    j["wires"] = nlohmann::json::array();
    for (const Wire &w : wires_) {
        nlohmann::json entry = {{"instrument", instrument_name(w.instrument.kind)},
                                {"instance", w.instrument.instance},
                                {"channel", w.channel},
                                {"port", w.port}};
        // A wire from an output of another instrument is saved by that output.
        if (const VirtualPort *v = const_cast<App *>(this)->virtual_port(w.port)) {
            entry["port"] = -1;
            entry["from"] = {{"instrument", instrument_name(v->output.kind)},
                             {"instance", v->output.instance},
                             {"channel", v->channel}};
        }
        j["wires"].push_back(entry);
    }
    save_settings("bench", j);
    last_save_time_ = std::chrono::steady_clock::now();
}

// ---- composite screenshot -----------------------------------------------

void App::request_bench_capture()
{
    rack_window_->capture_memory();
    for (OpenInstrument &i : instruments_) {
        i.window->capture_memory();
    }
    for (Overlay &o : overlays_) {
        if (o.used) {
            o.window->capture_memory();
        }
    }
}

// Gathers the captured windows, in drawing order: rack, instruments, cables.
bool App::collect_bench_capture()
{
    bench_layers_.clear();
    auto take = [&](ui::Window &w) {
        Layer layer;
        if (!w.take_capture(layer.rgba, layer.width, layer.height)) {
            return false;
        }
        w.position(layer.x, layer.y);
        bench_layers_.push_back(std::move(layer));
        return true;
    };
    if (!take(*rack_window_)) {
        return false;
    }
    for (OpenInstrument &i : instruments_) {
        if (!take(*i.window)) {
            return false;
        }
    }
    for (Overlay &o : overlays_) {
        if (o.used && !take(*o.window)) {
            return false;
        }
    }
    return true;
}

void App::write_bench_capture(const std::string &path)
{
    if (bench_layers_.empty()) {
        return;
    }
    int left = bench_layers_[0].x;
    int top = bench_layers_[0].y;
    int right = left + bench_layers_[0].width;
    int bottom = top + bench_layers_[0].height;
    for (const Layer &l : bench_layers_) {
        left = std::min(left, l.x);
        top = std::min(top, l.y);
        right = std::max(right, l.x + l.width);
        bottom = std::max(bottom, l.y + l.height);
    }
    int width = right - left;
    int height = bottom - top;
    std::vector<uint8_t> canvas(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0);
    // Straight-alpha "over" of every layer onto a transparent canvas.
    const size_t canvas_w = static_cast<size_t>(width);
    for (const Layer &l : bench_layers_) {
        const size_t layer_w = static_cast<size_t>(l.width);
        for (int y = 0; y < l.height; y++) {
            for (int x = 0; x < l.width; x++) {
                const uint8_t *src = &l.rgba[(static_cast<size_t>(y) * layer_w + static_cast<size_t>(x)) * 4];
                int ix = l.x - left + x;
                int iy = l.y - top + y;
                size_t cx = static_cast<size_t>(ix);
                size_t cy = static_cast<size_t>(iy);
                uint8_t *dst = &canvas[(cy * canvas_w + cx) * 4];
                float sa = static_cast<float>(src[3]) / 255.0f;
                float da = static_cast<float>(dst[3]) / 255.0f;
                float oa = sa + da * (1.0f - sa);
                for (int c = 0; c < 3; c++) {
                    float sv = static_cast<float>(src[c]);
                    float dv = static_cast<float>(dst[c]);
                    float v = oa > 0.0f ? (sv * sa + dv * da * (1.0f - sa)) / oa : 0.0f;
                    dst[c] = static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f));
                }
                dst[3] = static_cast<uint8_t>(std::clamp(oa * 255.0f, 0.0f, 255.0f));
            }
        }
    }
    if (!ui::write_png(path, width, height, canvas.data())) {
        std::fprintf(stderr, "rtr-bench: cannot write %s\n", path.c_str());
    }
}

// ---- frame loop -----------------------------------------------------------

// Once per frame: drain the probe into the port state and the instruments,
// and keep the event rate.
void App::pump_probe()
{
    events_.clear();
    probe_->poll(events_);
    for (const core::DigitalEvent &e : events_) {
        ports_.apply(e);
    }
    analog_.clear();
    probe_->poll_analog(analog_);
    pump_virtual_ports();   // outputs wired straight into inputs
    for (OpenInstrument &i : instruments_) {
        i.instrument->feed(events_);
        if (!analog_.empty()) {
            i.instrument->feed_analog(analog_);
        }
    }
    if (auto *emu = dynamic_cast<probes::EmulatorProbe *>(probe_.get())) {
        functions_.clear();
        emu->poll_functions(functions_);
        for (const probes::EmulatorProbe::FunctionChange &f : functions_) {
            ports_.set_direction(f.port, probes::fsel_direction(f.fsel));
        }
    }
    ports_.end_frame();

    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - last_rate_time_).count();
    if (elapsed >= 0.5) {
        uint64_t count = probe_->stats().events;
        events_per_second_ = static_cast<double>(count - last_event_count_) / elapsed;
        last_event_count_ = count;
        last_rate_time_ = now;
    }
}

int App::run()
{
    if (!ui::platform_init()) {
        std::fprintf(stderr, "rtr-bench: cannot initialize the window system\n");
        return 1;
    }
    ui::set_theme(ui::ThemeKind::Dark);
    overlays_enabled_ = ui::platform_has_window_positions();
    // The command line wins over the saved settings.
    bool probe_from_command_line = probe_from_command_line_;
    ProbeKind asked = probe_kind_;
    if (screenshots_.empty()) {
        load_bench_settings();   // a screenshot run is reproducible: command line only
    }
    if (probe_from_command_line) {
        probe_kind_ = asked;
    }
    if (theme_from_command_line_) {
        ui::set_theme(theme_);
    }
    open_rack();
    if (!rack_window_) {
        ui::platform_shutdown();
        return 1;
    }
    create_probe(probe_kind_);
    // Instruments open where they were, plus those the command line asks for.
    for (const SavedWindow &saved : saved_instruments_) {
        create_instrument(saved.id.kind, saved.id.instance, saved.x, saved.y);
    }
    for (Instrument kind : open_at_start_) {
        int x = 40;
        int y = 280;
        for (const Placement &p : placements_) {
            if (p.window == command_line_name(kind)) {
                x = p.x;
                y = p.y;
            }
        }
        create_instrument(kind, 0, x, y);
    }
    for (const Wire &w : wires_at_start_) {
        make_wire(w.instrument, w.channel, w.port);
    }
    last_save_time_ = std::chrono::steady_clock::now();

    auto start = std::chrono::steady_clock::now();
    bool capture_asked = false;
    while (rack_window_ && !rack_window_->close_requested()) {
        ui::platform_poll();
        open_pending();
        // When the bench gets the focus, every one of its windows comes to the
        // front, so none stays under another application.
        {
            bool focused_now = bench_focused();
            if (focused_now && !bench_was_focused_) {
                for (OpenInstrument &i : instruments_) {
                    if (!i.window->focused()) {
                        i.window->raise_without_focus();
                    }
                }
                if (!rack_window_->focused()) {
                    rack_window_->raise_without_focus();
                }
                for (OpenInstrument &i : instruments_) {
                    if (i.window->focused()) {
                        i.window->raise_without_focus();
                    }
                }
                if (rack_window_->focused()) {
                    rack_window_->raise_without_focus();
                }
            }
            bench_was_focused_ = focused_now;
        }
        if (switch_pending_) {
            switch_pending_ = false;
            create_probe(switch_to_);
        }
        pump_probe();
        begin_anchors();
        if (!screenshots_.empty()) {
            force_wires_ = true;
            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            const ScreenshotTarget *bench = nullptr;
            for (const ScreenshotTarget &target : screenshots_) {
                if (target.window == "bench") {
                    bench = &target;
                }
            }
            if (!capture_asked && elapsed >= screenshot_delay_) {
                for (const ScreenshotTarget &target : screenshots_) {
                    if (target.window == "rack") {
                        rack_window_->capture(target.path);
                    }
                    for (OpenInstrument &i : instruments_) {
                        if (target.window == instrument_name(i.instrument->kind()) && i.instrument->instance() == 0) {
                            i.window->capture(target.path);
                        }
                    }
                }
                if (bench) {
                    request_bench_capture();
                }
                capture_asked = true;
            } else if (capture_asked) {
                bool all = true;
                for (const ScreenshotTarget &target : screenshots_) {
                    if (target.window == "rack" && !rack_window_->captured()) {
                        all = false;
                    }
                    for (OpenInstrument &i : instruments_) {
                        if (target.window == instrument_name(i.instrument->kind()) && i.instrument->instance() == 0 &&
                            !i.window->captured()) {
                            all = false;
                        }
                    }
                }
                if (bench && bench_layers_.empty()) {
                    if (collect_bench_capture()) {
                        write_bench_capture(bench->path);
                    } else {
                        all = false;
                    }
                }
                if (all) {
                    rack_window_->request_close();
                }
            }
        }
        rack_window_->frame();
        for (OpenInstrument &i : instruments_) {
            i.window->frame();
        }
        update_overlays();
        if (screenshots_.empty() &&
            std::chrono::duration<double>(std::chrono::steady_clock::now() - last_save_time_).count() >= 10.0) {
            save_bench_settings();
        }
        // A closed instrument keeps its wires out of the way: they are removed.
        for (OpenInstrument &i : instruments_) {
            if (i.window->close_requested()) {
                InstrumentId id = i.instrument->id();
                wires_.erase(std::remove_if(wires_.begin(), wires_.end(),
                                            [&](const Wire &w) { return w.instrument == id; }),
                             wires_.end());
            }
        }
        instruments_.erase(std::remove_if(instruments_.begin(), instruments_.end(),
                                          [](const OpenInstrument &i) {
                                              return i.window->close_requested();
                                          }),
                           instruments_.end());
    }

    if (screenshots_.empty()) {
        save_bench_settings();   // a screenshot run must not change the user's layout
    }
    probe_->disconnect();
    overlays_.clear();
    instruments_.clear();
    rack_window_.reset();
    ui::platform_shutdown();
    return 0;
}

}  // namespace app
