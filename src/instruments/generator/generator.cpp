// SPDX-License-Identifier: Apache-2.0
#include "instruments/generator/generator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/app.h"
#include "core/scope_engine.h"
#include "instruments/common/panel.h"
#include "ui/chassis.h"
#include "ui/fonts.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/window.h"

namespace app {

const std::vector<double> &generator_frequency_steps()
{
    static const std::vector<double> steps = [] {
        std::vector<double> v;
        for (int e = 0; e <= 5; e++) {
            double decade = std::pow(10.0, e);
            v.push_back(decade);
            v.push_back(decade * 2.0);
            v.push_back(decade * 5.0);
        }
        v.push_back(1000000.0);
        return v;
    }();
    return steps;
}

namespace {

const char *mode_label(Generator::Mode m)
{
    switch (m) {
    case Generator::Mode::Low:
        return "LOW";
    case Generator::Mode::High:
        return "HIGH";
    case Generator::Mode::Clock:
        return "CLOCK";
    case Generator::Mode::Pwm:
        return "PWM";
    }
    return "";
}

constexpr float row_h = 56.0f;

}  // namespace

Generator::Generator(App &app) : app_(app)
{
    outputs_.push_back(std::make_unique<Output>());   // no wires yet: nothing to sync
    for (int n : app_.generator_outputs_on()) {
        while (static_cast<int>(outputs_.size()) < n && static_cast<int>(outputs_.size()) < max_outputs) {
            outputs_.push_back(std::make_unique<Output>());
        }
        if (n >= 1 && n <= static_cast<int>(outputs_.size())) {
            outputs_[static_cast<size_t>(n - 1)]->on = true;
        }
    }
    last_apply_ = std::chrono::steady_clock::now();
}

void Generator::add_output()
{
    if (static_cast<int>(outputs_.size()) >= max_outputs) {
        return;
    }
    outputs_.push_back(std::make_unique<Output>());
    wiring_changed();
}

void Generator::remove_output(int index)
{
    if (outputs_.size() <= 1 || index < 0 || static_cast<size_t>(index) >= outputs_.size()) {
        return;
    }
    Output &o = *outputs_[static_cast<size_t>(index)];
    if (o.port >= 0) {
        app_.probe().drive_pattern(o.port, 0, 0);   // leave the port low
    }
    app_.unwire(this->id(), index);
    for (int c = index + 1; c < static_cast<int>(outputs_.size()); c++) {
        int port = app_.wired_port(this->id(), c);
        app_.unwire(this->id(), c);
        if (port >= 0) {
            app_.rewire(this->id(), c - 1, port);
        }
    }
    outputs_.erase(outputs_.begin() + index);
    wiring_changed();
}

void Generator::wiring_changed()
{
    for (size_t i = 0; i < outputs_.size(); i++) {
        Output &o = *outputs_[i];
        int port = app_.wired_port(this->id(), static_cast<int>(i));
        if (port != o.port) {
            if (o.port >= 0) {
                app_.probe().drive_pattern(o.port, 0, 0);
            }
            o.port = port;
            o.dirty = true;
        }
    }
}

// Tells the probe what this output does now.
void Generator::apply(Output &o, int)
{
    o.dirty = false;
    if (o.port < 0) {
        return;
    }
    const core::PortInfo *info = app_.port_info(o.port);
    if (!info || !info->drivable || !app_.probe().capabilities().drive) {
        return;
    }
    if (!o.on) {
        app_.probe().drive_pattern(o.port, 0, 0);
        return;
    }
    switch (o.mode) {
    case Mode::Low:
        app_.probe().drive_pattern(o.port, 0, 0);
        break;
    case Mode::High:
        app_.probe().drive_pattern(o.port, 0, 1);
        break;
    case Mode::Clock:
    case Mode::Pwm: {
        double hz = generator_frequency_steps()[static_cast<size_t>(o.freq_step)];
        int64_t period = static_cast<int64_t>(1e9 / hz);
        int duty = o.mode == Mode::Clock ? 50 : o.duty;
        app_.probe().drive_pattern(o.port, period, period * duty / 100);
        break;
    }
    }
}

void Generator::fit_window(ui::Window &window)
{
    int width = 0;
    int height = 0;
    window.size(width, height);
    int wanted = static_cast<int>(130.0f + (row_h + 10.0f) * static_cast<float>(outputs_.size()) + 40.0f);
    if (height != wanted) {
        window.set_size(width, wanted);
    }
}

void Generator::draw(ui::Window &window)
{
    ui::ChassisSpec chassis;
    chassis.model = "GEN-1";
    chassis.title = std::string("Pattern Generator") + title_suffix();
    chassis.corner = 12.0f;
    fit_window(window);
    ui::ChassisFrame frame = ui::begin_chassis(window, chassis);
    const ui::Theme &t = ui::current_theme();
    const float s = window.scale();
    ImDrawList *draw = frame.draw;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        app_.cancel_wiring();
    }
    bool can_drive = app_.probe().capabilities().drive;

