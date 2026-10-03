// SPDX-License-Identifier: Apache-2.0
#include "instruments/common/panel.h"

#include <algorithm>
#include <cfloat>

#include "ui/fonts.h"
#include "ui/theme.h"

namespace app::panel {

namespace {

uint32_t with_alpha(uint32_t colour, uint32_t alpha)
{
    return (colour & 0x00FFFFFFu) | (alpha << 24u);
}

}  // namespace

void screen(ImDrawList *draw, ImVec2 min, ImVec2 max, float s, ImVec2 &inner_min, ImVec2 &inner_max)
{
    const ui::Theme &t = ui::current_theme();
    draw->AddRectFilled(min, max, t.screen_bezel, 10.0f * s);
    inner_min = ImVec2(min.x + 8.0f * s, min.y + 8.0f * s);
    inner_max = ImVec2(max.x - 8.0f * s, max.y - 8.0f * s);
    draw->AddRectFilled(inner_min, inner_max, t.screen, 4.0f * s);
}

void graticule(ImDrawList *draw, ImVec2 min, ImVec2 max, int cols, int rows)
{
    const ui::Theme &t = ui::current_theme();
    for (int i = 0; i <= cols; i++) {
        float x = min.x + (max.x - min.x) * static_cast<float>(i) / static_cast<float>(cols);
        draw->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), i == cols / 2 ? t.graticule_axis : t.graticule, 1.0f);
    }
    for (int i = 0; i <= rows; i++) {
        float y = min.y + (max.y - min.y) * static_cast<float>(i) / static_cast<float>(rows);
        draw->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), i == rows / 2 ? t.graticule_axis : t.graticule, 1.0f);
    }
}

void digital_lane(ImDrawList *draw, const core::DigitalTrace &trace, int64_t t0, int64_t t1, float x0, float x1,
                  float top, float bottom, uint32_t colour, float s)
{
    int64_t span = t1 - t0;
    if (span <= 0) {
        return;
    }
    const float w = x1 - x0;
    auto level_y = [&](int level) { return level ? top : bottom; };
    int level = trace.level_at(t0);
    size_t i = trace.lower_bound(t0);
    size_t end = trace.lower_bound(t1 + 1);
    float x = x0;
    float thickness = 1.6f * s;
    if (level < 0 && i < end) {
        level = trace.at(i).level ? 0 : 1;
    }
    if (level < 0) {
        draw->AddLine(ImVec2(x0, (top + bottom) * 0.5f), ImVec2(x1, (top + bottom) * 0.5f), with_alpha(colour, 70), 1.0f);
        return;
    }
    // Edges that land in the same pixel as the previous edge make a band
    // from the first of them to the last; the band ends where the edges
    // spread out again.
    float dense_from = -1.0f;
    bool have_edge = false;
    for (; i < end; i++) {
        const core::Transition &tr = trace.at(i);
        float xe = x0 + static_cast<float>(static_cast<double>(tr.ns - t0) / static_cast<double>(span)) * w;
        if (have_edge && xe - x < 1.0f) {
            if (dense_from < 0.0f) {
                dense_from = x;
            }
            x = xe;
            level = tr.level;
            continue;
        }
        if (dense_from >= 0.0f) {
            draw->AddRectFilled(ImVec2(dense_from, top), ImVec2(std::max(x, dense_from + 1.0f), bottom), with_alpha(colour, 170));
            dense_from = -1.0f;
        }
        draw->AddLine(ImVec2(x, level_y(level)), ImVec2(xe, level_y(level)), colour, thickness);
        draw->AddLine(ImVec2(xe, top), ImVec2(xe, bottom), colour, thickness);
        x = xe;
        level = tr.level;
        have_edge = true;
    }
    if (dense_from >= 0.0f) {
        draw->AddRectFilled(ImVec2(dense_from, top), ImVec2(std::max(x, dense_from + 1.0f), bottom), with_alpha(colour, 170));
    }
    draw->AddLine(ImVec2(x, level_y(level)), ImVec2(x1, level_y(level)), colour, thickness);
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

void seven_text(ImDrawList *draw, ImVec2 pos, uint32_t colour, const char *text)
{
    ImFont *seven = ui::fonts().seven;
    draw->AddText(seven, seven->FontSize, pos, colour, text);
}

float seven_width(const char *text)
{
    ImFont *seven = ui::fonts().seven;
    return seven->CalcTextSizeA(seven->FontSize, FLT_MAX, 0.0f, text).x;
}

float seven_height()
{
    return ui::fonts().seven->FontSize;
}

void seven_display(ImDrawList *draw, ImVec2 min, ImVec2 max, uint32_t value_colour, const char *value, uint32_t unit_colour,
                   const char *unit, float s, float top_inset)
{
    const ui::Theme &t = ui::current_theme();
    ImFont *seven = ui::fonts().seven;
    ImFont *mono = ui::fonts().mono;
    draw->AddRectFilled(min, max, t.screen, 4.0f * s);
    draw->AddRect(min, max, t.chassis_shadow, 4.0f * s, 0, 1.0f * s);
    float uw = unit[0] != 0 ? mono_width(unit) + 6.0f * s : 0.0f;
    float avail = (max.x - min.x) - 16.0f * s - uw;
    float size = std::min(seven->FontSize, (max.y - min.y - top_inset) - 10.0f * s);
    float tw = seven->CalcTextSizeA(size, FLT_MAX, 0.0f, value).x;
    if (tw > avail && tw > 0.0f) {
        size *= avail / tw;
        tw = avail;
    }
    float cy = (min.y + top_inset + max.y) * 0.5f;
    draw->AddText(seven, size, ImVec2(max.x - 8.0f * s - uw - tw, cy - size * 0.5f), value_colour, value);
    if (unit[0] != 0) {
        draw->AddText(mono, mono->FontSize, ImVec2(max.x - 8.0f * s - mono_width(unit), max.y - 6.0f * s - mono->FontSize),
                      unit_colour, unit);
    }
}

std::string short_port_name(const core::PortInfo *info)
{
    if (!info) {
        return "--";
    }
    if (info->name.rfind("GPIO ", 0) == 0) {
        return "G" + std::to_string(info->index);
    }
    // An instrument output, "GEN #2 OUT1": the instrument and the output, "GEN2:1".
    size_t out = info->name.rfind(" OUT");
    if (out != std::string::npos) {
        std::string head = info->name.substr(0, out);
        std::string instance;
        size_t hash = head.find(" #");
        if (hash != std::string::npos) {
            instance = head.substr(hash + 2);
            head = head.substr(0, hash);
        }
        return head + instance + ":" + info->name.substr(out + 4);
    }
    return info->name;
}

}  // namespace app::panel
