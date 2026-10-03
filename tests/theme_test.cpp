// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "ui/theme.h"

TEST_CASE("every theme is opaque where the chassis is drawn")
{
    for (ui::ThemeKind kind : {ui::ThemeKind::Light, ui::ThemeKind::Dark, ui::ThemeKind::Amber}) {
        const ui::Theme &t = ui::theme(kind);
        REQUIRE(t.kind == kind);
        REQUIRE((t.chassis >> 24) == 255u);
        REQUIRE((t.screen >> 24) == 255u);
        REQUIRE(t.name != nullptr);
    }
}

TEST_CASE("set_theme selects the current theme")
{
    ui::set_theme(ui::ThemeKind::Amber);
    REQUIRE(ui::current_theme().kind == ui::ThemeKind::Amber);
    ui::set_theme(ui::ThemeKind::Dark);
    REQUIRE(ui::current_theme().kind == ui::ThemeKind::Dark);
}

TEST_CASE("channel colours are distinct and opaque")
{
    for (int i = 0; i < ui::channel_count; i++) {
        REQUIRE((ui::channel_colour(i) >> 24) == 255u);
        for (int j = i + 1; j < ui::channel_count; j++) {
            REQUIRE(ui::channel_colour(i) != ui::channel_colour(j));
        }
    }
    REQUIRE(ui::channel_colour(-1) == ui::channel_colour(ui::channel_count));
}
