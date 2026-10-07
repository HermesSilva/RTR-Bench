// SPDX-License-Identifier: Apache-2.0
#include "instruments/circuit/circuit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "app/app.h"
#include "app/settings.h"
#include "instruments/common/panel.h"
#include "sim/ngspice.h"
#include "ui/chassis.h"
#include "ui/fonts.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/window.h"

namespace app {

namespace {

constexpr float base_pitch = 12.0f;   // logical pixels per grid unit at zoom 1

uint32_t mix(uint32_t a, uint32_t b, float f)
{
    uint32_t out = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        float ca = static_cast<float>((a >> shift) & 0xFFu);
        float cb = static_cast<float>((b >> shift) & 0xFFu);
        out |= (static_cast<uint32_t>(ca + (cb - ca) * f) & 0xFFu) << shift;
    }
    return out;
}

uint32_t with_alpha(uint32_t colour, uint32_t alpha)
{
    return (colour & 0x00FFFFFFu) | (alpha << 24u);
}

// A node by its voltage: dim at zero, towards green when positive and
// towards red when negative, full at 5 V.
uint32_t volts_colour(double volts)
{
    const ui::Theme &t = ui::current_theme();
    float f = std::clamp(static_cast<float>(std::fabs(volts)) / 5.0f, 0.0f, 1.0f);
    return mix(t.readout_dim, volts >= 0.0 ? t.led_run : t.led_stop, f);
}

void rotate_local(float &x, float &y, int rotation)
{
    float rx = x;
    float ry = y;
    switch (rotation & 3) {
    case 1:
        rx = -y;
        ry = x;
        break;
    case 2:
        rx = -x;
        ry = -y;
        break;
    case 3:
        rx = y;
        ry = -x;
        break;
    default:
        break;
    }
    x = rx;
    y = ry;
}

// The grid on the screen.
struct View {
    ImVec2 origin;
    float pitch = base_pitch;
    ImVec2 at(float gx, float gy) const { return ImVec2(origin.x + gx * pitch, origin.y + gy * pitch); }
    ImVec2 at(core::GridPoint p) const { return at(static_cast<float>(p.x), static_cast<float>(p.y)); }
};

// Draws a part: coordinates are grid units around the part, before its rotation.
struct Pen {
    ImDrawList *draw;
    View view;
    const core::Part *part;
    uint32_t colour;
    float width;

    ImVec2 at(float lx, float ly) const
    {
        rotate_local(lx, ly, part->rotation);
        return view.at(static_cast<float>(part->x) + lx, static_cast<float>(part->y) + ly);
    }
    void line(float x0, float y0, float x1, float y1) const { draw->AddLine(at(x0, y0), at(x1, y1), colour, width); }
    void poly(const float (*points)[2], int count) const
    {
        for (int i = 0; i + 1 < count; i++) {
            line(points[i][0], points[i][1], points[i + 1][0], points[i + 1][1]);
        }
    }
    void circle(float x, float y, float radius) const { draw->AddCircle(at(x, y), radius * view.pitch, colour, 32, width); }
    // An arrow head with its tip at (x, y), pointing along (dx, dy).
    void head(float x, float y, float dx, float dy, float size) const
    {
        float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.0f) {
            return;
        }
        dx /= length;
        dy /= length;
        float bx = x - dx * size;
        float by = y - dy * size;
        draw->AddTriangleFilled(at(x, y), at(bx - dy * size * 0.45f, by + dx * size * 0.45f),
                                at(bx + dy * size * 0.45f, by - dx * size * 0.45f), colour);
    }
};

const float zigzag[][2] = {{-1.2f, 0.0f}, {-1.0f, -0.4f}, {-0.6f, 0.4f}, {-0.2f, -0.4f},
                           {0.2f, 0.4f},  {0.6f, -0.4f},  {1.0f, 0.4f},  {1.2f, 0.0f}};

void draw_resistor_body(const Pen &pen)
{
    pen.line(-2.0f, 0.0f, -1.2f, 0.0f);
    pen.poly(zigzag, 8);
    pen.line(1.2f, 0.0f, 2.0f, 0.0f);
}

void draw_transistor(const Pen &pen, bool npn)
{
    pen.circle(0.2f, 0.0f, 1.45f);
    pen.line(-2.0f, 0.0f, -0.5f, 0.0f);
    pen.line(-0.5f, -0.9f, -0.5f, 0.9f);
    pen.line(-0.5f, -0.4f, 1.0f, -1.2f);
    pen.line(1.0f, -1.2f, 1.0f, -2.0f);
    pen.line(-0.5f, 0.4f, 1.0f, 1.2f);
    pen.line(1.0f, 1.2f, 1.0f, 2.0f);
    if (npn) {
        pen.head(1.0f, 1.2f, 1.5f, 0.8f, 0.55f);     // out of the emitter
    } else {
        pen.head(-0.35f, 0.48f, -1.5f, -0.8f, 0.55f);   // into the base
    }
}

void draw_mosfet(const Pen &pen, bool n_channel)
{
    pen.circle(0.2f, 0.0f, 1.45f);
    pen.line(-2.0f, 0.0f, -0.7f, 0.0f);
    pen.line(-0.7f, -0.8f, -0.7f, 0.8f);     // the gate
    pen.line(-0.4f, -0.95f, -0.4f, 0.95f);   // the channel
    pen.line(-0.4f, -0.6f, 1.0f, -0.6f);
    pen.line(1.0f, -0.6f, 1.0f, -2.0f);
    pen.line(-0.4f, 0.6f, 1.0f, 0.6f);
    pen.line(1.0f, 0.6f, 1.0f, 2.0f);
    pen.line(-0.4f, 0.0f, 1.0f, 0.0f);       // the body, on the source
    pen.line(1.0f, 0.0f, 1.0f, 0.6f);
    if (n_channel) {
        pen.head(-0.4f, 0.0f, -1.0f, 0.0f, 0.45f);
    } else {
        pen.head(0.35f, 0.0f, 1.0f, 0.0f, 0.45f);
    }
}

// The body of a source or of a meter: a circle between two leads.
void draw_round_body(const Pen &pen)
{
    pen.line(-2.0f, 0.0f, -1.0f, 0.0f);
    pen.line(1.0f, 0.0f, 2.0f, 0.0f);
    pen.circle(0.0f, 0.0f, 1.0f);
}

// A NOR gate: the shield, the circle of the inversion and the leads.
void draw_nor(const Pen &pen)
{
    float back[9][2];
    float top[9][2];
    float bottom[9][2];
    for (int k = 0; k < 9; k++) {
        float y = -1.5f + 3.0f * static_cast<float>(k) / 8.0f;
        back[k][0] = -2.0f + 0.45f * (1.0f - (y / 1.5f) * (y / 1.5f));
        back[k][1] = y;
        float a = 1.5707963f * static_cast<float>(k) / 8.0f;
        top[k][0] = -2.0f + 3.4f * std::sin(a);
        top[k][1] = -1.5f * std::cos(a);
        bottom[k][0] = top[k][0];
        bottom[k][1] = -top[k][1];
    }
    pen.poly(back, 9);
    pen.poly(top, 9);
    pen.poly(bottom, 9);
    pen.circle(1.7f, 0.0f, 0.3f);
    pen.line(2.0f, 0.0f, 3.0f, 0.0f);
    pen.line(-3.0f, -1.0f, -1.75f, -1.0f);
    pen.line(-3.0f, 1.0f, -1.75f, 1.0f);
    pen.line(0.0f, -2.0f, 0.0f, -1.21f);
    pen.line(0.0f, 2.0f, 0.0f, 1.21f);
}

// An integrated circuit drawn as a box: a lead from every pin to the body.
void draw_box(const Pen &pen)
{
    const core::PartDef &def = core::part_def(pen.part->kind);
    const auto x0 = static_cast<float>(def.box_min.x + 1);
    const auto y0 = static_cast<float>(def.box_min.y + 1);
    const auto x1 = static_cast<float>(def.box_max.x - 1);
    const auto y1 = static_cast<float>(def.box_max.y - 1);
    pen.line(x0, y0, x1, y0);
    pen.line(x1, y0, x1, y1);
    pen.line(x1, y1, x0, y1);
    pen.line(x0, y1, x0, y0);
    for (int j = 0; j < def.pins; j++) {
        const auto x = static_cast<float>(def.pin[j].x);
        const auto y = static_cast<float>(def.pin[j].y);
        pen.line(x, y, std::clamp(x, x0, x1), std::clamp(y, y0, y1));
    }
}

// A reading with its sign and SI prefix: "-4.7mA".
std::string reading_text(double value, const char *unit)
{
    if (std::fabs(value) < 1e-12) {
        return std::string("0") + unit;
    }
    return (value < 0.0 ? "-" : "") + core::format_value(std::fabs(value), unit);
}

// `glow` is how lit an LED is, 0..1, in `glow_colour`.
void draw_symbol(const Pen &pen, float glow, uint32_t glow_colour)
{
    const core::Part &part = *pen.part;
    switch (part.kind) {
    case core::PartKind::Ground:
        pen.line(0.0f, 0.0f, 0.0f, 0.6f);
        pen.line(-0.7f, 0.6f, 0.7f, 0.6f);
        pen.line(-0.45f, 0.85f, 0.45f, 0.85f);
        pen.line(-0.2f, 1.1f, 0.2f, 1.1f);
        break;
    case core::PartKind::Resistor:
        draw_resistor_body(pen);
        break;
    case core::PartKind::Capacitor:
        pen.line(-2.0f, 0.0f, -0.2f, 0.0f);
        pen.line(-0.2f, -0.7f, -0.2f, 0.7f);
        pen.line(0.2f, -0.7f, 0.2f, 0.7f);
        pen.line(0.2f, 0.0f, 2.0f, 0.0f);
        break;
    case core::PartKind::Inductor: {
        pen.line(-2.0f, 0.0f, -1.2f, 0.0f);
        for (int bump = 0; bump < 4; bump++) {
            float centre = -0.9f + 0.6f * static_cast<float>(bump);
            float points[7][2];
            for (int k = 0; k < 7; k++) {
                float a = 3.14159265f * static_cast<float>(k) / 6.0f;
                points[k][0] = centre - 0.3f * std::cos(a);
                points[k][1] = -0.3f * std::sin(a);
            }
            pen.poly(points, 7);
        }
        pen.line(1.2f, 0.0f, 2.0f, 0.0f);
        break;
    }
    case core::PartKind::Potentiometer:
        draw_resistor_body(pen);
        pen.line(0.0f, -2.0f, 0.0f, -0.9f);
        pen.head(0.0f, -0.45f, 0.0f, 1.0f, 0.5f);
        break;
    case core::PartKind::Switch: {
        pen.line(-2.0f, 0.0f, -1.0f, 0.0f);
        pen.line(1.0f, 0.0f, 2.0f, 0.0f);
        pen.circle(-1.0f, 0.0f, 0.15f);
        pen.circle(1.0f, 0.0f, 0.15f);
        if (part.setting >= 0.5) {
            pen.line(-1.0f, 0.0f, 1.0f, 0.0f);
        } else {
            pen.line(-1.0f, 0.0f, 0.8f, -0.8f);
        }
        break;
    }
    case core::PartKind::Diode:
    case core::PartKind::Led:
        if (part.kind == core::PartKind::Led && glow > 0.01f) {
            pen.draw->AddCircleFilled(pen.at(0.0f, 0.0f), 1.3f * pen.view.pitch,
                                      with_alpha(glow_colour, static_cast<uint32_t>(40.0f + 180.0f * glow)), 32);
        }
        pen.line(-2.0f, 0.0f, -0.5f, 0.0f);
        pen.line(-0.5f, -0.6f, -0.5f, 0.6f);
        pen.line(-0.5f, -0.6f, 0.5f, 0.0f);
        pen.line(-0.5f, 0.6f, 0.5f, 0.0f);
        pen.line(0.5f, -0.6f, 0.5f, 0.6f);
        pen.line(0.5f, 0.0f, 2.0f, 0.0f);
        if (part.kind == core::PartKind::Led) {
            pen.line(0.0f, -0.8f, 0.5f, -1.4f);
            pen.head(0.5f, -1.4f, 0.5f, -0.6f, 0.3f);
            pen.line(0.5f, -0.8f, 1.0f, -1.4f);
            pen.head(1.0f, -1.4f, 0.5f, -0.6f, 0.3f);
        }
        break;
    case core::PartKind::Npn:
        draw_transistor(pen, true);
        break;
    case core::PartKind::Pnp:
        draw_transistor(pen, false);
        break;
    case core::PartKind::OpAmp:
        pen.line(-2.0f, -2.0f, -2.0f, 2.0f);
        pen.line(-2.0f, -2.0f, 2.0f, 0.0f);
        pen.line(-2.0f, 2.0f, 2.0f, 0.0f);
        pen.line(-3.0f, 1.0f, -2.0f, 1.0f);
        pen.line(-3.0f, -1.0f, -2.0f, -1.0f);
        pen.line(2.0f, 0.0f, 3.0f, 0.0f);
        pen.line(0.0f, -2.0f, 0.0f, -1.0f);
        pen.line(0.0f, 2.0f, 0.0f, 1.0f);
        pen.line(-1.75f, 1.0f, -1.25f, 1.0f);     // +
        pen.line(-1.5f, 0.75f, -1.5f, 1.25f);
        pen.line(-1.75f, -1.0f, -1.25f, -1.0f);   // -
        break;
    case core::PartKind::VSource:
        draw_round_body(pen);
        pen.line(-0.75f, 0.0f, -0.35f, 0.0f);     // +
        pen.line(-0.55f, -0.2f, -0.55f, 0.2f);
        pen.line(0.35f, 0.0f, 0.75f, 0.0f);       // -
        break;
    case core::PartKind::VSine: {
        draw_round_body(pen);
        float points[13][2];
        for (int k = 0; k < 13; k++) {
            points[k][0] = -0.6f + 0.1f * static_cast<float>(k);
            points[k][1] = -0.35f * std::sin(6.2831853f * static_cast<float>(k) / 12.0f);
        }
        pen.poly(points, 13);
        break;
    }
    case core::PartKind::ISource:
        draw_round_body(pen);
        pen.line(-0.6f, 0.0f, 0.3f, 0.0f);
        pen.head(0.7f, 0.0f, 1.0f, 0.0f, 0.45f);
        break;
    case core::PartKind::Nmos:
        draw_mosfet(pen, true);
        break;
    case core::PartKind::Pmos:
        draw_mosfet(pen, false);
        break;
    case core::PartKind::Voltmeter:
        draw_round_body(pen);
        pen.line(-0.35f, -0.4f, 0.0f, 0.4f);      // V
        pen.line(0.0f, 0.4f, 0.35f, -0.4f);
        break;
    case core::PartKind::Ammeter:
        draw_round_body(pen);
        pen.line(-0.35f, 0.4f, 0.0f, -0.4f);      // A
        pen.line(0.0f, -0.4f, 0.35f, 0.4f);
        pen.line(-0.18f, 0.1f, 0.18f, 0.1f);
        break;
    case core::PartKind::Nor:
        draw_nor(pen);
        break;
    case core::PartKind::Bbd:
        draw_box(pen);
        break;
    }
}

