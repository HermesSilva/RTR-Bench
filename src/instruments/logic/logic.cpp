// SPDX-License-Identifier: Apache-2.0
#include "instruments/logic/logic.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "app/app.h"
#include "instruments/common/panel.h"
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

constexpr float controls_w = 230.0f;
constexpr float lane_h = 34.0f;

}  // namespace

LogicAnalyzer::LogicAnalyzer(App &app) : app_(app)
{
    for (int i = 0; i < 4; i++) {
        add_channel();
    }
    engine_.settings.time_step = 15;   // 1 ms/div
}

void LogicAnalyzer::add_channel()
{
    if (static_cast<int>(ch_.size()) >= max_channels) {
        return;
    }
    ch_.push_back(std::make_unique<Channel>());
    refresh_wiring();
}

void LogicAnalyzer::remove_channel(int index)
{
    if (ch_.size() <= 1 || index < 0 || static_cast<size_t>(index) >= ch_.size()) {
        return;
    }
    app_.unwire(this->id(), index);
    for (int c = index + 1; c < static_cast<int>(ch_.size()); c++) {
        int port = app_.wired_port(this->id(), c);
        app_.unwire(this->id(), c);
        if (port >= 0) {
            app_.rewire(this->id(), c - 1, port);
        }
    }
    ch_.erase(ch_.begin() + index);
    if (engine_.settings.trigger_channel >= static_cast<int>(ch_.size())) {
        engine_.settings.trigger_channel = 0;
    }
    refresh_wiring();
}

void LogicAnalyzer::refresh_wiring()
{
    for (size_t i = 0; i < ch_.size(); i++) {
        Channel &c = *ch_[i];
        int port = app_.wired_port(this->id(), static_cast<int>(i));
        if (port != c.port) {
            c.port = port;
            c.trace.clear();
        }
    }
}

void LogicAnalyzer::wiring_changed()
{
    refresh_wiring();
    engine_.clear();
}

void LogicAnalyzer::feed(const std::vector<core::DigitalEvent> &events)
{
    for (const core::DigitalEvent &e : events) {
        for (auto &cp : ch_) {
            Channel &c = *cp;
            if (c.port == e.port) {
                if (e.kind == core::DigitalEvent::Snapshot) {
                    c.trace.snapshot(e.ns, e.level);
                } else {
                    c.trace.add(e.ns, e.level);
                }
            }
        }
        latest_ns_ = std::max(latest_ns_, e.ns);
    }
}

void LogicAnalyzer::fit_window(ui::Window &window)
{
    int width = 0;
    int height = 0;
    window.size(width, height);
    float lanes = static_cast<float>(ch_.size()) + (bus_ ? 1.0f : 0.0f);
    int wanted = static_cast<int>(std::max(300.0f, 120.0f + lane_h * lanes + 60.0f));
    int rows = (static_cast<int>(ch_.size()) + 3) / 4;
    int controls = static_cast<int>(100.0f + 48.0f * static_cast<float>(rows) + 180.0f);
    wanted = std::max(wanted, controls);
    if (height != wanted) {
        window.set_size(width, wanted);
    }
}

