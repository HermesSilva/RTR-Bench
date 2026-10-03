// SPDX-License-Identifier: Apache-2.0
#include "instruments/generator/generator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

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

constexpr float module_h = 82.0f;
constexpr float header_h = 40.0f;

bool is_modulation(core::Waveform k)
{
    return k == core::Waveform::AM || k == core::Waveform::FM || k == core::Waveform::PM || k == core::Waveform::PwmMod;
}

// One period of the wave drawn in a small box, as the icon of the kind.
void draw_wave_icon(ImDrawList *draw, ImVec2 pos, ImVec2 size, core::Waveform kind, uint32_t colour, float s)
{
    const int n = 32;
    ImVec2 pts[n];
    float mid = pos.y + size.y * 0.5f;
    float amp = size.y * 0.42f;
    for (int i = 0; i < n; i++) {
        float p = static_cast<float>(i) / static_cast<float>(n - 1);
        float v = 0.0f;
        switch (kind) {
        case core::Waveform::Low:
            v = -1.0f;
            break;
        case core::Waveform::High:
        case core::Waveform::Dc:
            v = 1.0f;
            break;
        case core::Waveform::Clock:
        case core::Waveform::Burst:
            v = std::fmod(p * 2.0f, 1.0f) < 0.5f ? 1.0f : -1.0f;
            break;
        case core::Waveform::Pwm:
        case core::Waveform::PwmMod:
            v = std::fmod(p * 2.0f, 1.0f) < 0.25f ? 1.0f : -1.0f;
            break;
        case core::Waveform::Sweep:
            v = std::fmod(p * p * 6.0f, 1.0f) < 0.5f ? 1.0f : -1.0f;
            break;
        case core::Waveform::Sine:
            v = std::sin(p * 6.2831853f);
            break;
        case core::Waveform::AM:
            v = std::sin(p * 6.2831853f * 3.0f) * (0.5f + 0.5f * std::sin(p * 6.2831853f));
            break;
        case core::Waveform::FM:
            v = std::sin(p * 6.2831853f * (1.5f + 2.5f * p));
            break;
        case core::Waveform::PM:
            v = std::sin(p * 6.2831853f * 2.0f + 1.5f * std::sin(p * 6.2831853f));
            break;
        case core::Waveform::Triangle:
            v = p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
            break;
        case core::Waveform::Sawtooth:
            v = 2.0f * p - 1.0f;
            break;
        case core::Waveform::RampDown:
            v = 1.0f - 2.0f * p;
            break;
        case core::Waveform::Noise:
            v = std::sin(p * 97.0f) * std::cos(p * 31.0f);
            break;
        case core::Waveform::Off:
            v = 0.0f;
            break;
        }
        pts[i] = ImVec2(pos.x + p * size.x, mid - v * amp);
    }
    draw->AddPolyline(pts, n, colour, 0, 1.2f * s);
}

}  // namespace

Generator::Generator(App &app) : app_(app)
{
    outputs_.push_back(std::make_unique<Output>());   // no wires yet: nothing to sync
    outputs_[0]->spec.kind = core::Waveform::Clock;
    for (int n : app_.generator_outputs_on()) {
        while (static_cast<int>(outputs_.size()) < n && static_cast<int>(outputs_.size()) < max_outputs) {
            outputs_.push_back(std::make_unique<Output>());
            outputs_.back()->spec.kind = core::Waveform::Clock;
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
    outputs_.back()->spec.kind = core::Waveform::Clock;
    wiring_changed();
}

void Generator::remove_output(int index)
{
    if (outputs_.size() <= 1 || index < 0 || static_cast<size_t>(index) >= outputs_.size()) {
        return;
    }
    Output &o = *outputs_[static_cast<size_t>(index)];
    if (o.port >= 0) {
        core::WaveSpec off;
        app_.probe().drive_waveform(o.port, off);
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
                core::WaveSpec off;
                app_.probe().drive_waveform(o.port, off);
            }
            o.port = port;
            o.dirty = true;
        }
    }
}