// Text beside a part: outwards from the centre of the part, never rotated.
void place_text(ImDrawList *draw, ImVec2 anchor, ImVec2 centre, const char *text, uint32_t colour)
{
    ImFont *font = ui::fonts().small;
    ImVec2 size = font->CalcTextSizeA(font->FontSize, 1e9f, 0.0f, text);
    float dx = anchor.x - centre.x;
    float dy = anchor.y - centre.y;
    ImVec2 pos;
    if (std::fabs(dx) > std::fabs(dy)) {
        pos = ImVec2(dx > 0.0f ? anchor.x : anchor.x - size.x, anchor.y - size.y * 0.5f);
    } else {
        pos = ImVec2(anchor.x - size.x * 0.5f, dy > 0.0f ? anchor.y : anchor.y - size.y);
    }
    draw->AddText(font, font->FontSize, pos, colour, text);
}

// The names of the pins of a part drawn as a box, inside it.
void draw_pin_names(const Pen &pen, uint32_t colour)
{
    const core::PartDef &def = core::part_def(pen.part->kind);
    for (int j = 0; j < def.pins; j++) {
        const auto x = static_cast<float>(def.pin[j].x);
        const auto y = static_cast<float>(def.pin[j].y);
        float ix = std::clamp(x, static_cast<float>(def.box_min.x) + 1.3f, static_cast<float>(def.box_max.x) - 1.3f);
        float iy = std::clamp(y, static_cast<float>(def.box_min.y) + 1.3f, static_cast<float>(def.box_max.y) - 1.3f);
        place_text(pen.draw, pen.at(ix, iy), pen.at(x, y), def.pin_name[j], colour);
    }
}

// Where the reference and the value of a kind go, around the part.
void text_anchor(core::PartKind kind, float &x, float &y)
{
    switch (kind) {
    case core::PartKind::Npn:
    case core::PartKind::Pnp:
    case core::PartKind::Nmos:
    case core::PartKind::Pmos:
        x = 1.9f;
        y = 0.0f;
        break;
    case core::PartKind::OpAmp:
        x = 0.9f;
        y = -1.5f;
        break;
    case core::PartKind::Nor:
        x = 1.2f;
        y = -1.0f;
        break;
    case core::PartKind::Bbd:
        x = 0.0f;   // in the middle of the box
        y = 0.0f;
        break;
    case core::PartKind::Led:
    case core::PartKind::Potentiometer:
        x = 0.0f;
        y = 1.2f;
        break;
    default:
        x = 0.0f;
        y = -1.2f;
        break;
    }
}

// The colour an LED of the catalog lights in.
uint32_t glow_colour(core::Glow glow)
{
    const ui::Theme &t = ui::current_theme();
    switch (glow) {
    case core::Glow::Red:
        return t.led_stop;
    case core::Glow::Green:
        return t.led_run;
    case core::Glow::Amber:
        return t.led_warn;
    case core::Glow::Blue:
        return ui::channel_colour(3);
    case core::Glow::White:
        return t.key_text;
    }
    return t.led_stop;
}

bool contains_nocase(const char *text, const char *wanted)
{
    if (!wanted[0]) {
        return true;
    }
    auto low = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    for (const char *start = text; *start; start++) {
        const char *a = start;
        const char *b = wanted;
        while (*a && *b && low(*a) == low(*b)) {
            a++;
            b++;
        }
        if (!*b) {
            return true;
        }
    }
    return false;
}

// The stock widgets inside the dialogs and the panel, in the colours of the theme.
constexpr int dialog_colours = 20;
constexpr int dialog_vars = 5;

void push_dialog_style(float s)
{
    const ui::Theme &t = ui::current_theme();
    ImGui::PushStyleColor(ImGuiCol_PopupBg, t.screen);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, t.screen);
    ImGui::PushStyleColor(ImGuiCol_Border, t.chassis_edge);
    ImGui::PushStyleColor(ImGuiCol_Text, t.readout);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, t.readout_dim);
    // Fields, buttons and list rows sit on the screen, which is dark in every
    // theme: their faces are the screen colour lifted towards its text.
    const uint32_t face = mix(t.screen, t.readout, 0.14f);
    const uint32_t face_hover = mix(t.screen, t.readout, 0.26f);
    const uint32_t face_active = mix(t.screen, t.readout, 0.38f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, face);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, face_hover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, face_active);
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, face_active);
    ImGui::PushStyleColor(ImGuiCol_Header, face);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, face_hover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, face_active);
    ImGui::PushStyleColor(ImGuiCol_Button, face);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, face_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, face_active);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, t.knob_mark);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, t.led_warn);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, t.led_run);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, t.screen);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, face_hover);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * s, 10.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f * s, 5.0f * s));
}

void pop_dialog_style()
{
    ImGui::PopStyleVar(dialog_vars);
    ImGui::PopStyleColor(dialog_colours);
}

}  // namespace

CircuitBench::CircuitBench(App &app) : app_(app) {}

CircuitBench::~CircuitBench()
{
    sim::Ngspice::instance().stop();
}

// ---- editing --------------------------------------------------------------

void CircuitBench::changed()
{
    clear_block();
    selected_wire_ = -1;
    if (!restoring_) {
        undo_.push_back(last_);
        if (undo_.size() > 200) {
            undo_.erase(undo_.begin());
        }
        redo_.clear();
    }
    last_ = circuit_;
    project_error_.clear();
    nets_ = circuit_.nets();
    dirty_ = true;
    watches_dirty_ = true;
}

void CircuitBench::add_wire(core::GridPoint a, core::GridPoint b)
{
    if (a != b) {
        circuit_.wires.push_back(core::CircuitWire{a, b});
    }
}

// The wires and the cables that ended on the pins of a part follow them.
void CircuitBench::move_pins(const std::vector<core::GridPoint> &from, const std::vector<core::GridPoint> &to, int moving_id)
{
    // A pin that touched the pin of another part stays connected to it: a
    // wire appears between the two as they part. To open a connection, the
    // wire is removed; moving a part never breaks the circuit.
    std::vector<core::CircuitWire> bridges;
    for (size_t k = 0; k < from.size() && k < to.size(); k++) {
        if (from[k] == to[k]) {
            continue;
        }
        bool touched = false;
        for (const core::Part &other : circuit_.parts) {
            for (int j = 0; other.id != moving_id && j < core::part_def(other.kind).pins; j++) {
                touched = touched || core::pin_position(other, j) == from[k];
            }
        }
        // So does a pin that sat along a wire, away from its ends.
        for (const core::CircuitWire &w : circuit_.wires) {
            touched = touched || (w.a != from[k] && w.b != from[k] && core::point_on_segment(from[k], w.a, w.b));
        }
        if (touched) {
            bridges.push_back(core::CircuitWire{from[k], to[k]});
        }
    }
    auto moved = [&](core::GridPoint &p) {
        for (size_t k = 0; k < from.size() && k < to.size(); k++) {
            if (p == from[k]) {
                p = to[k];
                return;
            }
        }
    };
    for (core::CircuitWire &w : circuit_.wires) {
        moved(w.a);
        moved(w.b);
    }
    for (core::Tap &tap : circuit_.taps) {
        moved(tap.at);
    }
    for (const core::CircuitWire &bridge : bridges) {
        circuit_.wires.push_back(bridge);
    }
    circuit_.wires.erase(std::remove_if(circuit_.wires.begin(), circuit_.wires.end(),
                                        [](const core::CircuitWire &w) { return w.a == w.b; }),
                         circuit_.wires.end());
}

// A part alone moves as a block of one.
void CircuitBench::move_part(size_t index, int dx, int dy)
{
    std::vector<int> parts{circuit_.parts[index].id};
    std::vector<char> wires;
    parts.swap(block_);
    wires.swap(block_wires_);
    move_block(dx, dy);
    block_ = std::move(parts);
    block_wires_ = std::move(wires);
}

bool CircuitBench::in_block(int id) const
{
    return std::find(block_.begin(), block_.end(), id) != block_.end();
}

void CircuitBench::clear_block()
{
    block_.clear();
    block_wires_.clear();
}

// The parts with every pin inside the rectangle and the wires with both
// ends inside it, in grid units.
void CircuitBench::select_block(float x0, float y0, float x1, float y1)
{
    clear_block();
    auto inside = [&](core::GridPoint p) {
        const auto x = static_cast<float>(p.x);
        const auto y = static_cast<float>(p.y);
        return x >= std::min(x0, x1) && x <= std::max(x0, x1) && y >= std::min(y0, y1) && y <= std::max(y0, y1);
    };
    for (const core::Part &part : circuit_.parts) {
        bool all = true;
        for (int j = 0; j < core::part_def(part.kind).pins; j++) {
            all = all && inside(core::pin_position(part, j));
        }
        if (all) {
            block_.push_back(part.id);
        }
    }
    block_wires_.assign(circuit_.wires.size(), 0);
    bool any = !block_.empty();
    for (size_t w = 0; w < circuit_.wires.size(); w++) {
        if (inside(circuit_.wires[w].a) && inside(circuit_.wires[w].b)) {
            block_wires_[w] = 1;
            any = true;
        }
    }
    if (!any) {
        clear_block();
    }
}

