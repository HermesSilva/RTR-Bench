// SPDX-License-Identifier: Apache-2.0
#include "instruments/scope/scope.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>

#include "app/app.h"
#include "ui/chassis.h"
#include "ui/fonts.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/window.h"

namespace app {

namespace {

uint32_t with_alpha(uint32_t colour, uint32_t alpha)
{
    return (colour & 0x00FFFFFFu) | (alpha << 24u);
}

void mono_text(ImDrawList *draw, ImVec2 pos, uint32_t colour, const char *text)
{
    ImFont *mono = ui::fonts().mono;
    draw->AddText(mono, mono->FontSize, pos, colour, text);
}

float mono_width(const char *text)
{
    ImFont *mono = ui::fonts().mono;
    return mono->CalcTextSizeA(mono->FontSize, FLT_MAX, 0.0f, text).x;
}

}  // namespace

namespace {

size_t math_slot(int m)
{
    return static_cast<size_t>(app::Scope::channels) + static_cast<size_t>(m);
}

}  // namespace

Scope::Scope(App &app) : app_(app)
{
    for (int m = 0; m < math_channels; m++) {
        Channel &c = ch_[math_slot(m)];
        c.math = true;
        c.trace = core::DigitalTrace(1u << 16);
        c.analog = core::AnalogTrace(1u << 14);
    }
    for (const App::MathPreset &preset : app_.math_presets()) {
        const std::vector<core::Formula> &list = core::formulas();
        for (size_t i = 0; i < list.size(); i++) {
            if (preset.formula == list[i].name && preset.channel >= 1 && preset.channel <= math_channels) {
                Channel &c = ch_[math_slot(preset.channel - 1)];
                c.enabled = true;
                c.formula = static_cast<int>(i);
            }
        }
    }
    refresh_wiring();
}

// Math channels are computed over the current view, every frame.
// Math channels are computed over the current view, every frame, M1 before
// M2 (so M2 may use M1; M1 using M2 sees the previous frame).
void Scope::evaluate_math()
{
    const core::ScopeView &view = engine_.view();
    for (int m = 0; m < math_channels; m++) {
        Channel &c = ch_[math_slot(m)];
        if (!c.enabled || !view.valid) {
            continue;
        }
        const core::Formula &f = core::formulas()[static_cast<size_t>(c.formula)];
        c.is_analog = f.kind == core::FormulaKind::Analog;
        auto usable = [&](int index) {
            if (index < 0 || index >= all_channels || index == channels + m) {
                return false;
            }
            const Channel &ch = ch_[static_cast<size_t>(index)];
            return wired_or_enabled(ch) && ch.is_analog == c.is_analog;
        };
        // An input of the wrong kind is replaced by the next usable channel
        // of the right kind, so that choosing a formula just works.
        for (int k = 0; k < f.inputs; k++) {
            if (usable(c.input[k])) {
                continue;
            }
            for (int j = 0; j < all_channels; j++) {
                bool taken = false;
                for (int o = 0; o < k; o++) {
                    taken = taken || c.input[o] == j;
                }
                if (usable(j) && !taken) {
                    c.input[k] = j;
                    break;
                }
            }
        }
        const Channel *in[core::formula_inputs_max];
        for (int k = 0; k < core::formula_inputs_max; k++) {
            in[k] = &ch_[static_cast<size_t>(std::clamp(c.input[k], 0, all_channels - 1))];
        }
        if (c.is_analog) {
            const core::AnalogTrace *a[core::formula_inputs_max];
            for (int k = 0; k < core::formula_inputs_max; k++) {
                a[k] = (k < f.inputs && usable(c.input[k])) ? &in[k]->analog : nullptr;
            }
            size_t samples = static_cast<size_t>(std::max(16.0f, screen_max_.x - screen_min_.x)) * 2;
            core::evaluate_analog(f, a, view.t0, view.t1, samples, c.analog);
        } else {
            const core::DigitalTrace *d[core::formula_inputs_max];
            for (int k = 0; k < core::formula_inputs_max; k++) {
                d[k] = (k < f.inputs && usable(c.input[k])) ? &in[k]->trace : nullptr;
            }
            core::evaluate_digital(f, d, view.t0, view.t1, c.trace);
        }
    }
}

void Scope::refresh_wiring()
{
    for (int i = 0; i < channels; i++) {
        Channel &c = ch_[static_cast<size_t>(i)];
        int port = app_.wired_port(Instrument::Scope, i);
        if (port != c.port) {
            c.port = port;
            c.trace.clear();
            c.analog.clear();
            const core::PortInfo *info = app_.port_info(port);
            c.is_analog = info && info->analog;
        }
    }
}

void Scope::wiring_changed()
{
    refresh_wiring();
    engine_.clear();
    latest_ns_ = -1;
}

bool Scope::any_analog() const
{
    for (const Channel &c : ch_) {
        if (wired_or_enabled(c) && c.is_analog) {
            return true;
        }
    }
    return false;
}

void Scope::feed(const std::vector<core::DigitalEvent> &events)
{
    for (const core::DigitalEvent &e : events) {
        for (Channel &c : ch_) {
            if (c.port == e.port && !c.is_analog) {
                if (e.kind == core::DigitalEvent::Snapshot) {
                    c.trace.snapshot(e.ns, e.level);
                } else {
                    c.trace.add(e.ns, e.level);
                }
            }
        }
        if (e.ns > latest_ns_) {
            latest_ns_ = e.ns;
        }
    }
}

void Scope::feed_analog(const std::vector<core::AnalogBlock> &blocks)
{
    for (const core::AnalogBlock &b : blocks) {
        for (Channel &c : ch_) {
            if (c.port == b.port && c.is_analog) {
                c.analog.add(b.t0_ns, b.dt_ns, b.volts.data(), b.volts.size());
            }
        }
        int64_t end = b.t0_ns + static_cast<int64_t>(b.volts.size()) * b.dt_ns;
        if (end > latest_ns_) {
            latest_ns_ = end;
        }
    }
}

float Scope::volts_to_y(const Channel &ch, float volts) const
{
    float div_px = (analog_max_.y - analog_min_.y) / 8.0f;
    float centre = (analog_min_.y + analog_max_.y) * 0.5f;
    float per_div = core::volts_per_div_steps()[static_cast<size_t>(ch.volts_step)];
    return centre - (volts / per_div + ch.offset_div) * div_px;
}

float Scope::time_to_x(int64_t ns) const
{
    const core::ScopeView &v = engine_.view();
    double span = static_cast<double>(v.t1 - v.t0);
    if (span <= 0.0) {
        return screen_min_.x;
    }
    double f = static_cast<double>(ns - v.t0) / span;
    return screen_min_.x + static_cast<float>(f) * (screen_max_.x - screen_min_.x);
}

void Scope::handle_keys()
{
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        if (engine_.running()) {
            engine_.stop();
        } else {
            engine_.run();
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        engine_.single();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_C, false)) {
        cursors_ = !cursors_;
    }
    int64_t step = engine_.ns_per_div() / 2;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        engine_.pan(-step);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        engine_.pan(step);
    }
    int max_step = static_cast<int>(core::time_per_div_steps().size()) - 1;
    if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) {
        engine_.settings.time_step = std::max(0, engine_.settings.time_step - 1);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) {
        engine_.settings.time_step = std::min(max_step, engine_.settings.time_step + 1);
    }
}

