// SPDX-License-Identifier: Apache-2.0
#include "core/scope_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace core {

const std::vector<int64_t> &time_per_div_steps()
{
    static const std::vector<int64_t> steps = [] {
        std::vector<int64_t> v;
        for (int64_t decade = 10; decade <= 1000000000LL; decade *= 10) {
            v.push_back(decade);
            v.push_back(decade * 2);
            v.push_back(decade * 5);
        }
        v.push_back(10000000000LL);  // 10 s
        return v;
    }();
    return steps;
}

int nearest_time_step(int64_t ns_per_div)
{
    const std::vector<int64_t> &steps = time_per_div_steps();
    int best = 0;
    for (size_t i = 0; i < steps.size(); i++) {
        if (steps[i] <= ns_per_div) {
            best = static_cast<int>(i);
        }
    }
    return best;
}

void format_duration(char *out, size_t size, int64_t ns)
{
    bool negative = ns < 0;
    double v = static_cast<double>(negative ? -ns : ns);
    const char *unit = "ns";
    if (v >= 1e9) {
        v /= 1e9;
        unit = "s";
    } else if (v >= 1e6) {
        v /= 1e6;
        unit = "ms";
    } else if (v >= 1e3) {
        v /= 1e3;
        unit = "us";
    }
    if (v >= 100.0 || unit[0] == 'n') {
        std::snprintf(out, size, "%s%.0f %s", negative ? "-" : "", v, unit);
    } else if (v >= 10.0) {
        std::snprintf(out, size, "%s%.1f %s", negative ? "-" : "", v, unit);
    } else {
        std::snprintf(out, size, "%s%.2f %s", negative ? "-" : "", v, unit);
    }
}

void format_frequency(char *out, size_t size, double hz)
{
    const char *unit = "Hz";
    if (hz >= 1e6) {
        hz /= 1e6;
        unit = "MHz";
    } else if (hz >= 1e3) {
        hz /= 1e3;
        unit = "kHz";
    }
    if (hz >= 100.0) {
        std::snprintf(out, size, "%.1f %s", hz, unit);
    } else {
        std::snprintf(out, size, "%.3f %s", hz, unit);
    }
}

int64_t find_trigger(const DigitalTrace &source, TriggerSlope slope, int64_t latest_ns, int64_t pre_ns,
                     int64_t post_ns, int64_t after_ns)
{
    if (source.size() < 2) {
        return -1;
    }
    // Walk back from the newest transition until one fits the screen.
    size_t i = source.size();
    while (i-- > 1) {
        const Transition &t = source.at(i);
        if (t.ns <= after_ns) {
            return -1;
        }
        if (t.ns + post_ns > latest_ns) {
            continue;
        }
        if (t.ns - pre_ns < source.first_ns() && source.size() == source.capacity()) {
            return -1;  // the pre-trigger part has been overwritten
        }
        bool rising = t.level != 0;
        if (slope == TriggerSlope::Either || (slope == TriggerSlope::Rising) == rising) {
            return t.ns;
        }
    }
    return -1;
}

int64_t find_analog_trigger(const AnalogTrace &source, float level, TriggerSlope slope, int64_t latest_ns,
                            int64_t pre_ns, int64_t post_ns, int64_t after_ns)
{
    if (source.size() < 2) {
        return -1;
    }
    int64_t dt = source.dt_ns();
    size_t i = source.size();
    while (i-- > 1) {
        int64_t t = source.first_ns() + static_cast<int64_t>(i) * dt;
        if (t <= after_ns) {
            return -1;
        }
        if (t + post_ns > latest_ns) {
            continue;
        }
        if (t - pre_ns < source.first_ns()) {
            return -1;
        }
        float prev = source.at(i - 1);
        float cur = source.at(i);
        bool rising = prev < level && cur >= level;
        bool falling = prev >= level && cur < level;
        if ((slope == TriggerSlope::Rising && rising) || (slope == TriggerSlope::Falling && falling) ||
            (slope == TriggerSlope::Either && (rising || falling))) {
            // Interpolate the crossing inside the sample interval.
            float span = cur - prev;
            float f = span != 0.0f ? (level - prev) / span : 0.0f;
            return t - dt + static_cast<int64_t>(f * static_cast<float>(dt));
        }
    }
    return -1;
}

AnalogMeasurements measure_analog(const AnalogTrace &trace, int64_t t0, int64_t t1)
{
    AnalogMeasurements m;
    size_t begin = trace.index_at(t0);
    size_t end = trace.index_at(t1 + 1);
    if (begin >= end) {
        return m;
    }
    m.valid = true;
    double sum = 0.0;
    double sum_sq = 0.0;
    m.vmin = trace.at(begin);
    m.vmax = m.vmin;
    for (size_t i = begin; i < end; i++) {
        float v = trace.at(i);
        m.vmin = std::min(m.vmin, v);
        m.vmax = std::max(m.vmax, v);
        sum += v;
        sum_sq += static_cast<double>(v) * v;
    }
    double n = static_cast<double>(end - begin);
    m.vpp = m.vmax - m.vmin;
    m.vmean = static_cast<float>(sum / n);
    m.vrms = static_cast<float>(std::sqrt(sum_sq / n));

    // Frequency from rising crossings of the mean, with hysteresis of 10 % of
    // the swing so noise does not count.
    float hyst = m.vpp * 0.1f;
    if (hyst <= 0.0f) {
        return m;
    }
    float hi = m.vmean + hyst * 0.5f;
    float lo = m.vmean - hyst * 0.5f;
    bool below = trace.at(begin) < lo;
    int64_t first_cross = -1;
    int64_t last_cross = -1;
    int crossings = 0;
    for (size_t i = begin + 1; i < end; i++) {
        float v = trace.at(i);
        if (below && v >= hi) {
            int64_t t = trace.first_ns() + static_cast<int64_t>(i) * trace.dt_ns();
            if (first_cross < 0) {
                first_cross = t;
            } else {
                crossings++;
            }
            last_cross = t;
            below = false;
        } else if (!below && v < lo) {
            below = true;
        }
    }
    if (crossings > 0) {
        m.periodic = true;
        m.period_ns = (last_cross - first_cross) / crossings;
        m.frequency_hz = m.period_ns > 0 ? 1e9 / static_cast<double>(m.period_ns) : 0.0;
    }
    return m;
}

