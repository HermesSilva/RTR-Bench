// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the three themes (Light, Dark, Amber) and the colours of every
// visible element. No colour literal lives outside theme.cpp.
#pragma once

#include <cstdint>

namespace ui {

enum class ThemeKind { Light, Dark, Amber };

// Colours are ImGui-style packed ABGR (IM_COL32), kept as plain integers so
// that headers that do not need ImGui can still carry a Theme.
struct Theme {
    ThemeKind kind;
    const char *name;

    // Chassis
    uint32_t chassis;          // front panel
    uint32_t chassis_edge;     // bevel, lighter
    uint32_t chassis_shadow;   // bevel, darker
    uint32_t handle;           // side handles and feet
    uint32_t label;            // silk-screen text on the panel
    uint32_t label_dim;        // secondary text
    uint32_t brand;            // product name

    // Screen
    uint32_t screen;           // phosphor background
    uint32_t screen_bezel;     // frame around the screen
    uint32_t graticule;        // grid lines
    uint32_t graticule_axis;   // centre lines
    uint32_t readout;          // text on the screen
    uint32_t readout_dim;

    // Controls
    uint32_t key;              // key face
    uint32_t key_hover;
    uint32_t key_pressed;
    uint32_t key_text;
    uint32_t knob;             // knob body
    uint32_t knob_ring;
    uint32_t knob_mark;        // pointer on the knob
    uint32_t led_off;
    uint32_t led_run;          // green
    uint32_t led_stop;         // red
    uint32_t led_warn;         // amber

    // Window controls drawn on the chassis
    uint32_t close_hover;
};

const Theme &theme(ThemeKind kind);
const Theme &current_theme();
void set_theme(ThemeKind kind);

// Channel colours are shared by every instrument and every theme.
constexpr int channel_count = 4;
uint32_t channel_colour(int channel);

}  // namespace ui
