// SPDX-License-Identifier: Apache-2.0
#include "probes/demo_probe.h"

#include <algorithm>
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
    for (int i = 0; i < input_ports; i++) {
        core::PortInfo p;
        p.index = digital_ports + analog_ports + i;
        p.name = "IN" + std::to_string(i);
        p.digital = true;
        p.analog = false;
        p.drivable = true;
        ports_.push_back(p);
    }
    for (int i = 0; i < analog_outputs; i++) {
        core::PortInfo p;
        p.index = analog_output_first + i;
        p.name = "AO" + std::to_string(i);
        p.digital = false;
        p.analog = true;
        p.drivable = true;
        ports_.push_back(p);
    }
    capabilities_.drive = true;
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

void DemoProbe::drive(int port, int level)
{
    drive_pattern(port, 0, level ? 1 : 0);
}

// period 0: a static level given by high_ns (0 or 1).
void DemoProbe::drive_pattern(int port, int64_t period_ns, int64_t high_ns)
{
    int i = port - (digital_ports + analog_ports);
    if (i < 0 || i >= input_ports) {
        return;
    }
    std::lock_guard<std::mutex> lock(drive_mutex_);
    Drive &d = drives_[i];
    if (period_ns <= 0) {
        d.period_ns = 0;
        d.high_ns = 0;
        d.level = high_ns ? 1 : 0;
    } else {
        d.period_ns = period_ns;
        d.high_ns = std::clamp<int64_t>(high_ns, 0, period_ns);
    }
    drive_changed_[i] = true;
}