// Moves the block as one piece, and nothing that was connected comes
// apart. A wire between two points of the block goes with it, and so does
// a wire that hangs from it with a free end. A wire that ends on the block
// stretches, unless something sits along it: then it stays where it is and
// a new wire joins its old end to the block. Where a pin of another part or
// the middle of a wire touched the block, a new wire joins them. The cables
// of the bench go with the point or the wire they are on.
void CircuitBench::move_block(int dx, int dy)
{
    std::vector<core::CircuitWire> &wires = circuit_.wires;
    if (block_wires_.size() != wires.size()) {
        block_wires_.assign(wires.size(), 0);
    }
    std::vector<core::GridPoint> points;   // every point that moves
    for (const core::Part &part : circuit_.parts) {
        for (int j = 0; in_block(part.id) && j < core::part_def(part.kind).pins; j++) {
            points.push_back(core::pin_position(part, j));
        }
    }
    for (size_t w = 0; w < wires.size(); w++) {
        if (block_wires_[w]) {
            points.push_back(wires[w].a);
            points.push_back(wires[w].b);
        }
    }
    auto moves = [&](core::GridPoint p) { return std::find(points.begin(), points.end(), p) != points.end(); };
    auto shifted = [&](core::GridPoint p) { return core::GridPoint{p.x + dx, p.y + dy}; };
    auto along = [](core::GridPoint p, const core::CircuitWire &w) {
        return p != w.a && p != w.b && core::point_on_segment(p, w.a, w.b);
    };
    // Whether anything outside the block is at a point, the wire `skip` aside.
    auto held = [&](core::GridPoint p, size_t skip) {
        for (const core::Part &part : circuit_.parts) {
            for (int j = 0; !in_block(part.id) && j < core::part_def(part.kind).pins; j++) {
                if (core::pin_position(part, j) == p) {
                    return true;
                }
            }
        }
        for (size_t w = 0; w < wires.size(); w++) {
            if (w != skip && !block_wires_[w] && core::point_on_segment(p, wires[w].a, wires[w].b)) {
                return true;
            }
        }
        return false;
    };
    // Whether a pin, the end of another wire or a cable sits along a wire.
    auto busy = [&](size_t index) {
        const core::CircuitWire &wire = wires[index];
        for (const core::Part &part : circuit_.parts) {
            for (int j = 0; j < core::part_def(part.kind).pins; j++) {
                if (along(core::pin_position(part, j), wire)) {
                    return true;
                }
            }
        }
        for (size_t w = 0; w < wires.size(); w++) {
            if (w != index && (along(wires[w].a, wire) || along(wires[w].b, wire))) {
                return true;
            }
        }
        for (const core::Tap &tap : circuit_.taps) {
            if (along(tap.at, wire)) {
                return true;
            }
        }
        return false;
    };
    // The wires that go with the block, until no more join it.
    for (bool grew = true; grew;) {
        grew = false;
        for (size_t w = 0; w < wires.size(); w++) {
            if (block_wires_[w]) {
                continue;
            }
            const bool a = moves(wires[w].a);
            const bool b = moves(wires[w].b);
            const bool hangs = (a != b) && !held(a ? wires[w].b : wires[w].a, w) && !busy(w);
            if ((a && b) || hangs) {
                block_wires_[w] = 1;
                points.push_back(wires[w].a);
                points.push_back(wires[w].b);
                grew = true;
            }
        }
    }
    std::vector<core::CircuitWire> bridges;
    auto bridge = [&](core::GridPoint p) {
        for (const core::CircuitWire &b : bridges) {
            if (b.a == p) {
                return;
            }
        }
        bridges.push_back(core::CircuitWire{p, shifted(p)});
    };
    for (const core::Part &other : circuit_.parts) {
        for (int j = 0; !in_block(other.id) && j < core::part_def(other.kind).pins; j++) {
            if (moves(core::pin_position(other, j))) {
                bridge(core::pin_position(other, j));
            }
        }
    }
    std::vector<char> stretches(wires.size(), 0);
    for (size_t w = 0; w < wires.size(); w++) {
        if (block_wires_[w]) {
            continue;
        }
        const bool a = moves(wires[w].a);
        const bool b = moves(wires[w].b);
        if (a || b) {
            if (busy(w)) {
                bridge(a ? wires[w].a : wires[w].b);
            } else {
                stretches[w] = 1;
            }
        }
        for (const core::GridPoint &p : points) {
            if (along(p, wires[w])) {
                bridge(p);
            }
        }
    }
    for (core::Tap &tap : circuit_.taps) {
        bool with_block = moves(tap.at);
        for (size_t w = 0; w < wires.size(); w++) {
            with_block = with_block || (block_wires_[w] && core::point_on_segment(tap.at, wires[w].a, wires[w].b));
        }
        if (with_block) {
            tap.at = shifted(tap.at);
        }
    }
    for (size_t w = 0; w < wires.size(); w++) {
        core::CircuitWire &wire = wires[w];
        const bool a = block_wires_[w] || (stretches[w] && moves(wire.a));
        const bool b = block_wires_[w] || (stretches[w] && moves(wire.b));
        wire.a = a ? shifted(wire.a) : wire.a;
        wire.b = b ? shifted(wire.b) : wire.b;
    }
    for (core::Part &part : circuit_.parts) {
        if (in_block(part.id)) {
            part.x += dx;
            part.y += dy;
        }
    }
    for (const core::CircuitWire &b : bridges) {
        wires.push_back(b);
        block_wires_.push_back(0);
    }
    for (size_t w = wires.size(); w-- > 0;) {
        if (wires[w].a == wires[w].b) {
            wires.erase(wires.begin() + static_cast<std::ptrdiff_t>(w));
            block_wires_.erase(block_wires_.begin() + static_cast<std::ptrdiff_t>(w));
        }
    }
}

