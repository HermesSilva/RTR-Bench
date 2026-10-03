// SPDX-License-Identifier: Apache-2.0
#include "ui/wire.h"

#include <algorithm>
#include <cmath>

namespace ui {

namespace {

uint32_t with_alpha(uint32_t colour, uint32_t alpha)
{
    return (colour & 0x00FFFFFFu) | (alpha << 24u);
}

uint32_t lighten(uint32_t colour, int amount)
{
    uint32_t r = std::min(255u, (colour & 0xFFu) + static_cast<uint32_t>(amount));
    uint32_t g = std::min(255u, ((colour >> 8u) & 0xFFu) + static_cast<uint32_t>(amount));
    uint32_t b = std::min(255u, ((colour >> 16u) & 0xFFu) + static_cast<uint32_t>(amount));
    return (colour & 0xFF000000u) | (b << 16u) | (g << 8u) | r;
}

uint32_t darken(uint32_t colour, int amount)
{
    uint32_t r = (colour & 0xFFu) > static_cast<uint32_t>(amount) ? (colour & 0xFFu) - static_cast<uint32_t>(amount) : 0;
    uint32_t g = ((colour >> 8u) & 0xFFu) > static_cast<uint32_t>(amount) ? ((colour >> 8u) & 0xFFu) - static_cast<uint32_t>(amount) : 0;
    uint32_t b = ((colour >> 16u) & 0xFFu) > static_cast<uint32_t>(amount) ? ((colour >> 16u) & 0xFFu) - static_cast<uint32_t>(amount) : 0;
    return (colour & 0xFF000000u) | (b << 16u) | (g << 8u) | r;
}

// The sag of a cable between two points: it hangs, more when longer.
float sag_for(ImVec2 from, ImVec2 to, float scale)
{
    float dx = to.x - from.x;
    float dy = to.y - from.y;
    float length = std::sqrt(dx * dx + dy * dy);
    return std::min(length * 0.25f, 160.0f * scale);
}

}  // namespace

float wire_margin(float scale)
{
    return 180.0f * scale;
}

void draw_wire(ImDrawList *draw, ImVec2 from, ImVec2 to, uint32_t colour, float scale, bool dangling)
{
    float sag = sag_for(from, to, scale);
    ImVec2 c1(from.x, from.y + sag);
    ImVec2 c2(to.x, to.y + sag);
    float thickness = 4.0f * scale;   // 20 % thinner than the first cut, as asked
    int segments = 48;

    // Shadow below, the cable, a highlight along its top, and the plugs.
    ImVec2 shadow(3.0f * scale, 5.0f * scale);
    draw->AddBezierCubic(ImVec2(from.x + shadow.x, from.y + shadow.y), ImVec2(c1.x + shadow.x, c1.y + shadow.y),
                         ImVec2(c2.x + shadow.x, c2.y + shadow.y), ImVec2(to.x + shadow.x, to.y + shadow.y),
                         with_alpha(0, 90), thickness + 2.0f * scale, segments);
    uint32_t body = dangling ? with_alpha(colour, 200) : colour;
    draw->AddBezierCubic(from, c1, c2, to, darken(body, 60), thickness + 1.5f * scale, segments);
    draw->AddBezierCubic(from, c1, c2, to, body, thickness, segments);
    draw->AddBezierCubic(ImVec2(from.x, from.y - 1.2f * scale), ImVec2(c1.x, c1.y - 1.2f * scale),
                         ImVec2(c2.x, c2.y - 1.2f * scale), ImVec2(to.x, to.y - 1.2f * scale),
                         with_alpha(lighten(body, 70), 150), 1.2f * scale, segments);

    // Plug bodies at the ends: a short sleeve in the cable colour with a
    // dark ring, like a banana plug.
    for (ImVec2 end : {from, to}) {
        draw->AddCircleFilled(end, 5.6f * scale, darken(colour, 90), 20);
        draw->AddCircleFilled(end, 4.4f * scale, colour, 20);
        draw->AddCircleFilled(ImVec2(end.x - 1.5f * scale, end.y - 1.5f * scale), 1.8f * scale,
                              with_alpha(0xFFFFFFFFu, 170), 10);
    }
}

}  // namespace ui
