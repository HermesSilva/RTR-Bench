// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "core/math_formula.h"

using namespace core;

static const Formula &by_name(const char *name)
{
    for (const Formula &f : formulas()) {
        if (std::strcmp(f.name, name) == 0) {
            return f;
        }
    }
    FAIL("no formula " << name);
    return formulas()[0];
}

TEST_CASE("digital AND of two square waves")
{
    DigitalTrace a;
    DigitalTrace b;
    // a: high 0..50, low 50..100, high 100..150 ...; b: high 25..75, low 75..125 ...
    for (int i = 0; i < 4; i++) {
        a.add(i * 100, 1);
        a.add(i * 100 + 50, 0);
        b.add(i * 100 + 25, 1);
        b.add(i * 100 + 75, 0);
    }
    DigitalTrace out(256);
    { const DigitalTrace *in[4] = {&a, &b, nullptr, nullptr}; evaluate_digital(by_name("A AND B"), in, 0, 400, out); }
    // Overlap 25..50 in every period: rising at 25, falling at 50.
    REQUIRE(out.size() >= 8);
    REQUIRE(out.at(0).ns == 0);
    REQUIRE(out.at(0).level == 0);
    REQUIRE(out.at(1).ns == 25);
    REQUIRE(out.at(1).level == 1);
    REQUIRE(out.at(2).ns == 50);
    REQUIRE(out.at(2).level == 0);
    REQUIRE(out.level_at(130) == 1);
    REQUIRE(out.level_at(160) == 0);
}

TEST_CASE("digital formulas with missing inputs read zero")
{
    DigitalTrace a;
    a.add(10, 1);
    DigitalTrace out(64);
    { const DigitalTrace *in[4] = {&a, nullptr, nullptr, nullptr}; evaluate_digital(by_name("A OR B"), in, 0, 100, out); }
    REQUIRE(out.level_at(5) == 0);
    REQUIRE(out.level_at(50) == 1);
    { const DigitalTrace *in[4] = {&a, nullptr, nullptr, nullptr}; evaluate_digital(by_name("NOT A"), in, 0, 100, out); }
    REQUIRE(out.level_at(5) == 1);
    REQUIRE(out.level_at(50) == 0);
}

TEST_CASE("SR latch keeps its state")
{
    DigitalTrace set;
    DigitalTrace reset;
    set.add(10, 1);
    set.add(20, 0);
    reset.add(60, 1);
    reset.add(70, 0);
    DigitalTrace out(64);
    { const DigitalTrace *in[4] = {&set, &reset, nullptr, nullptr}; evaluate_digital(by_name("SR LATCH  A set, B reset"), in, 0, 100, out); }
    REQUIRE(out.level_at(5) == 0);
    REQUIRE(out.level_at(15) == 1);
    REQUIRE(out.level_at(40) == 1);   // held after the set pulse
    REQUIRE(out.level_at(65) == 0);
    REQUIRE(out.level_at(90) == 0);
}

TEST_CASE("three-input majority")
{
    DigitalTrace a;
    DigitalTrace b;
    DigitalTrace c;
    a.add(10, 1);
    b.add(20, 1);
    c.add(30, 1);
    b.add(40, 0);
    DigitalTrace out(64);
    { const DigitalTrace *in[4] = {&a, &b, &c, nullptr}; evaluate_digital(by_name("MAJORITY A, B, C"), in, 0, 100, out); }
    REQUIRE(out.level_at(15) == 0);
    REQUIRE(out.level_at(25) == 1);
    REQUIRE(out.level_at(35) == 1);
    REQUIRE(out.level_at(45) == 1);   // a and c still high
}

TEST_CASE("analog sum and difference")
{
    float va[5] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    float vb[5] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    AnalogTrace a(16);
    AnalogTrace b(16);
    a.add(0, 10, va, 5);
    b.add(0, 10, vb, 5);
    AnalogTrace out(64);
    { const AnalogTrace *in[4] = {&a, &b, nullptr, nullptr}; evaluate_analog(by_name("A + B"), in, 0, 40, 5, out); }
    REQUIRE(out.size() == 5);
    REQUIRE(out.at(0) == 1.0f);
    REQUIRE(out.at(4) == 5.0f);
    { const AnalogTrace *in[4] = {&a, &b, nullptr, nullptr}; evaluate_analog(by_name("A - B"), in, 0, 40, 5, out); }
    REQUIRE(out.at(2) == 1.0f);
    { const AnalogTrace *in[4] = {&a, nullptr, nullptr, nullptr}; evaluate_analog(by_name("A / B"), in, 0, 40, 5, out); }   // b missing: 0 V, no division
    REQUIRE(out.at(2) == 0.0f);
}

TEST_CASE("four-input parity and average")
{
    DigitalTrace a;
    DigitalTrace b;
    DigitalTrace c;
    DigitalTrace d;
    a.add(10, 1);
    b.add(20, 1);
    c.add(30, 1);
    d.add(40, 1);
    DigitalTrace out(64);
    const DigitalTrace *in[4] = {&a, &b, &c, &d};
    evaluate_digital(by_name("PARITY A, B, C, D"), in, 0, 100, out);
    REQUIRE(out.level_at(15) == 1);
    REQUIRE(out.level_at(25) == 0);
    REQUIRE(out.level_at(35) == 1);
    REQUIRE(out.level_at(45) == 0);
    float v[2] = {1.0f, 3.0f};
    AnalogTrace x(8);
    x.add(0, 10, v, 2);
    AnalogTrace y(8);
    y.add(0, 10, v, 2);
    const AnalogTrace *ain[4] = {&x, &y, &x, &y};
    AnalogTrace aout(16);
    evaluate_analog(by_name("AVG(A, B, C, D)"), ain, 0, 10, 2, aout);
    REQUIRE(aout.at(0) == 1.0f);
    REQUIRE(aout.at(1) == 3.0f);
}

TEST_CASE("formula table is consistent")
{
    for (const Formula &f : formulas()) {
        REQUIRE(f.inputs >= 1);
        REQUIRE(f.inputs <= 4);
        if (f.kind == FormulaKind::Digital) {
            REQUIRE(static_cast<bool>(f.digital));
        } else {
            REQUIRE(static_cast<bool>(f.analog));
        }
    }
}
