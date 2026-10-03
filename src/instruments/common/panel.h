// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - drawing shared by the instruments: the screen with its bezel,
// graticules, digital lanes, text in the readout fonts, port names.
#pragma once

#include <imgui.h>

#include <cstdint>
#include <string>

#include "core/probe.h"
#include "core/trace.h"

namespace app::panel {

// Bezel and phosphor; returns the inner (phosphor) rectangle.
void screen(ImDrawList *draw, ImVec2 min, ImVec2 max, float scale, ImVec2 &inner_min, ImVec2 &inner_max);
void graticule(ImDrawList *draw, ImVec2 min, ImVec2 max, int cols, int rows);

// A digital trace as a lane between `top` and `bottom`, from time t0 at x0
// to t1 at x1; dense regions (several edges per pixel) become a band.
void digital_lane(ImDrawList *draw, const core::DigitalTrace &trace, int64_t t0, int64_t t1, float x0, float x1,
                  float top, float bottom, uint32_t colour, float scale);

void mono_text(ImDrawList *draw, ImVec2 pos, uint32_t colour, const char *text);
float mono_width(const char *text);
// Seven-segment digits (DSEG7). Characters outside the font fall back to mono.
void seven_text(ImDrawList *draw, ImVec2 pos, uint32_t colour, const char *text);
float seven_width(const char *text);
float seven_height();
// A display window: dark screen, the value in seven-segment digits right-aligned
// before the unit. The value shrinks when it would not fit the window.
// `top_inset` keeps the top of the window free (a line of statistics).
void seven_display(ImDrawList *draw, ImVec2 min, ImVec2 max, uint32_t value_colour, const char *value, uint32_t unit_colour,
                   const char *unit, float scale, float top_inset = 0.0f);

// "G18" for "GPIO 18", else the port name; "--" when unwired.
std::string short_port_name(const core::PortInfo *info);

}  // namespace app::panel
