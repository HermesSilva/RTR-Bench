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

// The window of signal a reading looks at: the last 200 ms known.
constexpr int64_t reading_window_ns = 200000000LL;
constexpr float logic_high_v = 3.3f;
constexpr float logic_threshold_v = 1.8f;
constexpr float module_row_h = 60.0f;

void format_volts(char *text, size_t text_size, char *unit, size_t unit_size, double volts)
{
    if (std::fabs(volts) < 1.0) {
        std::snprintf(text, text_size, "%.1f", volts * 1000.0);
        std::snprintf(unit, unit_size, "mV");
    } else {
        std::snprintf(text, text_size, "%.3f", volts);
        std::snprintf(unit, unit_size, "V");
    }
}

void format_amps(char *text, size_t text_size, char *unit, size_t unit_size, double amps)
{
    if (std::fabs(amps) < 1e-3) {
        std::snprintf(text, text_size, "%.1f", amps * 1e6);
        std::snprintf(unit, unit_size, "uA");
    } else if (std::fabs(amps) < 1.0) {
        std::snprintf(text, text_size, "%.3f", amps * 1e3);
        std::snprintf(unit, unit_size, "mA");
    } else {
        std::snprintf(text, text_size, "%.3f", amps);
        std::snprintf(unit, unit_size, "A");
    }
}

void format_hertz(char *text, size_t text_size, char *unit, size_t unit_size, double hz)
{
    if (hz >= 1e6) {
        std::snprintf(text, text_size, "%.4f", hz / 1e6);
        std::snprintf(unit, unit_size, "MHz");
    } else if (hz >= 1e3) {
        std::snprintf(text, text_size, "%.3f", hz / 1e3);
        std::snprintf(unit, unit_size, "kHz");
    } else {
        std::snprintf(text, text_size, "%.2f", hz);
        std::snprintf(unit, unit_size, "Hz");
    }
}

void format_seconds(char *text, size_t text_size, char *unit, size_t unit_size, double ns)
{
    if (ns >= 1e9) {
        std::snprintf(text, text_size, "%.3f", ns / 1e9);
        std::snprintf(unit, unit_size, "s");
    } else if (ns >= 1e6) {
        std::snprintf(text, text_size, "%.3f", ns / 1e6);
        std::snprintf(unit, unit_size, "ms");
    } else if (ns >= 1e3) {
        std::snprintf(text, text_size, "%.2f", ns / 1e3);
        std::snprintf(unit, unit_size, "us");
    } else {
        std::snprintf(text, text_size, "%.0f", ns);
        std::snprintf(unit, unit_size, "ns");
    }
}

}  // namespace

void Multimeter::Lead::reset()
{
    trace.clear();
    analog.clear();
}

bool Multimeter::Lead::volts_at(int64_t ns, float &v) const
{
    if (port < 0) {
        v = 0.0f;   // the bench ground
        return true;
    }
    if (is_analog) {
        return analog.value_at(ns, v);
    }
    int level = trace.level_at(ns);
    if (level < 0) {
        return false;
    }
    v = level ? logic_high_v : 0.0f;
    return true;
}

bool Multimeter::channel_wants_current(int channel) const
{
    size_t tip = static_cast<size_t>(channel / 2);
    if (channel < 0 || tip >= tips_.size()) {
        return false;
    }
    return tips_[tip]->function == Function::AmpsDC || tips_[tip]->function == Function::AmpsAC;
}

std::string Multimeter::channel_name(int channel) const
{
    return "TIP " + std::to_string(channel / 2 + 1) + (channel % 2 ? " COM" : "");
}