// Tells the probe what this output does now.
void Generator::apply(Output &o)
{
    o.dirty = false;
    if (o.port < 0) {
        return;
    }
    const core::PortInfo *info = app_.port_info(o.port);
    if (!info || !info->drivable || !app_.probe().capabilities().drive) {
        return;
    }
    core::WaveSpec spec = o.spec;
    spec.freq_hz = generator_frequency_steps()[static_cast<size_t>(o.freq_step)];
    spec.mod_freq_hz = generator_frequency_steps()[static_cast<size_t>(o.mod_step)];
    spec.sweep_end_hz = generator_frequency_steps()[static_cast<size_t>(o.sweep_step)];
    if (!o.on) {
        spec.kind = core::Waveform::Off;
    }
    app_.probe().drive_waveform(o.port, spec);
}

void Generator::fit_window(ui::Window &window)
{
    int width = 0;
    int height = 0;
    window.size(width, height);
    int rows = (static_cast<int>(outputs_.size()) + 1) / 2;
    int wanted = static_cast<int>(70.0f + header_h + (module_h + 10.0f) * static_cast<float>(rows) + 24.0f);
    if (height != wanted) {
        window.set_size(width, wanted);
    }
}

void Generator::draw_waveform_combo(Output &o, int index, ImVec2 pos, ImVec2 size, float s)
{
    const ui::Theme &t = ui::current_theme();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    uint32_t colour = ui::channel_colour(index % ui::channel_count);
    char id[32];
    std::snprintf(id, sizeof(id), "##wave%d", index);
    char popup[32];
    std::snprintf(popup, sizeof(popup), "##wave-list%d", index);

    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, size);
    bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) {
        ImGui::OpenPopup(popup);
    }
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), hovered ? t.key_hover : t.screen, 4.0f * s);
    draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), t.chassis_shadow, 4.0f * s, 0, 1.0f * s);
    ImFont *mono = ui::fonts().mono;
    // A small picture of the wave, then its name.
    draw_wave_icon(draw, ImVec2(pos.x + 6.0f * s, pos.y + 3.0f * s), ImVec2(30.0f * s, size.y - 6.0f * s), o.spec.kind, colour, s);
    draw->AddText(mono, mono->FontSize, ImVec2(pos.x + 42.0f * s, pos.y + (size.y - mono->FontSize) * 0.5f), colour,
                  core::waveform_name(o.spec.kind));
    float cx = pos.x + size.x - 12.0f * s;
    float cy = pos.y + size.y * 0.5f;
    draw->AddTriangleFilled(ImVec2(cx - 5.0f * s, cy - 3.0f * s), ImVec2(cx + 5.0f * s, cy - 3.0f * s),
                            ImVec2(cx, cy + 3.0f * s), t.label_dim);

    float popup_h = static_cast<float>(core::waveform_count - 1) * (mono->FontSize + 3.0f * s) +
                    4.0f * (ui::fonts().small->FontSize + 7.0f * s) + 20.0f * s;
    ImGui::SetNextWindowPos(ui::popup_position(pos, size, popup_h, s));
    ImGui::SetNextWindowSize(ImVec2(size.x + 60.0f * s, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, t.screen);
    ImGui::PushStyleColor(ImGuiCol_Border, t.chassis_edge);
    ImGui::PushStyleColor(ImGuiCol_Text, t.readout);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, t.key_hover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, t.key_pressed);
    ImGui::PushStyleColor(ImGuiCol_Header, t.key);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * s, 8.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f * s, 3.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f * s);
    if (ImGui::BeginPopup(popup)) {
        const char *group = nullptr;
        for (int k = 1; k < core::waveform_count; k++) {   // OFF is the OUTPUT key, not a kind
            auto kind = static_cast<core::Waveform>(k);
            if (!group || std::strcmp(group, core::waveform_group(kind)) != 0) {
                group = core::waveform_group(kind);
                if (k > 1) {
                    ImGui::Spacing();
                }
                ImGui::PushFont(ui::fonts().small);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(t.label_dim), "%s", group);
                ImGui::PopFont();
            }
            bool allowed = true;   // an analog wave on a digital port drives its logic level
            bool is_current = kind == o.spec.kind;
            ImGui::PushFont(mono);
            if (!allowed) {
                ImGui::BeginDisabled();
            }
            if (is_current) {
                ImGui::PushStyleColor(ImGuiCol_Text, colour);
            }
            if (ImGui::Selectable(core::waveform_name(kind), is_current)) {
                o.spec.kind = kind;
                o.dirty = true;
                ImGui::CloseCurrentPopup();
            }
            if (is_current) {
                ImGui::PopStyleColor();
            }
            if (!allowed) {
                ImGui::EndDisabled();
            }
            ImGui::PopFont();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(6);
}

