// SPDX-License-Identifier: Apache-2.0
#include "app/app.h"

#include <algorithm>
#include <cstdio>

#include "instruments/scope/scope.h"
#include "ui/theme.h"

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

App::App()
{
    last_rate_time_ = std::chrono::steady_clock::now();
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
    spec.width = 1180;
    spec.height = 200;
    rack_window_ = std::make_unique<ui::Window>(spec);
    if (!rack_window_->valid()) {
        std::fprintf(stderr, "rtr-bench: cannot create the rack window\n");
        rack_window_.reset();
        return;
    }
    rack_window_->set_draw([this](ui::Window &w) { rack_.draw(w); });
}

InstrumentBase *App::find_instrument(Instrument kind)
{
    for (OpenInstrument &i : instruments_) {
        if (i.instrument->kind() == kind) {
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

// Called from inside a frame (a key on the rack): creating a window there
// would switch the ImGui context under the drawing code, so it is queued
// and done between frames.
void App::open_instrument(Instrument kind)
{
    for (OpenInstrument &i : instruments_) {
        if (i.instrument->kind() == kind) {
            i.window->raise();
            return;
        }
    }
    pending_open_.push_back(kind);
}

void App::open_pending()
{
    std::vector<Instrument> pending;
    pending.swap(pending_open_);
    for (Instrument kind : pending) {
        create_instrument(kind);
    }
}

void App::create_instrument(Instrument kind)
{
    if (instrument_open(kind)) {
        return;
    }
    std::unique_ptr<InstrumentBase> instrument;
    ui::WindowSpec spec;
    switch (kind) {
    case Instrument::Scope:
        instrument = std::make_unique<Scope>(*this);
        spec.title = "RTR-Bench Oscilloscope";
        spec.width = 1180;
        spec.height = 640;
        break;
    default:
        return;  // not built yet
    }
    auto window = std::make_unique<ui::Window>(spec);
    if (!window->valid()) {
        return;
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

int App::wired_port(Instrument instrument, int channel) const
{
    for (const Wire &w : wires_) {
        if (w.instrument == instrument && w.channel == channel) {
            return w.port;
        }
    }
    return -1;
}

bool App::port_wired_to(int port, Instrument &instrument, int &channel) const
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
    Instrument instrument;
    int channel = 0;
    if (!port_wired_to(port, instrument, channel)) {
        return 0;
    }
    return ui::channel_colour(channel);
}

void App::make_wire(Instrument instrument, int channel, int port)
{
    unwire(instrument, channel);
    wires_.push_back(Wire{instrument, channel, port});
    selected_port_ = -1;
    offered_ = false;
    if (InstrumentBase *i = find_instrument(instrument)) {
        i->wiring_changed();
    }
}

void App::unwire(Instrument instrument, int channel)
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
    selected_port_ = selected_port_ == port ? -1 : port;
}

void App::offer_channel(Instrument instrument, int channel)
{
    if (selected_port_ >= 0) {
        make_wire(instrument, channel, selected_port_);
        return;
    }
    if (offered_ && offered_instrument_ == instrument && offered_channel_ == channel) {
        offered_ = false;
        return;
    }
    offered_ = true;
    offered_instrument_ = instrument;
    offered_channel_ = channel;
}

bool App::channel_offered(Instrument instrument, int channel) const
{
    return offered_ && offered_instrument_ == instrument && offered_channel_ == channel;
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
    open_rack();
    if (!rack_window_) {
        ui::platform_shutdown();
        return 1;
    }
    create_probe(probe_kind_);
    for (Instrument kind : open_at_start_) {
        create_instrument(kind);
    }
    for (const Wire &w : wires_at_start_) {
        make_wire(w.instrument, w.channel, w.port);
    }

    auto start = std::chrono::steady_clock::now();
    bool capture_asked = false;
    while (rack_window_ && !rack_window_->close_requested()) {
        ui::platform_poll();
        open_pending();
        if (switch_pending_) {
            switch_pending_ = false;
            create_probe(switch_to_);
        }
        pump_probe();
        if (!screenshots_.empty()) {
            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (!capture_asked && elapsed >= screenshot_delay_) {
                for (const ScreenshotTarget &target : screenshots_) {
                    if (target.window == "rack") {
                        rack_window_->capture(target.path);
                    }
                    for (OpenInstrument &i : instruments_) {
                        if (target.window == instrument_name(i.instrument->kind())) {
                            i.window->capture(target.path);
                        }
                    }
                }
                capture_asked = true;
            } else if (capture_asked) {
                bool all = true;
                for (const ScreenshotTarget &target : screenshots_) {
                    if (target.window == "rack" && !rack_window_->captured()) {
                        all = false;
                    }
                    for (OpenInstrument &i : instruments_) {
                        if (target.window == instrument_name(i.instrument->kind()) && !i.window->captured()) {
                            all = false;
                        }
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
        instruments_.erase(std::remove_if(instruments_.begin(), instruments_.end(),
                                          [](const OpenInstrument &i) {
                                              return i.window->close_requested();
                                          }),
                           instruments_.end());
    }

    probe_->disconnect();
    instruments_.clear();
    rack_window_.reset();
    ui::platform_shutdown();
    return 0;
}

}  // namespace app
