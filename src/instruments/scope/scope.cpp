// SPDX-License-Identifier: Apache-2.0
#include "instruments/scope/scope.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>

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

Scope::Scope(App &app) : app_(app)
{
    refresh_wiring();
}

void Scope::refresh_wiring()
{
    for (int i = 0; i < channels; i++) {
        int port = app_.wired_port(Instrument::Scope, i);
        if (port != ch_[static_cast<size_t>(i)].port) {
            ch_[static_cast<size_t>(i)].port = port;
            ch_[static_cast<size_t>(i)].trace.clear();
        }
    }
}

void Scope::wiring_changed()
{
    refresh_wiring();
    engine_.clear();
}

void Scope::feed(const std::vector<core::DigitalEvent> &events)
{
    for (const core::DigitalEvent &e : events) {
        for (Channel &c : ch_) {
            if (c.port == e.port) {
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
    chassis.model = "DSO-1";
    chassis.title = "Digital Oscilloscope";
    ui::ChassisFrame frame = ui::begin_chassis(window, chassis);
    const float s = window.scale();

    handle_keys();

    // The trigger source feeds the engine; the view it decides is drawn.
    const Channel &source = ch_[static_cast<size_t>(engine_.settings.trigger_channel)];
    engine_.update(source.port >= 0 ? &source.trace : nullptr, latest_ns_);

    float controls_w = 300.0f * s;
    ImVec2 screen_min = frame.panel_min;
    ImVec2 screen_max(frame.panel_max.x - controls_w - 10.0f * s, frame.panel_max.y);
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
    const float lane_h = h / channels;
    if (view.valid) {
        int64_t span = view.t1 - view.t0;
        for (int c = 0; c < channels; c++) {
            const Channel &ch = ch_[static_cast<size_t>(c)];
            if (ch.port < 0 || !ch.visible) {
                continue;
            }
            uint32_t colour = ui::channel_colour(c);
            float lane_top = screen_min_.y + lane_h * static_cast<float>(c) + lane_h * 0.18f;
            float lane_bottom = screen_min_.y + lane_h * static_cast<float>(c + 1) - lane_h * 0.18f;
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
                continue;
            }
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
            // Channel tag at the left of the lane.
            std::snprintf(text, sizeof(text), "%d", c + 1);
            draw->AddRectFilled(ImVec2(screen_min_.x + 2.0f * s, lane_top - 2.0f * s),
                                ImVec2(screen_min_.x + 16.0f * s, lane_top + 14.0f * s), colour, 2.0f * s);
            mono_text(draw, ImVec2(screen_min_.x + 5.0f * s, lane_top - 1.0f * s), t.screen, text);
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
    for (int c = 0; c < channels; c++) {
        const Channel &ch = ch_[static_cast<size_t>(c)];
        uint32_t colour = ch.port >= 0 ? ui::channel_colour(c) : t.readout_dim;
        if (ch.port >= 0) {
            std::snprintf(text, sizeof(text), "%d GPIO %d", c + 1, ch.port);
        } else {
            std::snprintf(text, sizeof(text), "%d --", c + 1);
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
    if (sel.port >= 0 && view.valid) {
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
            std::snprintf(text, sizeof(text), "CH%d  f %s  T %s  +w %s  -w %s  duty %.1f %%  edges %d", selected_ + 1,
                          f, p, hi, lo, m.duty * 100.0, m.rising + m.falling);
        } else {
            std::snprintf(text, sizeof(text), "CH%d  no full period on screen  edges %d", selected_ + 1,
                          m.rising + m.falling);
        }
        mono_text(draw, ImVec2(inner_min.x + 8.0f * s, ry + line_h), ui::channel_colour(selected_), text);
    } else if (sel.port < 0) {
        mono_text(draw, ImVec2(inner_min.x + 8.0f * s, ry + line_h), t.readout_dim,
                  "wire a port: press a CH key, then click a jack on the rack");
    }
}

void Scope::draw_controls(ui::Window &window, ImVec2 min, ImVec2 max)
{
    const ui::Theme &t = ui::current_theme();
    const float s = window.scale();
    const float key_h = 26.0f * s;
    const float gap = 6.0f * s;
    float x = min.x + 10.0f * s;
    float y = min.y + 12.0f * s;
    float inner_w = max.x - min.x - 20.0f * s;

    // RUN control.
    ui::group_frame(ImVec2(min.x, min.y), ImVec2(max.x, y + key_h + 10.0f * s), "RUN CONTROL", s);
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
        }
        engine_.clear();
    }
    y += key_h + 32.0f * s;

    // HORIZONTAL: time/div and position knobs with their readouts.
    float group_top = y - 12.0f * s;
    float knob_r = 22.0f * s;
    ImVec2 k1(x + inner_w * 0.25f, y + knob_r + 4.0f * s);
    ImVec2 k2(x + inner_w * 0.75f, y + knob_r + 4.0f * s);
    int max_step = static_cast<int>(core::time_per_div_steps().size()) - 1;
    bool pressed = false;
    int steps = ui::knob("##timediv", k1, knob_r, "SCALE", s, &pressed);
    if (steps != 0) {
        engine_.settings.time_step = std::clamp(engine_.settings.time_step - steps, 0, max_step);
    }
    steps = ui::knob("##position", k2, knob_r, "POSITION", s, &pressed);
    if (steps != 0) {
        engine_.pan(static_cast<int64_t>(steps) * engine_.ns_per_div() / 4);
    }
    if (pressed) {
        engine_.settings.position_ns = 0;
    }
    y += knob_r * 2.0f + 26.0f * s;
    char per_div[32];
    core::format_duration(per_div, sizeof(per_div), engine_.ns_per_div());
    char text[48];
    std::snprintf(text, sizeof(text), "%s/div", per_div);
    ui::readout(ImVec2(x, y), ImVec2(x + inner_w * 0.5f - gap, y + 22.0f * s), text, t.readout, s);
    core::format_duration(per_div, sizeof(per_div), engine_.settings.position_ns);
    ui::readout(ImVec2(x + inner_w * 0.5f, y), ImVec2(x + inner_w, y + 22.0f * s), per_div, t.readout, s);
    y += 22.0f * s + 10.0f * s;
    ui::group_frame(ImVec2(min.x, group_top), ImVec2(max.x, y), "HORIZONTAL", s);
    y += 22.0f * s;

    // TRIGGER: source, slope, mode.
    group_top = y - 12.0f * s;
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
    kw = (inner_w - 2.0f * gap) / 3.0f;
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
    if (ui::key("##normal", "NORMAL", ImVec2(x + 2.0f * (kw + gap), y), ImVec2(kw, key_h),
                engine_.settings.mode == core::TriggerMode::Normal, t.led_run, s)) {
        engine_.settings.mode = core::TriggerMode::Normal;
    }
    y += key_h + 10.0f * s;
    ui::group_frame(ImVec2(min.x, group_top), ImVec2(max.x, y), "TRIGGER", s);
    y += 22.0f * s;

    // CHANNELS: a key per channel; press to wire (with the rack), press a
    // wired one to select it for the measurements; second press unwires.
    group_top = y - 12.0f * s;
    kw = (inner_w - 3.0f * gap) / 4.0f;
    for (int c = 0; c < channels; c++) {
        const Channel &ch = ch_[static_cast<size_t>(c)];
        char id[16];
        char label[16];
        std::snprintf(id, sizeof(id), "##ch%d", c);
        if (ch.port >= 0) {
            std::snprintf(label, sizeof(label), "%d:G%d", c + 1, ch.port);
        } else {
            std::snprintf(label, sizeof(label), "CH%d", c + 1);
        }
        bool lit = ch.port >= 0 || app_.channel_offered(Instrument::Scope, c);
        if (ui::key(id, label, ImVec2(x + static_cast<float>(c) * (kw + gap), y), ImVec2(kw, key_h), lit,
                    ui::channel_colour(c), s)) {
            if (ch.port >= 0 && selected_ == c && app_.selected_port() < 0) {
                app_.unwire(Instrument::Scope, c);
            } else if (ch.port >= 0 && app_.selected_port() < 0) {
                selected_ = c;
            } else {
                app_.offer_channel(Instrument::Scope, c);
                selected_ = c;
            }
        }
        if (app_.channel_offered(Instrument::Scope, c)) {
            ImGui::GetWindowDrawList()->AddRect(ImVec2(x + static_cast<float>(c) * (kw + gap) - 2.0f * s, y - 2.0f * s),
                                                ImVec2(x + static_cast<float>(c) * (kw + gap) + kw + 2.0f * s, y + key_h + 2.0f * s),
                                                t.led_warn, 4.0f * s, 0, 1.5f * s);
        }
    }
    y += key_h + gap;
    kw = (inner_w - gap) / 2.0f;
    if (ui::key("##cursors", "CURSORS", ImVec2(x, y), ImVec2(kw, key_h), cursors_, t.led_warn, s)) {
        cursors_ = !cursors_;
    }
    if (ui::key("##hide", ch_[static_cast<size_t>(selected_)].visible ? "SHOWN" : "HIDDEN", ImVec2(x + kw + gap, y),
                ImVec2(kw, key_h), !ch_[static_cast<size_t>(selected_)].visible, t.led_stop, s)) {
        ch_[static_cast<size_t>(selected_)].visible = !ch_[static_cast<size_t>(selected_)].visible;
    }
    y += key_h + 10.0f * s;
    ui::group_frame(ImVec2(min.x, group_top), ImVec2(max.x, y), "CHANNELS", s);

    // Keys legend at the bottom.
    ImFont *small = ui::fonts().small;
    ImGui::GetWindowDrawList()->AddText(small, small->FontSize, ImVec2(x, max.y - small->FontSize - 2.0f * s),
                                        t.label_dim, "space run/stop   S single   C cursors   arrows pan   + - scale");
}

}  // namespace app