    const float key_h = 21.0f * s;
    const float gap = 5.0f * s;
    float x = frame.panel_min.x;
    float y = frame.panel_min.y + 4.0f * s;
    ImFont *small = ui::fonts().small;

    if (ui::key("##add", "ADD OUTPUT", ImVec2(x, y), ImVec2(90.0f * s, key_h), false, t.led_run, s,
                static_cast<int>(outputs_.size()) < max_outputs)) {
        add_output();
    }
    draw->AddText(small, small->FontSize, ImVec2(x + 90.0f * s + 8.0f * s, y + 4.0f * s),
                  can_drive ? t.label_dim : t.led_stop,
                  can_drive ? "outputs go to the input ports of the target (IN0.. on the demo probe)"
                            : "this probe cannot drive: the emulator needs the version 2 probe (qemu-pi4 fork)");
    y += key_h + 18.0f * s;

    // One mini module per output: [x] [jack port] [mode] [FREQ knob + readout] [DUTY knob + readout] [OUTPUT]
    for (int i = 0; i < static_cast<int>(outputs_.size()); i++) {
        Output &o = *outputs_[static_cast<size_t>(i)];
        float rh = row_h * s;
        char title[32];
        std::snprintf(title, sizeof(title), "OUTPUT %d", i + 1);
        ui::group_frame(ImVec2(x, y - 2.0f * s), ImVec2(frame.panel_max.x, y + rh + 2.0f * s), title, s);
        float rx = x + 10.0f * s;
        float cy = y + rh * 0.5f;
        uint32_t colour = ui::channel_colour(i % ui::channel_count);
        char id[32];
        std::snprintf(id, sizeof(id), "##rm%d", i);
        if (ui::key(id, "x", ImVec2(rx, cy - key_h * 0.5f), ImVec2(20.0f * s, key_h), false, t.led_stop, s,
                    outputs_.size() > 1)) {
            remove_output(i);
            break;
        }
        rx += 20.0f * s + gap;
        ImVec2 jack_c(rx + 9.0f * s, cy);
        ui::JackLook look;
        look.name = "";
        look.level = -1;
        look.active = false;
        look.input = false;
        look.output = true;
        look.wire_colour = o.port >= 0 ? colour : 0;
        std::snprintf(id, sizeof(id), "##jack%d", i);
        if (ui::jack(id, jack_c, 8.0f * s, look, s)) {
            app_.offer_channel(this->id(), i);
        }
        if (app_.channel_offered(this->id(), i)) {
            draw->AddCircle(jack_c, 11.0f * s, t.led_warn, 20, 1.5f * s);
        }
        app_.anchor_channel(this->id(), i, window, jack_c.x, jack_c.y);
        const core::PortInfo *info = app_.port_info(o.port);
        std::string port_name = panel::short_port_name(info);
        bool drivable = info && info->drivable;
        draw->AddText(small, small->FontSize, ImVec2(rx + 21.0f * s, cy - small->FontSize * 0.5f),
                      o.port < 0 ? t.label_dim : (drivable ? colour : t.led_stop), port_name.c_str());
        if (o.port >= 0 && !drivable) {
            draw->AddText(small, small->FontSize, ImVec2(rx + 21.0f * s, cy + small->FontSize * 0.5f), t.led_stop,
                          "not an input");
        }
        rx += 66.0f * s;
        std::snprintf(id, sizeof(id), "##mode%d", i);
        if (ui::key(id, mode_label(o.mode), ImVec2(rx, cy - key_h * 0.5f), ImVec2(56.0f * s, key_h), true, colour, s)) {
            o.mode = static_cast<Mode>((static_cast<int>(o.mode) + 1) % 4);
            o.dirty = true;
        }
        rx += 56.0f * s + gap;
        // Frequency knob and readout.
        float kr = 13.0f * s;
        bool pressed = false;
        std::snprintf(id, sizeof(id), "##freq%d", i);
        int steps = ui::knob(id, ImVec2(rx + kr, cy), kr, nullptr, s, &pressed);
        int fmax = static_cast<int>(generator_frequency_steps().size()) - 1;
        if (steps != 0) {
            o.freq_step = std::clamp(o.freq_step + steps, 0, fmax);
            o.dirty = true;
        }
        rx += kr * 2.0f + gap;
        char text[32];
        core::format_frequency(text, sizeof(text), generator_frequency_steps()[static_cast<size_t>(o.freq_step)]);
        bool periodic = o.mode == Mode::Clock || o.mode == Mode::Pwm;
        ui::readout(ImVec2(rx, cy - 11.0f * s), ImVec2(rx + 84.0f * s, cy + 11.0f * s), text,
                    periodic ? colour : t.readout_dim, s);
        rx += 84.0f * s + gap * 2.0f;
        // Duty knob and readout.
        std::snprintf(id, sizeof(id), "##duty%d", i);
        steps = ui::knob(id, ImVec2(rx + kr, cy), kr, nullptr, s, &pressed);
        if (steps != 0 && o.mode == Mode::Pwm) {
            o.duty = std::clamp(o.duty + steps, 1, 99);
            o.dirty = true;
        }
        if (pressed && o.mode == Mode::Pwm) {
            o.duty = 50;
            o.dirty = true;
        }
        rx += kr * 2.0f + gap;
        std::snprintf(text, sizeof(text), "%d %%", o.mode == Mode::Pwm ? o.duty : 50);
        ui::readout(ImVec2(rx, cy - 11.0f * s), ImVec2(rx + 56.0f * s, cy + 11.0f * s), text,
                    o.mode == Mode::Pwm ? colour : t.readout_dim, s);
        rx += 56.0f * s + gap * 2.0f;
        std::snprintf(id, sizeof(id), "##out%d", i);
        if (ui::key(id, o.on ? "OUTPUT ON" : "OUTPUT", ImVec2(rx, cy - key_h * 0.5f), ImVec2(80.0f * s, key_h), o.on,
                    t.led_run, s, o.port >= 0 && drivable && can_drive)) {
            o.on = !o.on;
            o.dirty = true;
        }
        y += rh + 14.0f * s;
    }
    draw->AddText(small, small->FontSize, ImVec2(x, y + 2.0f * s), t.label_dim,
                  "knobs: frequency 1 Hz to 1 MHz in 1-2-5 steps, duty 1 to 99 % (PWM); click the duty knob for 50 %");