void LogicAnalyzer::handle_keys()
{
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        app_.cancel_wiring();
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

void LogicAnalyzer::draw(ui::Window &window)
{
    ui::ChassisSpec chassis;
    chassis.model = "LA-1";
    chassis.title = std::string("Logic Analyzer") + title_suffix();
    fit_window(window);
    ui::ChassisFrame frame = ui::begin_chassis(window, chassis);
    const float s = window.scale();
    handle_keys();

    const Channel &source = *ch_[static_cast<size_t>(std::clamp(engine_.settings.trigger_channel, 0, static_cast<int>(ch_.size()) - 1))];
    engine_.update(source.port >= 0 ? &source.trace : nullptr, latest_ns_);

    ImVec2 screen_max(frame.panel_max.x - controls_w * s - 10.0f * s, frame.panel_max.y);
    draw_screen(window, frame.panel_min, screen_max);
    draw_controls(window, ImVec2(screen_max.x + 10.0f * s, frame.panel_min.y), frame.panel_max);
    ui::end_chassis();
}

void LogicAnalyzer::draw_screen(ui::Window &window, ImVec2 min, ImVec2 max)
{
    const ui::Theme &t = ui::current_theme();
    const float s = window.scale();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const core::ScopeView &view = engine_.view();
    const float line_h = ui::fonts().mono->FontSize + 4.0f * s;
    ImVec2 inner_min;
    ImVec2 inner_max;
    panel::screen(draw, min, max, s, inner_min, inner_max);
    const float label_w = 52.0f * s;
    screen_min_ = ImVec2(inner_min.x + label_w, inner_min.y + line_h + 6.0f * s);
    screen_max_ = ImVec2(inner_max.x - 6.0f * s, inner_max.y - line_h - 6.0f * s);
    const float w = screen_max_.x - screen_min_.x;
    const float h = screen_max_.y - screen_min_.y;

    // Vertical graticule only: lanes have their own baselines.
    for (int i = 0; i <= core::scope_divisions; i++) {
        float x = screen_min_.x + w * static_cast<float>(i) / core::scope_divisions;
        draw->AddLine(ImVec2(x, screen_min_.y), ImVec2(x, screen_max_.y),
                      i == core::scope_divisions / 2 ? t.graticule_axis : t.graticule, 1.0f);
    }

    // Status bar.
    char text[160];
    char per_div[32];
    core::format_duration(per_div, sizeof(per_div), engine_.ns_per_div());
    float x = inner_min.x + 8.0f * s;
    float y = inner_min.y + 4.0f * s;
    std::snprintf(text, sizeof(text), "H %s/div", per_div);
    panel::mono_text(draw, ImVec2(x, y), t.readout, text);
    x += panel::mono_width(text) + 18.0f * s;
    const char *mode = engine_.settings.mode == core::TriggerMode::Auto ? "AUTO" : "NORM";
    const char *slope = engine_.settings.slope == core::TriggerSlope::Rising    ? "rise"
                        : engine_.settings.slope == core::TriggerSlope::Falling ? "fall"
                                                                                : "both";
    std::snprintf(text, sizeof(text), "T CH%d %s %s", engine_.settings.trigger_channel + 1, slope, mode);
    panel::mono_text(draw, ImVec2(x, y), ui::channel_colour(engine_.settings.trigger_channel % ui::channel_count), text);
    x += panel::mono_width(text) + 18.0f * s;
    const char *state = !engine_.running() ? "STOP" : (view.triggered ? "TRIG'D" : "AUTO");
    panel::mono_text(draw, ImVec2(x, y), !engine_.running() ? t.led_stop : (view.triggered ? t.led_run : t.led_warn), state);
    if (latest_ns_ >= 0) {
        std::snprintf(text, sizeof(text), "%s%.6f s", app_.probe().capabilities().virtual_time ? "virtual " : "",
                      static_cast<double>(latest_ns_) / 1e9);
        panel::mono_text(draw, ImVec2(inner_max.x - 8.0f * s - panel::mono_width(text), y), t.readout_dim, text);
    }

    // Lanes: every channel, then the bus.
    int lanes = static_cast<int>(ch_.size()) + (bus_ ? 1 : 0);
    float lh = std::min(lane_h * s, h / static_cast<float>(std::max(lanes, 1)));
    draw->PushClipRect(ImVec2(inner_min.x, screen_min_.y), screen_max_, true);
    for (int c = 0; c < static_cast<int>(ch_.size()); c++) {
        const Channel &ch = *ch_[static_cast<size_t>(c)];
        float top = screen_min_.y + lh * static_cast<float>(c);
        uint32_t colour = ui::channel_colour(c % ui::channel_count);
        draw->AddLine(ImVec2(inner_min.x, top + lh), ImVec2(screen_max_.x, top + lh), t.graticule, 1.0f);
        std::string name = std::to_string(c + 1) + " " + panel::short_port_name(app_.port_info(ch.port));
        panel::mono_text(draw, ImVec2(inner_min.x + 6.0f * s, top + (lh - ui::fonts().mono->FontSize) * 0.5f),
                         ch.port >= 0 ? colour : t.readout_dim, name.c_str());
        if (ch.port >= 0 && view.valid) {
            panel::digital_lane(draw, ch.trace, view.t0, view.t1, screen_min_.x, screen_max_.x, top + lh * 0.22f,
                                top + lh * 0.82f, colour, s);
        }
    }
    if (bus_ && view.valid) {
        // Bus lane: hexagons between the instants where any channel changes,
        // each with the value of the wired channels at that time.
        float top = screen_min_.y + lh * static_cast<float>(ch_.size());
        float mid = top + lh * 0.52f;
        float half = lh * 0.3f;
        panel::mono_text(draw, ImVec2(inner_min.x + 6.0f * s, top + (lh - ui::fonts().mono->FontSize) * 0.5f), t.readout, "BUS");
        std::vector<int64_t> changes;
        for (const auto &cp : ch_) {
            if (cp->port < 0) {
                continue;
            }
            size_t i = cp->trace.lower_bound(view.t0 + 1);
            size_t end = cp->trace.lower_bound(view.t1 + 1);
            for (; i < end; i++) {
                changes.push_back(cp->trace.at(i).ns);
            }
        }
        std::sort(changes.begin(), changes.end());
        changes.erase(std::unique(changes.begin(), changes.end()), changes.end());
        changes.insert(changes.begin(), view.t0);
        changes.push_back(view.t1);
        int64_t span = view.t1 - view.t0;
        int merged = 0;
        for (size_t k = 0; k + 1 < changes.size(); k++) {
            int64_t a = changes[k];
            int64_t b = changes[k + 1];
            float xa = screen_min_.x + static_cast<float>(static_cast<double>(a - view.t0) / static_cast<double>(span)) * w;
            float xb = screen_min_.x + static_cast<float>(static_cast<double>(b - view.t0) / static_cast<double>(span)) * w;
            if (xb - xa < 3.0f * s) {
                merged++;
                continue;   // too narrow: shown as a dense band below
            }
            unsigned value = 0;
            for (size_t c = 0; c < ch_.size(); c++) {
                int level = ch_[c]->port >= 0 ? ch_[c]->trace.level_at(a) : 0;
                if (level > 0) {
                    value |= 1u << c;
                }
            }
            float slant = std::min(4.0f * s, (xb - xa) * 0.5f);
            ImVec2 pts[6] = {ImVec2(xa, mid), ImVec2(xa + slant, mid - half), ImVec2(xb - slant, mid - half),
                             ImVec2(xb, mid), ImVec2(xb - slant, mid + half), ImVec2(xa + slant, mid + half)};
            draw->AddPolyline(pts, 6, t.readout, ImDrawFlags_Closed, 1.3f * s);
            char hex[16];
            std::snprintf(hex, sizeof(hex), "%X", value);
            float tw = panel::mono_width(hex);
            if (tw + 6.0f * s < xb - xa) {
                panel::mono_text(draw, ImVec2((xa + xb) * 0.5f - tw * 0.5f, mid - ui::fonts().mono->FontSize * 0.5f),
                                 t.readout, hex);
            }
        }
        if (merged > 0) {
            draw->AddRectFilled(ImVec2(screen_min_.x, mid + half + 2.0f * s), ImVec2(screen_max_.x, mid + half + 4.0f * s),
                                with_alpha(t.readout, 60));
        }
    }
    // Trigger marker.
    if (view.valid && view.trigger_ns >= 0) {
        float tx = screen_min_.x + static_cast<float>(static_cast<double>(view.trigger_ns - view.t0) /
                                                      static_cast<double>(view.t1 - view.t0)) * w;
        uint32_t tc = ui::channel_colour(engine_.settings.trigger_channel % ui::channel_count);
        draw->AddTriangleFilled(ImVec2(tx - 6.0f * s, screen_min_.y), ImVec2(tx + 6.0f * s, screen_min_.y),
                                ImVec2(tx, screen_min_.y + 8.0f * s), tc);
        draw->AddLine(ImVec2(tx, screen_min_.y), ImVec2(tx, screen_max_.y), with_alpha(tc, 60), 1.0f);
    }
    draw->PopClipRect();

    // Cursors and their readout.
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
            panel::mono_text(draw, ImVec2(cx + 3.0f * s, screen_min_.y + 2.0f * s), cc, names[k]);
        }
        int64_t ta = view.t0 + static_cast<int64_t>(cursor_a_ * static_cast<double>(span));
        int64_t tb = view.t0 + static_cast<int64_t>(cursor_b_ * static_cast<double>(span));
        char dd[32];
        char df[32];
        core::format_duration(dd, sizeof(dd), tb - ta);
        if (tb != ta) {
            core::format_frequency(df, sizeof(df), 1e9 / static_cast<double>(tb > ta ? tb - ta : ta - tb));
        } else {
            std::snprintf(df, sizeof(df), "--");
        }
        std::snprintf(text, sizeof(text), "cursors  dt %s  1/dt %s", dd, df);
        panel::mono_text(draw, ImVec2(inner_min.x + 8.0f * s, screen_max_.y + 4.0f * s), t.readout, text);
    } else {
        panel::mono_text(draw, ImVec2(inner_min.x + 8.0f * s, screen_max_.y + 4.0f * s), t.readout_dim,
                         "space run/stop   S single   C cursors   arrows pan   + - scale");
    }
}

