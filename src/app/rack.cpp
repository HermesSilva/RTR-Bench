// SPDX-License-Identifier: Apache-2.0
#include "app/rack.h"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "app/app.h"
#include "ui/chassis.h"
#include "ui/fonts.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/window.h"

namespace app {

namespace {

const char *state_text(core::ProbeState state)
{
    switch (state) {
    case core::ProbeState::Connected:
        return "connected";
    case core::ProbeState::Connecting:
        return "reconnecting";
    case core::ProbeState::Disconnected:
        break;
    }
    return "disconnected";
}

// "12.345 678 901 s" style readout of a time in ns.
void format_time(char *out, size_t size, int64_t ns)
{
    if (ns < 0) {
        std::snprintf(out, size, "--.--- --- --- s");
        return;
    }
    int64_t s = ns / 1000000000LL;
    int64_t rem = ns % 1000000000LL;
    std::snprintf(out, size, "%lld.%03lld %03lld %03lld s", static_cast<long long>(s),
                  static_cast<long long>(rem / 1000000), static_cast<long long>((rem / 1000) % 1000),
                  static_cast<long long>(rem % 1000));
}

const char *direction_text(core::PortDirection d)
{
    switch (d) {
    case core::PortDirection::Output:
        return "output";
    case core::PortDirection::Input:
        return "input";
    case core::PortDirection::Alternate:
        return "alternate function";
    case core::PortDirection::Unknown:
        break;
    }
    return "direction unknown";
}

}  // namespace

void Rack::draw(ui::Window &window)
{
    ui::ChassisSpec chassis;
    chassis.model = "RACK-1";
    chassis.title = "Mini Rack   v" RTR_BENCH_VERSION;
    chassis.corner = 12.0f;
    ui::ChassisFrame frame = ui::begin_chassis(window, chassis);

    const ui::Theme &t = ui::current_theme();
    const float s = window.scale();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    core::Probe &probe = app_.probe();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        app_.cancel_wiring();
    }
    const core::ProbeCapabilities &cap = probe.capabilities();
    core::ProbeState state = probe.state();
    core::ProbeStats stats = probe.stats();
    const float key_h = 21.0f * s;
    const float gap = 5.0f * s;
    bool emu = app_.probe_kind() == App::ProbeKind::Emulator;

    // ---- One row of keys: probe, state, instruments, theme, wires ----------
    float x = frame.panel_min.x;
    float y = frame.panel_min.y + 2.0f * s;
    float kw = 50.0f * s;
    if (ui::key("##probe-emu", "EMU", ImVec2(x, y), ImVec2(kw, key_h), emu, t.led_run, s)) {
        app_.switch_probe(App::ProbeKind::Emulator);
    }
    x += kw + gap;
    if (ui::key("##probe-demo", "DEMO", ImVec2(x, y), ImVec2(kw, key_h), !emu, t.led_run, s)) {
        app_.switch_probe(App::ProbeKind::Demo);
    }
    x += kw + gap;
    ui::key("##probe-m2k", "M2K", ImVec2(x, y), ImVec2(kw, key_h), false, t.led_run, s, false);
    x += kw + gap;
    ui::key("##probe-replay", "REPLAY", ImVec2(x, y), ImVec2(kw + 10.0f * s, key_h), false, t.led_run, s, false);
    x += kw + 10.0f * s + 12.0f * s;

    // Probe state between the probe keys and the instrument keys.
    uint32_t state_colour = state == core::ProbeState::Connected   ? t.led_run
                            : state == core::ProbeState::Connecting ? t.led_warn
                                                                    : t.led_stop;
    ui::led(ImVec2(x + 5.0f * s, y + key_h * 0.5f), 4.0f * s, state_colour, state != core::ProbeState::Disconnected, s);
    char line[160];
    auto *emu_probe = dynamic_cast<probes::EmulatorProbe *>(&probe);
    if (state == core::ProbeState::Connected) {
        char clock[48];
        format_time(clock, sizeof(clock), stats.last_ns);
        std::snprintf(line, sizeof(line), "%s  %.0f ev/s  %s%s", emu_probe ? emu_probe->address().c_str() : cap.name.c_str(),
                      app_.events_per_second(), clock, stats.dropped > 0 ? "  dropped" : "");
    } else {
        std::snprintf(line, sizeof(line), "%s  %s", state_text(state),
                      !stats.error.empty() ? stats.error.c_str() : cap.target.c_str());
    }
    ImFont *small = ui::fonts().small;