void CircuitBench::remove_block()
{
    for (size_t w = circuit_.wires.size(); w-- > 0;) {
        if (w < block_wires_.size() && block_wires_[w]) {
            circuit_.wires.erase(circuit_.wires.begin() + static_cast<std::ptrdiff_t>(w));
        }
    }
    for (size_t i = circuit_.parts.size(); i-- > 0;) {
        if (in_block(circuit_.parts[i].id)) {
            circuit_.parts.erase(circuit_.parts.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
    clear_block();
}

void CircuitBench::rotate_part(size_t index)
{
    core::Part &part = circuit_.parts[index];
    std::vector<core::GridPoint> from;
    std::vector<core::GridPoint> to;
    const int pins = core::part_def(part.kind).pins;
    from.reserve(static_cast<size_t>(pins));
    to.reserve(static_cast<size_t>(pins));
    for (int j = 0; j < pins; j++) {
        from.push_back(core::pin_position(part, j));
    }
    part.rotation = (part.rotation + 1) & 3;
    for (int j = 0; j < pins; j++) {
        to.push_back(core::pin_position(part, j));
    }
    move_pins(from, to, part.id);
}

void CircuitBench::remove_part(size_t index)
{
    if (circuit_.parts[index].id == selected_id_) {
        selected_id_ = -1;
    }
    circuit_.parts.erase(circuit_.parts.begin() + static_cast<std::ptrdiff_t>(index));
}

int CircuitBench::part_index(int id) const
{
    for (size_t i = 0; i < circuit_.parts.size(); i++) {
        if (circuit_.parts[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// The cable in hand goes into the schematic at `at`: a new tap, a new port.
void CircuitBench::plug_cable(core::GridPoint at)
{
    int slot = 0;
    for (bool taken = true; taken; slot++) {
        taken = false;
        for (const core::Tap &tap : circuit_.taps) {
            taken = taken || tap.slot == slot;
        }
        if (!taken) {
            break;
        }
    }
    circuit_.taps.push_back(core::Tap{slot, at});
    app_.declare_circuit_port(slot, "TP" + std::to_string(slot + 1));
    nets_ = circuit_.nets();
    watches_dirty_ = true;
    app_.select_port(App::circuit_port_base + slot);
}

// A tap exists while a cable is in it.
void CircuitBench::drop_unused_taps()
{
    for (size_t i = circuit_.taps.size(); i-- > 0;) {
        InstrumentId wired{Instrument::Scope, 0};
        int channel = 0;
        if (!app_.port_wired_to(App::circuit_port_base + circuit_.taps[i].slot, wired, channel)) {
            app_.remove_circuit_port(circuit_.taps[i].slot);
            circuit_.taps.erase(circuit_.taps.begin() + static_cast<std::ptrdiff_t>(i));
            nets_ = circuit_.nets();
            watches_dirty_ = true;
        }
    }
}

// Back to the circuit before the last change, and forth again. The cables
// are not part of it: they stay where they are.
void CircuitBench::undo()
{
    if (undo_.empty()) {
        return;
    }
    redo_.push_back(circuit_);
    std::vector<core::Tap> taps = circuit_.taps;
    circuit_ = undo_.back();
    undo_.pop_back();
    circuit_.taps = std::move(taps);
    selected_id_ = -1;
    selected_wire_ = -1;
    drag_id_ = -1;
    restoring_ = true;
    changed();
    restoring_ = false;
}

void CircuitBench::redo()
{
    if (redo_.empty()) {
        return;
    }
    undo_.push_back(circuit_);
    std::vector<core::Tap> taps = circuit_.taps;
    circuit_ = redo_.back();
    redo_.pop_back();
    circuit_.taps = std::move(taps);
    selected_id_ = -1;
    selected_wire_ = -1;
    drag_id_ = -1;
    restoring_ = true;
    changed();
    restoring_ = false;
}

// One more of a part, beside it, with the same value, model and position.
void CircuitBench::duplicate_part(size_t index)
{
    const core::Part original = circuit_.parts[index];
    size_t made = circuit_.add_part(original.kind, original.x + 2, original.y + 2, original.rotation);
    core::Part &copy = circuit_.parts[made];
    copy.value = original.value;
    copy.value2 = original.value2;
    copy.setting = original.setting;
    copy.model = original.model;
    selected_id_ = copy.id;
    changed();
}

// The circuit as a SPICE deck, beside the projects: for another simulator,
// or to see what the bench simulates.
bool CircuitBench::export_netlist()
{
    project_bad_ = true;
    if (projects_dir().empty()) {
        project_error_ = "no projects folder";
        return false;
    }
    const std::string name = project_.empty() ? std::string("circuit") : project_;
    std::ofstream file(std::filesystem::path(projects_dir()) / (name + ".cir"));
    file << "* RTR-Bench circuit " << name << "\n";
    file << "* sources marked external are commanded by the bench (outputs, potentiometers, switches)\n";
    for (const std::string &line : core::netlist(circuit_, circuit_.nets(), drives_, shunts_, loads_)) {
        file << line << "\n";
    }
    file << ".end\n";
    if (!file) {
        project_error_ = "cannot write " + name + ".cir";
        return false;
    }
    project_error_ = "netlist in .RT-Lab/" + name + ".cir";
    project_bad_ = false;
    return true;
}

// A click on a part: a switch moves; a double click edits the value.
void CircuitBench::toggle_or_edit(size_t index, bool edit)
{
    core::Part &part = circuit_.parts[index];
    if (edit) {
        if (core::part_def(part.kind).default_value > 0.0) {
            edit_id_ = part.id;
            edit_open_ = true;
            std::snprintf(edit_text_, sizeof(edit_text_), "%s", core::format_value(part.value, "").c_str());
        }
        return;
    }
    if (part.kind == core::PartKind::Switch) {
        part.setting = part.setting >= 0.5 ? 0.0 : 1.0;
    }
}

// ---- projects -------------------------------------------------------------

// An empty schematic: the cables go back to their instruments.
void CircuitBench::clear_circuit()
{
    for (const core::Tap &tap : circuit_.taps) {
        app_.remove_circuit_port(tap.slot);
    }
    circuit_ = core::Circuit();
    point_net_.clear();
    vectors_.clear();
    drive_amps_.clear();
    tap_level_.clear();
    selected_id_ = -1;
    selected_wire_ = -1;
    drag_id_ = -1;
    placing_ = false;
    wiring_ = false;
    changed();
}

bool CircuitBench::open_project(const std::string &name)
{
    if (projects_dir().empty() || name.empty()) {
        return false;
    }
    std::ifstream file(std::filesystem::path(projects_dir()) / (name + ".json"));
    nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
    if (!j.is_object()) {
        project_error_ = "cannot read " + name;
        project_bad_ = true;
        return false;
    }
    clear_circuit();
    load(j);
    project_ = name;
    project_error_.clear();
    return true;
}

bool CircuitBench::save_project(const std::string &name)
{
    // A file name: letters, digits, space, dash, underscore and dot.
    std::string clean;
    for (char c : name) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' ||
                  c == '_' || c == '.';
        if (ok) {
            clean += c;
        }
    }
    while (!clean.empty() && (clean.back() == ' ' || clean.back() == '.')) {
        clean.pop_back();
    }
    if (clean.size() > 5 && clean.compare(clean.size() - 5, 5, ".json") == 0) {
        clean.erase(clean.size() - 5);
    }
    if (projects_dir().empty() || clean.empty()) {
        project_error_ = "no name";
        project_bad_ = true;
        return false;
    }
    project_ = clean;
    nlohmann::json j;
    save(j);
    std::ofstream file(std::filesystem::path(projects_dir()) / (clean + ".json"));
    file << j.dump(2) << '\n';
    if (!file) {
        project_error_ = "cannot write " + clean;
        project_bad_ = true;
        return false;
    }
    project_error_.clear();
    return true;
}

// The list OPEN shows and the name SAVE asks for.
void CircuitBench::draw_project_popups(float s)
{
    const ui::Theme &t = ui::current_theme();
    if (open_ask_) {
        open_ask_ = false;
        project_files_.clear();
        std::error_code ec;
        if (!projects_dir().empty()) {
            for (const auto &entry : std::filesystem::directory_iterator(projects_dir(), ec)) {
                if (entry.path().extension() == ".json") {
                    project_files_.push_back(entry.path().stem().string());
                }
            }
        }
        std::sort(project_files_.begin(), project_files_.end());
        ImGui::SetNextWindowPos(ImGui::GetIO().MousePos);
        ImGui::OpenPopup("##open-project");
    }
    if (save_ask_) {
        save_ask_ = false;
        std::snprintf(save_text_, sizeof(save_text_), "%s", project_.c_str());
        ImGui::SetNextWindowPos(ImGui::GetIO().MousePos);
        ImGui::OpenPopup("##save-project");
    }
    ImGui::PushStyleColor(ImGuiCol_PopupBg, t.screen);
    ImGui::PushStyleColor(ImGuiCol_Border, t.chassis_edge);
    ImGui::PushStyleColor(ImGuiCol_Text, t.readout);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, t.key);
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, t.key_hover);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, t.key_hover);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, t.key_pressed);
    ImGui::PushStyleColor(ImGuiCol_Header, t.key);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * s, 8.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f * s);
    ImGui::PushFont(ui::fonts().mono);
    if (ImGui::BeginPopup("##open-project")) {
        if (project_files_.empty()) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(t.readout_dim), "no projects");
        }
        std::string chosen;
        for (const std::string &name : project_files_) {
            if (ImGui::Selectable(name.c_str(), name == project_)) {
                chosen = name;
            }
        }
        if (!chosen.empty()) {
            open_project(chosen);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##save-project")) {
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(220.0f * s);
        if (ImGui::InputText("##name", save_text_, sizeof(save_text_),
                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            save_project(save_text_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopFont();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(8);
}

// ---- simulation -----------------------------------------------------------

bool CircuitBench::vector_value(const std::string &name, double &value) const
{
    for (const auto &entry : vectors_) {
        if (entry.first == name) {
            value = entry.second;
            return true;
        }
    }
    return false;
}

double CircuitBench::net_volts(int net) const
{
    if (net < 0 || static_cast<size_t>(net) >= nets_.name.size()) {
        return 0.0;
    }
    double value = 0.0;
    vector_value(nets_.name[static_cast<size_t>(net)], value);
    return value;
}

// The outputs of the bench wired into the schematic.
std::vector<core::Drive> CircuitBench::wired_drives() const
{
    std::vector<core::Drive> drives;
    for (const core::Tap &tap : circuit_.taps) {
        InstrumentId wired{Instrument::Scope, 0};
        int channel = 0;
        if (!app_.port_wired_to(App::circuit_port_base + tap.slot, wired, channel)) {
            continue;
        }
        if (App::is_output_instrument(wired.kind)) {
            drives.push_back(core::Drive{tap.slot, wired.kind == Instrument::Supply ? supply_ohms : generator_ohms});
        } else if (const InstrumentBase *source = app_.instrument(wired); source && source->channel_drives(channel)) {
            drives.push_back(core::Drive{tap.slot, stream_ohms});   // an audio input of the computer
        }
    }
    return drives;
}

// The tips of a multimeter on an ampere function: the meter goes in series
// between the tap of the tip and the tap of its COM.
std::vector<core::Shunt> CircuitBench::wired_shunts() const
{
    std::vector<core::Shunt> shunts;
    for (const core::Tap &tap : circuit_.taps) {
        InstrumentId wired{Instrument::Scope, 0};
        int channel = 0;
        if (!app_.port_wired_to(App::circuit_port_base + tap.slot, wired, channel) || channel % 2 != 0) {
            continue;
        }
        const InstrumentBase *meter = app_.instrument(wired);
        if (!meter || !meter->channel_wants_current(channel)) {
            continue;
        }
        core::Shunt shunt{tap.slot, -1};
        for (const core::Tap &other : circuit_.taps) {
            InstrumentId other_wired{Instrument::Scope, 0};
            int other_channel = 0;
            if (app_.port_wired_to(App::circuit_port_base + other.slot, other_wired, other_channel) && other_wired == wired &&
                other_channel == channel + 1) {
                shunt.slot_com = other.slot;
            }
        }
        shunts.push_back(shunt);
    }
    return shunts;
}

// The measuring inputs of the bench (oscilloscope, logic analyzer,
// multimeter on volts) are ideal: they take nothing from the node they are
// on. An audio output of the computer is not a probe but where the signal
// goes: a line input, 47 kilohm seen from the circuit.
std::vector<core::Load> CircuitBench::wired_loads() const
{
    std::vector<core::Load> loads;
    for (const core::Tap &tap : circuit_.taps) {
        InstrumentId wired{Instrument::Scope, 0};
        int channel = 0;
        if (!app_.port_wired_to(App::circuit_port_base + tap.slot, wired, channel) || wired.kind != Instrument::Audio) {
            continue;
        }
        const InstrumentBase *instrument = app_.instrument(wired);
        if (instrument && !instrument->channel_drives(channel)) {
            loads.push_back(core::Load{tap.slot, 47e3});
        }
    }
    return loads;
}

// Hands the circuit as it is now to the simulator. The run in progress is
// replaced; every node that is still at the same place on the schematic
// starts at the voltage it had, so an edit does not restart the circuit.
void CircuitBench::rebuild()
{
    sim::Ngspice &sim = sim::Ngspice::instance();
    dirty_ = false;
    nets_ = circuit_.nets();
    bool any = false;
    for (const core::Part &part : circuit_.parts) {
        any = any || part.kind != core::PartKind::Ground;
    }
    if (!any) {
        sim.stop();
        point_net_.clear();
        return;
    }
    std::vector<std::string> lines = core::netlist(circuit_, nets_, drives_, shunts_, loads_);
    std::vector<bool> done(static_cast<size_t>(nets_.count), false);
    for (const auto &entry : point_net_) {
        double volts = 0.0;
        int net = circuit_.net_at(nets_, entry.first);
        if (net < 0 || done[static_cast<size_t>(net)] || nets_.name[static_cast<size_t>(net)] == "0" ||
            !vector_value(entry.second, volts) || !std::isfinite(volts)) {
            continue;
        }
        done[static_cast<size_t>(net)] = true;
        char text[64];
        std::snprintf(text, sizeof(text), ".ic v(%s)=%.9g", nets_.name[static_cast<size_t>(net)].c_str(), volts);
        lines.emplace_back(text);
    }
    if (!point_net_.empty()) {
        for (const core::Part &part : circuit_.parts) {
            const std::string name = core::element_name(part);
            double value = 0.0;
            if (part.kind == core::PartKind::Inductor && vector_value(name + "#branch", value) && std::isfinite(value)) {
                for (std::string &line : lines) {
                    if (line.compare(0, name.size() + 1, name + " ") == 0) {
                        char text[32];
                        std::snprintf(text, sizeof(text), " ic=%.9g", value);
                        line += text;
                    }
                }
            }
            if (part.kind == core::PartKind::OpAmp && vector_value(name + ".b", value) && std::isfinite(value)) {
                char text[64];
                std::snprintf(text, sizeof(text), ".ic v(%s.b)=%.9g", name.c_str(), value);
                lines.emplace_back(text);
            }
        }
    }
    sim.set_digital(core::digital(circuit_, nets_, drives_, shunts_));
    sim.load(std::move(lines), false);

    point_net_.clear();
    for (size_t i = 0; i < circuit_.parts.size(); i++) {
        for (int j = 0; j < core::part_def(circuit_.parts[i].kind).pins; j++) {
            int net = nets_.pin_net[i * core::max_part_pins + static_cast<size_t>(j)];
            point_net_.emplace_back(core::pin_position(circuit_.parts[i], j), nets_.name[static_cast<size_t>(net)]);
        }
    }
    for (size_t w = 0; w < circuit_.wires.size(); w++) {
        const std::string &name = nets_.name[static_cast<size_t>(nets_.wire_net[w])];
        point_net_.emplace_back(circuit_.wires[w].a, name);
        point_net_.emplace_back(circuit_.wires[w].b, name);
    }
    watches_dirty_ = true;
}

// What the bench samples: the voltage at every tap, and the current of
// every output wired in.
void CircuitBench::set_watches()
{
    watches_dirty_ = false;
    watches_.clear();
    std::vector<std::string> names;
    for (size_t t = 0; t < circuit_.taps.size() && t < nets_.tap_net.size(); t++) {
        int net = nets_.tap_net[t];
        const int slot = circuit_.taps[t].slot;
        watches_.push_back(Watch{slot, false});
        // A tip that measures current receives the current through its
        // shunt, in amperes, and its COM receives zero: the reading is the
        // difference, as for a voltage.
        bool shunt_tip = false;
        bool shunt_com = false;
        for (const core::Shunt &shunt : shunts_) {
            shunt_tip = shunt_tip || shunt.slot == slot;
            shunt_com = shunt_com || shunt.slot_com == slot;
        }
        if (shunt_tip) {
            names.push_back(core::shunt_vector(slot));
        } else if (shunt_com) {
            names.emplace_back();
        } else {
            names.push_back(net >= 0 ? nets_.name[static_cast<size_t>(net)] : std::string());
        }
    }
    for (const core::Drive &drive : drives_) {
        watches_.push_back(Watch{drive.slot, true});
        names.push_back(core::drive_source(drive.slot) + "#branch");
    }
    sim::Ngspice::instance().set_watches(names);
    samples_.clear();
    synced_ = false;
}

// The sources the bench commands, every frame: the position of every
// potentiometer and switch, and what every wired output produces.
void CircuitBench::update_sources()
{
    sim::Ngspice &sim = sim::Ngspice::instance();
    for (const core::Part &part : circuit_.parts) {
        if (part.kind == core::PartKind::Potentiometer) {
            sim.set_resistance(core::pot_resistor(part, 0), core::pot_ohms(part, 0));
            sim.set_resistance(core::pot_resistor(part, 1), core::pot_ohms(part, 1));
        }
        if (part.kind == core::PartKind::Switch) {
            sim.set_constant(core::control_source(part), std::clamp(part.setting, 0.0, 1.0));
        }
        if (part.kind == core::PartKind::VSine) {
            core::WaveSpec wave;
            wave.kind = core::Waveform::Sine;
            wave.amplitude_v = part.value;
            wave.freq_hz = part.value2;
            sim.set_wave(core::element_name(part), wave, true);
        }
    }
    for (const core::Tap &tap : circuit_.taps) {
        InstrumentId wired{Instrument::Scope, 0};
        int channel = 0;
        if (!app_.port_wired_to(App::circuit_port_base + tap.slot, wired, channel)) {
            continue;
        }
        if (InstrumentBase *source = app_.instrument(wired); source && source->channel_drives(channel)) {
            // What the audio input heard since the last frame goes to the simulator.
            double rate = 0.0;
            size_t count = source->drain_stream(channel, stream_, rate);
            sim.push_stream(core::drive_source(tap.slot), stream_.data(), count, rate);
            continue;
        }
        if (!App::is_output_instrument(wired.kind)) {
            continue;
        }
        core::WaveSpec spec;
        bool on = false;
        if (app_.output_spec(wired, channel, spec, on)) {
            sim.set_wave(core::drive_source(tap.slot), spec, on);
        }
    }
}

void CircuitBench::produce(int64_t now_ns, std::vector<core::DigitalEvent> &events, std::vector<core::AnalogBlock> &blocks)
{
    sim::Ngspice &sim = sim::Ngspice::instance();
    if (!sim.available()) {
        return;
    }
    drop_unused_taps();
    std::vector<core::Drive> drives = wired_drives();
    bool same = drives.size() == drives_.size();
    for (size_t i = 0; same && i < drives.size(); i++) {
        same = drives[i].slot == drives_[i].slot && drives[i].series_ohms == drives_[i].series_ohms;
    }
    if (!same) {
        drives_ = std::move(drives);
        dirty_ = true;
    }
    std::vector<core::Shunt> shunts = wired_shunts();
    bool same_shunts = shunts.size() == shunts_.size();
    for (size_t i = 0; same_shunts && i < shunts.size(); i++) {
        same_shunts = shunts[i].slot == shunts_[i].slot && shunts[i].slot_com == shunts_[i].slot_com;
    }
    if (!same_shunts) {
        shunts_ = std::move(shunts);
        dirty_ = true;
    }
    std::vector<core::Load> loads = wired_loads();
    bool same_loads = loads.size() == loads_.size();
    for (size_t i = 0; same_loads && i < loads.size(); i++) {
        same_loads = loads[i].slot == loads_[i].slot && loads[i].ohms == loads_[i].ohms;
    }
    if (!same_loads) {
        loads_ = std::move(loads);
        dirty_ = true;
    }
    // A part being dragged is in transit: the simulator waits for it to land.
    if (dirty_ && drag_id_ < 0 && !block_drag_) {
        rebuild();
    }
    if (watches_dirty_) {
        set_watches();
    }
    update_sources();
    sim.snapshot(vectors_);
    if (sim.state() != sim::Ngspice::State::Running || paused_) {
        // Paused: the target stays where it is, and so does the simulation.
        synced_ = false;
        speed_ = 0.0;
        return;
    }

    // The simulation follows the bench clock from where they were synchronised.
    const int64_t dt = sim::Ngspice::sample_ns;
    if (!synced_) {
        synced_ = true;
        sync_bench_ns_ = now_ns;
        sync_sim_ = sim.time();
        stream_next_ns_ = (now_ns / dt + 1) * dt;
        speed_sim_ = sync_sim_;
        speed_bench_ns_ = now_ns;
    }
    sim.set_target(sync_sim_ + static_cast<double>(now_ns - sync_bench_ns_) / 1e9);
    if (now_ns - speed_bench_ns_ >= 500000000LL) {
        double simulated = sim.time();
        speed_ = (simulated - speed_sim_) / (static_cast<double>(now_ns - speed_bench_ns_) / 1e9);
        speed_sim_ = simulated;
        speed_bench_ns_ = now_ns;
    }

    size_t count = sim.drain(samples_);
    if (count > 0 && samples_.size() == watches_.size()) {
        for (size_t w = 0; w < watches_.size(); w++) {
            const Watch &watch = watches_[w];
            std::vector<float> &list = samples_[w];
            if (watch.current) {
                // ngspice counts the current into the positive terminal of a source.
                bool known = false;
                for (auto &entry : drive_amps_) {
                    if (entry.first == watch.slot) {
                        entry.second = -list.back();
                        known = true;
                    }
                }
                if (!known) {
                    drive_amps_.emplace_back(watch.slot, -list.back());
                }
                continue;
            }
            const int port = App::circuit_port_base + watch.slot;
            // The logic level of the node, for the digital inputs.
            int *level = nullptr;
            for (auto &entry : tap_level_) {
                if (entry.first == watch.slot) {
                    level = &entry.second;
                }
            }
            if (!level) {
                tap_level_.emplace_back(watch.slot, -1);
                level = &tap_level_.back().second;
            }
            for (size_t k = 0; k < list.size(); k++) {
                int now_level = list[k] >= logic_threshold ? 1 : 0;
                if (now_level != *level) {
                    *level = now_level;
                    events.push_back(core::DigitalEvent{stream_next_ns_ + static_cast<int64_t>(k) * dt,
                                                        static_cast<uint16_t>(port & 0xFFFF), static_cast<uint8_t>(now_level),
                                                        core::DigitalEvent::Transition});
                }
            }
            core::AnalogBlock block;
            block.t0_ns = stream_next_ns_;
            block.dt_ns = dt;
            block.port = static_cast<uint16_t>(port & 0xFFFF);
            block.volts = std::move(list);
            blocks.push_back(std::move(block));
        }
        stream_next_ns_ += static_cast<int64_t>(count) * dt;
    }
    // Too far behind the clock (a simulation slower than real time): start
    // again from now rather than show an ever older signal.
    if (now_ns - stream_next_ns_ > 500000000LL) {
        synced_ = false;
    }
}

bool CircuitBench::port_current(int port, float &amps) const
{
    for (const auto &entry : drive_amps_) {
        if (App::circuit_port_base + entry.first == port) {
            amps = entry.second;
            return true;
        }
    }
    return false;
}

// ---- drawing --------------------------------------------------------------

void CircuitBench::draw(ui::Window &window)
{
    if (want_w_ > 0 && want_h_ > 0) {
        window.set_size(want_w_, want_h_);
        want_w_ = 0;
        want_h_ = 0;
    }
    window.size(last_w_, last_h_);
    ui::ChassisSpec chassis;
    chassis.model = "LAB-1";
    chassis.title = project_.empty() ? std::string("Circuit Bench") : "Circuit Bench   " + project_;
    chassis.corner = 12.0f;
    ui::ChassisFrame frame = ui::begin_chassis(window, chassis);
    const float s = window.scale();
    const float key_h = 21.0f * s;

    ImVec2 bar_min(frame.panel_min.x, frame.panel_min.y + 2.0f * s);
    draw_toolbar(window, bar_min, ImVec2(frame.panel_max.x, bar_min.y + key_h));
    const float top = bar_min.y + key_h + 8.0f * s;
    float canvas_right = frame.panel_max.x;
    if (part_index(selected_id_) >= 0) {
        const float panel_w = 250.0f * s;
        canvas_right -= panel_w + 8.0f * s;
        draw_properties(window, ImVec2(canvas_right + 8.0f * s, top), ImVec2(frame.panel_max.x, frame.panel_max.y - 18.0f * s));
    }
    draw_canvas(window, ImVec2(frame.panel_min.x, top), ImVec2(canvas_right, frame.panel_max.y));
    draw_value_editor(s);
    draw_add_dialog(s);
    draw_project_popups(s);
    resize_grip(window, frame.panel_max);
    app_.grab_near(window);
    ui::end_chassis();
}

void CircuitBench::draw_toolbar(ui::Window &window, ImVec2 min, ImVec2 max)
{
    const ui::Theme &t = ui::current_theme();
    const float s = window.scale();
    const float key_h = max.y - min.y;
    const float gap = 5.0f * s;
    float x = min.x;
    // ADD opens the catalog; the part chosen there is in hand until it is
    // placed (the key shows its name meanwhile; a click on it gives it up).
    {
        const char *label = placing_ && place_entry_ ? place_entry_->name : "ADD";
        float w = std::max(64.0f * s, ImGui::CalcTextSize(label).x + 20.0f * s);
        if (ui::key("##add", label, ImVec2(x, min.y), ImVec2(w, key_h), placing_, t.led_warn, s)) {
            if (placing_) {
                placing_ = false;
            } else {
                add_ask_ = true;
            }
        }
        x += w + gap;
    }
    x += 8.0f * s;
    if (ui::key("##run", paused_ ? "PAUSED" : "RUN", ImVec2(x, min.y), ImVec2(60.0f * s, key_h), !paused_, t.led_run, s)) {
        paused_ = !paused_;
    }
    x += 60.0f * s + gap;
    if (ui::key("##reset", "RESET", ImVec2(x, min.y), ImVec2(56.0f * s, key_h), false, t.led_stop, s)) {
        // From the beginning: nothing is carried from the run in progress.
        point_net_.clear();
        vectors_.clear();
        dirty_ = true;
    }
    x += 56.0f * s + gap + 8.0f * s;
    if (ui::key("##undo", "UNDO", ImVec2(x, min.y), ImVec2(48.0f * s, key_h), false, t.led_run, s, !undo_.empty())) {
        undo();
    }
    x += 48.0f * s + gap;
    if (ui::key("##redo", "REDO", ImVec2(x, min.y), ImVec2(48.0f * s, key_h), false, t.led_run, s, !redo_.empty())) {
        redo();
    }
    x += 48.0f * s + gap;
    if (ui::key("##fit", "FIT", ImVec2(x, min.y), ImVec2(40.0f * s, key_h), false, t.led_run, s)) {
        fit_ask_ = true;
    }
    x += 40.0f * s + gap + 8.0f * s;
    // Projects: an empty schematic, one of the saved ones, save under a name.
    if (ui::key("##new", "NEW", ImVec2(x, min.y), ImVec2(44.0f * s, key_h), false, t.led_run, s)) {
        clear_circuit();
        project_.clear();
        project_error_.clear();
    }
    x += 44.0f * s + gap;
    if (ui::key("##open", "OPEN", ImVec2(x, min.y), ImVec2(48.0f * s, key_h), false, t.led_run, s)) {
        open_ask_ = true;
    }
    x += 48.0f * s + gap;
    if (ui::key("##save", "SAVE", ImVec2(x, min.y), ImVec2(48.0f * s, key_h), false, t.led_run, s)) {
        save_ask_ = true;
    }
    x += 48.0f * s + gap;
    if (ui::key("##spice", "SPICE", ImVec2(x, min.y), ImVec2(52.0f * s, key_h), false, t.led_run, s)) {
        export_netlist();
    }
    x += 52.0f * s + gap + 8.0f * s;

    sim::Ngspice &sim = sim::Ngspice::instance();
    char text[200];
    uint32_t colour = t.readout;
    switch (sim.state()) {
    case sim::Ngspice::State::Missing:
        std::snprintf(text, sizeof(text), "%.80s", sim.status().empty() ? "no simulator" : sim.status().c_str());
        colour = t.led_stop;
        break;
    case sim::Ngspice::State::Failed:
        std::snprintf(text, sizeof(text), "%.80s", sim.status().c_str());
        colour = t.led_stop;
        break;
    case sim::Ngspice::State::Idle:
        std::snprintf(text, sizeof(text), "IDLE");
        colour = t.readout_dim;
        break;
    case sim::Ngspice::State::Running:
        if (paused_) {
            std::snprintf(text, sizeof(text), "PAUSED  %.1f s   %s", sim.time(), hover_text_.c_str());
            colour = t.led_warn;
        } else {
            std::snprintf(text, sizeof(text), "RUN  %.2fx  %.1f s   %s", speed_, sim.time(), hover_text_.c_str());
        }
        break;
    }
    if (!project_error_.empty()) {
        std::snprintf(text, sizeof(text), "%.80s", project_error_.c_str());
        colour = project_bad_ ? t.led_stop : t.readout;
    }
    if (x < max.x - 60.0f * s) {
        ui::readout(ImVec2(x, min.y), ImVec2(max.x, max.y), text, colour, s);
    }
}

void CircuitBench::draw_canvas(ui::Window &window, ImVec2 min, ImVec2 max)
{
    const ui::Theme &t = ui::current_theme();
    const float s = window.scale();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 inner_min;
    ImVec2 inner_max;
    panel::screen(draw, min, max, s, inner_min, inner_max);
    if (inner_max.x <= inner_min.x || inner_max.y <= inner_min.y) {
        return;
    }
    ImGui::SetCursorScreenPos(inner_min);
    ImGui::InvisibleButton("##canvas", ImVec2(inner_max.x - inner_min.x, inner_max.y - inner_min.y),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered() && !ImGui::IsPopupOpen("##value");

    if (fit_ask_) {
        // The whole schematic in the window, centred.
        fit_ask_ = false;
        bool any = false;
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        auto take = [&](core::GridPoint point, int margin) {
            if (!any) {
                x0 = point.x - margin;
                x1 = point.x + margin;
                y0 = point.y - margin;
                y1 = point.y + margin;
                any = true;
            }
            x0 = std::min(x0, point.x - margin);
            x1 = std::max(x1, point.x + margin);
            y0 = std::min(y0, point.y - margin);
            y1 = std::max(y1, point.y + margin);
        };
        for (const core::Part &part : circuit_.parts) {
            take(core::GridPoint{part.x, part.y}, 4);
        }
        for (const core::CircuitWire &wire : circuit_.wires) {
            take(wire.a, 2);
            take(wire.b, 2);
        }
        if (any) {
            float w = inner_max.x - inner_min.x;
            float h = inner_max.y - inner_min.y;
            float fit = std::min(w / (static_cast<float>(x1 - x0) * base_pitch * s), h / (static_cast<float>(y1 - y0) * base_pitch * s));
            zoom_ = std::clamp(fit, 0.4f, 4.0f);
            float pitch = base_pitch * s * zoom_;
            pan_.x = w * 0.5f - static_cast<float>(x0 + x1) * 0.5f * pitch;
            pan_.y = h * 0.5f - static_cast<float>(y0 + y1) * 0.5f * pitch;
        }
    }
    View view;
    view.pitch = base_pitch * s * zoom_;
    view.origin = ImVec2(inner_min.x + pan_.x, inner_min.y + pan_.y);
    const float mgx = (io.MousePos.x - view.origin.x) / view.pitch;
    const float mgy = (io.MousePos.y - view.origin.y) / view.pitch;
    const core::GridPoint g{static_cast<int>(std::lround(mgx)), static_cast<int>(std::lround(mgy))};
    const float off_x = mgx - static_cast<float>(g.x);
    const float off_y = mgy - static_cast<float>(g.y);
    const bool near_point = off_x * off_x + off_y * off_y < 0.45f * 0.45f;

    // Where each cable is drawn: beside its point, so the node stays free.
    auto tap_marker = [&](size_t index) {
        int stacked = 1;
        for (size_t k = 0; k < index; k++) {
            if (circuit_.taps[k].at == circuit_.taps[index].at) {
                stacked++;
            }
        }
        ImVec2 p = view.at(circuit_.taps[index].at);
        return ImVec2(p.x + static_cast<float>(stacked) * 11.0f * s, p.y - static_cast<float>(stacked) * 11.0f * s);
    };

    // ---- what is under the mouse ----
    int hover_part = -1;
    bool pin_here = false;
    int wire_here = -1;
    int hover_tap = -1;
    if (hovered) {
        for (size_t i = 0; i < circuit_.parts.size(); i++) {
            const core::Part &part = circuit_.parts[i];
            const core::PartDef &def = core::part_def(part.kind);
            float lx = mgx - static_cast<float>(part.x);
            float ly = mgy - static_cast<float>(part.y);
            rotate_local(lx, ly, 4 - part.rotation);
            if (lx >= static_cast<float>(def.box_min.x) - 0.2f && lx <= static_cast<float>(def.box_max.x) + 0.2f &&
                ly >= static_cast<float>(def.box_min.y) - 0.2f && ly <= static_cast<float>(def.box_max.y) + 0.2f) {
                hover_part = static_cast<int>(i);
            }
            for (int j = 0; near_point && j < def.pins; j++) {
                pin_here = pin_here || core::pin_position(part, j) == g;
            }
        }
        for (size_t w = 0; near_point && w < circuit_.wires.size(); w++) {
            if (core::point_on_segment(g, circuit_.wires[w].a, circuit_.wires[w].b)) {
                wire_here = static_cast<int>(w);
            }
        }
        for (size_t k = 0; k < circuit_.taps.size(); k++) {
            ImVec2 m = tap_marker(k);
            float dx = io.MousePos.x - m.x;
            float dy = io.MousePos.y - m.y;
            if (dx * dx + dy * dy <= 8.0f * s * 8.0f * s) {
                hover_tap = static_cast<int>(k);
            }
        }
    }
    const bool on_net = pin_here || wire_here >= 0;

    // ---- the mouse and the keys ----
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
        if (placing_ || wiring_) {
            placing_ = false;
            wiring_ = false;
        } else if (!block_.empty() || !block_wires_.empty()) {
            clear_block();
        } else if (hovered && hover_part >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            // The menu of the part under the mouse.
            selected_id_ = circuit_.parts[static_cast<size_t>(hover_part)].id;
            ImGui::OpenPopup("##part-menu");
        } else if (hovered && wire_here >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            menu_wire_ = wire_here;
            ImGui::OpenPopup("##wire-menu");
        } else {
            app_.cancel_wiring();
        }
    }
    if (hovered && !io.WantTextInput) {
        int target = hover_part >= 0 ? hover_part : part_index(selected_id_);
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            if (placing_) {
                place_rotation_ = (place_rotation_ + 1) & 3;
            } else if (target >= 0) {
                rotate_part(static_cast<size_t>(target));
                changed();
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
            if (!block_.empty() || !block_wires_.empty()) {
                remove_block();
                changed();
            } else if (selected_wire_ >= 0 && static_cast<size_t>(selected_wire_) < circuit_.wires.size()) {
                // The wire that was clicked, wherever the mouse is now; the
                // wire the click had started is dropped.
                circuit_.wires.erase(circuit_.wires.begin() + selected_wire_);
                wiring_ = false;
                changed();
            } else if (wire_here >= 0 && !pin_here) {
                circuit_.wires.erase(circuit_.wires.begin() + wire_here);
                changed();
            } else if (target >= 0) {
                remove_part(static_cast<size_t>(target));
                changed();
            }
            hover_part = -1;
            wire_here = -1;
        }
        if (io.MouseWheel != 0.0f) {
            if (hover_part >= 0 && circuit_.parts[static_cast<size_t>(hover_part)].kind == core::PartKind::Potentiometer) {
                core::Part &pot = circuit_.parts[static_cast<size_t>(hover_part)];
                double step = io.KeyShift ? 0.01 : 0.05;
                pot.setting = std::clamp(pot.setting + (io.MouseWheel > 0.0f ? step : -step), 0.0, 1.0);
            } else {
                // Zoom around the mouse: the point under it stays there.
                float before = view.pitch;
                zoom_ = std::clamp(zoom_ * (io.MouseWheel > 0.0f ? 1.15f : 1.0f / 1.15f), 0.4f, 4.0f);
                float after = base_pitch * s * zoom_;
                pan_.x += mgx * (before - after);
                pan_.y += mgy * (before - after);
            }
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
            pan_.x += io.MouseDelta.x;
            pan_.y += io.MouseDelta.y;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !io.KeyCtrl) {
            fit_ask_ = true;
        }
        // The keys that replace the circuit come last: what was under the
        // mouse is no longer there.
        bool replaced = false;
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && target >= 0 &&
            static_cast<size_t>(target) < circuit_.parts.size()) {
            duplicate_part(static_cast<size_t>(target));
            replaced = true;
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            undo();
            replaced = true;
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
            redo();
            replaced = true;
        }
        if (replaced) {
            hover_part = -1;
            wire_here = -1;
            hover_tap = -1;
        }
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hover_part >= 0 && !placing_ && !wiring_) {
        drag_id_ = -1;
        toggle_or_edit(static_cast<size_t>(hover_part), true);
    } else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsKeyDown(ImGuiKey_Space) &&
               !io.WantTextInput) {
        // With the space bar held the mouse takes the whole sheet.
        panning_ = true;
    } else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const int clicked_wire = !placing_ && !wiring_ && !app_.cable_offered() && hover_tap < 0 && wire_here >= 0 && !pin_here
                                     ? wire_here
                                     : -1;
        selected_wire_ = -1;
        if (placing_ && place_entry_) {
            size_t index = circuit_.add_part(place_entry_->kind, g.x, g.y, place_rotation_);
            if (place_entry_->value > 0.0) {
                circuit_.parts[index].value = place_entry_->value;
            }
            if (place_entry_->model) {
                circuit_.parts[index].model = place_entry_->name;
            }
            selected_id_ = circuit_.parts[index].id;
            placing_ = io.KeyShift;   // shift places one more
            changed();
        } else if (app_.cable_offered()) {
            if (on_net) {
                plug_cable(g);
            }
        } else if (hover_tap >= 0) {
            // Takes the cable out: it hangs from its instrument again.
            app_.select_port(App::circuit_port_base + circuit_.taps[static_cast<size_t>(hover_tap)].slot);
        } else if (wiring_) {
            if (g != wire_from_) {
                if (g.x != wire_from_.x && g.y != wire_from_.y) {
                    core::GridPoint corner{g.x, wire_from_.y};
                    add_wire(wire_from_, corner);
                    add_wire(corner, g);
                } else {
                    add_wire(wire_from_, g);
                }
            }
            // Ends on a pin or a wire; goes on from an empty point.
            wiring_ = !(on_net || g == wire_from_);
            wire_from_ = g;
            changed();
        } else if (on_net) {
            // A click on a wire selects it, and a new wire starts there.
            wiring_ = true;
            wire_from_ = g;
            selected_wire_ = clicked_wire;
            if (clicked_wire >= 0) {
                selected_id_ = -1;
                clear_block();
            }
        } else if (hover_part >= 0 && io.KeyShift) {
            // Shift adds the part to the block, or takes it out.
            const int id = circuit_.parts[static_cast<size_t>(hover_part)].id;
            if (block_.empty() && part_index(selected_id_) >= 0 && selected_id_ != id) {
                block_.push_back(selected_id_);
            }
            if (in_block(id)) {
                block_.erase(std::remove(block_.begin(), block_.end(), id), block_.end());
            } else {
                block_.push_back(id);
            }
            selected_id_ = -1;
        } else if (hover_part >= 0 && in_block(circuit_.parts[static_cast<size_t>(hover_part)].id)) {
            block_drag_ = true;
            block_moved_ = false;
            drag_grid_ = g;
            drag_circuit_ = circuit_;
            drag_wires_ = block_wires_;
        } else if (hover_part >= 0) {
            clear_block();
            selected_id_ = circuit_.parts[static_cast<size_t>(hover_part)].id;
            drag_id_ = selected_id_;
            drag_moved_ = false;
            drag_grid_ = g;
            drag_circuit_ = circuit_;
        } else {
            // On the empty sheet: a rectangle that selects what it holds.
            selected_id_ = -1;
            clear_block();
            banding_ = true;
            band_x_ = mgx;
            band_y_ = mgy;
        }
    }
    if (banding_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        banding_ = false;
        select_block(band_x_, band_y_, mgx, mgy);
    }
    if (block_drag_) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (g != drag_grid_ || block_moved_) {
                circuit_ = drag_circuit_;
                block_wires_ = drag_wires_;
                move_block(g.x - drag_grid_.x, g.y - drag_grid_.y);
                block_moved_ = true;
                nets_ = circuit_.nets();
            }
        } else {
            block_drag_ = false;
            if (block_moved_) {
                // The block stays selected after the move.
                std::vector<int> parts = block_;
                std::vector<char> wires = block_wires_;
                changed();
                block_ = std::move(parts);
                block_wires_ = std::move(wires);
            }
        }
    }
    if (drag_id_ >= 0) {
        int index = part_index(drag_id_);
        if (index < 0) {
            drag_id_ = -1;
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (g != drag_grid_ || drag_moved_) {
                circuit_ = drag_circuit_;
                move_part(static_cast<size_t>(part_index(drag_id_)), g.x - drag_grid_.x, g.y - drag_grid_.y);
                drag_moved_ = true;
                nets_ = circuit_.nets();
            }
        } else {
            if (drag_moved_) {
                changed();
            } else {
                toggle_or_edit(static_cast<size_t>(index), false);
            }
            drag_id_ = -1;
        }
    }
    if (panning_) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            pan_.x += io.MouseDelta.x;
            pan_.y += io.MouseDelta.y;
        } else {
            panning_ = false;
        }
    }
    view.pitch = base_pitch * s * zoom_;
    view.origin = ImVec2(inner_min.x + pan_.x, inner_min.y + pan_.y);

    // ---- the schematic ----
    draw->PushClipRect(inner_min, inner_max, true);
    if (view.pitch >= 8.0f * s) {
        int gx0 = static_cast<int>(std::floor((inner_min.x - view.origin.x) / view.pitch));
        int gx1 = static_cast<int>(std::ceil((inner_max.x - view.origin.x) / view.pitch));
        int gy0 = static_cast<int>(std::floor((inner_min.y - view.origin.y) / view.pitch));
        int gy1 = static_cast<int>(std::ceil((inner_max.y - view.origin.y) / view.pitch));
        for (int y = gy0; y <= gy1; y++) {
            for (int x = gx0; x <= gx1; x++) {
                ImVec2 p = view.at(core::GridPoint{x, y});
                draw->AddRectFilled(ImVec2(p.x - 0.5f * s, p.y - 0.5f * s), ImVec2(p.x + 0.5f * s, p.y + 0.5f * s), t.graticule);
            }
        }
    }
    const float line_w = std::max(1.0f, 1.6f * s * std::sqrt(zoom_));
    const bool have_nets = nets_.wire_net.size() == circuit_.wires.size() &&
                           nets_.pin_net.size() == circuit_.parts.size() * core::max_part_pins;
    for (size_t w = 0; w < circuit_.wires.size(); w++) {
        const core::CircuitWire &wire = circuit_.wires[w];
        uint32_t colour = have_nets ? volts_colour(net_volts(nets_.wire_net[w])) : t.readout_dim;
        if ((static_cast<int>(w) == wire_here && !wiring_ && !placing_) || static_cast<int>(w) == selected_wire_ ||
            (w < block_wires_.size() && block_wires_[w])) {
            colour = t.led_warn;
        }
        draw->AddLine(view.at(wire.a), view.at(wire.b), colour, line_w * 1.2f);
        // A dot where three or more ends meet.
        for (const core::GridPoint &end : {wire.a, wire.b}) {
            int meeting = 0;
            for (const core::CircuitWire &other : circuit_.wires) {
                if (other.a == end || other.b == end) {
                    meeting++;
                } else if (core::point_on_segment(end, other.a, other.b)) {
                    meeting += 2;
                }
            }
            for (const core::Part &part : circuit_.parts) {
                for (int j = 0; j < core::part_def(part.kind).pins; j++) {
                    meeting += core::pin_position(part, j) == end ? 1 : 0;
                }
            }
            if (meeting >= 3) {
                draw->AddCircleFilled(view.at(end), 2.6f * s * std::sqrt(zoom_), colour, 12);
            }
        }
    }
    // How many pins and wires each net has: a pin alone on its net is
    // connected to nothing, and is marked.
    std::vector<int> members(static_cast<size_t>(std::max(nets_.count, 0)), 0);
    if (have_nets) {
        for (int net : nets_.pin_net) {
            if (net >= 0) {
                members[static_cast<size_t>(net)]++;
            }
        }
        for (int net : nets_.wire_net) {
            members[static_cast<size_t>(net)] += 2;
        }
        for (int net : nets_.tap_net) {
            if (net >= 0 && static_cast<size_t>(net) < members.size()) {
                members[static_cast<size_t>(net)]++;   // a cable of the bench is a connection too
            }
        }
    }
    for (size_t i = 0; i < circuit_.parts.size(); i++) {
        const core::Part &part = circuit_.parts[i];
        const core::PartDef &def = core::part_def(part.kind);
        uint32_t colour = part.id == selected_id_ || in_block(part.id)
                              ? t.led_warn
                              : (static_cast<int>(i) == hover_part ? t.key_text : t.readout);
        Pen pen{draw, view, &part, colour, line_w};
        const core::CatalogEntry &entry = core::catalog_entry(part);
        float glow = 0.0f;
        if (part.kind == core::PartKind::Led && have_nets && entry.led_is > 0.0) {
            // The current of the diode model from the voltage across it.
            double across = net_volts(nets_.pin_net[i * core::max_part_pins]) - net_volts(nets_.pin_net[i * core::max_part_pins + 1]);
            double amps = entry.led_is * (std::exp(std::clamp(across, 0.0, 4.0) / (entry.led_n * 0.02585)) - 1.0);
            glow = static_cast<float>(std::clamp(amps / 0.01, 0.0, 1.0));
        }
        draw_symbol(pen, glow, glow_colour(entry.glow));
        if (part.kind == core::PartKind::Bbd && view.pitch >= 7.0f * s) {
            draw_pin_names(pen, t.label_dim);
        }
        for (int j = 0; j < def.pins; j++) {
            ImVec2 at = view.at(core::pin_position(part, j));
            draw->AddCircleFilled(at, 1.8f * s * std::sqrt(zoom_), colour, 10);
            int net = have_nets ? nets_.pin_net[i * core::max_part_pins + static_cast<size_t>(j)] : -1;
            if (net >= 0 && members[static_cast<size_t>(net)] == 1) {
                draw->AddCircle(at, 4.5f * s * std::sqrt(zoom_), t.led_stop, 14, 1.4f * s);   // not connected
            }
        }
        // Reference and value.
        std::string text = circuit_.reference(part);
        if (def.default_value > 0.0) {
            text += " " + core::format_value(part.value, def.unit);
        }
        if (entry.model) {
            text += std::string(" ") + entry.name;
        }
        if (part.kind == core::PartKind::VSine) {
            text += " " + core::format_value(part.value2, "Hz");
        }
        if (have_nets && view.pitch >= 7.0f * s &&
            (part.kind == core::PartKind::Voltmeter || part.kind == core::PartKind::Ammeter)) {
            // The reading, under the meter.
            std::string reading;
            if (part.kind == core::PartKind::Voltmeter) {
                reading = reading_text(net_volts(nets_.pin_net[i * core::max_part_pins]) -
                                           net_volts(nets_.pin_net[i * core::max_part_pins + 1]),
                                       "V");
            } else {
                double amps = 0.0;
                vector_value(core::current_vector(part), amps);
                reading = reading_text(amps, "A");
            }
            place_text(draw, pen.at(0.0f, 1.25f), pen.at(0.0f, 0.0f), reading.c_str(), t.led_warn);
        }
        if (part.kind == core::PartKind::Potentiometer) {
            char position[16];
            std::snprintf(position, sizeof(position), " %d%%", static_cast<int>(std::lround(part.setting * 100.0)));
            text += position;
        }
        if (!text.empty() && view.pitch >= 7.0f * s) {
            float ax = 0.0f;
            float ay = 0.0f;
            text_anchor(part.kind, ax, ay);
            place_text(draw, pen.at(ax, ay), pen.at(0.0f, 0.0f), text.c_str(), t.readout);
        }
    }
    if (banding_) {
        draw->AddRect(view.at(band_x_, band_y_), view.at(mgx, mgy), t.led_warn, 0.0f, 0, 1.2f * s);
    }
    // The cables plugged in.
    ImFont *small = ui::fonts().small;
    for (size_t k = 0; k < circuit_.taps.size(); k++) {
        const core::Tap &tap = circuit_.taps[k];
        const int port = App::circuit_port_base + tap.slot;
        ImVec2 point = view.at(tap.at);
        ImVec2 marker = tap_marker(k);
        uint32_t colour = app_.port_wire_colour(port);
        if (colour == 0) {
            colour = t.label_dim;
        }
        draw->AddLine(point, marker, colour, 1.2f * s);
        draw->AddCircleFilled(point, 2.4f * s, colour, 12);
        draw->AddCircleFilled(marker, 6.0f * s, t.chassis_shadow, 20);
        draw->AddCircleFilled(marker, 4.5f * s, colour, 20);
        if (static_cast<int>(k) == hover_tap) {
            draw->AddCircle(marker, 8.0f * s, t.led_warn, 20, 1.5f * s);
        }
        InstrumentId wired{Instrument::Scope, 0};
        int channel = 0;
        if (app_.port_wired_to(port, wired, channel)) {
            const InstrumentBase *target = app_.instrument(wired);
            std::string name = instrument_label(wired) + " " + (target ? target->channel_name(channel) : std::to_string(channel + 1));
            draw->AddText(small, small->FontSize, ImVec2(marker.x + 8.0f * s, marker.y - small->FontSize * 0.5f), colour,
                          name.c_str());
        }
        app_.anchor_port(port, window, marker.x, marker.y);
    }
    // The tool in hand.
    if (hovered && placing_ && place_entry_) {
        core::Part ghost;
        ghost.kind = place_entry_->kind;
        ghost.x = g.x;
        ghost.y = g.y;
        ghost.rotation = place_rotation_;
        Pen pen{draw, view, &ghost, t.led_warn, line_w};
        draw_symbol(pen, 0.0f, 0);
    }
    if (wiring_) {
        ImVec2 from = view.at(wire_from_);
        ImVec2 corner = view.at(core::GridPoint{g.x, wire_from_.y});
        ImVec2 to = view.at(g);
        draw->AddLine(from, corner, t.led_warn, line_w);
        draw->AddLine(corner, to, t.led_warn, line_w);
    }
    if (hovered && near_point && (on_net || app_.cable_offered()) && !placing_) {
        draw->AddCircle(view.at(g), 5.0f * s, on_net ? t.led_warn : t.label_dim, 16, 1.5f * s);
    }
    draw->PopClipRect();

    // The context menus.
    push_dialog_style(s);
    ImGui::PushFont(ui::fonts().panel);
    if (ImGui::BeginPopup("##part-menu")) {
        int index = part_index(selected_id_);
        if (index < 0) {
            ImGui::CloseCurrentPopup();
        } else {
            if (ImGui::MenuItem("Rotate", "R")) {
                rotate_part(static_cast<size_t>(index));
                changed();
            }
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
                duplicate_part(static_cast<size_t>(index));
            }
            if (core::part_def(circuit_.parts[static_cast<size_t>(index)].kind).default_value > 0.0 &&
                ImGui::MenuItem("Edit value", "double click")) {
                toggle_or_edit(static_cast<size_t>(index), true);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Remove", "Del")) {
                remove_part(static_cast<size_t>(index));
                changed();
            }
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##wire-menu")) {
        if (ImGui::MenuItem("Remove wire", "Del") && menu_wire_ >= 0 &&
            static_cast<size_t>(menu_wire_) < circuit_.wires.size()) {
            circuit_.wires.erase(circuit_.wires.begin() + menu_wire_);
            changed();
        }
        ImGui::EndPopup();
    }
    ImGui::PopFont();
    pop_dialog_style();

    // What the status line says about the point under the mouse.
    hover_text_.clear();
    if (hovered && on_net && have_nets) {
        int net = circuit_.net_at(nets_, g);
        if (net >= 0) {
            char text[64];
            std::snprintf(text, sizeof(text), "%s  %.3f V", nets_.name[static_cast<size_t>(net)].c_str(), net_volts(net));
            hover_text_ = text;
        }
    }
}

