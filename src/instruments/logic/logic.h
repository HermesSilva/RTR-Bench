// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the logic analyzer: as many digital channels as needed (the
// ADD key makes one more), lanes on a shared time base with the trigger of
// the oscilloscope engine, and a bus lane showing the value of all the
// wired channels in hexadecimal (channel 1 is bit 0).
#pragma once

#include <imgui.h>

#include <memory>
#include <vector>

#include "app/instrument.h"
#include "core/scope_engine.h"
#include "core/trace.h"

namespace app {

class App;

class LogicAnalyzer : public InstrumentBase {
public:
    explicit LogicAnalyzer(App &app);

    Instrument kind() const override { return Instrument::Logic; }
    int channel_count() const override { return static_cast<int>(ch_.size()); }
    void draw(ui::Window &window) override;
    void feed(const std::vector<core::DigitalEvent> &events) override;
    void wiring_changed() override;
    void save(nlohmann::json &out) const override;
    void load(const nlohmann::json &in) override;

    static constexpr int max_channels = 16;

private:
    struct Channel {
        core::DigitalTrace trace{1u << 18};
        int port = -1;
    };
    void refresh_wiring();
    void add_channel();
    void remove_channel(int index);
    void fit_window(ui::Window &window);
    void draw_screen(ui::Window &window, ImVec2 min, ImVec2 max);
    void draw_controls(ui::Window &window, ImVec2 min, ImVec2 max);
    void handle_keys();

    App &app_;
    std::vector<std::unique_ptr<Channel>> ch_;
    core::ScopeEngine engine_;
    bool bus_ = true;
    bool cursors_ = false;
    double cursor_a_ = 0.3;
    double cursor_b_ = 0.7;
    int64_t latest_ns_ = -1;
    ImVec2 screen_min_;
    ImVec2 screen_max_;
};

}  // namespace app