void LogicAnalyzer::draw_controls(ui::Window &window, ImVec2 min, ImVec2 max)
{
    const ui::Theme &t = ui::current_theme();
    const float s = window.scale();
    const float key_h = 21.0f * s;
    const float gap = 5.0f * s;
    const float pad = 8.0f * s;
    const float title_h = 11.0f * s;
    float x = min.x + pad;
    float y = min.y + title_h;
    float inner_w = max.x - min.x - 2.0f * pad;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    (void)max;

    // RUN CONTROL
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
        for (auto &c : ch_) {
            c->trace.clear();
        }
        engine_.clear();
    }
    y += key_h + 6.0f * s;
    ui::group_frame(ImVec2(min.x, group_top), ImVec2(max.x, y), "RUN CONTROL", s);
    y += title_h + 6.0f * s;

    // HORIZONTAL: two knobs and the readout.
    group_top = y - title_h;
    float knob_r = 15.0f * s;
    float ky = y + knob_r + 2.0f * s;
    int max_step = static_cast<int>(core::time_per_div_steps().size()) - 1;
    bool pressed = false;
    int steps = ui::knob("##timediv", ImVec2(x + inner_w * 0.25f, ky), knob_r, "SCALE", s, &pressed);
    if (steps != 0) {
        engine_.settings.time_step = std::clamp(engine_.settings.time_step - steps, 0, max_step);
    }
    steps = ui::knob("##position", ImVec2(x + inner_w * 0.75f, ky), knob_r, "POS", s, &pressed);
    if (steps != 0) {
        engine_.pan(static_cast<int64_t>(steps) * engine_.ns_per_div() / 4);
    }
    if (pressed) {
        engine_.settings.position_ns = 0;
    }
    y = ky + knob_r + 18.0f * s;
    char per_div[32];
    char text[48];
    core::format_duration(per_div, sizeof(per_div), engine_.ns_per_div());
    std::snprintf(text, sizeof(text), "%s/div", per_div);
    ui::readout(ImVec2(x, y), ImVec2(x + inner_w * 0.5f - gap, y + 19.0f * s), text, t.readout, s);
    core::format_duration(per_div, sizeof(per_div), engine_.settings.position_ns);
    ui::readout(ImVec2(x + inner_w * 0.5f, y), ImVec2(x + inner_w, y + 19.0f * s), per_div, t.readout, s);
    y += 19.0f * s + 6.0f * s;
    ui::group_frame(ImVec2(min.x, group_top), ImVec2(max.x, y), "HORIZONTAL", s);
    y += title_h + 6.0f * s;

    // TRIGGER: source cycles, slope, mode.
    group_top = y - title_h;
    kw = (inner_w - 2.0f * gap) / 3.0f;
    std::snprintf(text, sizeof(text), "CH%d", engine_.settings.trigger_channel + 1);
    if (ui::key("##trig", text, ImVec2(x, y), ImVec2(kw, key_h), true,
                ui::channel_colour(engine_.settings.trigger_channel % ui::channel_count), s)) {
        engine_.settings.trigger_channel = (engine_.settings.trigger_channel + 1) % static_cast<int>(ch_.size());
        engine_.clear();
    }
    const char *slope_label = engine_.settings.slope == core::TriggerSlope::Rising    ? "RISE"
                              : engine_.settings.slope == core::TriggerSlope::Falling ? "FALL"
                                                                                      : "BOTH";
    if (ui::key("##slope", slope_label, ImVec2(x + kw + gap, y), ImVec2(kw, key_h), true, t.led_warn, s)) {
        engine_.settings.slope = engine_.settings.slope == core::TriggerSlope::Rising    ? core::TriggerSlope::Falling
                                 : engine_.settings.slope == core::TriggerSlope::Falling ? core::TriggerSlope::Either
                                                                                         : core::TriggerSlope::Rising;
    }
    bool auto_mode = engine_.settings.mode == core::TriggerMode::Auto;
    if (ui::key("##mode", auto_mode ? "AUTO" : "NORM", ImVec2(x + 2.0f * (kw + gap), y), ImVec2(kw, key_h), true,
                t.led_run, s)) {
        engine_.settings.mode = auto_mode ? core::TriggerMode::Normal : core::TriggerMode::Auto;
    }
    y += key_h + 6.0f * s;
    ui::group_frame(ImVec2(min.x, group_top), ImVec2(max.x, y), "TRIGGER", s);
    y += title_h + 6.0f * s;

    // CHANNELS: ADD, BUS, then a jack per channel in rows of four.
    group_top = y - title_h;
    kw = (inner_w - gap) / 2.0f;
    if (ui::key("##add", "ADD CHANNEL", ImVec2(x, y), ImVec2(kw, key_h), false, t.led_run, s,
                static_cast<int>(ch_.size()) < max_channels)) {
        add_channel();
    }
    if (ui::key("##bus", "BUS", ImVec2(x + kw + gap, y), ImVec2(kw, key_h), bus_, t.led_warn, s)) {
        bus_ = !bus_;
    }
    y += key_h + 8.0f * s;
    float cell = inner_w / 4.0f;
    for (int c = 0; c < static_cast<int>(ch_.size()); c++) {
        const Channel &ch = *ch_[static_cast<size_t>(c)];
        int row = c / 4;
        float cx = x + cell * static_cast<float>(c % 4) + cell * 0.5f;
        float cy = y + 30.0f * s * static_cast<float>(row) + 10.0f * s;
        uint32_t colour = ui::channel_colour(c % ui::channel_count);
        ui::JackLook look;
        char name[16];
        std::snprintf(name, sizeof(name), "%d", c + 1);
        look.name = name;
        look.level = -1;
        look.active = false;
        look.input = true;
        look.output = false;
        look.wire_colour = ch.port >= 0 ? colour : 0;
        char id[32];
        std::snprintf(id, sizeof(id), "##jack%d", c);
        ImVec2 jack_c(cx, cy);
        if (ui::jack(id, jack_c, 8.0f * s, look, s)) {
            if (ImGui::GetIO().KeyCtrl) {
                remove_channel(c);
                break;
            }
            app_.offer_channel(this->id(), c);
        }
        if (app_.channel_offered(this->id(), c)) {
            draw->AddCircle(jack_c, 11.0f * s, t.led_warn, 20, 1.5f * s);
        }
        app_.anchor_channel(this->id(), c, window, jack_c.x, jack_c.y);
    }
    int jack_rows = (static_cast<int>(ch_.size()) + 3) / 4;
    y += 30.0f * s * static_cast<float>(jack_rows) + 14.0f * s;
    ImFont *small = ui::fonts().small;
    draw->AddText(small, small->FontSize, ImVec2(x, y), t.label_dim, "ctrl+click a jack removes the channel");
    y += small->FontSize + 6.0f * s;
    ui::group_frame(ImVec2(min.x, group_top), ImVec2(max.x, y), "CHANNELS", s);
}

