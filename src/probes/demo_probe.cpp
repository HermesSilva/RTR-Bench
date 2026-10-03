// SPDX-License-Identifier: Apache-2.0
#include "probes/demo_probe.h"

#include <chrono>
#include <cmath>
#include <random>

namespace probes {

namespace {

constexpr double pi = 3.14159265358979323846;

}  // namespace

DemoProbe::DemoProbe()
{
    capabilities_.name = "Demo";
    capabilities_.target = "Synthetic signals (no target)";
    capabilities_.observe = true;
    capabilities_.drive = false;
    capabilities_.analog = true;
    capabilities_.virtual_time = false;
    capabilities_.resolution_ns = 1;

    for (int i = 0; i < digital_ports; i++) {
        core::PortInfo p;
        p.index = i;
        p.name = "D" + std::to_string(i);
        p.digital = true;
        p.analog = false;
        ports_.push_back(p);
    }
    for (int i = 0; i < analog_ports; i++) {
        core::PortInfo p;
        p.index = digital_ports + i;
        p.name = "A" + std::to_string(i + 1);
        p.digital = false;
        p.analog = true;
        ports_.push_back(p);
    }
}

DemoProbe::~DemoProbe()
{
    running_.store(false);
    if (thread_.joinable()) {
        thread_.join();
    }
}

void DemoProbe::connect()
{
    if (running_.exchange(true)) {
        return;
    }
    generated_ns_ = 0;
    snapshot_pending_ = true;
    thread_ = std::thread([this] { run(); });
}

void DemoProbe::disconnect()
{
    if (!running_.exchange(false)) {
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    state_.store(core::ProbeState::Disconnected);
}

core::ProbeStats DemoProbe::stats() const
{
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void DemoProbe::poll(std::vector<core::DigitalEvent> &out)
{
    events_.drain(out);
}

void DemoProbe::poll_analog(std::vector<core::AnalogBlock> &out)
{
    std::lock_guard<std::mutex> lock(analog_mutex_);
    for (core::AnalogBlock &b : analog_) {
        out.push_back(std::move(b));
    }
    analog_.clear();
}

void DemoProbe::request_snapshot()
{
    snapshot_pending_ = true;
}

void DemoProbe::drive(int, int) {}

// Produces everything between two instants: digital edges at their exact
// times, analog samples on the 1 MS/s grid.
void DemoProbe::generate(int64_t from_ns, int64_t to_ns)
{
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> noise(-0.05f, 0.05f);

    // Digital: counter ticks every 100 us; PWM 1 kHz, high 300 us.
    const int64_t tick = 100000;
    for (int64_t t = (from_ns / tick + 1) * tick; t <= to_ns; t += tick) {
        int64_t n = t / tick;
        for (int bit = 0; bit < 8; bit++) {
            int level = static_cast<int>((n >> bit) & 1);
            if (level != levels_[bit]) {
                levels_[bit] = level;
                core::DigitalEvent e{t, static_cast<uint16_t>(bit), static_cast<uint8_t>(level),
                                     core::DigitalEvent::Transition};
                events_.push(e);
                pushed_++;
            }
        }
    }
    const int64_t pwm_period = 1000000;
    for (int64_t edge = (from_ns / pwm_period) * pwm_period; edge <= to_ns + pwm_period; edge += pwm_period) {
        int64_t rise = edge;
        int64_t fall = edge + 300000;
        if (rise > from_ns && rise <= to_ns) {
            levels_[8] = 1;
            events_.push(core::DigitalEvent{rise, 8, 1, core::DigitalEvent::Transition});
            pushed_++;
        }
        if (fall > from_ns && fall <= to_ns) {
            levels_[8] = 0;
            events_.push(core::DigitalEvent{fall, 8, 0, core::DigitalEvent::Transition});
            pushed_++;
        }
    }
    if (snapshot_pending_) {
        for (int i = 0; i < digital_ports; i++) {
            events_.push(core::DigitalEvent{to_ns, static_cast<uint16_t>(i), static_cast<uint8_t>(levels_[i]),
                                            core::DigitalEvent::Snapshot});
        }
        snapshot_pending_ = false;
    }

    // Analog: samples at every multiple of dt in (from, to].
    int64_t first = (from_ns / analog_dt_ns + 1) * analog_dt_ns;
    size_t count = first <= to_ns ? static_cast<size_t>((to_ns - first) / analog_dt_ns + 1) : 0;
    if (count == 0) {
        return;
    }
    std::vector<core::AnalogBlock> blocks(analog_ports);
    for (int c = 0; c < analog_ports; c++) {
        blocks[static_cast<size_t>(c)].t0_ns = first;
        blocks[static_cast<size_t>(c)].dt_ns = analog_dt_ns;
        blocks[static_cast<size_t>(c)].port = static_cast<uint16_t>(digital_ports + c);
        blocks[static_cast<size_t>(c)].volts.resize(count);
    }
    for (size_t i = 0; i < count; i++) {
        double t = static_cast<double>(first + static_cast<int64_t>(i) * analog_dt_ns) / 1e9;
        double phase1 = std::fmod(t * 1000.0, 1.0);   // 1 kHz
        double phase2 = std::fmod(t * 500.0, 1.0);    // 500 Hz
        double phase3 = std::fmod(t * 250.0, 1.0);    // 250 Hz
        double phase4 = std::fmod(t * 2000.0, 1.0);   // 2 kHz
        blocks[0].volts[i] = static_cast<float>(std::sin(2.0 * pi * phase1));
        blocks[1].volts[i] = static_cast<float>(0.5 + 1.5 * (phase2 < 0.5 ? 4.0 * phase2 - 1.0 : 3.0 - 4.0 * phase2));
        blocks[2].volts[i] = static_cast<float>(phase3 - 0.5);
        blocks[3].volts[i] = static_cast<float>(0.5 * std::sin(2.0 * pi * phase4)) + noise(rng);
    }
    std::lock_guard<std::mutex> lock(analog_mutex_);
    for (core::AnalogBlock &b : blocks) {
        analog_.push_back(std::move(b));
    }
}

void DemoProbe::run()
{
    state_.store(core::ProbeState::Connected);
    auto start = std::chrono::steady_clock::now();
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
        generate(generated_ns_, now);
        generated_ns_ = now;
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.last_ns = now;
        stats_.events = pushed_;
    }
    state_.store(core::ProbeState::Disconnected);
}

}  // namespace probes
