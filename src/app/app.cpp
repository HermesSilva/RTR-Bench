// SPDX-License-Identifier: Apache-2.0
#include "app/app.h"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "ui/chassis.h"
#include "ui/theme.h"

namespace app {

App::App() = default;
App::~App() = default;

// Stage 0: a single window with the chassis, to prove the shape, the drag and
// the transparency on both platforms. The rack takes its place in stage 1.
void App::open_rack()
{
    ui::WindowSpec spec;
    spec.title = "RTR-Bench";
    spec.width = 1120;
    spec.height = 640;
    auto window = std::make_unique<ui::Window>(spec);
    if (!window->valid()) {
        std::fprintf(stderr, "rtr-bench: cannot create the window\n");
        return;
    }
    window->set_draw([](ui::Window &w) {
        ui::ChassisSpec chassis;
        chassis.model = "DSO-1";
        chassis.title = "Digital Oscilloscope";
        ui::ChassisFrame frame = ui::begin_chassis(w, chassis);

        // Placeholder screen: the bezel and the graticule, so the proportions
        // can be judged before the real screen exists.
        const ui::Theme &t = ui::current_theme();
        const float s = w.scale();
        ImVec2 screen_min = frame.panel_min;
        ImVec2 screen_max(frame.panel_min.x + (frame.panel_max.x - frame.panel_min.x) * 0.68f,
                          frame.panel_max.y);
        frame.draw->AddRectFilled(screen_min, screen_max, t.screen_bezel, 10.0f * s);
        ImVec2 inner_min = ImVec2(screen_min.x + 10.0f * s, screen_min.y + 10.0f * s);
        ImVec2 inner_max = ImVec2(screen_max.x - 10.0f * s, screen_max.y - 10.0f * s);
        frame.draw->AddRectFilled(inner_min, inner_max, t.screen, 4.0f * s);
        const int cols = 10;
        const int rows = 8;
        for (int i = 1; i < cols; i++) {
            float x = inner_min.x + (inner_max.x - inner_min.x) * static_cast<float>(i) / cols;
            frame.draw->AddLine(ImVec2(x, inner_min.y), ImVec2(x, inner_max.y),
                                i == cols / 2 ? t.graticule_axis : t.graticule, 1.0f);
        }
        for (int i = 1; i < rows; i++) {
            float y = inner_min.y + (inner_max.y - inner_min.y) * static_cast<float>(i) / rows;
            frame.draw->AddLine(ImVec2(inner_min.x, y), ImVec2(inner_max.x, y),
                                i == rows / 2 ? t.graticule_axis : t.graticule, 1.0f);
        }
        frame.draw->AddText(ImVec2(inner_min.x + 12.0f * s, inner_min.y + 10.0f * s), t.readout_dim,
                            "no probe");

        ui::end_chassis();
    });
    windows_.push_back(std::move(window));
}

int App::run()
{
    if (!ui::platform_init()) {
        std::fprintf(stderr, "rtr-bench: cannot initialize the window system\n");
        return 1;
    }
    ui::set_theme(ui::ThemeKind::Dark);
    open_rack();

    while (!windows_.empty()) {
        ui::platform_poll();
        for (auto &window : windows_) {
            window->frame();
        }
        windows_.erase(std::remove_if(windows_.begin(), windows_.end(),
                                      [](const std::unique_ptr<ui::Window> &w) {
                                          return w->close_requested();
                                      }),
                       windows_.end());
    }

    ui::platform_shutdown();
    return 0;
}

}  // namespace app
