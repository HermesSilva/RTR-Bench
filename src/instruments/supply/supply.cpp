// SPDX-License-Identifier: Apache-2.0
#include "instruments/supply/supply.h"

#include <algorithm>
#include <cstdio>

#include "app/app.h"
#include "instruments/common/panel.h"
#include "ui/chassis.h"
#include "ui/fonts.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/window.h"

namespace app {

namespace {

constexpr float row_h = 58.0f;

}  // namespace

Supply::Supply(App &app) : app_(app)
{
    outputs_.push_back(std::make_unique<Output>());   // no wires yet: nothing to sync
    last_apply_ = std::chrono::steady_clock::now();
}

void Supply::add_output()
{
    if (static_cast<int>(outputs_.size()) >= max_outputs) {
        return;
    }
    outputs_.push_back(std::make_unique<Output>());
    wiring_changed();
}

void Supply::remove_output(int index)
{
    if (outputs_.size() <= 1 || index < 0 || static_cast<size_t>(index) >= outputs_.size()) {
        return;
    }
    Output &o = *outputs_[static_cast<size_t>(index)];
    if (o.port >= 0) {
        app_.probe().drive(o.port, 0);
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

void Supply::wiring_changed()
{
    for (size_t i = 0; i < outputs_.size(); i++) {
        Output &o = *outputs_[i];
        int port = app_.wired_port(this->id(), static_cast<int>(i));
        if (port != o.port) {
            if (o.port >= 0) {
                app_.probe().drive(o.port, 0);
            }
            o.port = port;
            o.dirty = true;
        }
    }
}

void Supply::apply(Output &o)
{
    o.dirty = false;
    if (o.port < 0) {
        return;
    }
    const core::PortInfo *info = app_.port_info(o.port);
    if (!info || !info->drivable || !app_.probe().capabilities().drive) {
        return;
    }
    app_.probe().drive(o.port, o.on && o.volts >= logic_threshold ? 1 : 0);
}

void Supply::fit_window(ui::Window &window)
{
    int width = 0;
    int height = 0;
    window.size(width, height);
    int rows = (static_cast<int>(outputs_.size()) + 1) / 2;
    int wanted = static_cast<int>(130.0f + (row_h + 22.0f) * static_cast<float>(rows) + 30.0f);
    if (height != wanted) {
        window.set_size(width, wanted);
    }
}

void Supply::draw(ui::Window &window)
{
    ui::ChassisSpec chassis;
    chassis.model = "PSU-1";
    chassis.title = std::string("DC Power Supply") + title_suffix();
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
    bool analog_target = app_.probe().capabilities().analog && false;   // real volts come with the ADALM2000

    const float key_h = 21.0f * s;
    const float gap = 5.0f * s;
    float x = frame.panel_min.x;
    float y = frame.panel_min.y + 4.0f * s;
    ImFont *small = ui::fonts().small;

    if (ui::key("##add", "ADD OUTPUT", ImVec2(x, y), ImVec2(90.0f * s, key_h), false, t.led_run, s,
                static_cast<int>(outputs_.size()) < max_outputs)) {
        add_output();
    }
    y += key_h + 18.0f * s;

    // One mini module per output, two per row.
    const float module_gap = 8.0f * s;
    const float mw = (frame.panel_max.x - frame.panel_min.x - module_gap) / 2.0f;
    const float rows_top = y;
    for (int i = 0; i < static_cast<int>(outputs_.size()); i++) {
        Output &o = *outputs_[static_cast<size_t>(i)];
        float rh = row_h * s;
        float mx = x + static_cast<float>(i % 2) * (mw + module_gap);
        int row = i / 2;
        y = rows_top + static_cast<float>(row) * (rh + 22.0f * s);
        char title[32];
        std::snprintf(title, sizeof(title), "OUTPUT %d", i + 1);
        ui::group_frame(ImVec2(mx, y - 2.0f * s), ImVec2(mx + mw, y + rh + 2.0f * s), title, s);
        float cy = y + rh * 0.5f;
        float rx = mx + 10.0f * s;
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
        core::WaveSpec published;
        published.kind = o.volts >= logic_threshold ? core::Waveform::High : core::Waveform::Low;
        int vport = app_.publish_output(this->id(), i, published, o.on);
        look.wire_colour = o.port >= 0 ? colour : app_.port_wire_colour(vport);
        std::snprintf(id, sizeof(id), "##jack%d", i);
        if (ui::jack(id, jack_c, 8.0f * s, look, s)) {
            app_.offer_channel(this->id(), i);
        }
        if (app_.channel_offered(this->id(), i)) {
            draw->AddCircle(jack_c, 11.0f * s, t.led_warn, 20, 1.5f * s);
        }
        app_.anchor_channel(this->id(), i, window, jack_c.x, jack_c.y);
        const core::PortInfo *info = app_.port_info(o.port);
        bool drivable = info && info->drivable;
        std::string port_name = panel::short_port_name(info);
        draw->AddText(small, small->FontSize, ImVec2(rx + 21.0f * s, cy - small->FontSize * 0.5f),
                      o.port < 0 ? t.label_dim : (drivable ? colour : t.led_stop), port_name.c_str());
        if (o.port >= 0 && !drivable) {
            draw->AddText(small, small->FontSize, ImVec2(rx + 21.0f * s, cy + small->FontSize * 0.5f), t.led_stop,
                          "not an input");
        }
        rx += 66.0f * s;
        // SET knob: 0.1 V per step, click for 3.3 V.
        float kr = 13.0f * s;
        bool pressed = false;
        std::snprintf(id, sizeof(id), "##set%d", i);
        int steps = ui::knob(id, ImVec2(rx + kr, cy), kr, "SET", s, &pressed);
        if (steps != 0) {
            o.volts = std::clamp(o.volts + 0.1f * static_cast<float>(steps), 0.0f, volts_max);
            o.dirty = true;
        }
        if (pressed) {
            o.volts = 3.3f;
            o.dirty = true;
        }
        rx += kr * 2.0f + gap * 2.0f;
        // Displays: volts and amperes, seven-segment.
        auto display = [&](const char *value, const char *unit, uint32_t colour_v, float width) {
            ImVec2 dmin(rx, cy - 20.0f * s);
            ImVec2 dmax(rx + width, cy + 20.0f * s);
            draw->AddRectFilled(dmin, dmax, t.screen, 4.0f * s);
            draw->AddRect(dmin, dmax, t.chassis_shadow, 4.0f * s, 0, 1.0f * s);
            float uw = panel::mono_width(unit);
            float tw = panel::seven_width(value);
            panel::seven_text(draw, ImVec2(dmax.x - 8.0f * s - uw - 6.0f * s - tw, cy - panel::seven_height() * 0.5f), colour_v,
                              value);
            panel::mono_text(draw, ImVec2(dmax.x - 8.0f * s - uw, dmax.y - 6.0f * s - ui::fonts().mono->FontSize), t.readout, unit);
            rx += width + gap;
        };
        char volts[16];
        // What the target sees: on a logic target the level the set voltage means.
        float shown = !o.on ? 0.0f : (analog_target ? o.volts : (o.volts >= logic_threshold ? 3.3f : 0.0f));
        std::snprintf(volts, sizeof(volts), "%.2f", static_cast<double>(shown));
        display(volts, "V", o.on ? colour : t.readout_dim, 94.0f * s);
        display("0.000", "A", o.on ? colour : t.readout_dim, 94.0f * s);
        // The set voltage, small, under the SET knob.
        char set_text[24];
        std::snprintf(set_text, sizeof(set_text), "%.1f V", static_cast<double>(o.volts));
        draw->AddText(small, small->FontSize, ImVec2(mx + 10.0f * s + 20.0f * s + gap + 66.0f * s - 4.0f * s, cy + kr + 12.0f * s),
                      t.label_dim, set_text);
        std::snprintf(id, sizeof(id), "##out%d", i);
        if (ui::key(id, o.on ? "ON" : "OUTPUT", ImVec2(rx, cy - key_h * 0.5f), ImVec2(74.0f * s, key_h), o.on, t.led_run, s,
                    (o.port >= 0 && drivable && can_drive) || app_.port_wire_colour(vport) != 0)) {
            o.on = !o.on;
            o.dirty = true;
        }
    }

    auto now = std::chrono::steady_clock::now();
    bool periodic = std::chrono::duration<double>(now - last_apply_).count() >= 1.0;
    if (periodic) {
        last_apply_ = now;
    }
    for (auto &op : outputs_) {
        if (op->dirty || (periodic && op->on)) {
            apply(*op);
        }
    }
    app_.grab_near(window);
    ui::end_chassis();
}

void Supply::save(nlohmann::json &out) const
{
    out["outputs"] = nlohmann::json::array();
    for (const auto &op : outputs_) {
        out["outputs"].push_back({{"on", op->on}, {"volts", op->volts}});
    }
}

void Supply::load(const nlohmann::json &in)
{
    if (!in.contains("outputs") || !in["outputs"].is_array() || in["outputs"].empty()) {
        return;
    }
    outputs_.clear();
    for (const nlohmann::json &j : in["outputs"]) {
        if (static_cast<int>(outputs_.size()) >= max_outputs) {
            break;
        }
        auto o = std::make_unique<Output>();
        o->on = j.value("on", false);
        o->volts = std::clamp(j.value("volts", 3.3f), 0.0f, volts_max);
        outputs_.push_back(std::move(o));
    }
    wiring_changed();
}

}  // namespace app
