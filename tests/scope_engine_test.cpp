// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "core/scope_engine.h"
#include "core/trace.h"

using namespace core;

// A square wave: period `period`, high for `high`, starting with a rising
// edge at `start`, `cycles` cycles.
static void square(DigitalTrace &t, int64_t start, int64_t period, int64_t high, int cycles)
{
    for (int i = 0; i < cycles; i++) {
        t.add(start + i * period, 1);
        t.add(start + i * period + high, 0);
    }
}

TEST_CASE("digital trace ring and search")
{
    DigitalTrace t(4);
    REQUIRE(t.level_at(0) == -1);
    t.add(10, 1);
    t.add(20, 0);
    t.add(30, 1);
    REQUIRE(t.size() == 3);
    REQUIRE(t.level_at(5) == -1);
    REQUIRE(t.level_at(10) == 1);
    REQUIRE(t.level_at(25) == 0);
    REQUIRE(t.level_at(100) == 1);
    REQUIRE(t.lower_bound(20) == 1);
    REQUIRE(t.lower_bound(21) == 2);
    t.add(40, 0);
    t.add(50, 1);   // drops the first
    REQUIRE(t.size() == 4);
    REQUIRE(t.first_ns() == 20);
    REQUIRE(t.last_ns() == 50);
    t.snapshot(60, 1);   // same level: nothing recorded
    REQUIRE(t.size() == 4);
    t.snapshot(70, 0);
    REQUIRE(t.size() == 4);
    REQUIRE(t.last_ns() == 70);
}

TEST_CASE("measurements of a square wave")
{
    DigitalTrace t;
    square(t, 1000, 1000, 300, 10);   // 1 MHz-ish in ns: period 1 us, 30 %
    Measurements m = measure(t, 0, 20000);
    REQUIRE(m.valid);
    REQUIRE(m.period_ns == 1000);
    REQUIRE(m.high_ns == 300);
    REQUIRE(m.low_ns == 700);
    REQUIRE(m.rising == 10);
    REQUIRE(m.falling == 10);
    REQUIRE(m.duty > 0.29);
    REQUIRE(m.duty < 0.31);
    REQUIRE(m.frequency_hz > 999999.0);
    REQUIRE(m.min_period_ns == 1000);
    REQUIRE(m.max_period_ns == 1000);

    Measurements none = measure(t, 0, 1500);   // one rising edge only
    REQUIRE_FALSE(none.valid);
    REQUIRE(none.rising == 1);
}

TEST_CASE("trigger search respects the screen and the slope")
{
    DigitalTrace t;
    square(t, 1000, 1000, 500, 5);   // edges at 1000..5500
    // Screen of 2000 ns centred: pre 1000, post 1000. Latest known time 6000.
    REQUIRE(find_trigger(t, TriggerSlope::Rising, 6000, 1000, 1000, -1) == 5000);
    REQUIRE(find_trigger(t, TriggerSlope::Falling, 6000, 1000, 1000, -1) == 4500);
    REQUIRE(find_trigger(t, TriggerSlope::Either, 6000, 1000, 1000, -1) == 5000);
    // Not enough post-trigger data for the last edges.
    REQUIRE(find_trigger(t, TriggerSlope::Rising, 5200, 1000, 1000, -1) == 4000);
    // Holdoff: nothing after 5000 qualifies.
    REQUIRE(find_trigger(t, TriggerSlope::Rising, 6000, 1000, 1000, 5000) == -1);
    DigitalTrace empty;
    REQUIRE(find_trigger(empty, TriggerSlope::Rising, 6000, 1000, 1000, -1) == -1);
}

TEST_CASE("engine triggers, holds a single shot and free-runs in auto")
{
    DigitalTrace t;
    ScopeEngine e;
    e.settings.time_step = nearest_time_step(100);   // 100 ns/div, 1 us screen
    REQUIRE(e.ns_per_div() == 100);
    e.settings.mode = TriggerMode::Normal;
    e.update(&t, 5000);
    REQUIRE_FALSE(e.view().valid);   // normal mode waits for a trigger

    square(t, 1000, 1000, 500, 5);
    e.update(&t, 6000);
    REQUIRE(e.view().valid);
    REQUIRE(e.view().triggered);
    REQUIRE(e.view().trigger_ns == 5000);
    REQUIRE(e.view().t0 == 4500);
    REQUIRE(e.view().t1 == 5500);

    e.single();
    square(t, 6000, 1000, 500, 2);
    e.update(&t, 8000);
    REQUIRE(e.view().trigger_ns == 7000);
    REQUIRE_FALSE(e.running());
    e.update(&t, 9000);   // stopped: the view stays
    REQUIRE(e.view().trigger_ns == 7000);

    ScopeEngine a;
    a.settings.time_step = nearest_time_step(100);
    DigitalTrace quiet;
    a.update(&quiet, 100000);
    REQUIRE(a.view().valid);
    REQUIRE_FALSE(a.view().triggered);
    REQUIRE(a.view().t1 == 100000);
    REQUIRE(a.view().t0 == 99000);
}

TEST_CASE("time steps and formatting")
{
    REQUIRE(time_per_div_steps().front() == 10);
    REQUIRE(time_per_div_steps().back() == 10000000000LL);
    REQUIRE(time_per_div_steps()[static_cast<size_t>(nearest_time_step(1000000))] == 1000000);
    REQUIRE(time_per_div_steps()[static_cast<size_t>(nearest_time_step(1500000))] == 1000000);
    char buf[32];
    format_duration(buf, sizeof(buf), 1000000);
    REQUIRE(std::string(buf) == "1.00 ms");
    format_duration(buf, sizeof(buf), 291600);
    REQUIRE(std::string(buf) == "292 us");
    format_duration(buf, sizeof(buf), 50);
    REQUIRE(std::string(buf) == "50 ns");
    format_frequency(buf, sizeof(buf), 1028.8);
    REQUIRE(std::string(buf) == "1.029 kHz");
}