void Generator::draw_module(ui::Window &window, int index, ImVec2 min, ImVec2 max)
{
    const ui::Theme &t = ui::current_theme();
    const float s = window.scale();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    Output &o = *outputs_[static_cast<size_t>(index)];
    uint32_t colour = ui::channel_colour(index % ui::channel_count);
    const float key_h = 21.0f * s;
    const float gap = 5.0f * s;
    bool can_drive = app_.probe().capabilities().drive;
    const core::PortInfo *info = app_.port_info(o.port);
    bool drivable = info && info->drivable;
    bool analog_port = info && info->analog;
    char title[32];
    std::snprintf(title, sizeof(title), "OUTPUT %d", index + 1);
    ui::group_frame(min, max, title, s);

    // Row 1: remove, jack and port, waveform, OUTPUT.
    float x = min.x + 10.0f * s;
    float y = min.y + 10.0f * s;
    char id[32];
    std::snprintf(id, sizeof(id), "##rm%d", index);
    if (ui::key(id, "x", ImVec2(x, y), ImVec2(20.0f * s, key_h), false, t.led_stop, s, outputs_.size() > 1)) {
        remove_output(index);
        return;
    }
    x += 20.0f * s + gap;
    ImVec2 jack_c(x + 9.0f * s, y + key_h * 0.5f);
    ui::JackLook look;
    look.name = "";
    look.level = -1;
    look.active = false;
    look.input = false;
    look.output = true;
    // The output is also a virtual port other instruments may be wired to.
    core::WaveSpec published = o.spec;
    published.freq_hz = generator_frequency_steps()[static_cast<size_t>(o.freq_step)];
    published.mod_freq_hz = generator_frequency_steps()[static_cast<size_t>(o.mod_step)];
    published.sweep_end_hz = generator_frequency_steps()[static_cast<size_t>(o.sweep_step)];
    int vport = app_.publish_output(this->id(), index, published, o.on);
    look.wire_colour = o.port >= 0 ? colour : app_.port_wire_colour(vport);
    std::snprintf(id, sizeof(id), "##jack%d", index);
    if (ui::jack(id, jack_c, 8.0f * s, look, s)) {
        app_.offer_channel(this->id(), index);
    }
    if (app_.channel_offered(this->id(), index)) {
        draw->AddCircle(jack_c, 11.0f * s, t.led_warn, 20, 1.5f * s);
    }
    app_.anchor_channel(this->id(), index, window, jack_c.x, jack_c.y);
    ImFont *small = ui::fonts().small;
    std::string port_name = panel::short_port_name(info);
    draw->AddText(small, small->FontSize, ImVec2(x + 21.0f * s, y + (key_h - small->FontSize) * 0.5f),
                  o.port < 0 ? t.label_dim : (drivable ? colour : t.led_stop), port_name.c_str());
    x += 70.0f * s;
    float out_w = 74.0f * s;
    float combo_w = max.x - 10.0f * s - out_w - gap - x;
    draw_waveform_combo(o, index, ImVec2(x, y), ImVec2(combo_w, key_h), s);
    x += combo_w + gap;
    std::snprintf(id, sizeof(id), "##out%d", index);
    bool usable = (o.port >= 0 && drivable && can_drive) || app_.port_wire_colour(vport) != 0;
    (void)analog_port;
    if (ui::key(id, o.on ? "ON" : "OFF", ImVec2(x, y), ImVec2(out_w, key_h), o.on, t.led_run, s, usable)) {
        o.on = !o.on;
        o.dirty = true;
    }
    if (o.port >= 0 && (!drivable || !can_drive)) {
        draw->AddText(small, small->FontSize, ImVec2(x, y + key_h + 2.0f * s), t.led_stop, "no drive");
    }

    // Row 2: three contextual knobs with readouts.
    y += key_h + 10.0f * s;
    x = min.x + 10.0f * s;
    const float kr = 12.0f * s;
    float slot_w = (max.x - min.x - 20.0f * s - 2.0f * gap) / 3.0f;
    int fmax = static_cast<int>(generator_frequency_steps().size()) - 1;
    bool pressed = false;
    char text[32];
    core::Waveform k = o.spec.kind;
    bool periodic = k != core::Waveform::Low && k != core::Waveform::High && k != core::Waveform::Dc &&
                    k != core::Waveform::Noise;

    // Knob 1: frequency.
    std::snprintf(id, sizeof(id), "##freq%d", index);
    int steps = ui::knob(id, ImVec2(x + kr, y + kr), kr, nullptr, s, &pressed);
    if (steps != 0 && periodic) {
        o.freq_step = std::clamp(o.freq_step + steps, 0, fmax);
        o.dirty = true;
    }
    core::format_frequency(text, sizeof(text), generator_frequency_steps()[static_cast<size_t>(o.freq_step)]);
    ui::readout(ImVec2(x + kr * 2.0f + 3.0f * s, y + 2.0f * s), ImVec2(x + slot_w, y + kr * 2.0f - 2.0f * s), text,
                periodic ? colour : t.readout_dim, s);
    x += slot_w + gap;

    // Knob 2: duty (PWM), count (burst), end (sweep), depth (modulation) or amplitude (analog).
    std::snprintf(id, sizeof(id), "##k2%d", index);
    steps = ui::knob(id, ImVec2(x + kr, y + kr), kr, nullptr, s, &pressed);
    uint32_t c2 = t.readout_dim;
    if (k == core::Waveform::Pwm) {
        if (steps != 0) {
            o.spec.duty = std::clamp(o.spec.duty + steps, 1, 99);
            o.dirty = true;
        }
        if (pressed) {
            o.spec.duty = 50;
            o.dirty = true;
        }
        std::snprintf(text, sizeof(text), "duty %d %%", o.spec.duty);
        c2 = colour;
    } else if (k == core::Waveform::Burst) {
        if (steps != 0) {
            o.spec.burst_count = std::clamp(o.spec.burst_count + steps, 1, 1000);
            o.dirty = true;
        }
        std::snprintf(text, sizeof(text), "%d pulses", o.spec.burst_count);
        c2 = colour;
    } else if (k == core::Waveform::Sweep) {
        if (steps != 0) {
            o.sweep_step = std::clamp(o.sweep_step + steps, 0, fmax);
            o.dirty = true;
        }
        char f[24];
        core::format_frequency(f, sizeof(f), generator_frequency_steps()[static_cast<size_t>(o.sweep_step)]);
        std::snprintf(text, sizeof(text), "to %s", f);
        c2 = colour;
    } else if (is_modulation(k)) {
        if (steps != 0) {
            o.spec.mod_depth = std::clamp(o.spec.mod_depth + 0.05 * steps, 0.0, 1.0);
            o.dirty = true;
        }
        std::snprintf(text, sizeof(text), "depth %.0f %%", o.spec.mod_depth * 100.0);
        c2 = colour;
    } else if (core::waveform_is_analog(k)) {
        if (steps != 0) {
            o.spec.amplitude_v = std::clamp(o.spec.amplitude_v + 0.1 * steps, 0.0, 5.0);
            o.dirty = true;
        }
        std::snprintf(text, sizeof(text), "%.1f V", o.spec.amplitude_v);
        c2 = colour;
    } else {
        std::snprintf(text, sizeof(text), "--");
    }
    ui::readout(ImVec2(x + kr * 2.0f + 3.0f * s, y + 2.0f * s), ImVec2(x + slot_w, y + kr * 2.0f - 2.0f * s), text, c2, s);
    x += slot_w + gap;

    // Knob 3: modulation frequency (modulations) or offset (analog).
    std::snprintf(id, sizeof(id), "##k3%d", index);
    steps = ui::knob(id, ImVec2(x + kr, y + kr), kr, nullptr, s, &pressed);
    uint32_t c3 = t.readout_dim;
    if (is_modulation(k)) {
        if (steps != 0) {
            o.mod_step = std::clamp(o.mod_step + steps, 0, fmax);
            o.dirty = true;
        }
        char f[24];
        core::format_frequency(f, sizeof(f), generator_frequency_steps()[static_cast<size_t>(o.mod_step)]);
        std::snprintf(text, sizeof(text), "mod %s", f);
        c3 = colour;
        // Modulations also have an amplitude: the click of this knob cycles it.
        if (pressed) {
            o.spec.amplitude_v = o.spec.amplitude_v >= 5.0 ? 0.5 : o.spec.amplitude_v + 0.5;
            o.dirty = true;
        }
    } else if (core::waveform_is_analog(k)) {
        if (steps != 0) {
            o.spec.offset_v = std::clamp(o.spec.offset_v + 0.1 * steps, -5.0, 5.0);
            o.dirty = true;
        }
        if (pressed) {
            o.spec.offset_v = 0.0;
            o.dirty = true;
        }
        std::snprintf(text, sizeof(text), "%+.1f V", o.spec.offset_v);
        c3 = colour;
    } else {
        std::snprintf(text, sizeof(text), "--");
    }
    ui::readout(ImVec2(x + kr * 2.0f + 3.0f * s, y + 2.0f * s), ImVec2(x + slot_w, y + kr * 2.0f - 2.0f * s), text, c3, s);
}