// Analog outputs keep the whole specification and are rendered as samples;
// digital kinds on the input ports go through the pattern mechanism (burst
// and sweep included, see generate()).
void DemoProbe::drive_waveform(int port, const core::WaveSpec &spec)
{
    int ao = port - analog_output_first;
    if (ao >= 0 && ao < analog_outputs) {
        std::lock_guard<std::mutex> lock(drive_mutex_);
        waves_[ao] = spec;
        return;
    }
    int i = port - (digital_ports + analog_ports);
    if (i < 0 || i >= input_ports) {
        return;
    }
    if (spec.kind == core::Waveform::Burst || spec.kind == core::Waveform::Sweep) {
        std::lock_guard<std::mutex> lock(drive_mutex_);
        Drive &d = drives_[i];
        d.period_ns = spec.freq_hz > 0.0 ? static_cast<int64_t>(1e9 / spec.freq_hz) : 0;
        d.high_ns = d.period_ns / 2;
        d.burst = spec.kind == core::Waveform::Burst ? spec.burst_count : 0;
        d.sweep_end_hz = spec.kind == core::Waveform::Sweep ? spec.sweep_end_hz : 0.0;
        drive_changed_[i] = true;
        return;
    }
    core::Probe::drive_waveform(port, spec);
    std::lock_guard<std::mutex> lock(drive_mutex_);
    drives_[i].burst = 0;
    drives_[i].sweep_end_hz = 0.0;
}

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
    // Inputs driven by the bench: a static level changes at once, a pattern
    // is generated on its grid like the PWM above.
    {
        std::lock_guard<std::mutex> lock(drive_mutex_);
        for (int i = 0; i < input_ports; i++) {
            Drive &d = drives_[i];
            int slot = digital_ports + i;
            uint16_t port = static_cast<uint16_t>(digital_ports + analog_ports + i);
            if (d.period_ns == 0) {
                if (drive_changed_[i] && levels_[slot] != d.level) {
                    levels_[slot] = d.level;
                    events_.push(core::DigitalEvent{to_ns, port, static_cast<uint8_t>(d.level), core::DigitalEvent::Transition});
                    pushed_++;
                }
            } else {
                for (int64_t edge = (from_ns / d.period_ns) * d.period_ns; edge <= to_ns + d.period_ns; edge += d.period_ns) {
                    int64_t period = d.period_ns;
                    int64_t high = d.high_ns;
                    int64_t rise = edge;
                    // Burst: only the first `burst` pulses of every second.
                    if (d.burst > 0 && (edge % 1000000000LL) / d.period_ns >= d.burst) {
                        continue;
                    }
                    // Sweep: the frequency climbs from the start to the end
                    // frequency over every second; the pulse starts on the
                    // nominal grid and takes the swept width.
                    if (d.sweep_end_hz > 0.0) {
                        double f0 = 1e9 / static_cast<double>(d.period_ns);
                        double frac = static_cast<double>(edge % 1000000000LL) / 1e9;
                        double f = f0 + (d.sweep_end_hz - f0) * frac;
                        period = static_cast<int64_t>(1e9 / std::max(f, 1.0));
                        high = period / 2;
                    }
                    int64_t fall = rise + high;
                    if (rise > from_ns && rise <= to_ns && high > 0 && levels_[slot] != 1) {
                        levels_[slot] = 1;
                        events_.push(core::DigitalEvent{rise, port, 1, core::DigitalEvent::Transition});
                        pushed_++;
                    }
                    if (fall > from_ns && fall <= to_ns && high < period && levels_[slot] != 0) {
                        levels_[slot] = 0;
                        events_.push(core::DigitalEvent{fall, port, 0, core::DigitalEvent::Transition});
                        pushed_++;
                    }
                }
            }
            drive_changed_[i] = false;
        }
    }
    if (snapshot_pending_) {
        for (int i = 0; i < digital_ports; i++) {
            events_.push(core::DigitalEvent{to_ns, static_cast<uint16_t>(i), static_cast<uint8_t>(levels_[i]),
                                            core::DigitalEvent::Snapshot});
        }
        for (int i = 0; i < input_ports; i++) {
            events_.push(core::DigitalEvent{to_ns, static_cast<uint16_t>(digital_ports + analog_ports + i),
                                            static_cast<uint8_t>(levels_[digital_ports + i]), core::DigitalEvent::Snapshot});
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
    // Analog outputs: the waveform the generator asked for, rendered on the
    // same grid, phase carried across blocks.
    {
        std::lock_guard<std::mutex> lock(drive_mutex_);
        for (int ao = 0; ao < analog_outputs; ao++) {
            const core::WaveSpec &w = waves_[ao];
            core::AnalogBlock out;
            out.t0_ns = first;
            out.dt_ns = analog_dt_ns;
            out.port = static_cast<uint16_t>(analog_output_first + ao);
            out.volts.resize(count);
            double dt = static_cast<double>(analog_dt_ns) / 1e9;
            double &phase = wave_phase_[ao];
            for (size_t i = 0; i < count; i++) {
                double t = static_cast<double>(first + static_cast<int64_t>(i) * analog_dt_ns) / 1e9;
                double mod = std::sin(2.0 * pi * w.mod_freq_hz * t);
                double f = w.freq_hz;
                double amp = w.amplitude_v;
                double v = 0.0;
                if (w.kind == core::Waveform::FM) {
                    f = w.freq_hz * (1.0 + w.mod_depth * mod);
                } else if (w.kind == core::Waveform::AM) {
                    amp = w.amplitude_v * (1.0 + w.mod_depth * mod) / (1.0 + w.mod_depth);
                }
                phase += f * dt;
                phase -= std::floor(phase);
                double p = phase;
                if (w.kind == core::Waveform::PM) {
                    p = p + w.mod_depth * 0.5 * mod;
                    p -= std::floor(p);
                }
                switch (w.kind) {
                case core::Waveform::Sine:
                case core::Waveform::AM:
                case core::Waveform::FM:
                case core::Waveform::PM:
                    v = amp * std::sin(2.0 * pi * p);
                    break;
                case core::Waveform::Triangle:
                    v = amp * (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
                    break;
                case core::Waveform::Sawtooth:
                    v = amp * (2.0 * p - 1.0);
                    break;
                case core::Waveform::RampDown:
                    v = amp * (1.0 - 2.0 * p);
                    break;
                case core::Waveform::Noise:
                    v = amp * (static_cast<double>(noise(rng)) * 20.0);
                    break;
                case core::Waveform::Dc:
                    v = amp;
                    break;
                case core::Waveform::PwmMod: {
                    double duty = 0.5 + 0.45 * w.mod_depth * mod;
                    v = p < duty ? amp : -amp;
                    break;
                }
                case core::Waveform::Clock:
                    v = p < 0.5 ? amp : -amp;
                    break;
                case core::Waveform::Pwm:
                    v = p < static_cast<double>(w.duty) / 100.0 ? amp : -amp;
                    break;
                case core::Waveform::High:
                    v = amp;
                    break;
                default:
                    v = 0.0;
                    break;
                }
                out.volts[i] = static_cast<float>(v + w.offset_v);
            }
            blocks.push_back(std::move(out));
        }
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