void LogicAnalyzer::save(nlohmann::json &out) const
{
    out["channels"] = static_cast<int>(ch_.size());
    out["time_step"] = engine_.settings.time_step;
    out["position_ns"] = engine_.settings.position_ns;
    out["trigger_channel"] = engine_.settings.trigger_channel;
    out["trigger_slope"] = static_cast<int>(engine_.settings.slope);
    out["trigger_mode"] = static_cast<int>(engine_.settings.mode);
    out["bus"] = bus_;
    out["cursors"] = cursors_;
}

void LogicAnalyzer::load(const nlohmann::json &in)
{
    int n = std::clamp(in.value("channels", 4), 1, max_channels);
    ch_.clear();
    for (int i = 0; i < n; i++) {
        ch_.push_back(std::make_unique<Channel>());
    }
    int max_step = static_cast<int>(core::time_per_div_steps().size()) - 1;
    engine_.settings.time_step = std::clamp(in.value("time_step", 15), 0, max_step);
    engine_.settings.position_ns = in.value("position_ns", 0LL);
    engine_.settings.trigger_channel = std::clamp(in.value("trigger_channel", 0), 0, n - 1);
    engine_.settings.slope = static_cast<core::TriggerSlope>(std::clamp(in.value("trigger_slope", 0), 0, 2));
    engine_.settings.mode = static_cast<core::TriggerMode>(std::clamp(in.value("trigger_mode", 0), 0, 1));
    bus_ = in.value("bus", true);
    cursors_ = in.value("cursors", false);
    refresh_wiring();
}

}  // namespace app
