// SPDX-License-Identifier: Apache-2.0
#include "ui/theme.h"

namespace ui {

namespace {

constexpr uint32_t rgb(uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255)
{
    return (a << 24u) | (b << 16u) | (g << 8u) | r;
}

// Dark: the graphite of a modern bench instrument.
constexpr Theme dark_theme = {
    ThemeKind::Dark, "Dark",
    rgb(44, 46, 50), rgb(72, 75, 80), rgb(22, 23, 26), rgb(30, 31, 34),
    rgb(205, 208, 212), rgb(140, 144, 150), rgb(235, 237, 240),
    rgb(10, 12, 14), rgb(18, 19, 22), rgb(42, 48, 52), rgb(70, 80, 86),
    rgb(220, 224, 228), rgb(130, 136, 142),
    rgb(62, 65, 70), rgb(78, 82, 88), rgb(36, 38, 42), rgb(225, 228, 232),
    rgb(58, 60, 64), rgb(26, 27, 30), rgb(240, 240, 240),
    rgb(40, 42, 46), rgb(60, 220, 90), rgb(235, 60, 60), rgb(240, 170, 40),
    rgb(200, 60, 60),
};

// Light: the beige/grey of a classic HP bench instrument.
constexpr Theme light_theme = {
    ThemeKind::Light, "Light",
    rgb(214, 212, 206), rgb(240, 239, 236), rgb(150, 148, 142), rgb(120, 118, 112),
    rgb(50, 52, 56), rgb(110, 112, 116), rgb(30, 32, 36),
    rgb(14, 20, 24), rgb(60, 62, 66), rgb(44, 54, 60), rgb(76, 90, 98),
    rgb(225, 230, 234), rgb(140, 148, 154),
    rgb(232, 231, 228), rgb(246, 245, 243), rgb(200, 198, 194), rgb(40, 42, 46),
    rgb(226, 224, 220), rgb(150, 148, 144), rgb(30, 30, 30),
    rgb(170, 170, 170), rgb(40, 180, 70), rgb(220, 50, 50), rgb(230, 150, 30),
    rgb(200, 60, 60),
};

// Amber: dark chassis, amber phosphor and warm keys.
constexpr Theme amber_theme = {
    ThemeKind::Amber, "Amber",
    rgb(40, 36, 32), rgb(70, 62, 54), rgb(20, 18, 16), rgb(28, 25, 22),
    rgb(230, 190, 120), rgb(160, 130, 90), rgb(255, 200, 110),
    rgb(14, 10, 6), rgb(22, 18, 14), rgb(70, 50, 24), rgb(110, 78, 34),
    rgb(255, 184, 72), rgb(170, 120, 50),
    rgb(64, 56, 48), rgb(84, 74, 62), rgb(40, 34, 28), rgb(255, 210, 140),
    rgb(60, 52, 44), rgb(26, 22, 18), rgb(255, 200, 120),
    rgb(50, 42, 36), rgb(120, 220, 90), rgb(240, 80, 50), rgb(255, 170, 40),
    rgb(220, 90, 50),
};

const Theme *active = &dark_theme;

constexpr uint32_t channel_colours[channel_count] = {
    rgb(255, 214, 10),   // CH1 yellow
    rgb(60, 220, 240),   // CH2 cyan
    rgb(240, 80, 220),   // CH3 magenta
    rgb(90, 140, 255),   // CH4 blue
};

}  // namespace

const Theme &theme(ThemeKind kind)
{
    switch (kind) {
    case ThemeKind::Light:
        return light_theme;
    case ThemeKind::Amber:
        return amber_theme;
    case ThemeKind::Dark:
        break;
    }
    return dark_theme;
}

const Theme &current_theme()
{
    return *active;
}

void set_theme(ThemeKind kind)
{
    active = &theme(kind);
}

uint32_t channel_colour(int channel)
{
    if (channel < 0 || channel >= channel_count) {
        return rgb(255, 255, 255);
    }
    return channel_colours[channel];
}

}  // namespace ui