void Scope::draw(ui::Window &window)
{
    ui::ChassisSpec chassis;
    chassis.model = "MSO-1";
    chassis.title = "Mixed Signal Oscilloscope";
    ui::ChassisFrame frame = ui::begin_chassis(window, chassis);
    const float s = window.scale();

    handle_keys();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        app_.cancel_wiring();
    }

    // The trigger source feeds the engine; the view it decides is drawn.
    const Channel &source = ch_[static_cast<size_t>(engine_.settings.trigger_channel)];
    core::TriggerSource ts;
    if (source.port >= 0) {
        if (source.is_analog) {
            ts.analog = &source.analog;
            ts.level = trigger_level_;
        } else {
            ts.digital = &source.trace;
        }
    }
    engine_.update(ts, latest_ns_);

    float controls_w = 300.0f * s;
    ImVec2 screen_min = frame.panel_min;
    ImVec2 screen_max(frame.panel_max.x - controls_w - 10.0f * s, frame.panel_max.y);
    evaluate_math();
    draw_screen(window, screen_min, screen_max);
    draw_controls(window, ImVec2(screen_max.x + 10.0f * s, frame.panel_min.y), frame.panel_max);

    ui::end_chassis();
}

void Scope::draw_screen(ui::Window &window, ImVec2 min, ImVec2 max)
{
    const ui::Theme &t = ui::current_theme();
    const float s = window.scale();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const core::ScopeView &view = engine_.view();
    const float line_h = ui::fonts().mono->FontSize + 4.0f * s;

    // Bezel, then the phosphor area with a status bar above and readouts below.
    draw->AddRectFilled(min, max, t.screen_bezel, 10.0f * s);
    ImVec2 inner_min(min.x + 8.0f * s, min.y + 8.0f * s);
    ImVec2 inner_max(max.x - 8.0f * s, max.y - 8.0f * s);
    draw->AddRectFilled(inner_min, inner_max, t.screen, 4.0f * s);
    screen_min_ = ImVec2(inner_min.x + 6.0f * s, inner_min.y + line_h + 6.0f * s);
    screen_max_ = ImVec2(inner_max.x - 6.0f * s, inner_max.y - line_h * 2.0f - 8.0f * s);
    const float w = screen_max_.x - screen_min_.x;
    const float h = screen_max_.y - screen_min_.y;

    // Graticule: 10 x 8 with the centre lines stronger and tick marks.
    for (int i = 0; i <= core::scope_divisions; i++) {
        float x = screen_min_.x + w * static_cast<float>(i) / core::scope_divisions;
        draw->AddLine(ImVec2(x, screen_min_.y), ImVec2(x, screen_max_.y),
                      i == core::scope_divisions / 2 ? t.graticule_axis : t.graticule, 1.0f);
    }
    for (int i = 0; i <= 8; i++) {
        float y = screen_min_.y + h * static_cast<float>(i) / 8.0f;
        draw->AddLine(ImVec2(screen_min_.x, y), ImVec2(screen_max_.x, y), i == 4 ? t.graticule_axis : t.graticule, 1.0f);
    }
    for (int i = 0; i <= core::scope_divisions * 5; i++) {
        float x = screen_min_.x + w * static_cast<float>(i) / (core::scope_divisions * 5.0f);
        draw->AddLine(ImVec2(x, screen_min_.y + h * 0.5f - 3.0f * s), ImVec2(x, screen_min_.y + h * 0.5f + 3.0f * s),
                      t.graticule_axis, 1.0f);
    }

    // Status bar: time base, trigger, state, clock.
    char text[256];
    char per_div[32];
    core::format_duration(per_div, sizeof(per_div), engine_.ns_per_div());
    float x = inner_min.x + 8.0f * s;
    float y = inner_min.y + 4.0f * s;
    std::snprintf(text, sizeof(text), "H %s/div", per_div);
    mono_text(draw, ImVec2(x, y), t.readout, text);
    x += mono_width(text) + 18.0f * s;
    if (engine_.settings.position_ns != 0) {
        char pos[32];
        core::format_duration(pos, sizeof(pos), engine_.settings.position_ns);
        std::snprintf(text, sizeof(text), "pos %s", pos);
        mono_text(draw, ImVec2(x, y), t.readout_dim, text);
        x += mono_width(text) + 18.0f * s;
    }
    const char *slope = engine_.settings.slope == core::TriggerSlope::Rising    ? "rise"
                        : engine_.settings.slope == core::TriggerSlope::Falling ? "fall"
                                                                                : "both";
    const char *mode = engine_.settings.mode == core::TriggerMode::Auto     ? "AUTO"
                       : engine_.settings.mode == core::TriggerMode::Normal ? "NORM"
                                                                           : "SINGLE";
    std::snprintf(text, sizeof(text), "T CH%d %s %s", engine_.settings.trigger_channel + 1, slope, mode);
    mono_text(draw, ImVec2(x, y), ui::channel_colour(engine_.settings.trigger_channel), text);
    x += mono_width(text) + 18.0f * s;
    const char *state = !engine_.running() ? "STOP" : (view.triggered ? "TRIG'D" : "AUTO");
    mono_text(draw, ImVec2(x, y), !engine_.running() ? t.led_stop : (view.triggered ? t.led_run : t.led_warn), state);
    if (latest_ns_ >= 0) {
        std::snprintf(text, sizeof(text), "%s%.6f s", app_.probe().capabilities().virtual_time ? "virtual " : "",
                      static_cast<double>(latest_ns_) / 1e9);
        mono_text(draw, ImVec2(inner_max.x - 8.0f * s - mono_width(text), y), t.readout_dim, text);
    }

    // Traces: one lane per channel, top to bottom.
    draw->PushClipRect(screen_min_, screen_max_, true);
    // Analog channels share the upper part (all of it when there is no
    // digital channel); digital channels get lanes below.
    int digital_count = 0;
    for (const Channel &c : ch_) {
        if (wired_or_enabled(c) && !c.is_analog && c.visible) {
            digital_count++;
        }
    }
    bool analog = any_analog();
    float digital_h = analog ? (digital_count > 0 ? h * 0.32f : 0.0f) : h;
    analog_min_ = screen_min_;
    analog_max_ = ImVec2(screen_max_.x, screen_max_.y - digital_h);
    if (view.valid) {
        if (analog) {
            for (int c = 0; c < all_channels; c++) {
                const Channel &ch = ch_[static_cast<size_t>(c)];
                if (wired_or_enabled(ch) && ch.is_analog && ch.visible) {
                    draw_analog(ch, c, draw, s);
                }
            }
            if (digital_count > 0) {
                draw->AddLine(ImVec2(screen_min_.x, analog_max_.y), ImVec2(screen_max_.x, analog_max_.y),
                              t.graticule_axis, 1.0f);
            }
        }
        // Without analog channels the digital lanes split the screen evenly
        // among the shown channels (at least four lanes, like the inputs).
        int lanes = analog ? std::max(1, digital_count) : std::max(channels, digital_count);
        float lane_h = digital_count > 0 ? digital_h / static_cast<float>(lanes) : 0.0f;
        int lane = 0;
        for (int c = 0; c < all_channels; c++) {
            const Channel &ch = ch_[static_cast<size_t>(c)];
            if (!wired_or_enabled(ch) || ch.is_analog || !ch.visible) {
                continue;
            }
            float top = analog_max_.y + lane_h * static_cast<float>(lane);
            draw_digital(ch, c, top + lane_h * 0.18f, top + lane_h - lane_h * 0.18f, draw, s);
            lane++;
        }
        // Trigger marker.
        if (view.trigger_ns >= 0) {
            float tx = time_to_x(view.trigger_ns);
            uint32_t tc = ui::channel_colour(engine_.settings.trigger_channel);
            draw->AddTriangleFilled(ImVec2(tx - 6.0f * s, screen_min_.y), ImVec2(tx + 6.0f * s, screen_min_.y),
                                    ImVec2(tx, screen_min_.y + 8.0f * s), tc);
            draw->AddLine(ImVec2(tx, screen_min_.y), ImVec2(tx, screen_max_.y), with_alpha(tc, 60), 1.0f);
        }
    }
    draw->PopClipRect();

    // Cursors: two draggable vertical lines with their readouts.
    if (cursors_ && view.valid) {
        int64_t span = view.t1 - view.t0;
        double *cursors[2] = {&cursor_a_, &cursor_b_};
        const char *names[2] = {"A", "B"};
        for (int k = 0; k < 2; k++) {
            float cx = screen_min_.x + static_cast<float>(*cursors[k]) * w;
            ImGui::SetCursorScreenPos(ImVec2(cx - 5.0f * s, screen_min_.y));
            ImGui::InvisibleButton(names[k], ImVec2(10.0f * s, h));
            if (ImGui::IsItemActive()) {
                double f = static_cast<double>(ImGui::GetIO().MousePos.x - screen_min_.x) / static_cast<double>(w);
                *cursors[k] = std::clamp(f, 0.0, 1.0);
                cx = screen_min_.x + static_cast<float>(*cursors[k]) * w;
            }
            uint32_t cc = ImGui::IsItemHovered() || ImGui::IsItemActive() ? t.readout : t.readout_dim;
            draw->AddLine(ImVec2(cx, screen_min_.y), ImVec2(cx, screen_max_.y), cc, 1.0f);
            mono_text(draw, ImVec2(cx + 3.0f * s, screen_max_.y - line_h), cc, names[k]);
        }
        int64_t ta = view.t0 + static_cast<int64_t>(cursor_a_ * static_cast<double>(span));
        int64_t tb = view.t0 + static_cast<int64_t>(cursor_b_ * static_cast<double>(span));
        char da[32];
        char db[32];
        char dd[32];
        char df[32];
        core::format_duration(da, sizeof(da), ta - (view.trigger_ns >= 0 ? view.trigger_ns : view.t0));
        core::format_duration(db, sizeof(db), tb - (view.trigger_ns >= 0 ? view.trigger_ns : view.t0));
        core::format_duration(dd, sizeof(dd), tb - ta);
        if (tb != ta) {
            core::format_frequency(df, sizeof(df), 1e9 / static_cast<double>(tb > ta ? tb - ta : ta - tb));
        } else {
            std::snprintf(df, sizeof(df), "--");
        }
        std::snprintf(text, sizeof(text), "A %s  B %s  dt %s  1/dt %s", da, db, dd, df);
        mono_text(draw, ImVec2(inner_min.x + 8.0f * s, screen_max_.y + 4.0f * s), t.readout, text);
    }

    // Readouts: channel line and measurement line.
    float ry = screen_max_.y + (cursors_ ? line_h : 0.0f) + 4.0f * s;
    x = inner_min.x + 8.0f * s;
    for (int c = 0; c < all_channels; c++) {
        const Channel &ch = ch_[static_cast<size_t>(c)];
        uint32_t colour = wired_or_enabled(ch) ? ui::channel_colour(c) : t.readout_dim;
        const core::PortInfo *info = app_.port_info(ch.port);
        char tag[8];
        if (ch.math) {
            std::snprintf(tag, sizeof(tag), "M%d", c - channels + 1);
        } else {
            std::snprintf(tag, sizeof(tag), "%d", c + 1);
        }
        if (ch.math && ch.enabled) {
            const core::Formula &f = core::formulas()[static_cast<size_t>(ch.formula)];
            const char *names = "ABCD";
            char expr[64];
            std::snprintf(expr, sizeof(expr), "%s", f.name);
            // "A", "B", "C" in the name become the input channel numbers.
            std::string shown;
            for (const char *p = expr; *p; p++) {
                const char *k = std::strchr(names, *p);
                bool word = k && (p == expr || !std::isalnum(static_cast<unsigned char>(p[-1]))) &&
                            !std::isalnum(static_cast<unsigned char>(p[1]));
                if (word) {
                    int src = ch.input[k - names];
                    shown += src >= channels ? "M" + std::to_string(src - channels + 1) : "CH" + std::to_string(src + 1);
                } else {
                    shown += *p;
                }
            }
            if (ch.is_analog) {
                char vd[24];
                core::format_volts(vd, sizeof(vd), core::volts_per_div_steps()[static_cast<size_t>(ch.volts_step)]);
                std::snprintf(text, sizeof(text), "%s = %s %s/div", tag, shown.c_str(), vd);
            } else {
                std::snprintf(text, sizeof(text), "%s = %s", tag, shown.c_str());
            }
        } else if (ch.math) {
            std::snprintf(text, sizeof(text), "%s off", tag);
        } else if (ch.port >= 0 && ch.is_analog) {
            char vd[24];
            core::format_volts(vd, sizeof(vd), core::volts_per_div_steps()[static_cast<size_t>(ch.volts_step)]);
            std::snprintf(text, sizeof(text), "%s %s %s/div", tag, info ? info->name.c_str() : "?", vd);
        } else if (ch.port >= 0) {
            std::snprintf(text, sizeof(text), "%s %s", tag, info ? info->name.c_str() : "?");
        } else {
            std::snprintf(text, sizeof(text), "%s --", tag);
        }
        if (c == selected_) {
            float tw = mono_width(text);
            draw->AddRect(ImVec2(x - 3.0f * s, ry - 1.0f * s), ImVec2(x + tw + 3.0f * s, ry + line_h - 3.0f * s), colour,
                          2.0f * s);
        }
        mono_text(draw, ImVec2(x, ry), colour, text);
        x += mono_width(text) + 16.0f * s;
    }
    const Channel &sel = ch_[static_cast<size_t>(selected_)];
    char sel_tag[8];
    if (sel.math) {
        std::snprintf(sel_tag, sizeof(sel_tag), "M%d", selected_ - channels + 1);
    } else {
        std::snprintf(sel_tag, sizeof(sel_tag), "CH%d", selected_ + 1);
    }
    if (wired_or_enabled(sel) && sel.is_analog && view.valid) {
        core::AnalogMeasurements m = core::measure_analog(sel.analog, view.t0, view.t1);
        if (m.valid) {
            char pp[24];
            char mx[24];
            char mn[24];
            char mean[24];
            char rms[24];
            char f[32];
            char p[32];
            core::format_volts(pp, sizeof(pp), m.vpp);
            core::format_volts(mx, sizeof(mx), m.vmax);
            core::format_volts(mn, sizeof(mn), m.vmin);
            core::format_volts(mean, sizeof(mean), m.vmean);
            core::format_volts(rms, sizeof(rms), m.vrms);
            if (m.periodic) {
                core::format_frequency(f, sizeof(f), m.frequency_hz);
                core::format_duration(p, sizeof(p), m.period_ns);
            } else {
                std::snprintf(f, sizeof(f), "--");
                std::snprintf(p, sizeof(p), "--");
            }
            std::snprintf(text, sizeof(text), "%s  Vpp %s  Vmax %s  Vmin %s  Vmean %s  Vrms %s  f %s  T %s",
                          sel_tag, pp, mx, mn, mean, rms, f, p);
        } else {
            std::snprintf(text, sizeof(text), "%s  no samples on screen", sel_tag);
        }
        mono_text(draw, ImVec2(inner_min.x + 8.0f * s, ry + line_h), ui::channel_colour(selected_), text);
    } else if (wired_or_enabled(sel) && view.valid) {
        core::Measurements m = core::measure(sel.trace, view.t0, view.t1);
        if (m.valid) {
            char f[32];
            char p[32];
            char hi[32];
            char lo[32];
            core::format_frequency(f, sizeof(f), m.frequency_hz);
            core::format_duration(p, sizeof(p), m.period_ns);
            core::format_duration(hi, sizeof(hi), m.high_ns);
            core::format_duration(lo, sizeof(lo), m.low_ns);
            std::snprintf(text, sizeof(text), "%s  f %s  T %s  +w %s  -w %s  duty %.1f %%  edges %d", sel_tag,
                          f, p, hi, lo, m.duty * 100.0, m.rising + m.falling);
        } else {
            std::snprintf(text, sizeof(text), "%s  no full period on screen  edges %d", sel_tag,
                          m.rising + m.falling);
        }
        mono_text(draw, ImVec2(inner_min.x + 8.0f * s, ry + line_h), ui::channel_colour(selected_), text);
    } else if (!wired_or_enabled(sel)) {
        mono_text(draw, ImVec2(inner_min.x + 8.0f * s, ry + line_h), t.readout_dim,
                  sel.math ? "math channel off: press its key and choose a formula"
                           : "wire a port: press a CH key, then click a jack on the rack");
    }
}

