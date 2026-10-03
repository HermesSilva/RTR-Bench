// SPDX-License-Identifier: Apache-2.0
#include "app/app.h"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "ui/chassis.h"
#include "ui/theme.h"

namespace app {

App::App()
{
    probe_ = std::make_unique<probes::EmulatorProbe>();
    last_rate_time_ = std::chrono::steady_clock::now();
}

App::~App() = default;

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

bool App::instrument_open(Instrument kind) const
{
    for (const OpenInstrument &i : instruments_) {
        if (i.kind == kind) {
            return true;
        }
    }
    return false;
}

// Stage 1: only the oscilloscope opens, with its placeholder screen. The
// real instruments arrive from stage 2 on.
void App::open_instrument(Instrument kind)
{
    for (OpenInstrument &i : instruments_) {
        if (i.kind == kind) {
            i.window->raise();
            return;
        }
    }
    if (kind != Instrument::Scope) {
        return;
    }
    ui::WindowSpec spec;
    spec.title = "RTR-Bench Oscilloscope";
    spec.width = 1120;
    spec.height = 640;
    auto window = std::make_unique<ui::Window>(spec);
    if (!window->valid()) {
        return;
    }
    window->set_draw([](ui::Window &w) {
        ui::ChassisSpec chassis;
        chassis.model = "DSO-1";
        chassis.title = "Digital Oscilloscope";
        ui::ChassisFrame frame = ui::begin_chassis(w, chassis);

        const ui::Theme &t = ui::current_theme();
        const float s = w.scale();
        ImVec2 screen_min = frame.panel_min;
        ImVec2 screen_max(frame.panel_min.x + (frame.panel_max.x - frame.panel_min.x) * 0.68f,
                          frame.panel_max.y);
        frame.draw->AddRectFilled(screen_min, screen_max, t.screen_bezel, 10.0f * s);
        ImVec2 inner_min = ImVec2(screen_min.x + 10.0f * s, screen_min.y + 10.0f * s);
        ImVec2 inner_max = ImVec2(screen_max.x - 10.0f * s, screen_max.y - 10.0f * s);
        frame.draw->AddRectFilled(inner_min, inner_max, t.screen, 4.0f * s);
        const int cols = 10;
        const int rows = 8;
        for (int i = 1; i < cols; i++) {
            float x = inner_min.x + (inner_max.x - inner_min.x) * static_cast<float>(i) / cols;
            frame.draw->AddLine(ImVec2(x, inner_min.y), ImVec2(x, inner_max.y),
                                i == cols / 2 ? t.graticule_axis : t.graticule, 1.0f);
        }
        for (int i = 1; i < rows; i++) {
            float y = inner_min.y + (inner_max.y - inner_min.y) * static_cast<float>(i) / rows;
            frame.draw->AddLine(ImVec2(inner_min.x, y), ImVec2(inner_max.x, y),
                                i == rows / 2 ? t.graticule_axis : t.graticule, 1.0f);
        }
        frame.draw->AddText(ImVec2(inner_min.x + 12.0f * s, inner_min.y + 10.0f * s), t.readout_dim,
                            "stage 2: the real screen");
        ui::end_chassis();
    });
    instruments_.push_back(OpenInstrument{kind, std::move(window)});
}

void App::set_theme(ui::ThemeKind kind)
{
    ui::set_theme(kind);
}

void App::screenshot(const std::string &path, double delay_seconds)
{
    screenshot_path_ = path;
    screenshot_delay_ = delay_seconds;
}

// Once per frame: drain the probe into the port state and keep the rate.
void App::pump_probe()
{
    events_.clear();
    probe_->poll(events_);
    for (const core::DigitalEvent &e : events_) {
        ports_.apply(e);
    }
    functions_.clear();
    probe_->poll_functions(functions_);
    for (const probes::EmulatorProbe::FunctionChange &f : functions_) {
        ports_.set_direction(f.port, probes::fsel_direction(f.fsel));
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
    probe_->connect();

    auto start = std::chrono::steady_clock::now();
    bool capture_asked = false;
    while (rack_window_ && !rack_window_->close_requested()) {
        ui::platform_poll();
        pump_probe();
        if (!screenshot_path_.empty()) {
            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (!capture_asked && elapsed >= screenshot_delay_) {
                rack_window_->capture(screenshot_path_);
                capture_asked = true;
            } else if (rack_window_->captured()) {
                rack_window_->request_close();
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
