// SPDX-License-Identifier: Apache-2.0
#include "ui/widgets.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "ui/fonts.h"
#include "ui/theme.h"

namespace ui {

// Where a list of height `h` opens for a control at `pos`/`size`: under it
// when that fits the window, above it when that does, else at the top.
ImVec2 popup_position(ImVec2 pos, ImVec2 size, float h, float s)
{
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    float bottom = vp->Pos.y + vp->Size.y - 4.0f * s;
    float below = pos.y + size.y + 4.0f * s;
    if (below + h <= bottom) {
        return ImVec2(pos.x, below);
    }
    float above = pos.y - 4.0f * s - h;
    if (above >= vp->Pos.y + 4.0f * s) {
        return ImVec2(pos.x, above);
    }
    return ImVec2(pos.x, std::max(vp->Pos.y + 4.0f * s, bottom - h));
}

namespace {

uint32_t with_alpha(uint32_t colour, uint32_t alpha)
{
    return (colour & 0x00FFFFFFu) | (alpha << 24u);
}

ImVec2 add(ImVec2 a, ImVec2 b) { return {a.x + b.x, a.y + b.y}; }
ImVec2 sub(ImVec2 a, ImVec2 b) { return {a.x - b.x, a.y - b.y}; }

}  // namespace

bool key(const char *id, const char *text, ImVec2 pos, ImVec2 size, bool lit, uint32_t lit_colour,
         float scale, bool enabled)
{
    const Theme &t = current_theme();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(pos);
    if (!enabled) {
        ImGui::BeginDisabled();
    }
    ImGui::InvisibleButton(id, size);
    if (!enabled) {
        ImGui::EndDisabled();
    }
    bool hovered = enabled && ImGui::IsItemHovered();
    bool held = enabled && ImGui::IsItemActive();
    bool clicked = enabled && ImGui::IsItemClicked();

    float r = 5.0f * scale;
    ImVec2 max = add(pos, size);
    // Shadow under the key, then the face, pressed keys sink by a pixel.
    draw->AddRectFilled(add(pos, ImVec2(0, 2.0f * scale)), add(max, ImVec2(0, 2.0f * scale)),
                        t.chassis_shadow, r);
    ImVec2 face_min = held ? add(pos, ImVec2(0, 1.0f * scale)) : pos;
    ImVec2 face_max = held ? add(max, ImVec2(0, 1.0f * scale)) : max;
    draw->AddRectFilled(face_min, face_max, held ? t.key_pressed : (hovered ? t.key_hover : t.key), r);
    draw->AddRect(face_min, face_max, t.chassis_edge, r, 0, 1.0f * scale);
    if (lit) {
        // Backlit legend: a glow strip at the top of the key and the text in colour.
        draw->AddRectFilled(add(face_min, ImVec2(4.0f * scale, 3.0f * scale)),
                            ImVec2(face_max.x - 4.0f * scale, face_min.y + 5.0f * scale), lit_colour,
                            1.0f * scale);
    }
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 tp(face_min.x + (size.x - ts.x) * 0.5f, face_min.y + (size.y - ts.y) * 0.5f + 1.0f * scale);
    uint32_t colour = !enabled ? t.label_dim : (lit ? lit_colour : t.key_text);
    draw->AddText(tp, colour, text);
    return clicked;
}

void led(ImVec2 centre, float radius, uint32_t colour, bool on, float scale)
{
    const Theme &t = current_theme();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddCircleFilled(centre, radius + 1.0f * scale, t.chassis_shadow, 20);
    if (on) {
        draw->AddCircleFilled(centre, radius + 3.0f * scale, with_alpha(colour, 60), 20);
        draw->AddCircleFilled(centre, radius, colour, 20);
        draw->AddCircleFilled(sub(centre, ImVec2(radius * 0.3f, radius * 0.3f)), radius * 0.3f,
                              with_alpha(0xFFFFFFFFu, 150), 12);
    } else {
        draw->AddCircleFilled(centre, radius, t.led_off, 20);
        draw->AddCircleFilled(centre, radius * 0.7f, with_alpha(colour, 70), 16);
    }
}

void label(ImVec2 pos, const char *text, bool dim)
{
    const Theme &t = current_theme();
    ImGui::GetWindowDrawList()->AddText(pos, dim ? t.label_dim : t.label, text);
}

bool jack(const char *id, ImVec2 centre, float radius, const JackLook &look, float scale)
{
    const Theme &t = current_theme();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    ImVec2 hit(radius * 2.4f, radius * 2.4f);
    ImGui::SetCursorScreenPos(sub(centre, ImVec2(hit.x * 0.5f, hit.y * 0.5f)));
    ImGui::InvisibleButton(id, hit);
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked();

    // Ring: full for an output, hollow (thin) for an input, dim when unknown.
    uint32_t ring = look.output ? t.label : (look.input ? t.label_dim : t.led_off);
    draw->AddCircleFilled(centre, radius + 2.0f * scale, t.chassis_shadow, 28);
    draw->AddCircleFilled(centre, radius, hovered ? t.key_hover : t.key, 28);
    draw->AddCircle(centre, radius - 1.0f * scale, ring, 28, look.output ? 2.5f * scale : 1.2f * scale);
    draw->AddCircleFilled(centre, radius * 0.42f, t.screen, 20);  // the hole
    if (look.wire_colour != 0) {
        // A plug in the jack: a disc in the wire colour with a highlight.
        draw->AddCircleFilled(centre, radius * 0.55f, look.wire_colour, 20);
        draw->AddCircleFilled(sub(centre, ImVec2(radius * 0.18f, radius * 0.18f)), radius * 0.16f,
                              with_alpha(0xFFFFFFFFu, 160), 10);
    }
    // Level LED above-right, activity mark above-left.
    ImVec2 led_c(centre.x + radius * 0.95f, centre.y - radius * 0.95f);
    led(led_c, radius * 0.28f, t.led_run, look.level == 1, scale);
    if (look.active) {
        ImVec2 act_c(centre.x - radius * 0.95f, centre.y - radius * 0.95f);
        draw->AddCircleFilled(act_c, radius * 0.26f, t.led_warn, 12);
    }
    // Name under the jack.
    ImVec2 ts = ImGui::CalcTextSize(look.name);
    draw->AddText(ImVec2(centre.x - ts.x * 0.5f, centre.y + radius + 4.0f * scale), t.label, look.name);
    return clicked;
}

int knob(const char *id, ImVec2 centre, float radius, const char *text, float scale, bool *pressed)
{
    const Theme &t = current_theme();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    ImVec2 hit(radius * 2.0f, radius * 2.0f);
    ImGui::SetCursorScreenPos(sub(centre, ImVec2(radius, radius)));
    ImGui::InvisibleButton(id, hit);
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();
    int steps = 0;

    // Remember the angle and the drag accumulator per knob.
    ImGuiStorage *storage = ImGui::GetStateStorage();
    ImGuiID key_angle = ImGui::GetID(id);
    ImGuiID key_drag = key_angle + 1;
    ImGuiID key_moved = key_angle + 2;
    float angle = storage->GetFloat(key_angle, 0.0f);
    float drag = storage->GetFloat(key_drag, 0.0f);
    float moved = storage->GetFloat(key_moved, 0.0f);   // how far the mouse went while the knob was held

    if (hovered) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel > 0.0f) {
            steps += 1;
        } else if (wheel < 0.0f) {
            steps -= 1;
        }
    }
    if (active) {
        moved += std::fabs(ImGui::GetIO().MouseDelta.x) + std::fabs(ImGui::GetIO().MouseDelta.y);
        drag += -ImGui::GetIO().MouseDelta.y;   // up = clockwise
        float notch = 10.0f * scale;
        while (drag >= notch) {
            steps += 1;
            drag -= notch;
        }
        while (drag <= -notch) {
            steps -= 1;
            drag += notch;
        }
    } else {
        drag = 0.0f;
    }
    // The push of the knob: pressed and released without turning it.
    bool released = ImGui::IsItemDeactivated();
    if (pressed) {
        *pressed = released && moved < 3.0f * scale;
    }
    if (!active) {
        moved = 0.0f;
    }
    storage->SetFloat(key_moved, moved);
    angle += static_cast<float>(steps) * 0.35f;
    storage->SetFloat(key_angle, angle);
    storage->SetFloat(key_drag, drag);

    // Body: shadow, ring, knurled edge, pointer.
    draw->AddCircleFilled(add(centre, ImVec2(0, 2.0f * scale)), radius + 2.0f * scale, t.chassis_shadow, 40);
    draw->AddCircleFilled(centre, radius, t.knob_ring, 40);
    draw->AddCircleFilled(centre, radius - 3.0f * scale, hovered || active ? t.key_hover : t.knob, 40);
    for (int i = 0; i < 24; i++) {
        float a = angle + static_cast<float>(i) * (6.2831853f / 24.0f);
        ImVec2 p0(centre.x + std::cos(a) * (radius - 1.0f * scale), centre.y + std::sin(a) * (radius - 1.0f * scale));
        ImVec2 p1(centre.x + std::cos(a) * (radius - 4.0f * scale), centre.y + std::sin(a) * (radius - 4.0f * scale));
        draw->AddLine(p0, p1, t.chassis_shadow, 1.0f * scale);
    }
    ImVec2 tip(centre.x + std::cos(angle - 1.5707963f) * (radius - 6.0f * scale),
               centre.y + std::sin(angle - 1.5707963f) * (radius - 6.0f * scale));
    draw->AddCircleFilled(tip, 2.5f * scale, t.knob_mark, 12);
    if (text && text[0]) {
        ImVec2 ts = ImGui::CalcTextSize(text);
        draw->AddText(ImVec2(centre.x - ts.x * 0.5f, centre.y + radius + 5.0f * scale), t.label, text);
    }
    return steps;
}

