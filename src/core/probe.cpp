// SPDX-License-Identifier: Apache-2.0
#include "core/probe.h"

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
    default:
        break;
    }
}

}  // namespace core
