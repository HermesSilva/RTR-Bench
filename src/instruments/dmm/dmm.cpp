// SPDX-License-Identifier: Apache-2.0
#include "instruments/dmm/dmm.h"

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

namespace {

const char *function_label(Multimeter::Function f)
{
    switch (f) {
    case Multimeter::Function::VoltsDC:
        return "V DC";
    case Multimeter::Function::VoltsAC:
        return "V AC";
    case Multimeter::Function::Frequency:
        return "FREQ";
    case Multimeter::Function::Duty:
        return "DUTY";
    case Multimeter::Function::Width:
        return "WIDTH";
    case Multimeter::Function::Count:
        return "COUNT";
    case Multimeter::Function::Level:
        return "LEVEL";
    }
    return "";
}

// The window of signal a reading looks at: the last 200 ms known.
constexpr int64_t reading_window_ns = 200000000LL;

}  // namespace

Multimeter::Multimeter(App &app) : app_(app)
{
    add_tip();
    last_update_ = std::chrono::steady_clock::now();
}

void Multimeter::add_tip()
{
    if (static_cast<int>(tips_.size()) >= max_tips) {
        return;
    }
    tips_.push_back(std::make_unique<Tip>());
    refresh_wiring();
}

void Multimeter::remove_tip(int index)
{
    if (tips_.size() <= 1 || index < 0 || static_cast<size_t>(index) >= tips_.size()) {
        return;
    }
    // Wires above the removed tip move down one channel.
    app_.unwire(this->id(), index);
    for (int c = index + 1; c < static_cast<int>(tips_.size()); c++) {
        int port = app_.wired_port(this->id(), c);
        app_.unwire(this->id(), c);
        if (port >= 0) {
            app_.rewire(this->id(), c - 1, port);
        }
    }
    tips_.erase(tips_.begin() + index);
    refresh_wiring();
}

void Multimeter::refresh_wiring()
{
    for (size_t i = 0; i < tips_.size(); i++) {
        Tip &t = *tips_[i];
        int port = app_.wired_port(this->id(), static_cast<int>(i));
        if (port != t.port) {
            t.port = port;
            t.trace.clear();
            t.analog.clear();
            t.pulses = 0;
            t.min = t.max = t.sum = 0.0;
            t.samples = 0;
            const core::PortInfo *info = app_.port_info(port);
            t.is_analog = info && info->analog;
            if (t.is_analog && t.function != Function::VoltsDC && t.function != Function::VoltsAC) {
                t.function = Function::VoltsDC;
            }
            if (!t.is_analog && (t.function == Function::VoltsDC || t.function == Function::VoltsAC)) {
                t.function = Function::Frequency;
            }
        }
    }
}

void Multimeter::wiring_changed()
{
    refresh_wiring();
}

void Multimeter::feed(const std::vector<core::DigitalEvent> &events)
{
    for (const core::DigitalEvent &e : events) {
        for (auto &tp : tips_) {
            Tip &t = *tp;
            if (t.port == e.port && !t.is_analog) {
                if (e.kind == core::DigitalEvent::Snapshot) {
                    t.trace.snapshot(e.ns, e.level);
                } else {
                    t.trace.add(e.ns, e.level);
                    if (e.level) {
                        t.pulses++;
                    }
                }
            }
        }
        latest_ns_ = std::max(latest_ns_, e.ns);
    }
}

void Multimeter::feed_analog(const std::vector<core::AnalogBlock> &blocks)
{
    for (const core::AnalogBlock &b : blocks) {
        for (auto &tp : tips_) {
            Tip &t = *tp;
            if (t.port == b.port && t.is_analog) {
                t.analog.add(b.t0_ns, b.dt_ns, b.volts.data(), b.volts.size());
            }
        }
        latest_ns_ = std::max(latest_ns_, b.t0_ns + static_cast<int64_t>(b.volts.size()) * b.dt_ns);
    }
}