// The value of a part, typed as on a schematic ("4k7", "100n").
void CircuitBench::draw_value_editor(float s)
{
    const ui::Theme &t = ui::current_theme();
    if (edit_open_) {
        edit_open_ = false;
        ImGui::SetNextWindowPos(ImGui::GetIO().MousePos);
        ImGui::OpenPopup("##value");
    }
    ImGui::PushStyleColor(ImGuiCol_PopupBg, t.screen);
    ImGui::PushStyleColor(ImGuiCol_Border, t.chassis_edge);
    ImGui::PushStyleColor(ImGuiCol_Text, t.readout);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, t.key);
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, t.key_hover);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * s, 8.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f * s);
    if (ImGui::BeginPopup("##value")) {
        ImGui::PushFont(ui::fonts().mono);
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(110.0f * s);
        if (ImGui::InputText("##text", edit_text_, sizeof(edit_text_),
                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            int index = part_index(edit_id_);
            double value = 0.0;
            if (index >= 0 && core::parse_value(edit_text_, value)) {
                circuit_.parts[static_cast<size_t>(index)].value = value;
                changed();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopFont();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(5);
}

// The ADD dialog: the catalog. Categories on the left, the parts of the
// chosen one on the right, a search over names and descriptions on top.
// The part clicked goes in hand and is placed with a click on the schematic.
void CircuitBench::draw_add_dialog(float s)
{
    const ui::Theme &t = ui::current_theme();
    if (add_ask_) {
        add_ask_ = false;
        add_search_[0] = '\0';
        ImGui::SetNextWindowPos(ImGui::GetIO().MousePos);
        ImGui::OpenPopup("##add-part");
    }
    push_dialog_style(s);
    ImGui::SetNextWindowSize(ImVec2(800.0f * s, 460.0f * s));
    if (ImGui::BeginPopup("##add-part")) {
        const core::CatalogEntry *chosen = nullptr;
        const core::CatalogEntry *first = nullptr;
        const core::CatalogEntry *pointed = nullptr;
        ImGui::PushFont(ui::fonts().panel_bold);
        ImGui::TextUnformatted("ADD A PART");
        ImGui::PopFont();
        ImGui::PushFont(ui::fonts().mono);
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1.0f);
        bool enter = ImGui::InputTextWithHint("##search", "search", add_search_, sizeof(add_search_),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopFont();
        const bool searching = add_search_[0] != '\0';

        ImGui::BeginChild("##categories", ImVec2(150.0f * s, 0.0f), ImGuiChildFlags_Borders);
        ImGui::PushFont(ui::fonts().panel);
        if (ImGui::Selectable("All", add_category_.empty())) {
            add_category_.clear();
        }
        const char *previous = "";
        for (const core::CatalogEntry &entry : core::catalog()) {
            if (std::strcmp(previous, entry.category) != 0) {
                previous = entry.category;
                if (ImGui::Selectable(entry.category, add_category_ == entry.category)) {
                    add_category_ = entry.category;
                }
            }
        }
        ImGui::PopFont();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##parts", ImVec2(-190.0f * s, 0.0f), ImGuiChildFlags_Borders);
        for (const core::CatalogEntry &entry : core::catalog()) {
            // A search looks in every category.
            bool shown = searching ? (contains_nocase(entry.name, add_search_) || contains_nocase(entry.description, add_search_) ||
                                      contains_nocase(entry.category, add_search_))
                                   : (add_category_.empty() || add_category_ == entry.category);
            if (!shown) {
                continue;
            }
            if (!first) {
                first = &entry;
            }
            ImGui::PushFont(ui::fonts().mono);
            ImGui::PushID(entry.name);
            if (ImGui::Selectable("##pick", false, ImGuiSelectableFlags_SpanAllColumns)) {
                chosen = &entry;
            }
            if (ImGui::IsItemHovered()) {
                pointed = &entry;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(entry.name);
            ImGui::PopID();
            ImGui::PopFont();
            ImGui::SameLine(150.0f * s);
            ImGui::PushFont(ui::fonts().small);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(t.readout_dim), "%s", entry.description);
            ImGui::PopFont();
        }
        if (!first) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(t.readout_dim), "no part matches");
        }
        ImGui::EndChild();
        // The part under the mouse (else the first of the list): its symbol
        // and what it is.
        ImGui::SameLine();
        ImGui::BeginChild("##preview", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
        if (const core::CatalogEntry *shown = pointed ? pointed : first) {
            const core::PartDef &def = core::part_def(shown->kind);
            ImVec2 corner = ImGui::GetCursorScreenPos();
            float width = ImGui::GetContentRegionAvail().x;
            float height = 130.0f * s;
            View preview;
            preview.pitch = 15.0f * s;
            float mid_x = static_cast<float>(def.box_min.x + def.box_max.x) * 0.5f;
            float mid_y = static_cast<float>(def.box_min.y + def.box_max.y) * 0.5f;
            preview.origin = ImVec2(corner.x + width * 0.5f - mid_x * preview.pitch, corner.y + height * 0.5f - mid_y * preview.pitch);
            core::Part ghost;
            ghost.kind = shown->kind;
            Pen pen{ImGui::GetWindowDrawList(), preview, &ghost, t.readout, 1.6f * s};
            draw_symbol(pen, 0.0f, 0);
            for (int j = 0; j < def.pins; j++) {
                ImGui::GetWindowDrawList()->AddCircleFilled(preview.at(core::pin_position(ghost, j)), 2.2f * s, t.led_warn, 10);
            }
            ImGui::Dummy(ImVec2(width, height));
            ImGui::PushFont(ui::fonts().panel_bold);
            ImGui::TextUnformatted(shown->name);
            ImGui::PopFont();
            ImGui::PushFont(ui::fonts().small);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(t.readout_dim), "%s", shown->description);
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(t.readout_dim), "%s  %d pins", def.name, def.pins);
            double value = shown->value > 0.0 ? shown->value : def.default_value;
            if (value > 0.0) {
                ImGui::Text("%s", core::format_value(value, def.unit).c_str());
            }
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(t.readout_dim), "%s", shown->model ? "SPICE model" : "generic model");
            ImGui::PopFont();
        }
        ImGui::EndChild();
        if (enter && first) {
            chosen = first;   // Enter takes the first of the list
        }
        if (chosen) {
            place_entry_ = chosen;
            placing_ = true;
            place_rotation_ = 0;
            wiring_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    pop_dialog_style();
}

// The properties of the selected part, on the right of the schematic: what
// it is, what can be changed (model, value, position) and what it is doing
// now (the voltage at each pin, the current and the power where they follow).
void CircuitBench::draw_properties(ui::Window &window, ImVec2 min, ImVec2 max)
{
    const ui::Theme &t = ui::current_theme();
    const float s = window.scale();
    int index = part_index(selected_id_);
    if (index < 0 || max.x <= min.x || max.y <= min.y) {
        return;
    }
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, max, t.screen_bezel, 10.0f * s);
    draw->AddRectFilled(ImVec2(min.x + 4.0f * s, min.y + 4.0f * s), ImVec2(max.x - 4.0f * s, max.y - 4.0f * s), t.screen, 6.0f * s);
    push_dialog_style(s);
    ImGui::SetCursorScreenPos(ImVec2(min.x + 6.0f * s, min.y + 6.0f * s));
    ImGui::BeginChild("##properties", ImVec2(max.x - min.x - 12.0f * s, max.y - min.y - 12.0f * s), ImGuiChildFlags_AlwaysUseWindowPadding);
    core::Part &part = circuit_.parts[static_cast<size_t>(index)];
    const core::PartDef &def = core::part_def(part.kind);
    const core::CatalogEntry &entry = core::catalog_entry(part);
    const ImVec4 dim = ImGui::ColorConvertU32ToFloat4(t.readout_dim);
    bool remove = false;

    ImGui::PushFont(ui::fonts().panel_bold);
    std::string reference = circuit_.reference(part);
    ImGui::TextUnformatted(reference.empty() ? def.name : reference.c_str());
    ImGui::PopFont();
    ImGui::PushFont(ui::fonts().small);
    ImGui::TextColored(dim, "%s", entry.description);
    ImGui::PopFont();
    ImGui::Spacing();

    ImGui::PushFont(ui::fonts().mono);
    // The model: any part of the catalog of the same kind.
    int same_kind = 0;
    for (const core::CatalogEntry &other : core::catalog()) {
        same_kind += other.kind == part.kind ? 1 : 0;
    }
    if (same_kind > 1) {
        ImGui::PushFont(ui::fonts().small);
        ImGui::TextColored(dim, "PART");
        ImGui::PopFont();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##model", entry.name)) {
            for (const core::CatalogEntry &other : core::catalog()) {
                if (other.kind == part.kind && ImGui::Selectable(other.name, &other == &entry)) {
                    part.model = other.model ? other.name : "";
                    changed();
                }
            }
            ImGui::EndCombo();
        }
    }
    if (def.default_value > 0.0) {
        if (prop_id_ != part.id) {
            prop_id_ = part.id;
            std::snprintf(prop_value_, sizeof(prop_value_), "%s", core::format_value(part.value, def.unit).c_str());
        }
        ImGui::PushFont(ui::fonts().small);
        ImGui::TextColored(dim, "VALUE");
        ImGui::PopFont();
        ImGui::SetNextItemWidth(-1.0f);
        bool enter = ImGui::InputText("##value", prop_value_, sizeof(prop_value_), ImGuiInputTextFlags_EnterReturnsTrue);
        if (enter || ImGui::IsItemDeactivatedAfterEdit()) {
            double value = 0.0;
            if (core::parse_value(prop_value_, value) && value != part.value) {
                part.value = value;
                changed();
            }
            prop_id_ = -1;   // shown again as the bench writes it
        } else if (!ImGui::IsItemActive()) {
            std::snprintf(prop_value_, sizeof(prop_value_), "%s", core::format_value(part.value, def.unit).c_str());
        }
    }
    if (part.kind == core::PartKind::VSine) {
        ImGui::PushFont(ui::fonts().small);
        ImGui::TextColored(dim, "FREQUENCY");
        ImGui::PopFont();
        ImGui::SetNextItemWidth(-1.0f);
        bool enter = ImGui::InputText("##frequency", prop_value2_, sizeof(prop_value2_), ImGuiInputTextFlags_EnterReturnsTrue);
        if (enter || ImGui::IsItemDeactivatedAfterEdit()) {
            double value = 0.0;
            if (core::parse_value(prop_value2_, value)) {
                part.value2 = value;   // the wave follows at the next frame: no new netlist
            }
        } else if (!ImGui::IsItemActive()) {
            std::snprintf(prop_value2_, sizeof(prop_value2_), "%s", core::format_value(part.value2, "Hz").c_str());
        }
    }
    if (part.kind == core::PartKind::Potentiometer) {
        ImGui::PushFont(ui::fonts().small);
        ImGui::TextColored(dim, "POSITION");
        ImGui::PopFont();
        float percent = static_cast<float>(part.setting * 100.0);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##position", &percent, 0.0f, 100.0f, "%.0f %%")) {
            part.setting = std::clamp(static_cast<double>(percent) / 100.0, 0.0, 1.0);
        }
    }
    if (part.kind == core::PartKind::Switch) {
        bool closed = part.setting >= 0.5;
        if (ImGui::Checkbox("closed", &closed)) {
            part.setting = closed ? 1.0 : 0.0;
        }
    }
    ImGui::Spacing();
    float half = (ImGui::GetContentRegionAvail().x - 6.0f * s) * 0.5f;
    if (ImGui::Button("ROTATE", ImVec2(half, 0.0f))) {
        rotate_part(static_cast<size_t>(index));
        changed();
    }
    ImGui::SameLine();
    if (ImGui::Button("REMOVE", ImVec2(half, 0.0f))) {
        remove = true;
    }

    // What the part is doing now.
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    const bool have_nets = nets_.pin_net.size() == circuit_.parts.size() * core::max_part_pins;
    if (have_nets) {
        double volts[core::max_part_pins] = {};
        for (int j = 0; j < def.pins; j++) {
            int net = nets_.pin_net[static_cast<size_t>(index) * core::max_part_pins + static_cast<size_t>(j)];
            volts[j] = net_volts(net);
            ImGui::TextColored(dim, "%-4s", def.pin_name[j]);
            ImGui::SameLine(60.0f * s);
            ImGui::Text("%9.4f V", volts[j]);
        }
        if (def.pins == 2) {
            ImGui::TextColored(dim, "A-B");
            ImGui::SameLine(60.0f * s);
            ImGui::Text("%9.4f V", volts[0] - volts[1]);
        }
        if (part.kind == core::PartKind::Resistor && part.value > 0.0) {
            double amps = (volts[0] - volts[1]) / part.value;
            ImGui::TextColored(dim, "I");
            ImGui::SameLine(60.0f * s);
            ImGui::Text("%9.4f mA", amps * 1e3);
            ImGui::TextColored(dim, "P");
            ImGui::SameLine(60.0f * s);
            ImGui::Text("%9.4f mW", amps * (volts[0] - volts[1]) * 1e3);
        }
        double through = 0.0;
        if (vector_value(core::current_vector(part), through)) {
            // A source or an ammeter: the simulator has its current.
            ImGui::TextColored(dim, "I");
            ImGui::SameLine(60.0f * s);
            ImGui::Text("%9.4f mA", through * 1e3);
        }
        if (part.kind == core::PartKind::Nmos || part.kind == core::PartKind::Pmos) {
            ImGui::TextColored(dim, "Vgs");
            ImGui::SameLine(60.0f * s);
            ImGui::Text("%9.4f V", volts[1] - volts[2]);
            ImGui::TextColored(dim, "Vds");
            ImGui::SameLine(60.0f * s);
            ImGui::Text("%9.4f V", volts[0] - volts[2]);
        }
        if (part.kind == core::PartKind::Npn || part.kind == core::PartKind::Pnp) {
            ImGui::TextColored(dim, "Vbe");
            ImGui::SameLine(60.0f * s);
            ImGui::Text("%9.4f V", volts[1] - volts[2]);
            ImGui::TextColored(dim, "Vce");
            ImGui::SameLine(60.0f * s);
            ImGui::Text("%9.4f V", volts[0] - volts[2]);
        }
    }
    ImGui::PopFont();
    ImGui::EndChild();
    pop_dialog_style();
    if (remove) {
        remove_part(static_cast<size_t>(index));
        changed();
    }
}

