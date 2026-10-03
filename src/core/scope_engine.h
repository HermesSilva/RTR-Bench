// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the acquisition logic of the oscilloscope: time base, trigger
// and measurements over digital traces. Pure: no drawing, no I/O.
#pragma once

#include <cstdint>
#include <vector>

#include "core/trace.h"

namespace core {

constexpr int scope_divisions = 10;   // horizontal divisions on the screen

// 1-2-5 steps from 10 ns/div to 10 s/div.
const std::vector<int64_t> &time_per_div_steps();
int nearest_time_step(int64_t ns_per_div);
// "1 ms", "500 µs" ... for a time in ns; short SI with the given precision.
void format_duration(char *out, size_t size, int64_t ns);
void format_frequency(char *out, size_t size, double hz);

enum class TriggerSlope { Rising, Falling, Either };
enum class TriggerMode { Auto, Normal, Single };

struct ScopeSettings {
    int time_step = 15;             // index into time_per_div_steps() (1 ms)
    int64_t position_ns = 0;        // horizontal offset of the trigger point
    int trigger_channel = 0;
    TriggerSlope slope = TriggerSlope::Rising;
    TriggerMode mode = TriggerMode::Auto;
    int64_t holdoff_ns = 0;
    double trigger_fraction = 0.5;  // where the trigger sits on the screen (0 left, 1 right)
};

struct ScopeView {
    int64_t t0 = 0;                 // left edge of the screen, ns
    int64_t t1 = 0;                 // right edge
    int64_t trigger_ns = -1;        // -1: free run
    bool valid = false;
    bool triggered = false;
};

struct Measurements {
    bool valid = false;             // at least one full period in the window
    double frequency_hz = 0.0;
    int64_t period_ns = 0;
    int64_t high_ns = 0;            // mean positive width
    int64_t low_ns = 0;             // mean negative width
    double duty = 0.0;              // 0..1
    int rising = 0;                 // edges in the window
    int falling = 0;
    int64_t min_period_ns = 0;
    int64_t max_period_ns = 0;
};

// The latest trigger on `source` whose screen fits before `latest_ns`:
// returns the trigger time or -1. `after_ns` excludes triggers at or before
// it (the previous one plus holdoff).
int64_t find_trigger(const DigitalTrace &source, TriggerSlope slope, int64_t latest_ns, int64_t pre_ns,
                     int64_t post_ns, int64_t after_ns);

Measurements measure(const DigitalTrace &trace, int64_t t0, int64_t t1);

class ScopeEngine {
public:
    ScopeSettings settings;

    bool running() const { return running_; }
    void run() { running_ = true; armed_single_ = false; }
    void stop() { running_ = false; }
    void single() { running_ = true; armed_single_ = true; }
    void clear();

    int64_t window_ns() const;
    int64_t ns_per_div() const { return time_per_div_steps()[static_cast<size_t>(settings.time_step)]; }

    // Decides the view for this frame from the trigger source and the latest
    // time known to the probe. `source` may be null (channel unwired).
    void update(const DigitalTrace *source, int64_t latest_ns);
    const ScopeView &view() const { return view_; }

    // Moves the view when stopped (scrolling through memory) or shifts the
    // position when running.
    void pan(int64_t delta_ns);

private:
    ScopeView view_;
    bool running_ = true;
    bool armed_single_ = false;
    int64_t last_trigger_ns_ = -1;
    int64_t last_update_ns_ = -1;   // latest_ns at the last view change
};

}  // namespace core