const std::vector<float> &volts_per_div_steps()
{
    static const std::vector<float> steps = {0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f};
    return steps;
}

void format_volts(char *out, size_t size, float volts)
{
    float a = volts < 0.0f ? -volts : volts;
    if (a < 1.0f) {
        std::snprintf(out, size, "%.0f mV", static_cast<double>(volts) * 1000.0);
    } else if (a < 10.0f) {
        std::snprintf(out, size, "%.2f V", static_cast<double>(volts));
    } else {
        std::snprintf(out, size, "%.1f V", static_cast<double>(volts));
    }
}

Measurements measure(const DigitalTrace &trace, int64_t t0, int64_t t1)
{
    Measurements m;
    size_t begin = trace.lower_bound(t0);
    size_t end = trace.lower_bound(t1 + 1);
    int64_t first_rise = -1;
    int64_t last_rise = -1;
    int64_t prev_rise = -1;
    int64_t high_total = 0;
    int64_t low_total = 0;
    int high_count = 0;
    int low_count = 0;
    int64_t prev_ns = -1;
    int prev_level = -1;
    int periods = 0;

    for (size_t i = begin; i < end; i++) {
        const Transition &t = trace.at(i);
        if (t.level) {
            m.rising++;
            if (first_rise < 0) {
                first_rise = t.ns;
            }
            if (prev_rise >= 0) {
                int64_t p = t.ns - prev_rise;
                periods++;
                if (m.min_period_ns == 0 || p < m.min_period_ns) {
                    m.min_period_ns = p;
                }
                if (p > m.max_period_ns) {
                    m.max_period_ns = p;
                }
            }
            prev_rise = t.ns;
            last_rise = t.ns;
        } else {
            m.falling++;
        }
        if (prev_ns >= 0 && first_rise >= 0) {
            // Widths only inside complete periods: after the first rising edge.
            if (prev_level == 1) {
                high_total += t.ns - prev_ns;
                high_count++;
            } else if (prev_level == 0 && prev_ns >= first_rise) {
                low_total += t.ns - prev_ns;
                low_count++;
            }
        }
        prev_ns = t.ns;
        prev_level = t.level;
    }

    if (periods > 0 && last_rise > first_rise) {
        m.valid = true;
        m.period_ns = (last_rise - first_rise) / periods;
        m.frequency_hz = m.period_ns > 0 ? 1e9 / static_cast<double>(m.period_ns) : 0.0;
        m.high_ns = high_count ? high_total / high_count : 0;
        m.low_ns = low_count ? low_total / low_count : 0;
        m.duty = m.period_ns > 0 ? static_cast<double>(m.high_ns) / static_cast<double>(m.period_ns) : 0.0;
    }
    return m;
}

void ScopeEngine::clear()
{
    view_ = ScopeView{};
    last_trigger_ns_ = -1;
    last_update_ns_ = -1;
}

int64_t ScopeEngine::window_ns() const
{
    return ns_per_div() * scope_divisions;
}

void ScopeEngine::update(const TriggerSource &source, int64_t latest_ns)
{
    if (!running_ || latest_ns < 0) {
        return;
    }
    int64_t width = window_ns();
    int64_t pre = static_cast<int64_t>(static_cast<double>(width) * settings.trigger_fraction);
    int64_t post = width - pre;
    bool roll = ns_per_div() >= 100000000LL;  // 100 ms/div and slower

    if (!roll && (source.digital || source.analog)) {
        int64_t after = last_trigger_ns_ >= 0 ? last_trigger_ns_ + settings.holdoff_ns : -1;
        int64_t trigger = source.digital
                              ? find_trigger(*source.digital, settings.slope, latest_ns, pre, post, after)
                              : find_analog_trigger(*source.analog, source.level, settings.slope, latest_ns, pre,
                                                    post, after);
        if (trigger >= 0) {
            view_.t0 = trigger - pre + settings.position_ns;
            view_.t1 = trigger + post + settings.position_ns;
            view_.trigger_ns = trigger;
            view_.valid = true;
            view_.triggered = true;
            last_trigger_ns_ = trigger;
            last_update_ns_ = latest_ns;
            if (armed_single_) {
                running_ = false;
                armed_single_ = false;
            }
            return;
        }
    }
    // No trigger: auto mode (and roll) free-runs on the latest data after a
    // screen's worth of time without a trigger; normal and single wait.
    bool free_run = roll || settings.mode == TriggerMode::Auto;
    if (free_run && (last_update_ns_ < 0 || latest_ns - last_update_ns_ >= width || roll)) {
        view_.t1 = latest_ns + settings.position_ns;
        view_.t0 = view_.t1 - width;
        view_.trigger_ns = -1;
        view_.valid = true;
        view_.triggered = false;
        last_update_ns_ = latest_ns;
    }
}

void ScopeEngine::pan(int64_t delta_ns)
{
    if (running_) {
        settings.position_ns += delta_ns;
    } else if (view_.valid) {
        view_.t0 += delta_ns;
        view_.t1 += delta_ns;
    }
}

}  // namespace core
