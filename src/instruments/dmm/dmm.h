// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the multimeter: as many probe tips as needed (the ADD key
// makes one more), each with its own function and a seven-segment readout
// with min/max/average. Every tip has two jacks: the tip itself and COM,
// its reference; COM unwired is the bench ground. Every function works on
// any port: a digital port is read as its logic levels (0 V / 3.3 V), an
// analog port is squared at the middle of its swing for the timing
// functions.
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
    // Two channels per tip: 2i is the tip, 2i+1 its COM.
    int channel_count() const override { return static_cast<int>(tips_.size()) * 2; }
    int channel_colour_index(int channel) const override { return channel / 2; }
    std::string channel_name(int channel) const override;
    void draw(ui::Window &window) override;
    void feed(const std::vector<core::DigitalEvent> &events) override;
    void feed_analog(const std::vector<core::AnalogBlock> &blocks) override;
    void wiring_changed() override;
    void save(nlohmann::json &out) const override;
    void load(const nlohmann::json &in) override;

    enum class Function { VoltsDC, VoltsAC, VoltsPP, Frequency, Period, Duty, Width, Count, Level };
    static constexpr int functions = 9;
    static constexpr int max_tips = 8;

private:
    struct Reading {
        bool valid = false;
        double value = 0.0;
        char text[16] = "";
        char unit[16] = "";
    };
    // One end of a tip: what is wired there and what it has received.
    struct Lead {
        core::DigitalTrace trace{1u << 16};
        core::AnalogTrace analog{1u << 16};
        int port = -1;
        bool is_analog = false;
        void reset();
        // The voltage at `ns`: logic levels on a digital port.
        bool volts_at(int64_t ns, float &v) const;
    };
    struct Tip {
        Lead tip;
        Lead com;
        uint64_t pulses = 0;         // rising edges since the last reset
        Function function = Function::VoltsDC;
        bool hold = false;
        Reading shown;
        double min = 0.0;
        double max = 0.0;
        double sum = 0.0;
        uint64_t samples = 0;
    };
    struct VoltStats {
        bool valid = false;
        double mean = 0.0;
        double rms = 0.0;
        double vmin = 0.0;
        double vmax = 0.0;
    };
    void refresh_wiring();
    void refresh_lead(Lead &lead, int channel);
    Reading measure(const Tip &tip) const;
    // Voltage statistics of tip minus COM over [t0, t1].
    VoltStats volt_stats(const Tip &tip, int64_t t0, int64_t t1) const;
    // The tip's signal as a digital trace over [t0, t1] (the analog one
    // squared at the middle of its swing) for the timing functions.
    bool timing_trace(const Tip &tip, int64_t t0, int64_t t1, core::DigitalTrace &out) const;
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
