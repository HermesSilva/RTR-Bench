// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the DC power supply: as many outputs as needed (the ADD key
// makes one more), each with a set voltage, an OUTPUT key and seven-segment
// volt and ampere displays. On a digital target the output is the logic
// level the set voltage means (high from 1.8 V, the BCM2711 threshold); the
// ADALM2000 will drive real volts.
#pragma once

#include <imgui.h>

#include <chrono>
#include <memory>
#include <vector>

#include "app/instrument.h"

namespace app {

class App;

class Supply : public InstrumentBase {
public:
    explicit Supply(App &app);

    Instrument kind() const override { return Instrument::Supply; }
    int channel_count() const override { return static_cast<int>(outputs_.size()); }
    void draw(ui::Window &window) override;
    void feed(const std::vector<core::DigitalEvent> &events) override { (void)events; }
    void wiring_changed() override;
    void save(nlohmann::json &out) const override;
    void load(const nlohmann::json &in) override;

    static constexpr int max_outputs = 6;
    static constexpr float logic_threshold = 1.8f;
    static constexpr float volts_max = 5.0f;

private:
    struct Output {
        int port = -1;
        bool on = false;
        float volts = 3.3f;
        bool dirty = true;
    };
    void add_output();
    void remove_output(int index);
    void apply(Output &o);
    void fit_window(ui::Window &window);

    App &app_;
    std::vector<std::unique_ptr<Output>> outputs_;
    std::chrono::steady_clock::time_point last_apply_;
};

}  // namespace app
