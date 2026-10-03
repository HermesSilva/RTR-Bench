// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the controls drawn on the front panels: keys, LEDs, jacks,
// labels. All of them draw on the current window draw list in screen
// coordinates and use the current theme. Nothing here looks like stock ImGui.
#pragma once

#include <imgui.h>

#include <cstdint>

namespace ui {

// A panel key: a rounded face with a label, lit when `lit`. Returns true when
// clicked. `scale` is the window scale (Window::scale()).
bool key(const char *id, const char *label, ImVec2 pos, ImVec2 size, bool lit, uint32_t lit_colour,
         float scale, bool enabled = true);

// A round LED, lit with `colour`, dim when off.
void led(ImVec2 centre, float radius, uint32_t colour, bool on, float scale);

// A silk-screen label on the panel. `dim` uses the secondary colour.
void label(ImVec2 pos, const char *text, bool dim = false);

// A banana jack with its LED: the ring, the hole, a level LED beside it and
// an activity mark. Returns true when clicked.
struct JackLook {
    const char *name;        // "18" (drawn under the jack)
    int level;               // -1 unknown, 0, 1
    bool active;             // a transition happened in this frame
    bool input;              // direction known as input (hollow ring)
    bool output;             // direction known as output (full ring)
    uint32_t wire_colour;    // 0: no wire
};
bool jack(const char *id, ImVec2 centre, float radius, const JackLook &look, float scale);

// A screw-less panel section with a thin frame and a title, like the groups
// on a real front panel ("VERTICAL", "TRIGGER").
void group_frame(ImVec2 min, ImVec2 max, const char *title, float scale);

// A rotary knob without end stops: returns the number of steps turned this
// frame (positive clockwise) from the mouse wheel or a vertical drag; a
// click reports `pressed` (the push function of the knob). `label` goes
// under it.
int knob(const char *id, ImVec2 centre, float radius, const char *label, float scale, bool *pressed = nullptr);

// A readout box on the panel: a dark window with text in the mono font.
void readout(ImVec2 min, ImVec2 max, const char *text, uint32_t colour, float scale);

// A dropdown: a dark window showing the current item, a click opens the
// list under it; items with the same group share a dim heading. Returns the
// index chosen this frame, or -1.
// Where a list of height `h` opens for a control at `pos`/`size`: under it
// when that fits the window, above it when that does, else at the top.
ImVec2 popup_position(ImVec2 pos, ImVec2 size, float h, float scale);

struct DropdownItem {
    const char *name;
    const char *group;   // may be null
};
int dropdown(const char *id, ImVec2 pos, ImVec2 size, const DropdownItem *items, int count, int current,
             uint32_t colour, float scale);

}  // namespace ui