    // The probe is told about changes at once, and about everything every second.
    auto now = std::chrono::steady_clock::now();
    bool periodic = std::chrono::duration<double>(now - last_apply_).count() >= 1.0;
    if (periodic) {
        last_apply_ = now;
    }
    for (int i = 0; i < static_cast<int>(outputs_.size()); i++) {
        Output &o = *outputs_[static_cast<size_t>(i)];
        if (o.dirty || (periodic && o.on)) {
            apply(o, i);
        }
    }
    ui::end_chassis();
}

void Generator::save(nlohmann::json &out) const
{
    out["outputs"] = nlohmann::json::array();
    for (const auto &op : outputs_) {
        out["outputs"].push_back({{"on", op->on}, {"mode", static_cast<int>(op->mode)}, {"freq_step", op->freq_step},
                                  {"duty", op->duty}});
    }
}

void Generator::load(const nlohmann::json &in)
{
    if (!in.contains("outputs") || !in["outputs"].is_array() || in["outputs"].empty()) {
        return;
    }
    outputs_.clear();
    int fmax = static_cast<int>(generator_frequency_steps().size()) - 1;
    for (const nlohmann::json &j : in["outputs"]) {
        if (static_cast<int>(outputs_.size()) >= max_outputs) {
            break;
        }
        auto o = std::make_unique<Output>();
        o->on = j.value("on", false);
        o->mode = static_cast<Mode>(std::clamp(j.value("mode", 2), 0, 3));
        o->freq_step = std::clamp(j.value("freq_step", 9), 0, fmax);
        o->duty = std::clamp(j.value("duty", 50), 1, 99);
        outputs_.push_back(std::move(o));
    }
    wiring_changed();
}

}  // namespace app