// The corner that sizes the window: the schematic takes the room it is given.
void CircuitBench::resize_grip(ui::Window &window, ImVec2 corner)
{
    const ui::Theme &t = ui::current_theme();
    const float s = window.scale();
    const float size = 14.0f * s;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    ImVec2 min(corner.x - size, corner.y - size);
    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton("##resize", ImVec2(size, size));
    bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    for (int k = 1; k <= 3; k++) {
        float d = size * static_cast<float>(k) / 4.0f;
        draw->AddLine(ImVec2(corner.x - d, corner.y - 1.0f * s), ImVec2(corner.x - 1.0f * s, corner.y - d),
                      hot ? t.led_warn : t.label_dim, 1.4f * s);
    }
    int cx = 0;
    int cy = 0;
    if (!ui::platform_cursor(cx, cy)) {
        return;
    }
    if (ImGui::IsItemActivated()) {
        window.size(grip_w_, grip_h_);
        grip_x_ = cx;
        grip_y_ = cy;
    }
    if (ImGui::IsItemActive()) {
        window.set_size(std::max(700, grip_w_ + cx - grip_x_), std::max(420, grip_h_ + cy - grip_y_));
    }
}

// ---- settings -------------------------------------------------------------

void CircuitBench::save(nlohmann::json &out) const
{
    out["parts"] = nlohmann::json::array();
    for (const core::Part &part : circuit_.parts) {
        out["parts"].push_back({{"id", part.id},
                                {"number", part.number},
                                {"kind", core::part_def(part.kind).name},
                                {"x", part.x},
                                {"y", part.y},
                                {"rotation", part.rotation},
                                {"value", part.value},
                                {"value2", part.value2},
                                {"setting", part.setting},
                                {"model", part.model}});
    }
    out["wires"] = nlohmann::json::array();
    for (const core::CircuitWire &w : circuit_.wires) {
        out["wires"].push_back({w.a.x, w.a.y, w.b.x, w.b.y});
    }
    out["taps"] = nlohmann::json::array();
    for (const core::Tap &tap : circuit_.taps) {
        out["taps"].push_back({{"slot", tap.slot}, {"x", tap.at.x}, {"y", tap.at.y}});
    }
    // The cables: which channel of which instrument is in each tap.
    out["cables"] = nlohmann::json::array();
    for (const core::Tap &tap : circuit_.taps) {
        InstrumentId wired{Instrument::Scope, 0};
        int channel = 0;
        if (app_.port_wired_to(App::circuit_port_base + tap.slot, wired, channel)) {
            out["cables"].push_back({{"slot", tap.slot},
                                     {"instrument", instrument_name(wired.kind)},
                                     {"instance", wired.instance},
                                     {"channel", channel}});
        }
    }
    out["project"] = project_;
    out["selected"] = selected_id_;
    out["zoom"] = zoom_;
    out["pan"] = {pan_.x, pan_.y};
    if (last_w_ > 0 && last_h_ > 0) {
        out["size"] = {last_w_, last_h_};
    }
}