void readout(ImVec2 min, ImVec2 max, const char *text, uint32_t colour, float scale)
{
    const Theme &t = current_theme();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, max, t.screen, 4.0f * scale);
    draw->AddRect(min, max, t.chassis_shadow, 4.0f * scale, 0, 1.0f * scale);
    ImFont *mono = fonts().mono;
    ImVec2 ts = mono->CalcTextSizeA(mono->FontSize, FLT_MAX, 0.0f, text);
    draw->AddText(mono, mono->FontSize,
                  ImVec2(min.x + (max.x - min.x - ts.x) * 0.5f, min.y + (max.y - min.y - ts.y) * 0.5f), colour, text);
}

void group_frame(ImVec2 min, ImVec2 max, const char *title, float scale)
{
    const Theme &t = current_theme();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    float r = 6.0f * scale;
    draw->AddRect(min, max, t.chassis_shadow, r, 0, 1.0f * scale);
    draw->AddRect(add(min, ImVec2(1.0f * scale, 1.0f * scale)), sub(max, ImVec2(1.0f * scale, 1.0f * scale)),
                  t.chassis_edge, r, 0, 1.0f * scale);
    if (title && title[0]) {
        ImVec2 ts = ImGui::CalcTextSize(title);
        ImVec2 tp(min.x + 10.0f * scale, min.y - ts.y * 0.5f);
        draw->AddRectFilled(sub(tp, ImVec2(4.0f * scale, 0)), add(tp, ImVec2(ts.x + 4.0f * scale, ts.y)), t.chassis);
        draw->AddText(tp, t.label_dim, title);
    }
}

}  // namespace ui

