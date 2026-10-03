// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the oscilloscope: four channels, digital or analog according
// to the port wired to them, time base, trigger, cursors and measurements,
// drawn as a bench instrument: screen on the left, control block on the
// right. Analog channels share the screen like on a real scope, each with
// its volts/div and offset; digital channels get lanes at the bottom.
#pragma once

#include <imgui.h>

#include <array>

#include "app/instrument.h"
#include "core/analog_trace.h"
#include "core/math_formula.h"
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
    void feed_analog(const std::vector<core::AnalogBlock> &blocks) override;
    void wiring_changed() override;
    void save(nlohmann::json &out) const override;
    void load(const nlohmann::json &in) override;

    static constexpr int channels = 4;       // wired inputs
    static constexpr int math_channels = 2;  // M1, M2: computed from the inputs
    static constexpr int all_channels = channels + math_channels;

private:
    struct Channel {
        core::DigitalTrace trace;
        core::AnalogTrace analog;
        int port = -1;
        bool is_analog = false;
        bool visible = true;
        int volts_step = 5;         // index into volts_per_div_steps() (500 mV)
        float offset_div = 0.0f;    // vertical position, divisions from the centre
        // Math channels only.
        bool math = false;
        bool enabled = false;
        int formula = 1;            // index into core::formulas() (A AND B)
        int input[core::formula_inputs_max] = {0, 1, 2, 3};   // A..D as channel indexes (inputs or the other math)
    };

    void evaluate_math();
    void draw_formula_combo(int math_index, ImVec2 pos, ImVec2 size, float scale);
    bool wired_or_enabled(const Channel &ch) const { return ch.math ? ch.enabled : ch.port >= 0; }

    void draw_screen(ui::Window &window, ImVec2 min, ImVec2 max);
    void draw_analog(const Channel &ch, int index, ImDrawList *draw, float scale);
    void draw_digital(const Channel &ch, int index, float lane_top, float lane_bottom, ImDrawList *draw, float scale);
    void draw_controls(ui::Window &window, ImVec2 min, ImVec2 max);
    void handle_keys();
    void refresh_wiring();
    float time_to_x(int64_t ns) const;
    float volts_to_y(const Channel &ch, float volts) const;
    bool any_analog() const;

    App &app_;
    std::array<Channel, all_channels> ch_;
    core::ScopeEngine engine_;
    float trigger_level_ = 0.0f;    // volts, for an analog trigger source
    int selected_ = 0;              // channel whose measurements show
    bool cursors_ = false;
    double cursor_a_ = 0.3;         // fraction of the screen width
    double cursor_b_ = 0.7;
    int64_t latest_ns_ = -1;

    // Screen geometry of the current frame.
    ImVec2 screen_min_;
    ImVec2 screen_max_;
    ImVec2 analog_min_;             // the part of the screen for analog traces
    ImVec2 analog_max_;
};

}  // namespace app