void Scope::draw_digital(const Channel &ch, int index, float lane_top, float lane_bottom, ImDrawList *draw, float s)
{
    const ui::Theme &t = ui::current_theme();
    const core::ScopeView &view = engine_.view();
    int64_t span = view.t1 - view.t0;
    const float w = screen_max_.x - screen_min_.x;
    uint32_t colour = ui::channel_colour(index);
    auto level_y = [&](int level) { return level ? lane_top : lane_bottom; };

    int level = ch.trace.level_at(view.t0);
    size_t i = ch.trace.lower_bound(view.t0);
    size_t end = ch.trace.lower_bound(view.t1 + 1);
    float x0 = screen_min_.x;
    float thickness = 1.6f * s;
    if (level < 0 && i < end) {
        level = ch.trace.at(i).level ? 0 : 1;  // before the first known edge
    }
    if (level < 0) {
        // Nothing known in this window: a dim line in the middle of the lane.
        draw->AddLine(ImVec2(screen_min_.x, (lane_top + lane_bottom) * 0.5f),
                      ImVec2(screen_max_.x, (lane_top + lane_bottom) * 0.5f), with_alpha(colour, 70), 1.0f);
    } else {
        // Dense columns (several edges in one pixel) are drawn as a band.
        float dense_from = -1.0f;
        for (; i < end; i++) {
            const core::Transition &tr = ch.trace.at(i);
            float x1 = screen_min_.x + static_cast<float>(static_cast<double>(tr.ns - view.t0) / static_cast<double>(span)) * w;
            if (x1 - x0 < 1.0f && dense_from < 0.0f) {
                dense_from = x0;
            }
            if (dense_from >= 0.0f) {
                if (x1 - dense_from >= 1.0f) {
                    draw->AddRectFilled(ImVec2(dense_from, lane_top), ImVec2(x1, lane_bottom), with_alpha(colour, 170));
                    dense_from = -1.0f;
                    x0 = x1;
                }
                level = tr.level;
                continue;
            }
            draw->AddLine(ImVec2(x0, level_y(level)), ImVec2(x1, level_y(level)), colour, thickness);
            draw->AddLine(ImVec2(x1, lane_top), ImVec2(x1, lane_bottom), colour, thickness);
            x0 = x1;
            level = tr.level;
        }
        if (dense_from >= 0.0f) {
            draw->AddRectFilled(ImVec2(dense_from, lane_top), ImVec2(screen_max_.x, lane_bottom), with_alpha(colour, 170));
        } else {
            draw->AddLine(ImVec2(x0, level_y(level)), ImVec2(screen_max_.x, level_y(level)), colour, thickness);
        }
    }
    char tag[16];
    std::snprintf(tag, sizeof(tag), "%d", index + 1);
    draw->AddRectFilled(ImVec2(screen_min_.x + 2.0f * s, lane_top - 2.0f * s),
                        ImVec2(screen_min_.x + 16.0f * s, lane_top + 14.0f * s), colour, 2.0f * s);
    mono_text(draw, ImVec2(screen_min_.x + 5.0f * s, lane_top - 1.0f * s), t.screen, tag);
}

