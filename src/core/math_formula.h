// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the formulas of the math channels: a trace computed from one
// to four input channels (A, B, C, D). Digital formulas map levels to a
// level; analog ones map volts to volts. Pure: the scope evaluates them
// over its view.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/analog_trace.h"
#include "core/trace.h"

namespace core {

enum class FormulaKind { Digital, Analog };

constexpr int formula_inputs_max = 4;

struct Formula {
    const char *name;        // "A AND B"
    const char *group;       // "DIGITAL  2 inputs"
    FormulaKind kind;
    int inputs;              // 1..4
    // Digital: levels in, level out. `state` carries memory (the SR latch).
    int (*digital)(int a, int b, int c, int d, int &state);
    // Analog: volts in, volts out.
    float (*analog)(float a, float b, float c, float d);
};

const std::vector<Formula> &formulas();
int formula_count();

// Digital: output transitions of `formula` over [t0, t1] from the input
// traces (null inputs read as level 0). The output is cleared first.
void evaluate_digital(const Formula &formula, const DigitalTrace *const inputs[formula_inputs_max], int64_t t0,
                      int64_t t1, DigitalTrace &out);

// Analog: samples of `formula` over [t0, t1] at `samples` points from the
// input traces (null inputs read as 0 V). The output is cleared first.
void evaluate_analog(const Formula &formula, const AnalogTrace *const inputs[formula_inputs_max], int64_t t0,
                     int64_t t1, size_t samples, AnalogTrace &out);

}  // namespace core
