// SPDX-License-Identifier: Apache-2.0
#include "instruments/common/panel.h"

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
    float dense_from = -1.0f;
    for (; i < end; i++) {
        const core::Transition &tr = trace.at(i);
        float xe = x0 + static_cast<float>(static_cast<double>(tr.ns - t0) / static_cast<double>(span)) * w;
        if (xe - x < 1.0f && dense_from < 0.0f) {
            dense_from = x;
        }
        if (dense_from >= 0.0f) {
            if (xe - dense_from >= 1.0f) {
                draw->AddRectFilled(ImVec2(dense_from, top), ImVec2(xe, bottom), with_alpha(colour, 170));
                dense_from = -1.0f;
                x = xe;
            }
            level = tr.level;
            continue;
        }
        draw->AddLine(ImVec2(x, level_y(level)), ImVec2(xe, level_y(level)), colour, thickness);
        draw->AddLine(ImVec2(xe, top), ImVec2(xe, bottom), colour, thickness);
        x = xe;
        level = tr.level;
    }
    if (dense_from >= 0.0f) {
        draw->AddRectFilled(ImVec2(dense_from, top), ImVec2(x1, bottom), with_alpha(colour, 170));
    } else {
        draw->AddLine(ImVec2(x, level_y(level)), ImVec2(x1, level_y(level)), colour, thickness);
    }
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

std::string short_port_name(const core::PortInfo *info)
{
    if (!info) {
        return "--";
    }
    if (info->name.rfind("GPIO ", 0) == 0) {
        return "G" + std::to_string(info->index);
    }
    return info->name;
}

}  // namespace app::panel