void Scope::draw_analog(const Channel &ch, int index, ImDrawList *draw, float s)
{
    const ui::Theme &t = ui::current_theme();
    const core::ScopeView &view = engine_.view();
    const core::AnalogTrace &a = ch.analog;
    uint32_t colour = ui::channel_colour(index);
    const float w = screen_max_.x - screen_min_.x;
    float zero_y = volts_to_y(ch, 0.0f);

    // Ground marker at the left edge, trigger level marker at the right.
    draw->AddTriangleFilled(ImVec2(screen_min_.x, zero_y - 5.0f * s), ImVec2(screen_min_.x, zero_y + 5.0f * s),
                            ImVec2(screen_min_.x + 7.0f * s, zero_y), colour);
    if (engine_.settings.trigger_channel == index) {
        float ty = volts_to_y(ch, trigger_level_);
        draw->AddTriangleFilled(ImVec2(screen_max_.x, ty - 5.0f * s), ImVec2(screen_max_.x, ty + 5.0f * s),
                                ImVec2(screen_max_.x - 7.0f * s, ty), colour);
        draw->AddLine(ImVec2(screen_min_.x, ty), ImVec2(screen_max_.x, ty), with_alpha(colour, 50), 1.0f);
    }
    if (a.empty()) {
        return;
    }
    // One column per pixel: the min and max of the samples in it, joined to
    // the previous column so the trace is continuous.
    int64_t span = view.t1 - view.t0;
    int columns = static_cast<int>(w);
    if (columns <= 0 || span <= 0) {
        return;
    }
    float prev_lo = 0.0f;
    float prev_hi = 0.0f;
    bool have_prev = false;
    float thickness = 1.5f * s;
    for (int col = 0; col < columns; col++) {
        int64_t tc0 = view.t0 + span * col / columns;
        int64_t tc1 = view.t0 + span * (col + 1) / columns;
        size_t i0 = a.index_at(tc0);
        size_t i1 = a.index_at(tc1);
        float lo = 0.0f;
        float hi = 0.0f;
        bool have = false;
        if (i1 > i0) {
            have = a.min_max(i0, i1, lo, hi);
        } else {
            float v = 0.0f;
            have = a.value_at(tc0, v);
            lo = hi = v;
        }
        if (!have) {
            have_prev = false;
            continue;
        }
        float x = screen_min_.x + static_cast<float>(col) + 0.5f;
        float y_lo = volts_to_y(ch, lo);
        float y_hi = volts_to_y(ch, hi);
        if (have_prev) {
            // Join the columns: the middle of the previous range to the middle of this one.
            draw->AddLine(ImVec2(x - 1.0f, volts_to_y(ch, (prev_lo + prev_hi) * 0.5f)), ImVec2(x, (y_lo + y_hi) * 0.5f),
                          colour, thickness);
        }
        if (y_lo - y_hi > 1.0f) {
            draw->AddLine(ImVec2(x, y_hi), ImVec2(x, y_lo), colour, thickness);
        }
        prev_lo = lo;
        prev_hi = hi;
        have_prev = true;
    }
    char tag[16];
    std::snprintf(tag, sizeof(tag), "%d", index + 1);
    draw->AddRectFilled(ImVec2(screen_min_.x + 9.0f * s, zero_y - 8.0f * s),
                        ImVec2(screen_min_.x + 23.0f * s, zero_y + 8.0f * s), colour, 2.0f * s);
    mono_text(draw, ImVec2(screen_min_.x + 12.0f * s, zero_y - 7.0f * s), t.screen, tag);
}

