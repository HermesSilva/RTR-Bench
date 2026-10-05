// SPDX-License-Identifier: Apache-2.0
#include "ui/chassis.h"

#include <algorithm>
#include <cstdint>

#include "ui/fonts.h"
#include "ui/theme.h"
#include "ui/window.h"

namespace ui {

namespace {

ImVec2 operator+(ImVec2 a, ImVec2 b) { return {a.x + b.x, a.y + b.y}; }
ImVec2 operator-(ImVec2 a, ImVec2 b) { return {a.x - b.x, a.y - b.y}; }

uint32_t with_alpha(uint32_t colour, uint32_t alpha)
{
    return (colour & 0x00FFFFFFu) | (alpha << 24);
}

// A soft shadow under the chassis: concentric rounded rectangles fading out.
void draw_shadow(ImDrawList *draw, ImVec2 min, ImVec2 max, float corner, float spread)
{
    const int steps = 6;
    for (int i = steps; i >= 1; i--) {
        float grow = spread * static_cast<float>(i) / static_cast<float>(steps);
        uint32_t alpha = static_cast<uint32_t>(26.0f * (1.0f - static_cast<float>(i) / (steps + 1.0f)));
        draw->AddRectFilled(min - ImVec2(grow, grow * 0.6f), max + ImVec2(grow, grow * 1.4f),
                            with_alpha(0, alpha), corner + grow);
    }
}

// A rack handle: a rounded bar with a lighter edge and a darker inside.
void draw_handle(ImDrawList *draw, ImVec2 min, ImVec2 max, const Theme &t, float scale)
{
    float radius = 6.0f * scale;
    draw->AddRectFilled(min, max, t.handle, radius);
    draw->AddRect(min, max, t.chassis_edge, radius, 0, 1.5f * scale);
    ImVec2 inner_min = min + ImVec2(5.0f * scale, 5.0f * scale);
    ImVec2 inner_max = max - ImVec2(5.0f * scale, 5.0f * scale);
    if (inner_max.y > inner_min.y) {
        draw->AddRectFilled(inner_min, inner_max, t.chassis_shadow, radius * 0.5f);
    }
}

// Screw head: a small circle with a slot.
void draw_screw(ImDrawList *draw, ImVec2 centre, const Theme &t, float scale)
{
    float r = 4.0f * scale;
    draw->AddCircleFilled(centre, r, t.chassis_shadow, 16);
    draw->AddCircleFilled(centre, r - 1.0f * scale, t.chassis_edge, 16);
    draw->AddLine(centre - ImVec2(r * 0.6f, 0), centre + ImVec2(r * 0.6f, 0), t.chassis_shadow, 1.2f * scale);
}

// Window control drawn on the panel (close, minimize, stay on top). Returns
// true on click. A lit control shows its glyph in the run colour.
enum class Glyph : uint8_t { Close, Minimize, Pin };

bool panel_button(ImDrawList *draw, const char *id, ImVec2 centre, float radius,
                  uint32_t hover_colour, const Theme &t, Glyph kind, float scale, bool lit = false)
{
    ImGui::SetCursorScreenPos(centre - ImVec2(radius, radius));
    ImGui::InvisibleButton(id, ImVec2(radius * 2, radius * 2));
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked();
    draw->AddCircleFilled(centre, radius, hovered ? hover_colour : t.key, 24);
    draw->AddCircle(centre, radius, t.chassis_shadow, 24, 1.0f * scale);
    uint32_t glyph = lit ? t.led_run : (hovered ? t.key_text : t.label_dim);
    float g = radius * 0.42f;
    switch (kind) {
    case Glyph::Close:
        draw->AddLine(centre - ImVec2(g, g), centre + ImVec2(g, g), glyph, 1.6f * scale);
        draw->AddLine(centre - ImVec2(g, -g), centre + ImVec2(g, -g), glyph, 1.6f * scale);
        break;
    case Glyph::Minimize:
        draw->AddLine(centre - ImVec2(g, 0), centre + ImVec2(g, 0), glyph, 1.6f * scale);
        break;
    case Glyph::Pin:
        // A push pin: the head, its collar and the needle.
        draw->AddCircleFilled(centre - ImVec2(0, g * 0.55f), g * 0.62f, glyph, 14);
        draw->AddLine(centre + ImVec2(-g * 0.9f, g * 0.1f), centre + ImVec2(g * 0.9f, g * 0.1f), glyph, 1.6f * scale);
        draw->AddLine(centre + ImVec2(0, g * 0.1f), centre + ImVec2(0, g * 1.25f), glyph, 1.4f * scale);
        break;
    }
    return clicked;
}

}  // namespace

ChassisFrame begin_chassis(Window &window, const ChassisSpec &spec)
{
    const Theme &t = current_theme();
    const float s = window.scale();
    ImGuiViewport *viewport = ImGui::GetMainViewport();

    // One invisible, full-window ImGui container: no title bar, no background,
    // no padding. Everything visible is drawn on its draw list.
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##chassis", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);
    ImGui::PopStyleVar(3);

    // Drag to move: the whole window is a button, declared first so that any
    // control declared after it takes the mouse over it.
    ImGui::SetCursorScreenPos(viewport->Pos);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##move", viewport->Size, ImGuiButtonFlags_MouseButtonLeft);
    if (ImGui::IsItemActivated()) {
        window.begin_move();
    }
    if (ImGui::IsItemActive()) {
        window.move_with_cursor();
    }

    ImDrawList *draw = ImGui::GetWindowDrawList();
    const float margin = spec.margin * s;
    const float corner = spec.corner * s;
    const float handle_w = spec.handles ? 22.0f * s : 0.0f;
    const float handle_gap = spec.handles ? 6.0f * s : 0.0f;

    ImVec2 win_min = viewport->Pos;
    ImVec2 win_max = viewport->Pos + viewport->Size;
    ImVec2 body_min = win_min + ImVec2(margin + handle_w + handle_gap, margin);
    ImVec2 body_max = win_max - ImVec2(margin + handle_w + handle_gap, margin);

    // Shadow, body, bevel.
    draw_shadow(draw, body_min, body_max, corner, margin * 0.8f);
    draw->AddRectFilled(body_min, body_max, t.chassis, corner);
    draw->AddRect(body_min, body_max, t.chassis_shadow, corner, 0, 2.0f * s);
    draw->AddRect(body_min + ImVec2(2.0f * s, 2.0f * s), body_max - ImVec2(2.0f * s, 2.0f * s),
                  t.chassis_edge, corner - 2.0f * s, 0, 1.0f * s);

    if (spec.handles) {
        float top = body_min.y + corner;
        float bottom = body_max.y - corner;
        draw_handle(draw, ImVec2(win_min.x + margin, top), ImVec2(body_min.x - handle_gap, bottom), t, s);
        draw_handle(draw, ImVec2(body_max.x + handle_gap, top), ImVec2(win_max.x - margin, bottom), t, s);
    }

    // Feet.
    float foot_h = 6.0f * s;
    float foot_w = 48.0f * s;
    draw->AddRectFilled(ImVec2(body_min.x + corner, body_max.y - 1.0f * s),
                        ImVec2(body_min.x + corner + foot_w, body_max.y + foot_h - 1.0f * s), t.handle, 3.0f * s);
    draw->AddRectFilled(ImVec2(body_max.x - corner - foot_w, body_max.y - 1.0f * s),
                        ImVec2(body_max.x - corner, body_max.y + foot_h - 1.0f * s), t.handle, 3.0f * s);

    // Header strip: brand, model, title, window controls.
    const float header_h = 34.0f * s;
    ImVec2 header_min = body_min + ImVec2(corner * 0.6f, 8.0f * s);
    float text_y = header_min.y + (header_h - ImGui::GetTextLineHeight()) * 0.5f;
    ImFont *bold = fonts().panel_bold;
    draw->AddText(bold, bold->FontSize, ImVec2(header_min.x, text_y), t.brand, spec.brand.c_str());
    float x = header_min.x + bold->CalcTextSizeA(bold->FontSize, 1e9f, 0.0f, spec.brand.c_str()).x + 10.0f * s;
    if (!spec.model.empty()) {
        draw->AddText(ImVec2(x, text_y), t.label, spec.model.c_str());
        x += ImGui::CalcTextSize(spec.model.c_str()).x + 14.0f * s;
    }
    if (!spec.title.empty()) {
        draw->AddText(ImVec2(x, text_y), t.label_dim, spec.title.c_str());
    }

    float radius = 9.0f * s;
    ImVec2 close_c(body_max.x - corner * 0.6f - radius, header_min.y + header_h * 0.5f);
    ImVec2 min_c = close_c - ImVec2(radius * 2.0f + 8.0f * s, 0);
    ImVec2 pin_c = min_c - ImVec2(radius * 2.0f + 8.0f * s, 0);
    if (panel_button(draw, "##close", close_c, radius, t.close_hover, t, Glyph::Close, s)) {
        window.request_close();
    }
    if (panel_button(draw, "##minimize", min_c, radius, t.key_hover, t, Glyph::Minimize, s)) {
        window.minimize();
    }
    // Stay on top: the window keeps above every other one while the pin is lit.
    if (panel_button(draw, "##on-top", pin_c, radius, t.key_hover, t, Glyph::Pin, s, window.on_top())) {
        window.set_on_top(!window.on_top());
    }

    // Screws in the corners of the panel.
    float screw_in = corner * 0.55f;
    draw_screw(draw, body_min + ImVec2(screw_in, screw_in), t, s);
    draw_screw(draw, ImVec2(body_max.x - screw_in, body_min.y + screw_in), t, s);
    draw_screw(draw, ImVec2(body_min.x + screw_in, body_max.y - screw_in), t, s);
    draw_screw(draw, body_max - ImVec2(screw_in, screw_in), t, s);

    ChassisFrame frame;
    frame.draw = draw;
    frame.panel_min = ImVec2(body_min.x + corner * 0.6f, header_min.y + header_h + 6.0f * s);
    frame.panel_max = ImVec2(body_max.x - corner * 0.6f, body_max.y - corner * 0.6f);
    return frame;
}

void end_chassis()
{
    ImGui::End();
}

}  // namespace ui
