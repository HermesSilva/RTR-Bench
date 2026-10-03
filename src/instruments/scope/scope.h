// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the oscilloscope: four digital channels (analog ones come
// with the ADALM2000), time base, trigger, cursors and measurements, drawn
// as a bench instrument: screen on the left, control block on the right.
#pragma once

#include <imgui.h>

#include <array>

#include "app/instrument.h"
#include "core/scope_engine.h"
#include "core/trace.h"

namespace app {

class App;

class Scope : public InstrumentBase {
public:
    explicit Scope(App &app);

    Instrument kind() const override { return Instrument::Scope; }
    int channel_count() const override { return channels; }
    void draw(ui::Window &window) override;
    void feed(const std::vector<core::DigitalEvent> &events) override;
    void wiring_changed() override;

    static constexpr int channels = 4;

private:
    struct Channel {
        core::DigitalTrace trace;
        int port = -1;
        bool visible = true;
    };

    void draw_screen(ui::Window &window, ImVec2 min, ImVec2 max);
    void draw_controls(ui::Window &window, ImVec2 min, ImVec2 max);
    void handle_keys();
    void refresh_wiring();
    float time_to_x(int64_t ns) const;

    App &app_;
    std::array<Channel, channels> ch_;
    core::ScopeEngine engine_;
    int selected_ = 0;              // channel whose measurements show
    bool cursors_ = false;
    double cursor_a_ = 0.3;         // fraction of the screen width
    double cursor_b_ = 0.7;
    int64_t latest_ns_ = -1;

    // Screen geometry of the current frame, for the cursors.
    ImVec2 screen_min_;
    ImVec2 screen_max_;
};

}  // namespace app