// The formula chooser: a readout-like button; its popup lists the formulas
// under their group headings, in the panel colours, the current one marked.
void Scope::draw_formula_combo(int math_index, ImVec2 pos, ImVec2 size, float s)
{
    const ui::Theme &t = ui::current_theme();
    Channel &mc = ch_[math_slot(math_index)];
    const std::vector<core::Formula> &list = core::formulas();
    const core::Formula &current = list[static_cast<size_t>(mc.formula)];
    ImDrawList *draw = ImGui::GetWindowDrawList();

    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("##formula", size);
    bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) {
        ImGui::OpenPopup("##formula-list");
    }
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), hovered ? t.key_hover : t.screen, 4.0f * s);
    draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), t.chassis_shadow, 4.0f * s, 0, 1.0f * s);
    ImFont *mono = ui::fonts().mono;
    draw->AddText(mono, mono->FontSize, ImVec2(pos.x + 8.0f * s, pos.y + (size.y - mono->FontSize) * 0.5f),
                  ui::channel_colour(channels + math_index), current.name);
    // The chevron of a combo at the right.
    float cx = pos.x + size.x - 12.0f * s;
    float cy = pos.y + size.y * 0.5f;
    draw->AddTriangleFilled(ImVec2(cx - 5.0f * s, cy - 3.0f * s), ImVec2(cx + 5.0f * s, cy - 3.0f * s),
                            ImVec2(cx, cy + 3.0f * s), t.label_dim);

    ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y + size.y + 4.0f * s));
    ImGui::SetNextWindowSize(ImVec2(size.x + 40.0f * s, 0.0f));
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
    if (ImGui::BeginPopup("##formula-list")) {
        const char *group = nullptr;
        for (size_t i = 0; i < list.size(); i++) {
            const core::Formula &f = list[i];
            if (!group || std::strcmp(group, f.group) != 0) {
                group = f.group;
                if (i > 0) {
                    ImGui::Spacing();
                }
                ImGui::PushFont(ui::fonts().small);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(t.label_dim), "%s", group);
                ImGui::PopFont();
            }
            bool is_current = static_cast<int>(i) == mc.formula;
            ImGui::PushFont(mono);
            if (is_current) {
                ImGui::PushStyleColor(ImGuiCol_Text, ui::channel_colour(channels + math_index));
            }
            if (ImGui::Selectable(f.name, is_current)) {
                mc.formula = static_cast<int>(i);
                mc.enabled = true;
                mc.trace.clear();
                mc.analog.clear();
                ImGui::CloseCurrentPopup();
            }
            if (is_current) {
                ImGui::PopStyleColor();
            }
            ImGui::PopFont();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(6);
}