namespace ui {

int dropdown(const char *id, ImVec2 pos, ImVec2 size, const DropdownItem *items, int count, int current,
             uint32_t colour, float s)
{
    const Theme &t = current_theme();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    char popup[64];
    std::snprintf(popup, sizeof(popup), "%s-list", id);

    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, size);
    bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) {
        ImGui::OpenPopup(popup);
    }
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), hovered ? t.key_hover : t.screen, 4.0f * s);
    draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), t.chassis_shadow, 4.0f * s, 0, 1.0f * s);
    ImFont *mono = fonts().mono;
    const char *name = current >= 0 && current < count ? items[current].name : "";
    draw->AddText(mono, mono->FontSize, ImVec2(pos.x + 8.0f * s, pos.y + (size.y - mono->FontSize) * 0.5f), colour, name);
    float cx = pos.x + size.x - 10.0f * s;
    float cy = pos.y + size.y * 0.5f;
    draw->AddTriangleFilled(ImVec2(cx - 4.0f * s, cy - 2.5f * s), ImVec2(cx + 4.0f * s, cy - 2.5f * s),
                            ImVec2(cx, cy + 2.5f * s), t.label_dim);

    int chosen = -1;
    // Below the control when it fits in the window, else above it, else
    // pinned to the top (the list is drawn inside the instrument's window).
    int groups = 0;
    for (int k = 0, g = -1; k < count; k++) {
        if (items[k].group && (g < 0 || std::strcmp(items[g].group, items[k].group) != 0)) {
            groups++;
            g = k;
        }
    }
    float popup_h = static_cast<float>(count) * (mono->FontSize + 3.0f * s) +
                    static_cast<float>(groups) * (fonts().small->FontSize + 7.0f * s) + 20.0f * s;
    ImGui::SetNextWindowPos(popup_position(pos, size, popup_h, s));
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
    if (ImGui::BeginPopup(popup)) {
        const char *group = nullptr;
        for (int k = 0; k < count; k++) {
            const char *g = items[k].group;
            if (g && (!group || std::strcmp(group, g) != 0)) {
                group = g;
                if (k > 0) {
                    ImGui::Spacing();
                }
                ImGui::PushFont(fonts().small);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(t.label_dim), "%s", g);
                ImGui::PopFont();
            }
            bool is_current = k == current;
            ImGui::PushFont(mono);
            if (is_current) {
                ImGui::PushStyleColor(ImGuiCol_Text, colour);
            }
            if (ImGui::Selectable(items[k].name, is_current)) {
                chosen = k;
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
    return chosen;
}

}  // namespace ui
