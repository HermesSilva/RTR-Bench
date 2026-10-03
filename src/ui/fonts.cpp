// SPDX-License-Identifier: Apache-2.0
#include "ui/fonts.h"

#include <imgui.h>

extern const unsigned char font_Inter_Regular[];
extern const unsigned long font_Inter_Regular_size;
extern const unsigned char font_Inter_SemiBold[];
extern const unsigned long font_Inter_SemiBold_size;
extern const unsigned char font_JetBrainsMono_Regular[];
extern const unsigned long font_JetBrainsMono_Regular_size;
extern const unsigned char font_DSEG7Classic_Regular[];
extern const unsigned long font_DSEG7Classic_Regular_size;

namespace ui {

namespace {

Fonts current;

ImFont *add(const unsigned char *data, unsigned long size, float pixels)
{
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;  // the data lives in the executable
    config.OversampleH = 2;
    config.OversampleV = 2;
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char *>(data), static_cast<int>(size), pixels, &config);
}

}  // namespace

Fonts load_fonts(float scale)
{
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();
    Fonts f;
    f.panel = add(font_Inter_Regular, font_Inter_Regular_size, 15.0f * scale);
    f.panel_bold = add(font_Inter_SemiBold, font_Inter_SemiBold_size, 15.0f * scale);
    f.small = add(font_Inter_Regular, font_Inter_Regular_size, 12.0f * scale);
    f.mono = add(font_JetBrainsMono_Regular, font_JetBrainsMono_Regular_size, 14.0f * scale);
    f.seven = add(font_DSEG7Classic_Regular, font_DSEG7Classic_Regular_size, 34.0f * scale);
    io.FontDefault = f.panel;
    return f;
}

const Fonts &fonts()
{
    return current;
}

void set_fonts(const Fonts &f)
{
    current = f;
}

}  // namespace ui