void Generator::draw(ui::Window &window)
{
    ui::ChassisSpec chassis;
    chassis.model = "GEN-1";
    chassis.title = std::string("Waveform Generator") + title_suffix();
    chassis.corner = 12.0f;
    fit_window(window);
    ui::ChassisFrame frame = ui::begin_chassis(window, chassis);
    const ui::Theme &t = ui::current_theme();
    const float s = window.scale();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        app_.cancel_wiring();
    }

    const float key_h = 21.0f * s;
    const float gap = 8.0f * s;
    float x = frame.panel_min.x;
    float y = frame.panel_min.y + 4.0f * s;
    if (ui::key("##add", "ADD OUTPUT", ImVec2(x, y), ImVec2(96.0f * s, key_h), false, t.led_run, s,
                static_cast<int>(outputs_.size()) < max_outputs)) {
        add_output();
    }
    y += key_h + 18.0f * s;

    // Two mini modules per row.
    float inner_w = frame.panel_max.x - frame.panel_min.x;
    float mw = (inner_w - gap) / 2.0f;
    int count = static_cast<int>(outputs_.size());
    for (int i = 0; i < count; i++) {
        int col = i % 2;
        int row = i / 2;
        ImVec2 min(x + static_cast<float>(col) * (mw + gap), y + static_cast<float>(row) * (module_h + 10.0f) * s);
        ImVec2 max(min.x + mw, min.y + module_h * s);
        draw_module(window, i, min, max);
        if (static_cast<int>(outputs_.size()) != count) {
            break;   // an output was removed this frame
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

void Generator::save(nlohmann::json &out) const
{
    out["outputs"] = nlohmann::json::array();
    for (const auto &op : outputs_) {
        out["outputs"].push_back({{"on", op->on},
                                  {"kind", static_cast<int>(op->spec.kind)},
                                  {"freq_step", op->freq_step},
                                  {"mod_step", op->mod_step},
                                  {"sweep_step", op->sweep_step},
                                  {"duty", op->spec.duty},
                                  {"burst", op->spec.burst_count},
                                  {"amplitude", op->spec.amplitude_v},
                                  {"offset", op->spec.offset_v},
                                  {"depth", op->spec.mod_depth}});
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
        o->spec.kind = static_cast<core::Waveform>(std::clamp(j.value("kind", 3), 1, core::waveform_count - 1));
        o->freq_step = std::clamp(j.value("freq_step", 9), 0, fmax);
        o->mod_step = std::clamp(j.value("mod_step", 6), 0, fmax);
        o->sweep_step = std::clamp(j.value("sweep_step", 12), 0, fmax);
        o->spec.duty = std::clamp(j.value("duty", 50), 1, 99);
        o->spec.burst_count = std::clamp(j.value("burst", 10), 1, 1000);
        o->spec.amplitude_v = std::clamp(j.value("amplitude", 1.0), 0.0, 5.0);
        o->spec.offset_v = std::clamp(j.value("offset", 0.0), -5.0, 5.0);
        o->spec.mod_depth = std::clamp(j.value("depth", 0.5), 0.0, 1.0);
        outputs_.push_back(std::move(o));
    }
    wiring_changed();
}

}  // namespace app