Multimeter::Reading Multimeter::measure(const Tip &t) const
{
    Reading r;
    if (t.port < 0 || latest_ns_ < 0) {
        return r;
    }
    int64_t t1 = latest_ns_;
    int64_t t0 = t1 - reading_window_ns;
    if (t.is_analog) {
        core::AnalogMeasurements m = core::measure_analog(t.analog, t0, t1);
        if (!m.valid) {
            return r;
        }
        r.valid = true;
        if (t.function == Function::VoltsAC) {
            double ac = std::sqrt(std::max(0.0, static_cast<double>(m.vrms) * m.vrms - static_cast<double>(m.vmean) * m.vmean));
            r.value = ac;
        } else {
            r.value = m.vmean;
        }
        std::snprintf(r.unit, sizeof(r.unit), "V");
        if (std::fabs(r.value) < 1.0) {
            std::snprintf(r.text, sizeof(r.text), "%.1f", r.value * 1000.0);
            std::snprintf(r.unit, sizeof(r.unit), "mV");
        } else {
            std::snprintf(r.text, sizeof(r.text), "%.3f", r.value);
        }
        return r;
    }
    core::Measurements m = core::measure(t.trace, t0, t1);
    switch (t.function) {
    case Function::Frequency:
        if (!m.valid) {
            return r;
        }
        r.valid = true;
        r.value = m.frequency_hz;
        if (r.value >= 1e6) {
            std::snprintf(r.text, sizeof(r.text), "%.4f", r.value / 1e6);
            std::snprintf(r.unit, sizeof(r.unit), "MHz");
        } else if (r.value >= 1e3) {
            std::snprintf(r.text, sizeof(r.text), "%.3f", r.value / 1e3);
            std::snprintf(r.unit, sizeof(r.unit), "kHz");
        } else {
            std::snprintf(r.text, sizeof(r.text), "%.2f", r.value);
            std::snprintf(r.unit, sizeof(r.unit), "Hz");
        }
        return r;
    case Function::Duty:
        if (!m.valid) {
            return r;
        }
        r.valid = true;
        r.value = m.duty * 100.0;
        std::snprintf(r.text, sizeof(r.text), "%.1f", r.value);
        std::snprintf(r.unit, sizeof(r.unit), "%%");
        return r;
    case Function::Width:
        if (!m.valid) {
            return r;
        }
        r.valid = true;
        r.value = static_cast<double>(m.high_ns);
        if (r.value >= 1e6) {
            std::snprintf(r.text, sizeof(r.text), "%.3f", r.value / 1e6);
            std::snprintf(r.unit, sizeof(r.unit), "ms");
        } else if (r.value >= 1e3) {
            std::snprintf(r.text, sizeof(r.text), "%.2f", r.value / 1e3);
            std::snprintf(r.unit, sizeof(r.unit), "us");
        } else {
            std::snprintf(r.text, sizeof(r.text), "%.0f", r.value);
            std::snprintf(r.unit, sizeof(r.unit), "ns");
        }
        return r;
    case Function::Count:
        r.valid = true;
        r.value = static_cast<double>(t.pulses);
        std::snprintf(r.text, sizeof(r.text), "%llu", static_cast<unsigned long long>(t.pulses));
        std::snprintf(r.unit, sizeof(r.unit), "pulses");
        return r;
    case Function::Level: {
        int level = t.trace.level();
        if (level < 0) {
            return r;
        }
        r.valid = true;
        r.value = level ? 3.3 : 0.0;
        bool toggling = m.rising + m.falling > 0;
        std::snprintf(r.text, sizeof(r.text), "%s", toggling ? "-" : (level ? "H" : "L"));
        std::snprintf(r.unit, sizeof(r.unit), "%s", toggling ? "toggling" : (level ? "high" : "low"));
        return r;
    }
    case Function::VoltsDC:
    case Function::VoltsAC:
        break;
    }
    return r;
}

void Multimeter::fit_window(ui::Window &window)
{
    int width = 0;
    int height = 0;
    window.size(width, height);
    int rows = (static_cast<int>(tips_.size()) + 1) / 2;
    int wanted = static_cast<int>(130.0f + 72.0f * static_cast<float>(rows) + 40.0f);
    if (height != wanted) {
        window.set_size(width, wanted);
    }
}