Multimeter::Multimeter(App &app) : app_(app)
{
    add_tip();
    // "--dmm 1=10": tip 1 starts on function number 10 (A DC).
    for (const auto &[n, function] : app_.multimeter_functions()) {
        while (static_cast<int>(tips_.size()) < n && static_cast<int>(tips_.size()) < max_tips) {
            add_tip();
        }
        if (n >= 1 && n <= static_cast<int>(tips_.size())) {
            tips_[static_cast<size_t>(n - 1)]->function = static_cast<Function>(std::clamp(function, 0, functions - 1));
        }
    }
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
    // Wires above the removed tip move down one tip (two channels).
    app_.unwire(this->id(), index * 2);
    app_.unwire(this->id(), index * 2 + 1);
    for (int c = (index + 1) * 2; c < static_cast<int>(tips_.size()) * 2; c++) {
        int port = app_.wired_port(this->id(), c);
        app_.unwire(this->id(), c);
        if (port >= 0) {
            app_.rewire(this->id(), c - 2, port);
        }
    }
    tips_.erase(tips_.begin() + index);
    refresh_wiring();
}

void Multimeter::refresh_lead(Lead &lead, int channel)
{
    int port = app_.wired_port(this->id(), channel);
    if (port == lead.port) {
        return;
    }
    lead.port = port;
    lead.reset();
    const core::PortInfo *info = app_.port_info(port);
    lead.is_analog = info && info->analog;
}