    // Instrument keys and theme keys from the right end.
    struct Entry {
        const char *id;
        const char *name;
        Instrument kind;
    };
    const Entry entries[] = {{"##scope", "SCOPE", Instrument::Scope},  {"##logic", "LOGIC", Instrument::Logic},
                             {"##gen", "GEN", Instrument::Generator},  {"##psu", "PSU", Instrument::Supply},
                             {"##dmm", "DMM", Instrument::Multimeter}};
    const ui::ThemeKind kinds[] = {ui::ThemeKind::Light, ui::ThemeKind::Dark, ui::ThemeKind::Amber};
    const char *names[] = {"LIGHT", "DARK", "AMBER"};
    const char *ids[] = {"##theme-light", "##theme-dark", "##theme-amber"};
    const float iw = 50.0f * s;
    float rx = frame.panel_max.x - iw;
    if (ui::key("##wires", "WIRES", ImVec2(rx, y), ImVec2(iw, key_h), app_.wires_shown(), t.led_run, s,
                app_.wires_across_desktop())) {
        app_.show_wires(!app_.wires_shown());
    }
    rx -= 3.0f * (iw + gap) + 8.0f * s;
    for (int i = 0; i < 3; i++) {
        if (ui::key(ids[i], names[i], ImVec2(rx + static_cast<float>(i) * (iw + gap), y), ImVec2(iw, key_h),
                    t.kind == kinds[i], t.led_warn, s)) {
            app_.set_theme(kinds[i]);
        }
    }
    rx -= 5.0f * (iw + gap) + 8.0f * s;
    for (int i = 0; i < 5; i++) {
        if (ui::key(entries[i].id, entries[i].name, ImVec2(rx + static_cast<float>(i) * (iw + gap), y), ImVec2(iw, key_h),
                    app_.instrument_open(entries[i].kind), t.led_run, s)) {
            // ctrl+click makes one more instance of the instrument.
            app_.open_instrument(entries[i].kind, ImGui::GetIO().KeyCtrl);
        }
    }
    y += key_h + 14.0f * s;

    // ---- PORTS: the whole width, up to 16 jacks per row ---------------------
    ImVec2 ports_min(frame.panel_min.x, y);
    ImVec2 ports_max(frame.panel_max.x, frame.panel_max.y);
    ui::group_frame(ports_min, ports_max, emu ? "PORTS  GPIO (BCM)  ground is automatic" : "PORTS  ground is automatic", s);
    // The probe state on the title line of the ports group, at the right.
    {
        float tw = small->CalcTextSizeA(small->FontSize, 1e9f, 0.0f, line).x;
        ImVec2 tp(ports_max.x - 10.0f * s - tw, ports_min.y - small->FontSize * 0.5f);
        draw->AddRectFilled(ImVec2(tp.x - 4.0f * s, tp.y), ImVec2(tp.x + tw + 4.0f * s, tp.y + small->FontSize), t.chassis);
        draw->AddText(small, small->FontSize, tp, stats.dropped > 0 ? t.led_stop : t.label_dim, line);
    }
    const std::vector<core::PortInfo> &ports = probe.ports();
    const core::PortState &state_of = app_.ports();
    const int per_row = 16;
    const int rows = std::max(1, (static_cast<int>(ports.size()) + per_row - 1) / per_row);
    const float radius = 10.0f * s;
    float cell_w = (ports_max.x - ports_min.x - 12.0f * s) / per_row;
    float row_h = (ports_max.y - ports_min.y - 10.0f * s) / static_cast<float>(std::max(rows, 2));
    for (size_t i = 0; i < ports.size() && i < state_of.size(); i++) {
        int row = static_cast<int>(i) / per_row;
        int col = static_cast<int>(i) % per_row;
        ImVec2 centre(ports_min.x + 6.0f * s + cell_w * (static_cast<float>(col) + 0.5f),
                      ports_min.y + 10.0f * s + row_h * static_cast<float>(row) + radius + 4.0f * s);
        const core::PortStatus &ps = state_of.at(i);
        ui::JackLook look;
        char name[16];
        if (emu) {
            std::snprintf(name, sizeof(name), "%d", ports[i].index);
        } else {
            std::snprintf(name, sizeof(name), "%s", ports[i].name.c_str());
        }
        look.name = name;
        look.level = ports[i].analog ? -1 : ps.level;
        look.active = ps.recent > 0;
        look.input = ps.direction == core::PortDirection::Input || ports[i].drivable;
        look.output = ps.direction == core::PortDirection::Output;
        look.wire_colour = app_.port_wire_colour(static_cast<int>(i));
        char id[40];
        std::snprintf(id, sizeof(id), "##jack%u", static_cast<unsigned>(i));
        if (ui::jack(id, centre, radius, look, s)) {
            app_.select_port(static_cast<int>(i));
        }
        app_.anchor_port(static_cast<int>(i), window, centre.x, centre.y);
        if (app_.selected_port() == static_cast<int>(i)) {
            draw->AddCircle(centre, radius + 3.0f * s, t.led_warn, 24, 2.0f * s);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s  pin %s", ports[i].name.c_str(), ports[i].pin.c_str());
            ImGui::Text("%s, %llu transitions", direction_text(ps.direction),
                        static_cast<unsigned long long>(ps.transitions));
            InstrumentId wired{Instrument::Scope, 0};
            int channel = 0;
            if (app_.port_wired_to(static_cast<int>(i), wired, channel)) {
                const InstrumentBase *target = app_.instrument(wired);
                std::string ch = target ? target->channel_name(channel) : "CH" + std::to_string(channel + 1);
                ImGui::Text("wired to %s %s", instrument_label(wired).c_str(), ch.c_str());
            } else if (app_.selected_port() == static_cast<int>(i)) {
                ImGui::Text("selected: click a jack on an instrument");
            }
            ImGui::EndTooltip();
        }
    }
    app_.grab_near(window);
    ui::end_chassis();
}

}  // namespace app