void CircuitBench::load(const nlohmann::json &in)
{
    if (in.contains("parts") && in["parts"].is_array()) {
        for (const nlohmann::json &j : in["parts"]) {
            core::Part part;
            if (!j.is_object() || !core::part_kind_from_name(j.value("kind", ""), part.kind)) {
                continue;
            }
            part.id = j.value("id", 0);
            part.number = j.value("number", 1);
            part.x = j.value("x", 0);
            part.y = j.value("y", 0);
            part.rotation = j.value("rotation", 0) & 3;
            part.value = j.value("value", core::part_def(part.kind).default_value);
            part.setting = std::clamp(j.value("setting", 0.5), 0.0, 1.0);
            part.model = j.value("model", "");
            part.value2 = j.value("value2", part.kind == core::PartKind::VSine ? 1000.0 : 0.0);
            if (part.id > 0 && !circuit_.part_by_id(part.id)) {
                circuit_.parts.push_back(part);
            }
        }
    }
    if (in.contains("wires") && in["wires"].is_array()) {
        for (const nlohmann::json &j : in["wires"]) {
            if (j.is_array() && j.size() == 4 && j[0].is_number_integer() && j[1].is_number_integer() &&
                j[2].is_number_integer() && j[3].is_number_integer()) {
                add_wire(core::GridPoint{j[0].get<int>(), j[1].get<int>()}, core::GridPoint{j[2].get<int>(), j[3].get<int>()});
            }
        }
    }
    if (in.contains("taps") && in["taps"].is_array()) {
        for (const nlohmann::json &j : in["taps"]) {
            int slot = j.is_object() ? j.value("slot", -1) : -1;
            bool taken = false;
            for (const core::Tap &tap : circuit_.taps) {
                taken = taken || tap.slot == slot;
            }
            if (slot >= 0 && slot < 1000 && !taken) {
                circuit_.taps.push_back(core::Tap{slot, core::GridPoint{j.value("x", 0), j.value("y", 0)}});
                app_.declare_circuit_port(slot, "TP" + std::to_string(slot + 1));
            }
        }
    }
    // The cables go back in, into the instruments that are open.
    if (in.contains("cables") && in["cables"].is_array()) {
        for (const nlohmann::json &j : in["cables"]) {
            if (!j.is_object()) {
                continue;
            }
            const std::string name = j.value("instrument", "");
            const int slot = j.value("slot", -1);
            for (int k = 0; k <= static_cast<int>(Instrument::Audio); k++) {
                InstrumentId id{static_cast<Instrument>(k), j.value("instance", 0)};
                bool tapped = false;
                for (const core::Tap &tap : circuit_.taps) {
                    tapped = tapped || tap.slot == slot;
                }
                if (tapped && name == instrument_name(id.kind) && id.kind != Instrument::Circuit && app_.instrument(id)) {
                    app_.rewire(id, j.value("channel", 0), App::circuit_port_base + slot);
                }
            }
        }
    }
    project_ = in.value("project", project_);
    selected_id_ = in.value("selected", -1);
    zoom_ = std::clamp(in.value("zoom", 1.0f), 0.4f, 4.0f);
    if (in.contains("pan") && in["pan"].is_array() && in["pan"].size() == 2 && in["pan"][0].is_number() &&
        in["pan"][1].is_number()) {
        pan_ = ImVec2(in["pan"][0].get<float>(), in["pan"][1].get<float>());
    }
    if (in.contains("size") && in["size"].is_array() && in["size"].size() == 2 && in["size"][0].is_number_integer() &&
        in["size"][1].is_number_integer()) {
        want_w_ = std::max(700, in["size"][0].get<int>());
        want_h_ = std::max(420, in["size"][1].get<int>());
    }
    changed();
}

}  // namespace app