void Multimeter::draw(ui::Window &window)
{
    ui::ChassisSpec chassis;
    chassis.model = "DMM-1";
    chassis.title = std::string("Digital Multimeter") + title_suffix();
    chassis.corner = 12.0f;
    fit_window(window);
    ui::ChassisFrame frame = ui::begin_chassis(window, chassis);
    const ui::Theme &t = ui::current_theme();
    const float s = window.scale();
    ImDrawList *draw = frame.draw;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        app_.cancel_wiring();
    }

    // Readings refresh at the chosen rate; the display keeps the last.
    const double period[3] = {0.5, 0.2, 0.1};
    auto now = std::chrono::steady_clock::now();
    bool refresh = std::chrono::duration<double>(now - last_update_).count() >= period[rate_];
    if (refresh) {
        last_update_ = now;
        for (auto &tp : tips_) {
            Tip &tip = *tp;
            if (tip.hold) {
                continue;
            }
            Reading r = measure(tip);
            if (r.valid) {
                tip.shown = r;
                if (tip.samples == 0) {
                    tip.min = tip.max = r.value;
                }
                tip.min = std::min(tip.min, r.value);
                tip.max = std::max(tip.max, r.value);
                tip.sum += r.value;
                tip.samples++;
            } else {
                tip.shown = Reading{};
            }
        }
    }

    const float key_h = 21.0f * s;
    const float gap = 5.0f * s;
    float x = frame.panel_min.x;
    float y = frame.panel_min.y + 4.0f * s;
    float inner_w = frame.panel_max.x - frame.panel_min.x;

    // Top row: ADD, RATE, RESET.
    float kw = 70.0f * s;
    if (ui::key("##add", "ADD TIP", ImVec2(x, y), ImVec2(kw, key_h), false, t.led_run, s,
                static_cast<int>(tips_.size()) < max_tips)) {
        add_tip();
    }
    const char *rates[3] = {"2 /s", "5 /s", "10 /s"};
    if (ui::key("##rate", rates[rate_], ImVec2(x + kw + gap, y), ImVec2(kw, key_h), false, t.led_warn, s)) {
        rate_ = (rate_ + 1) % 3;
    }
    if (ui::key("##reset", "RESET", ImVec2(x + 2.0f * (kw + gap), y), ImVec2(kw, key_h), false, t.led_warn, s)) {
        for (auto &tp : tips_) {
            tp->pulses = 0;
            tp->min = tp->max = tp->sum = 0.0;
            tp->samples = 0;
        }
    }
    ImFont *small = ui::fonts().small;
    y += key_h + 18.0f * s;

    // One mini module per tip, two per row: [x] [function] [jack+port] [display] [HOLD]
    const float row_h = 50.0f * s;
    const float module_gap = 8.0f * s;
    const float mw = (frame.panel_max.x - frame.panel_min.x - module_gap) / 2.0f;
    const float rows_top = y;
    for (int i = 0; i < static_cast<int>(tips_.size()); i++) {
        Tip &tip = *tips_[static_cast<size_t>(i)];
        float mx = x + static_cast<float>(i % 2) * (mw + module_gap);
        int row = i / 2;
        y = rows_top + static_cast<float>(row) * (row_h + 22.0f * s);
        char title[32];
        std::snprintf(title, sizeof(title), "TIP %d", i + 1);
        ui::group_frame(ImVec2(mx, y - 2.0f * s), ImVec2(mx + mw, y + row_h + 2.0f * s), title, s);
        float rx = mx + 10.0f * s;
        char id[32];
        uint32_t colour = ui::channel_colour(i % ui::channel_count);
        // Remove key.
        std::snprintf(id, sizeof(id), "##rm%d", i);
        if (ui::key(id, "x", ImVec2(rx, y + (row_h - key_h) * 0.5f), ImVec2(20.0f * s, key_h), false, t.led_stop, s,
                    tips_.size() > 1)) {
            remove_tip(i);
            break;
        }
        rx += 20.0f * s + gap;
        // Function key cycles through the functions allowed by the port kind.
        std::snprintf(id, sizeof(id), "##fn%d", i);
        if (ui::key(id, function_label(tip.function), ImVec2(rx, y + (row_h - key_h) * 0.5f), ImVec2(58.0f * s, key_h),
                    true, colour, s)) {
            int f = static_cast<int>(tip.function);
            for (int k = 0; k < functions; k++) {
                f = (f + 1) % functions;
                auto fn = static_cast<Function>(f);
                bool analog_fn = fn == Function::VoltsDC || fn == Function::VoltsAC;
                if (tip.port < 0 || analog_fn == tip.is_analog) {
                    break;
                }
            }
            tip.function = static_cast<Function>(f);
            tip.min = tip.max = tip.sum = 0.0;
            tip.samples = 0;
        }
        rx += 58.0f * s + gap;
        // Jack and the port name.
        ImVec2 jack_c(rx + 9.0f * s, y + row_h * 0.5f);
        ui::JackLook look;
        look.name = "";
        look.level = -1;
        look.active = false;
        look.input = true;
        look.output = false;
        look.wire_colour = tip.port >= 0 ? colour : 0;
        std::snprintf(id, sizeof(id), "##jack%d", i);
        if (ui::jack(id, jack_c, 8.0f * s, look, s)) {
            app_.offer_channel(this->id(), i);
        }
        if (app_.channel_offered(this->id(), i)) {
            draw->AddCircle(jack_c, 11.0f * s, t.led_warn, 20, 1.5f * s);
        }
        app_.anchor_channel(this->id(), i, window, jack_c.x, jack_c.y);
        std::string port_name = panel::short_port_name(app_.port_info(tip.port));
        draw->AddText(small, small->FontSize, ImVec2(rx + 21.0f * s, y + row_h * 0.5f - small->FontSize * 0.5f),
                      tip.port >= 0 ? colour : t.label_dim, port_name.c_str());
        rx += 60.0f * s;
        // The display: a dark window with the seven-segment value and the unit.
        float disp_w = 190.0f * s;
        ImVec2 dmin(rx, y + 2.0f * s);
        ImVec2 dmax(rx + disp_w, y + row_h - 2.0f * s);
        draw->AddRectFilled(dmin, dmax, t.screen, 4.0f * s);
        draw->AddRect(dmin, dmax, t.chassis_shadow, 4.0f * s, 0, 1.0f * s);
        const char *text = tip.shown.valid ? tip.shown.text : "----";
        float tw = panel::seven_width(text);
        float uw = panel::mono_width(tip.shown.valid ? tip.shown.unit : "");
        float tx = dmax.x - 8.0f * s - uw - 6.0f * s - tw;
        panel::seven_text(draw, ImVec2(tx, dmin.y + (dmax.y - dmin.y - panel::seven_height()) * 0.5f),
                          tip.shown.valid ? colour : t.readout_dim, text);
        if (tip.shown.valid) {
            panel::mono_text(draw, ImVec2(dmax.x - 8.0f * s - uw, dmax.y - 8.0f * s - ui::fonts().mono->FontSize), t.readout,
                             tip.shown.unit);
        }
        if (tip.hold) {
            panel::mono_text(draw, ImVec2(dmin.x + 6.0f * s, dmin.y + 4.0f * s), t.led_warn, "HOLD");
        }
        rx += disp_w + gap;
        // HOLD key and the statistics.
        std::snprintf(id, sizeof(id), "##hold%d", i);
        if (ui::key(id, "HOLD", ImVec2(rx, y + (row_h - key_h) * 0.5f), ImVec2(48.0f * s, key_h), tip.hold, t.led_warn, s)) {
            tip.hold = !tip.hold;
        }
        if (tip.samples > 0 && tip.function != Function::Level) {
            // Statistics in the corner of the display.
            char stats[96];
            std::snprintf(stats, sizeof(stats), "min %.3g  max %.3g  avg %.3g", tip.min, tip.max,
                          tip.sum / static_cast<double>(tip.samples));
            draw->AddText(small, small->FontSize, ImVec2(dmin.x + 6.0f * s, dmin.y + 3.0f * s), t.readout_dim, stats);
        }
    }
    (void)inner_w;
    ui::end_chassis();
}

void Multimeter::save(nlohmann::json &out) const
{
    out["rate"] = rate_;
    out["tips"] = nlohmann::json::array();
    for (const auto &tp : tips_) {
        out["tips"].push_back({{"function", static_cast<int>(tp->function)}, {"hold", tp->hold}});
    }
}

void Multimeter::load(const nlohmann::json &in)
{
    rate_ = std::clamp(in.value("rate", 1), 0, 2);
    if (in.contains("tips") && in["tips"].is_array() && !in["tips"].empty()) {
        tips_.clear();
        for (const nlohmann::json &j : in["tips"]) {
            if (static_cast<int>(tips_.size()) >= max_tips) {
                break;
            }
            auto tip = std::make_unique<Tip>();
            tip->function = static_cast<Function>(std::clamp(j.value("function", 2), 0, functions - 1));
            tip->hold = j.value("hold", false);
            tips_.push_back(std::move(tip));
        }
        refresh_wiring();
    }
}

}  // namespace app
