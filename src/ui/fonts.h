// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the fonts of the panels, embedded in the executable: Inter for
// labels and keys, JetBrains Mono for readouts, DSEG7 for seven-segment
// displays. Loaded once per ImGui context (each window has its own).
#pragma once

struct ImFont;

namespace ui {

struct Fonts {
    ImFont *panel = nullptr;        // Inter Regular, labels
    ImFont *panel_bold = nullptr;   // Inter SemiBold, brand and group titles
    ImFont *small = nullptr;        // Inter Regular, small labels
    ImFont *mono = nullptr;         // JetBrains Mono, readouts on the screen
    ImFont *seven = nullptr;        // DSEG7, large displays
};

// Builds the atlas of the current ImGui context at the given scale.
Fonts load_fonts(float scale);

// The fonts of the current ImGui context (set by Window at every frame).
const Fonts &fonts();
void set_fonts(const Fonts &fonts);

}  // namespace ui
