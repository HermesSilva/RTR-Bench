// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the generator: as many outputs as needed (the ADD key makes
// one more), two mini modules per row, each with a waveform dropdown
// (levels, digital patterns, analog waves, modulations), three contextual
// knobs and an OUTPUT key. The waveform is produced by the probe, at the
// target side.
#pragma once

#include <imgui.h>

#include <chrono>
#include <memory>
#include <vector>

#include "app/instrument.h"

namespace app {

class App;

class Generator : public InstrumentBase {
public:
    explicit Generator(App &app);

    Instrument kind() const override { return Instrument::Generator; }
    int channel_count() const override { return static_cast<int>(outputs_.size()); }
    void draw(ui::Window &window) override;
    void feed(const std::vector<core::DigitalEvent> &events) override { (void)events; }
    void wiring_changed() override;
    void save(nlohmann::json &out) const override;
    void load(const nlohmann::json &in) override;

    static constexpr int max_outputs = 8;

private:
    struct Output {
        int port = -1;
        bool on = false;
        core::WaveSpec spec;
        int freq_step = 9;        // 1-2-5 index (1 kHz)
        int mod_step = 6;         // 1-2-5 index of the modulation frequency (100 Hz)
        int sweep_step = 12;      // 1-2-5 index of the sweep end (10 kHz)
        bool dirty = true;
    };
    void add_output();
    void remove_output(int index);
    void apply(Output &o);
    void fit_window(ui::Window &window);
    void draw_module(ui::Window &window, int index, ImVec2 min, ImVec2 max);
    void draw_waveform_combo(Output &o, int index, ImVec2 pos, ImVec2 size, bool analog_port, float scale);

    App &app_;
    std::vector<std::unique_ptr<Output>> outputs_;
    std::chrono::steady_clock::time_point last_apply_;
};

// 1-2-5 steps from 1 Hz to 1 MHz, in Hz.
const std::vector<double> &generator_frequency_steps();

}  // namespace app
