// SPDX-License-Identifier: Apache-2.0
#include "core/probe.h"

#include <cmath>

namespace core {

const char *waveform_name(Waveform kind)
{
    switch (kind) {
    case Waveform::Off:
        return "OFF";
    case Waveform::Low:
        return "LOW";
    case Waveform::High:
        return "HIGH";
    case Waveform::Clock:
        return "CLOCK";
    case Waveform::Pwm:
        return "PWM";
    case Waveform::Burst:
        return "BURST";
    case Waveform::Sweep:
        return "SWEEP";
    case Waveform::Sine:
        return "SINE";
    case Waveform::Triangle:
        return "TRIANGLE";
    case Waveform::Sawtooth:
        return "SAWTOOTH";
    case Waveform::RampDown:
        return "RAMP DOWN";
    case Waveform::Noise:
        return "NOISE";
    case Waveform::Dc:
        return "DC";
    case Waveform::AM:
        return "AM";
    case Waveform::FM:
        return "FM";
    case Waveform::PM:
        return "PM";
    case Waveform::PwmMod:
        return "PWM MOD";
    }
    return "";
}

const char *waveform_group(Waveform kind)
{
    switch (kind) {
    case Waveform::Off:
    case Waveform::Low:
    case Waveform::High:
        return "LEVEL";
    case Waveform::Clock:
    case Waveform::Pwm:
    case Waveform::Burst:
    case Waveform::Sweep:
        return "DIGITAL";
    case Waveform::Sine:
    case Waveform::Triangle:
    case Waveform::Sawtooth:
    case Waveform::RampDown:
    case Waveform::Noise:
    case Waveform::Dc:
        return "ANALOG";
    case Waveform::AM:
    case Waveform::FM:
    case Waveform::PM:
    case Waveform::PwmMod:
        return "MODULATION";
    }
    return "";
}

bool waveform_is_analog(Waveform kind)
{
    return static_cast<int>(kind) >= static_cast<int>(Waveform::Sine);
}

double waveform_volts(const WaveSpec &s, double t)
{
    const double two_pi = 6.28318530717958647692;
    auto frac = [](double v) { return v - std::floor(v); };
    const double mod = std::sin(two_pi * s.mod_freq_hz * t);
    const double p = frac(s.freq_hz * t);
    switch (s.kind) {
    case Waveform::Off:
    case Waveform::Low:
        return 0.0;
    case Waveform::High:
        return logic_high_volts;
    case Waveform::Clock:
        return p < 0.5 ? logic_high_volts : 0.0;
    case Waveform::Pwm:
        return p < s.duty / 100.0 ? logic_high_volts : 0.0;
    case Waveform::Burst:
        // One burst per second, as on the other outputs.
        return frac(t) * s.freq_hz < s.burst_count && p < 0.5 ? logic_high_volts : 0.0;
    case Waveform::Sweep: {
        // The phase is the integral of a frequency that ramps over one second.
        double u = frac(t);
        double cycles = s.freq_hz * u + 0.5 * (s.sweep_end_hz - s.freq_hz) * u * u;
        return frac(cycles) < 0.5 ? logic_high_volts : 0.0;
    }
    case Waveform::Sine:
        return s.amplitude_v * std::sin(two_pi * p) + s.offset_v;
    case Waveform::AM:
        return s.amplitude_v * (1.0 + s.mod_depth * mod) / (1.0 + s.mod_depth) * std::sin(two_pi * p) + s.offset_v;
    case Waveform::FM: {
        double swing = s.mod_freq_hz > 0.0 ? s.freq_hz * s.mod_depth / (two_pi * s.mod_freq_hz) : 0.0;
        double cycles = s.freq_hz * t - swing * std::cos(two_pi * s.mod_freq_hz * t);
        return s.amplitude_v * std::sin(two_pi * cycles) + s.offset_v;
    }
    case Waveform::PM:
        return s.amplitude_v * std::sin(two_pi * (p + s.mod_depth * 0.5 * mod)) + s.offset_v;
    case Waveform::Triangle:
        return s.amplitude_v * (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p) + s.offset_v;
    case Waveform::Sawtooth:
        return s.amplitude_v * (2.0 * p - 1.0) + s.offset_v;
    case Waveform::RampDown:
        return s.amplitude_v * (1.0 - 2.0 * p) + s.offset_v;
    case Waveform::Noise: {
        // Held for 20 us, so that the same instant always has the same value.
        auto n = static_cast<uint64_t>(t / 20e-6);
        n = (n ^ (n >> 33)) * 0xFF51AFD7ED558CCDull;
        n = (n ^ (n >> 33)) * 0xC4CEB9FE1A85EC53ull;
        n ^= n >> 33;
        return s.amplitude_v * (static_cast<double>(n & 0xFFFFFFu) / 8388607.5 - 1.0) + s.offset_v;
    }
    case Waveform::Dc:
        return s.amplitude_v + s.offset_v;
    case Waveform::PwmMod:
        return (p < 0.5 + 0.45 * s.mod_depth * mod ? s.amplitude_v : -s.amplitude_v) + s.offset_v;
    }
    return 0.0;
}

// Probes without analog outputs get the digital kinds through the pattern
// command; the others are ignored.
void Probe::drive_waveform(int port, const WaveSpec &spec)
{
    switch (spec.kind) {
    case Waveform::Off:
    case Waveform::Low:
        drive_pattern(port, 0, 0);
        break;
    case Waveform::High:
        drive_pattern(port, 0, 1);
        break;
    case Waveform::Clock:
    case Waveform::Pwm:
    case Waveform::Burst:
    case Waveform::Sweep: {
        int64_t period = spec.freq_hz > 0.0 ? static_cast<int64_t>(1e9 / spec.freq_hz) : 0;
        int duty = spec.kind == Waveform::Pwm ? spec.duty : 50;
        drive_pattern(port, period, period * duty / 100);
        break;
    }
    case Waveform::Dc:
        drive_pattern(port, 0, spec.amplitude_v + spec.offset_v > 0.0 ? 1 : 0);
        break;
    default: {
        // An analog wave on a digital port: its logic level (zero crossing),
        // a clock at the carrier frequency.
        int64_t period = spec.freq_hz > 0.0 ? static_cast<int64_t>(1e9 / spec.freq_hz) : 0;
        drive_pattern(port, period, period / 2);
        break;
    }
    }
}

}  // namespace core