void Multimeter::refresh_wiring()
{
    for (size_t i = 0; i < tips_.size(); i++) {
        Tip &t = *tips_[i];
        int before_tip = t.tip.port;
        int before_com = t.com.port;
        refresh_lead(t.tip, static_cast<int>(i) * 2);
        refresh_lead(t.com, static_cast<int>(i) * 2 + 1);
        if (before_tip != t.tip.port || before_com != t.com.port) {
            t.pulses = 0;
            t.min = t.max = t.sum = 0.0;
            t.samples = 0;
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
            for (Lead *lead : {&t.tip, &t.com}) {
                if (lead->port != e.port || lead->is_analog) {
                    continue;
                }
                if (e.kind == core::DigitalEvent::Snapshot) {
                    lead->trace.snapshot(e.ns, e.level);
                } else {
                    lead->trace.add(e.ns, e.level);
                    if (e.level && lead == &t.tip) {
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
            for (Lead *lead : {&t.tip, &t.com}) {
                if (lead->port == b.port && lead->is_analog) {
                    lead->analog.add(b.t0_ns, b.dt_ns, b.volts.data(), b.volts.size());
                }
            }
        }
        latest_ns_ = std::max(latest_ns_, b.t0_ns + static_cast<int64_t>(b.volts.size()) * b.dt_ns);
    }
}

Multimeter::VoltStats Multimeter::volt_stats(const Tip &t, int64_t t0, int64_t t1) const
{
    VoltStats st;
    double sum = 0.0;
    double sum2 = 0.0;
    double weight = 0.0;
    auto take = [&](int64_t ns, double w) {
        float a = 0.0f;
        float b = 0.0f;
        if (!t.tip.volts_at(ns, a) || !t.com.volts_at(ns, b)) {
            return;
        }
        double v = static_cast<double>(a) - static_cast<double>(b);
        if (!st.valid) {
            st.valid = true;
            st.vmin = st.vmax = v;
        }
        st.vmin = std::min(st.vmin, v);
        st.vmax = std::max(st.vmax, v);
        sum += v * w;
        sum2 += v * v * w;
        weight += w;
    };
    // The grid: the samples of an analog lead when there is one, otherwise
    // the transitions of both digital leads (piecewise constant).
    const core::AnalogTrace *grid = t.tip.is_analog ? &t.tip.analog : (t.com.is_analog ? &t.com.analog : nullptr);
    if (grid) {
        if (!grid->empty()) {
            size_t from = grid->index_at(t0);
            size_t to = grid->index_at(t1);
            for (size_t i = from; i < to; i++) {
                take(grid->first_ns() + static_cast<int64_t>(i) * grid->dt_ns(), 1.0);
            }
        }
    } else {
        std::vector<int64_t> times;
        times.push_back(t0);
        for (const Lead *lead : {&t.tip, &t.com}) {
            if (lead->port < 0) {
                continue;
            }
            const core::DigitalTrace &tr = lead->trace;
            for (size_t i = tr.lower_bound(t0); i < tr.size() && tr.at(i).ns < t1; i++) {
                times.push_back(tr.at(i).ns);
            }
        }
        std::sort(times.begin(), times.end());
        for (size_t i = 0; i < times.size(); i++) {
            int64_t next = i + 1 < times.size() ? times[i + 1] : t1;
            if (next > times[i]) {
                take(times[i], static_cast<double>(next - times[i]));
            }
        }
    }
    if (weight <= 0.0) {
        st.valid = false;
        return st;
    }
    st.mean = sum / weight;
    st.rms = std::sqrt(std::max(0.0, sum2 / weight));
    return st;
}

bool Multimeter::timing_trace(const Tip &t, int64_t t0, int64_t t1, core::DigitalTrace &out) const
{
    if (!t.tip.is_analog) {
        return false;
    }
    const core::AnalogTrace &a = t.tip.analog;
    if (a.empty()) {
        return false;
    }
    size_t from = a.index_at(t0);
    size_t to = a.index_at(t1);
    float lo = 0.0f;
    float hi = 0.0f;
    if (from >= to || !a.min_max(from, to, lo, hi) || hi - lo < 0.05f) {
        return false;
    }
    // Squared at the middle of the swing with a tenth of hysteresis.
    float mid = (lo + hi) * 0.5f;
    float band = (hi - lo) * 0.05f;
    int level = a.at(from) > mid ? 1 : 0;
    out.clear();
    out.snapshot(a.first_ns() + static_cast<int64_t>(from) * a.dt_ns(), static_cast<uint8_t>(level));
    for (size_t i = from; i < to; i++) {
        float v = a.at(i);
        int next = level ? (v < mid - band ? 0 : 1) : (v > mid + band ? 1 : 0);
        if (next != level) {
            level = next;
            out.add(a.first_ns() + static_cast<int64_t>(i) * a.dt_ns(), static_cast<uint8_t>(level));
        }
    }
    return true;
}

Multimeter::Reading Multimeter::measure(const Tip &t) const
{
    Reading r;
    if (t.tip.port < 0 || latest_ns_ < 0) {
        return r;
    }
    int64_t t1 = latest_ns_;
    int64_t t0 = t1 - reading_window_ns;
    if (t.function == Function::AmpsDC || t.function == Function::AmpsAC) {
        // In a circuit the tip receives the current through the meter, in
        // amperes; anywhere else there is no current to read.
        if (!App::is_circuit_port(t.tip.port)) {
            return r;
        }
        VoltStats st = volt_stats(t, t0, t1);
        if (!st.valid) {
            return r;
        }
        r.valid = true;
        r.value = t.function == Function::AmpsDC ? st.mean : std::sqrt(std::max(0.0, st.rms * st.rms - st.mean * st.mean));
        format_amps(r.text, sizeof(r.text), r.unit, sizeof(r.unit), r.value);
        return r;
    }
    if (t.function == Function::VoltsDC || t.function == Function::VoltsAC || t.function == Function::VoltsPP) {
        VoltStats st = volt_stats(t, t0, t1);
        if (!st.valid) {
            return r;
        }
        r.valid = true;
        if (t.function == Function::VoltsDC) {
            r.value = st.mean;
        } else if (t.function == Function::VoltsAC) {
            r.value = std::sqrt(std::max(0.0, st.rms * st.rms - st.mean * st.mean));
        } else {
            r.value = st.vmax - st.vmin;
        }
        format_volts(r.text, sizeof(r.text), r.unit, sizeof(r.unit), r.value);
        return r;
    }
    // The timing functions read the tip squared.
    core::DigitalTrace squared{1u << 14};
    bool analog = timing_trace(t, t0, t1, squared);
    if (t.tip.is_analog && !analog) {
        return r;
    }
    const core::DigitalTrace &trace = analog ? squared : t.tip.trace;
    core::Measurements m = core::measure(trace, t0, t1);
    switch (t.function) {
    case Function::Frequency:
        if (!m.valid) {
            return r;
        }
        r.valid = true;
        r.value = m.frequency_hz;
        format_hertz(r.text, sizeof(r.text), r.unit, sizeof(r.unit), r.value);
        return r;
    case Function::Period:
        if (!m.valid) {
            return r;
        }
        r.valid = true;
        r.value = static_cast<double>(m.period_ns);
        format_seconds(r.text, sizeof(r.text), r.unit, sizeof(r.unit), r.value);
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
        format_seconds(r.text, sizeof(r.text), r.unit, sizeof(r.unit), r.value);
        return r;
    case Function::Count: {
        // Digital ports count every rising edge since RESET; an analog one
        // the rising crossings in the window.
        uint64_t pulses = analog ? static_cast<uint64_t>(std::max(0, m.rising)) : t.pulses;
        r.valid = true;
        r.value = static_cast<double>(pulses);
        std::snprintf(r.text, sizeof(r.text), "%llu", static_cast<unsigned long long>(pulses));
        std::snprintf(r.unit, sizeof(r.unit), "pulses");
        return r;
    }
    case Function::Level: {
        int level = -1;
        if (analog) {
            float v = 0.0f;
            if (t.tip.analog.value_at(t1, v)) {
                level = v >= logic_threshold_v ? 1 : 0;
            }
        } else {
            level = trace.level();
        }
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
    case Function::VoltsPP:
    case Function::AmpsDC:
    case Function::AmpsAC:
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
    int wanted = static_cast<int>(130.0f + (module_row_h + 22.0f) * static_cast<float>(rows) + 30.0f);
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

    // One mini module per tip, two per row:
    // [x] [function] [tip jack] [COM jack] [display] [HOLD]
    const float row_h = module_row_h * s;
    const float module_gap = 8.0f * s;
    const float mw = (frame.panel_max.x - frame.panel_min.x - module_gap) / 2.0f;
    const float rows_top = y;
    for (int i = 0; i < static_cast<int>(tips_.size()); i++) {
        Tip &tip = *tips_[static_cast<size_t>(i)];
        float mx = x + static_cast<float>(i % 2) * (mw + module_gap);
        int row = i / 2;
        y = rows_top + static_cast<float>(row) * (row_h + 22.0f * s);
        float cy = y + row_h * 0.5f;
        char title[32];
        std::snprintf(title, sizeof(title), "TIP %d", i + 1);
        ui::group_frame(ImVec2(mx, y - 2.0f * s), ImVec2(mx + mw, y + row_h + 2.0f * s), title, s);
        float rx = mx + 10.0f * s;
        char id[32];
        uint32_t colour = ui::channel_colour(i % ui::channel_count);
        // Remove key.
        std::snprintf(id, sizeof(id), "##rm%d", i);
        if (ui::key(id, "x", ImVec2(rx, cy - key_h * 0.5f), ImVec2(20.0f * s, key_h), false, t.led_stop, s,
                    tips_.size() > 1)) {
            remove_tip(i);
            break;
        }
        rx += 20.0f * s + gap;
        // The function, from a dropdown.
        static const ui::DropdownItem items[functions] = {
            {"V DC", "VOLTAGE"}, {"V AC", "VOLTAGE"}, {"V PP", "VOLTAGE"},  {"FREQ", "TIMING"}, {"PERIOD", "TIMING"},
            {"DUTY", "TIMING"},  {"WIDTH", "TIMING"}, {"COUNT", "TIMING"}, {"LEVEL", "LOGIC"},
            {"A DC", "CURRENT"}, {"A AC", "CURRENT"},
        };
        std::snprintf(id, sizeof(id), "##fn%d", i);
        int chosen = ui::dropdown(id, ImVec2(rx, cy - key_h * 0.5f), ImVec2(78.0f * s, key_h), items, functions,
                                  static_cast<int>(tip.function), colour, s);
        if (chosen >= 0) {
            tip.function = static_cast<Function>(chosen);
            tip.min = tip.max = tip.sum = 0.0;
            tip.samples = 0;
            // Volts and amperes do not mix in the window of a reading.
            tip.tip.reset();
            tip.com.reset();
            tip.shown = Reading{};
        }
        rx += 78.0f * s + gap;
        // The two jacks: the tip and its COM, each with its port name under it.
        for (int lead = 0; lead < 2; lead++) {
            int channel = i * 2 + lead;
            const Lead &l = lead == 0 ? tip.tip : tip.com;
            ImVec2 jack_c(rx + 9.0f * s, cy - 5.0f * s);
            ui::JackLook look;
            look.name = "";
            look.level = -1;
            look.active = false;
            look.input = true;
            look.output = false;
            look.wire_colour = l.port >= 0 ? colour : 0;
            std::snprintf(id, sizeof(id), "##jack%d-%d", i, lead);
            if (ui::jack(id, jack_c, 8.0f * s, look, s)) {
                app_.offer_channel(this->id(), channel);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", lead == 0 ? "tip" : "COM: the reference, the bench ground when free");
            }
            if (app_.channel_offered(this->id(), channel)) {
                draw->AddCircle(jack_c, 11.0f * s, t.led_warn, 20, 1.5f * s);
            }
            app_.anchor_channel(this->id(), channel, window, jack_c.x, jack_c.y);
            // The port name under the jack: "--" for a free tip, "GND" for a free COM.
            std::string port_name =
                l.port >= 0 ? panel::short_port_name(app_.port_info(l.port)) : (lead == 0 ? "--" : "GND");
            float nw = small->CalcTextSizeA(small->FontSize, 1e9f, 0.0f, port_name.c_str()).x;
            draw->AddText(small, small->FontSize, ImVec2(jack_c.x - nw * 0.5f, jack_c.y + 11.0f * s),
                          l.port >= 0 ? colour : t.label_dim, port_name.c_str());
            if (lead == 1) {
                const char *tag = "COM";
                float tw = small->CalcTextSizeA(small->FontSize, 1e9f, 0.0f, tag).x;
                draw->AddText(small, small->FontSize, ImVec2(jack_c.x - tw * 0.5f, jack_c.y - 11.0f * s - small->FontSize),
                              t.label_dim, tag);
            }
            rx += 36.0f * s;
        }
        rx += gap;
        // The display: a dark window, the statistics on its top line, the
        // seven-segment value with the unit under them.
        float hold_w = 46.0f * s;
        float disp_w = mx + mw - 10.0f * s - hold_w - gap - rx;
        ImVec2 dmin(rx, y + 2.0f * s);
        ImVec2 dmax(rx + disp_w, y + row_h - 2.0f * s);
        const char *text = tip.shown.valid ? tip.shown.text : "----";
        float inset = small->FontSize + 4.0f * s;
        panel::seven_display(draw, dmin, dmax, tip.shown.valid ? colour : t.readout_dim, text, t.readout,
                             tip.shown.valid ? tip.shown.unit : "", s, inset);
        if (tip.hold) {
            float hw = small->CalcTextSizeA(small->FontSize, 1e9f, 0.0f, "HOLD").x;
            draw->AddText(small, small->FontSize, ImVec2(dmax.x - 8.0f * s - hw, dmin.y + 3.0f * s), t.led_warn, "HOLD");
        }
        if (tip.samples > 0 && tip.function != Function::Level) {
            char stats[96];
            std::snprintf(stats, sizeof(stats), "min %.3g  max %.3g  avg %.3g", tip.min, tip.max,
                          tip.sum / static_cast<double>(tip.samples));
            draw->AddText(small, small->FontSize, ImVec2(dmin.x + 6.0f * s, dmin.y + 3.0f * s), t.readout_dim, stats);
        }
        rx += disp_w + gap;
        std::snprintf(id, sizeof(id), "##hold%d", i);
        if (ui::key(id, "HOLD", ImVec2(rx, cy - key_h * 0.5f), ImVec2(hold_w, key_h), tip.hold, t.led_warn, s)) {
            tip.hold = !tip.hold;
        }
    }
    app_.grab_near(window);
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
            tip->function = static_cast<Function>(std::clamp(j.value("function", 0), 0, functions - 1));
            tip->hold = j.value("hold", false);
            tips_.push_back(std::move(tip));
        }
        refresh_wiring();
    }
}

}  // namespace app
