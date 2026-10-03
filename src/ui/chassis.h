// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the chassis of an instrument: the front panel drawn with alpha
// on a transparent window, with handles, brand, title, close and minimize
// controls, and drag-to-move. Every instrument starts its frame here and
// draws its screen and controls inside the panel rectangle it returns.
#pragma once

#include <imgui.h>

#include <string>

namespace ui {

class Window;

struct ChassisSpec {
    std::string brand = "RTR-Bench";
    std::string model;        // e.g. "DSO-1" shown next to the brand
    std::string title;        // e.g. "Digital Oscilloscope"
    bool handles = true;      // side handles like a rack instrument
    float corner = 18.0f;     // corner radius, logical pixels
    float margin = 14.0f;     // transparent border around the chassis (shadow)
};

struct ChassisFrame {
    ImVec2 panel_min;   // the usable front panel, inside the bevel
    ImVec2 panel_max;
    ImDrawList *draw;
};

// Draws the chassis for the current frame and handles move/close/minimize.
// Call once per frame, first thing in the instrument's draw callback.
ChassisFrame begin_chassis(Window &window, const ChassisSpec &spec);

// Ends the full-window ImGui container opened by begin_chassis.
void end_chassis();

}  // namespace ui
