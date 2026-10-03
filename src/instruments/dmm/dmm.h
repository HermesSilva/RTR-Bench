// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the multimeter: as many probe tips as needed (the ADD key
// makes one more), each with its own function and a seven-segment readout
// with min/max/average. Digital ports: level, frequency, duty, pulse width,
// pulse count. Analog ports (demo, ADALM2000): V DC and V AC as well.
#pragma once

#include <imgui.h>

#include <chrono>
#include <memory>
#include <vector>

#include "app/instrument.h"
#include "core/analog_trace.h"
#include "core/trace.h"

namespace app {

class App;

class Multimeter : public InstrumentBase {
public:
    explicit Multimeter(App &app);

    Instrument kind() const override { return Instrument::Multimeter; }
    int channel_count() const override { return static_cast<int>(tips_.size()); }
    void draw(ui::Window &window) override;
    void feed(const std::vector<core::DigitalEvent> &events) override;
    void feed_analog(const std::vector<core::AnalogBlock> &blocks) override;
    void wiring_changed() override;
    void save(nlohmann::json &out) const override;
    void load(const nlohmann::json &in) override;

    enum class Function { VoltsDC, VoltsAC, Frequency, Duty, Width, Count, Level };
    static constexpr int functions = 7;
    static constexpr int max_tips = 8;

private:
    struct Reading {
        bool valid = false;
        double value = 0.0;
        char text[16] = "";
        char unit[16] = "";
    };
    struct Tip {
        core::DigitalTrace trace{1u << 16};
        core::AnalogTrace analog{1u << 16};
        int port = -1;
        bool is_analog = false;
        uint64_t pulses = 0;         // rising edges since the last reset
        Function function = Function::Frequency;
        bool hold = false;
        Reading shown;
        double min = 0.0;
        double max = 0.0;
        double sum = 0.0;
        uint64_t samples = 0;
    };
    void refresh_wiring();
    Reading measure(const Tip &tip) const;
    void add_tip();
    void remove_tip(int index);
    void fit_window(ui::Window &window);

    App &app_;
    std::vector<std::unique_ptr<Tip>> tips_;
    int64_t latest_ns_ = -1;
    int rate_ = 1;                   // 0: 2/s, 1: 5/s, 2: 10/s
    std::chrono::steady_clock::time_point last_update_;
};

}  // namespace app
