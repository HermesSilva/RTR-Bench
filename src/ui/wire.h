// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the cable drawn between two points: a hanging curve with a
// shadow and a highlight, in the colour of the channel. Drawn on the
// current window draw list in screen coordinates of that window.
#pragma once

#include <imgui.h>

#include <cstdint>

namespace ui {

// The margin an overlay window needs around the two ends so the sag and
// the shadow of the cable fit.
float wire_margin(float scale);

void draw_wire(ImDrawList *draw, ImVec2 from, ImVec2 to, uint32_t colour, float scale, bool dangling = false);

}  // namespace ui
