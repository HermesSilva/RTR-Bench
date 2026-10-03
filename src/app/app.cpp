// SPDX-License-Identifier: Apache-2.0
#include "app/app.h"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "app/settings.h"
#include "instruments/scope/scope.h"
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
    spec.x = rack_x_;
    spec.y = rack_y_;
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
        spec.width = 1080;
        spec.height = 560;
        spec.x = 40;
        spec.y = 280;
        break;
    default:
        return;  // not built yet
    }
    for (const SavedWindow &saved : saved_instruments_) {
        if (saved.kind == kind) {
            spec.x = saved.x;
            spec.y = saved.y;
        }
    }
    auto window = std::make_unique<ui::Window>(spec);
    if (!window->valid()) {
        return;
    }
    std::string file = instrument_name(kind);
    for (char &c : file) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    instrument->load(load_settings(file));
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
    if (port < 0 || static_cast<size_t>(port) >= probe_->ports().size() || channel < 0) {
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

void App::cancel_wiring()
{
    selected_port_ = -1;
    offered_ = false;
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

void App::anchor_channel(Instrument instrument, int channel, ui::Window &window, float local_x, float local_y)
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

const App::Anchor *App::channel_anchor(Instrument instrument, int channel) const
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

// Once per frame, after the rack and the instruments reported their anchors:
// place one overlay per wire over the two ends, plus one following the mouse
// while a wire is being made.
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
        if (w.port < 0 || static_cast<size_t>(w.port) >= port_anchors_.size()) {
            continue;
        }
        const Anchor &p = port_anchors_[static_cast<size_t>(w.port)];
        const Anchor *c = channel_anchor(w.instrument, w.channel);
        if (!p.valid || !c || !c->valid) {
            continue;
        }
        place(p.x, p.y, c->x, c->y, ui::channel_colour(w.channel), false);
    }
    // The cable being made follows the cursor from its known end.
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
            colour = ui::channel_colour(offered_channel_);
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

}  // namespace

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
                saved_instruments_.push_back(SavedWindow{kind, w.value("x", 40), w.value("y", 280)});
            }
        }
    }
    if (j.contains("wires") && j["wires"].is_array()) {
        for (const nlohmann::json &w : j["wires"]) {
            Instrument kind;
            if (w.is_object() && instrument_from_name(w.value("instrument", ""), kind)) {
                // Before the command-line wires, so those win on a conflict.
                wires_at_start_.insert(wires_at_start_.begin(), Wire{kind, w.value("channel", 0), w.value("port", -1)});
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
        j["instruments"].push_back({{"name", instrument_name(i.instrument->kind())}, {"x", x}, {"y", y}});
        nlohmann::json inst;
        i.instrument->save(inst);
        std::string file = instrument_name(i.instrument->kind());
        for (char &c : file) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        save_settings(file, inst);
    }
    j["wires"] = nlohmann::json::array();
    for (const Wire &w : wires_) {
        j["wires"].push_back({{"instrument", instrument_name(w.instrument)}, {"channel", w.channel}, {"port", w.port}});
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
    open_rack();
    if (!rack_window_) {
        ui::platform_shutdown();
        return 1;
    }
    create_probe(probe_kind_);
    // Instruments open where they were, unless the command line says otherwise.
    for (const SavedWindow &saved : saved_instruments_) {
        if (std::find(open_at_start_.begin(), open_at_start_.end(), saved.kind) == open_at_start_.end()) {
            open_at_start_.push_back(saved.kind);
        }
    }
    for (Instrument kind : open_at_start_) {
        create_instrument(kind);
    }
    last_save_time_ = std::chrono::steady_clock::now();
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
                        if (target.window == instrument_name(i.instrument->kind())) {
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
                        if (target.window == instrument_name(i.instrument->kind()) && !i.window->captured()) {
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