void Scope::draw_controls(ui::Window &window, ImVec2 min, ImVec2 max)
{
    const ui::Theme &t = ui::current_theme();
    const float s = window.scale();
    const float key_h = 21.0f * s;
    const float gap = 5.0f * s;
    const float pad = 8.0f * s;
    const float title_h = 11.0f * s;    // room for the group title above its frame
    const float knob_r = 15.0f * s;
    const float read_h = 19.0f * s;
    float x = min.x + pad;
    float y = min.y + title_h;
    float inner_w = max.x - min.x - 2.0f * pad;
    char text[64];
    ImDrawList *draw = ImGui::GetWindowDrawList();
    auto next_group = [&](float bottom) {
        y = bottom + title_h + 6.0f * s;
    };

    // ---- RUN CONTROL: one row of three keys ---------------------------------
    float group_top = y - title_h;
    float kw = (inner_w - 2.0f * gap) / 3.0f;
    if (ui::key("##run", engine_.running() ? "RUN" : "STOP", ImVec2(x, y), ImVec2(kw, key_h), true,
                engine_.running() ? t.led_run : t.led_stop, s)) {
        if (engine_.running()) {
            engine_.stop();
        } else {
            engine_.run();
        }
    }
    if (ui::key("##single", "SINGLE", ImVec2(x + kw + gap, y), ImVec2(kw, key_h), false, t.led_warn, s)) {
        engine_.single();
    }
    if (ui::key("##clear", "CLEAR", ImVec2(x + 2.0f * (kw + gap), y), ImVec2(kw, key_h), false, t.led_warn, s)) {
        for (Channel &c : ch_) {
            c.trace.clear();
            c.analog.clear();
        }
        engine_.clear();
    }
    y += key_h + 6.0f * s;
    ui::group_frame(ImVec2(min.x, group_top), ImVec2(max.x, y), "RUN CONTROL", s);
    next_group(y);

    // ---- HORIZONTAL | VERTICAL: four small knobs in one row, readouts below ----
    group_top = y - title_h;
    Channel &selected = ch_[static_cast<size_t>(selected_)];
    bool vertical = wired_or_enabled(selected) && selected.is_analog;
    float quarter = inner_w / 4.0f;
    float ky = y + knob_r + 2.0f * s;
    int max_step = static_cast<int>(core::time_per_div_steps().size()) - 1;
    int vmax_step = static_cast<int>(core::volts_per_div_steps().size()) - 1;
    bool pressed = false;
    int steps = ui::knob("##timediv", ImVec2(x + quarter * 0.5f, ky), knob_r, "SCALE", s, &pressed);
    if (steps != 0) {
        engine_.settings.time_step = std::clamp(engine_.settings.time_step - steps, 0, max_step);
    }
    steps = ui::knob("##position", ImVec2(x + quarter * 1.5f, ky), knob_r, "POS", s, &pressed);
    if (steps != 0) {
        engine_.pan(static_cast<int64_t>(steps) * engine_.ns_per_div() / 4);
    }
    if (pressed) {
        engine_.settings.position_ns = 0;
    }
    steps = ui::knob("##vscale", ImVec2(x + quarter * 2.5f, ky), knob_r, "V/DIV", s, &pressed);
    if (vertical && steps != 0) {
        selected.volts_step = std::clamp(selected.volts_step - steps, 0, vmax_step);
    }
    steps = ui::knob("##voffset", ImVec2(x + quarter * 3.5f, ky), knob_r, "OFFSET", s, &pressed);
    if (vertical && steps != 0) {
        selected.offset_div = std::clamp(selected.offset_div + static_cast<float>(steps) * 0.25f, -4.0f, 4.0f);
    }
    if (vertical && pressed) {
        selected.offset_div = 0.0f;
    }
    // A thin divider between the horizontal and the vertical pair.
    draw->AddLine(ImVec2(x + quarter * 2.0f, y), ImVec2(x + quarter * 2.0f, ky + knob_r + 14.0f * s), t.chassis_shadow, 1.0f);
    y = ky + knob_r + 18.0f * s;
    char per_div[32];
    core::format_duration(per_div, sizeof(per_div), engine_.ns_per_div());
    std::snprintf(text, sizeof(text), "%s/div", per_div);
    float rw = quarter - gap;
    ui::readout(ImVec2(x, y), ImVec2(x + rw, y + read_h), text, t.readout, s);
    core::format_duration(per_div, sizeof(per_div), engine_.settings.position_ns);
    ui::readout(ImVec2(x + quarter, y), ImVec2(x + quarter + rw, y + read_h), per_div, t.readout, s);
    if (vertical) {
        char vd[24];
        core::format_volts(vd, sizeof(vd), core::volts_per_div_steps()[static_cast<size_t>(selected.volts_step)]);
        std::snprintf(text, sizeof(text), "%s/div", vd);
        ui::readout(ImVec2(x + 2.0f * quarter, y), ImVec2(x + 2.0f * quarter + rw, y + read_h), text,
                    ui::channel_colour(selected_), s);
        std::snprintf(text, sizeof(text), "%+.2f div", static_cast<double>(selected.offset_div));
        ui::readout(ImVec2(x + 3.0f * quarter, y), ImVec2(x + 3.0f * quarter + rw, y + read_h), text,
                    ui::channel_colour(selected_), s);
    } else {
        ui::readout(ImVec2(x + 2.0f * quarter, y), ImVec2(x + 2.0f * quarter + rw, y + read_h), "digital", t.readout_dim, s);
        ui::readout(ImVec2(x + 3.0f * quarter, y), ImVec2(x + 3.0f * quarter + rw, y + read_h), "--", t.readout_dim, s);
    }
    y += read_h + 6.0f * s;
    char htitle[48];
    if (selected.math) {
        std::snprintf(htitle, sizeof(htitle), "HORIZONTAL   VERTICAL M%d", selected_ - channels + 1);
    } else {
        std::snprintf(htitle, sizeof(htitle), "HORIZONTAL   VERTICAL CH%d", selected_ + 1);
    }
    ui::group_frame(ImVec2(min.x, group_top), ImVec2(max.x, y), htitle, s);
    next_group(y);

    // ---- TRIGGER: source row, then slope / mode / level --------------------
    group_top = y - title_h;
    kw = (inner_w - 3.0f * gap) / 4.0f;
    for (int c = 0; c < channels; c++) {
        char id[16];
        char label[8];
        std::snprintf(id, sizeof(id), "##trig%d", c);
        std::snprintf(label, sizeof(label), "CH%d", c + 1);
        if (ui::key(id, label, ImVec2(x + static_cast<float>(c) * (kw + gap), y), ImVec2(kw, key_h),
                    engine_.settings.trigger_channel == c, ui::channel_colour(c), s)) {
            engine_.settings.trigger_channel = c;
            engine_.clear();
        }
    }
    y += key_h + gap;
    const Channel &trig = ch_[static_cast<size_t>(engine_.settings.trigger_channel)];
    bool analog_trigger = trig.port >= 0 && trig.is_analog;
    float lr = 11.0f * s;
    float keys_w = inner_w - (lr * 2.0f + gap) - (rw + gap);
    kw = (keys_w - 2.0f * gap) / 3.0f;
    const char *slope_label = engine_.settings.slope == core::TriggerSlope::Rising    ? "RISE"
                              : engine_.settings.slope == core::TriggerSlope::Falling ? "FALL"
                                                                                      : "BOTH";
    if (ui::key("##slope", slope_label, ImVec2(x, y), ImVec2(kw, key_h), true, t.led_warn, s)) {
        engine_.settings.slope = engine_.settings.slope == core::TriggerSlope::Rising    ? core::TriggerSlope::Falling
                                 : engine_.settings.slope == core::TriggerSlope::Falling ? core::TriggerSlope::Either
                                                                                         : core::TriggerSlope::Rising;
    }
    if (ui::key("##auto", "AUTO", ImVec2(x + kw + gap, y), ImVec2(kw, key_h),
                engine_.settings.mode == core::TriggerMode::Auto, t.led_run, s)) {
        engine_.settings.mode = core::TriggerMode::Auto;
    }
    if (ui::key("##normal", "NORM", ImVec2(x + 2.0f * (kw + gap), y), ImVec2(kw, key_h),
                engine_.settings.mode == core::TriggerMode::Normal, t.led_run, s)) {
        engine_.settings.mode = core::TriggerMode::Normal;
    }
    // Level knob and readout, live only with an analog source.
    ImVec2 lk(x + keys_w + gap + lr, y + key_h * 0.5f);
    int lsteps = ui::knob("##level", lk, lr, nullptr, s, &pressed);
    if (analog_trigger) {
        float volts_div = core::volts_per_div_steps()[static_cast<size_t>(trig.volts_step)];
        if (lsteps != 0) {
            trigger_level_ += static_cast<float>(lsteps) * volts_div * 0.1f;
        }
        if (pressed) {
            trigger_level_ = 0.0f;
        }
        char lv[24];
        core::format_volts(lv, sizeof(lv), trigger_level_);
        ui::readout(ImVec2(x + inner_w - rw, y), ImVec2(x + inner_w, y + key_h), lv,
                    ui::channel_colour(engine_.settings.trigger_channel), s);
    } else {
        ui::readout(ImVec2(x + inner_w - rw, y), ImVec2(x + inner_w, y + key_h), "level", t.readout_dim, s);
    }
    y += key_h + 6.0f * s;
    ui::group_frame(ImVec2(min.x, group_top), ImVec2(max.x, y), "TRIGGER", s);
    next_group(y);

    // ---- CHANNELS: a key per channel with its jack beside it ----------------
    group_top = y - title_h;
    float jack_r = 7.0f * s;
    float cell = (inner_w - 3.0f * gap) / 4.0f;
    kw = cell - jack_r * 2.0f - 4.0f * s;
    for (int c = 0; c < channels; c++) {
        const Channel &ch = ch_[static_cast<size_t>(c)];
        float cx = x + static_cast<float>(c) * (cell + gap);
        char id[16];
        char label[16];
        std::snprintf(id, sizeof(id), "##ch%d", c);
        const core::PortInfo *info = app_.port_info(ch.port);
        if (ch.port >= 0 && info) {
            std::string short_name = info->name.rfind("GPIO ", 0) == 0 ? "G" + std::to_string(info->index) : info->name;
            std::snprintf(label, sizeof(label), "%d:%s", c + 1, short_name.c_str());
        } else {
            std::snprintf(label, sizeof(label), "CH%d", c + 1);
        }
        bool lit = ch.port >= 0 || app_.channel_offered(Instrument::Scope, c);
        if (ui::key(id, label, ImVec2(cx, y), ImVec2(kw, key_h), lit, ui::channel_colour(c), s)) {
            if (ch.port >= 0 && selected_ == c && app_.selected_port() < 0) {
                app_.unwire(Instrument::Scope, c);
            } else if (ch.port >= 0 && app_.selected_port() < 0) {
                selected_ = c;
            } else {
                app_.offer_channel(Instrument::Scope, c);
                selected_ = c;
            }
        }
        if (selected_ == c) {
            draw->AddRect(ImVec2(cx - 2.0f * s, y - 2.0f * s), ImVec2(cx + kw + 2.0f * s, y + key_h + 2.0f * s),
                          ui::channel_colour(c), 4.0f * s, 0, 1.5f * s);
        }
        ImVec2 jack_c(cx + kw + 4.0f * s + jack_r, y + key_h * 0.5f);
        ui::JackLook look;
        look.name = "";
        look.level = -1;
        look.active = false;
        look.input = true;
        look.output = false;
        look.wire_colour = ch.port >= 0 ? ui::channel_colour(c) : 0;
        char jid[16];
        std::snprintf(jid, sizeof(jid), "##chjack%d", c);
        if (ui::jack(jid, jack_c, jack_r, look, s)) {
            app_.offer_channel(Instrument::Scope, c);
            selected_ = c;
        }
        if (app_.channel_offered(Instrument::Scope, c)) {
            draw->AddCircle(jack_c, jack_r + 3.0f * s, t.led_warn, 20, 1.5f * s);
        }
        app_.anchor_channel(Instrument::Scope, c, window, jack_c.x, jack_c.y);
    }
    y += key_h + gap;
    kw = (inner_w - gap) / 2.0f;
    if (ui::key("##cursors", "CURSORS", ImVec2(x, y), ImVec2(kw, key_h), cursors_, t.led_warn, s)) {
        cursors_ = !cursors_;
    }
    if (ui::key("##hide", selected.visible ? "SHOWN" : "HIDDEN", ImVec2(x + kw + gap, y), ImVec2(kw, key_h),
                !selected.visible, t.led_stop, s)) {
        selected.visible = !selected.visible;
    }
    y += key_h + 6.0f * s;
    ui::group_frame(ImVec2(min.x, group_top), ImVec2(max.x, y), "CHANNELS", s);
    next_group(y);

    // ---- MATH: M1 M2 + formula, then the inputs A..D ------------------------
    group_top = y - title_h;
    static int math_shown = 0;   // which math channel the combo edits
    float mw = 44.0f * s;
    for (int m = 0; m < math_channels; m++) {
        Channel &mc = ch_[math_slot(m)];
        char id[16];
        char label[8];
        std::snprintf(id, sizeof(id), "##math%d", m);
        std::snprintf(label, sizeof(label), "M%d", m + 1);
        float kx = x + static_cast<float>(m) * (mw + gap);
        if (ui::key(id, label, ImVec2(kx, y), ImVec2(mw, key_h), mc.enabled, ui::channel_colour(channels + m), s)) {
            if (math_shown == m && mc.enabled) {
                mc.enabled = false;
            } else {
                mc.enabled = true;
                math_shown = m;
                selected_ = channels + m;
            }
        }
        if (math_shown == m) {
            draw->AddRect(ImVec2(kx - 2.0f * s, y - 2.0f * s), ImVec2(kx + mw + 2.0f * s, y + key_h + 2.0f * s),
                          ui::channel_colour(channels + m), 4.0f * s, 0, 1.5f * s);
        }
    }
    float combo_x = x + 2.0f * (mw + gap);
    draw_formula_combo(math_shown, ImVec2(combo_x, y), ImVec2(x + inner_w - combo_x, key_h), s);
    y += key_h + gap;
    {
        Channel &mc = ch_[math_slot(math_shown)];
        const core::Formula &f = core::formulas()[static_cast<size_t>(mc.formula)];
        float iw = (inner_w - 3.0f * gap) / 4.0f;
        const char *names[core::formula_inputs_max] = {"A", "B", "C", "D"};
        for (int k = 0; k < core::formula_inputs_max; k++) {
            char id[16];
            char label[16];
            std::snprintf(id, sizeof(id), "##in%d", k);
            int src = mc.input[k];
            if (src >= channels) {
                std::snprintf(label, sizeof(label), "%s:M%d", names[k], src - channels + 1);
            } else {
                std::snprintf(label, sizeof(label), "%s:CH%d", names[k], src + 1);
            }
            bool used = k < f.inputs;
            if (ui::key(id, label, ImVec2(x + static_cast<float>(k) * (iw + gap), y), ImVec2(iw, key_h), used,
                        ui::channel_colour(src), s, used)) {
                // Cycle through the inputs and the other math channel.
                int next = src;
                do {
                    next = (next + 1) % all_channels;
                } while (next == channels + math_shown);
                mc.input[k] = next;
            }
        }
    }
    y += key_h + 6.0f * s;
    ui::group_frame(ImVec2(min.x, group_top), ImVec2(max.x, y), "MATH", s);

    // Keys legend at the bottom.
    ImFont *small = ui::fonts().small;
    draw->AddText(small, small->FontSize, ImVec2(x, max.y - small->FontSize - 2.0f * s), t.label_dim,
                  "space run/stop   S single   C cursors   arrows pan   + - scale");
}

}  // namespace app