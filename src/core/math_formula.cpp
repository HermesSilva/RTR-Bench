// SPDX-License-Identifier: Apache-2.0
#include "core/math_formula.h"

#include <algorithm>
#include <cmath>

namespace core {

namespace {

// ---- digital ----------------------------------------------------------------
int f_not(int a, int, int, int, int &) { return a ? 0 : 1; }
int f_and(int a, int b, int, int, int &) { return (a && b) ? 1 : 0; }
int f_or(int a, int b, int, int, int &) { return (a || b) ? 1 : 0; }
int f_xor(int a, int b, int, int, int &) { return (a != b) ? 1 : 0; }
int f_nand(int a, int b, int, int, int &) { return (a && b) ? 0 : 1; }
int f_nor(int a, int b, int, int, int &) { return (a || b) ? 0 : 1; }
int f_xnor(int a, int b, int, int, int &) { return (a == b) ? 1 : 0; }
int f_a_not_b(int a, int b, int, int, int &) { return (a && !b) ? 1 : 0; }
// SR latch: A sets, B resets, reset wins; the state is the output.
int f_sr(int a, int b, int, int, int &q)
{
    if (b) {
        q = 0;
    } else if (a) {
        q = 1;
    }
    return q;
}
int f_and3(int a, int b, int c, int, int &) { return (a && b && c) ? 1 : 0; }
int f_or3(int a, int b, int c, int, int &) { return (a || b || c) ? 1 : 0; }
int f_xor3(int a, int b, int c, int, int &) { return ((a ^ b ^ c) & 1) ? 1 : 0; }
int f_majority(int a, int b, int c, int, int &) { return (a + b + c >= 2) ? 1 : 0; }
int f_gated(int a, int, int c, int, int &) { return (a && c) ? 1 : 0; }
int f_mux(int a, int b, int c, int, int &) { return c ? b : a; }
int f_and4(int a, int b, int c, int d, int &) { return (a && b && c && d) ? 1 : 0; }
int f_or4(int a, int b, int c, int d, int &) { return (a || b || c || d) ? 1 : 0; }
int f_parity4(int a, int b, int c, int d, int &) { return ((a + b + c + d) & 1) ? 1 : 0; }
int f_two_of_four(int a, int b, int c, int d, int &) { return (a + b + c + d >= 2) ? 1 : 0; }
int f_ab_or_cd(int a, int b, int c, int d, int &) { return ((a && b) || (c && d)) ? 1 : 0; }
// D-type latch: A is the data, B the enable.
int f_d_latch(int a, int b, int, int, int &q)
{
    if (b) {
        q = a;
    }
    return q;
}

// ---- analog ---------------------------------------------------------------
float g_neg(float a, float, float, float) { return -a; }
float g_abs(float a, float, float, float) { return std::fabs(a); }
float g_sq(float a, float, float, float) { return a * a; }
float g_add(float a, float b, float, float) { return a + b; }
float g_sub(float a, float b, float, float) { return a - b; }
float g_mul(float a, float b, float, float) { return a * b; }
float g_div(float a, float b, float, float) { return std::fabs(b) > 1e-6f ? a / b : 0.0f; }
float g_avg2(float a, float b, float, float) { return (a + b) * 0.5f; }
float g_min(float a, float b, float, float) { return std::min(a, b); }
float g_max(float a, float b, float, float) { return std::max(a, b); }
float g_sum3(float a, float b, float c, float) { return a + b + c; }
float g_avg3(float a, float b, float c, float) { return (a + b + c) / 3.0f; }
float g_a_minus_b_times_c(float a, float b, float c, float) { return (a - b) * c; }
float g_sum4(float a, float b, float c, float d) { return a + b + c + d; }
float g_avg4(float a, float b, float c, float d) { return (a + b + c + d) * 0.25f; }
float g_ab_minus_cd(float a, float b, float c, float d) { return a * b - c * d; }
float g_a_minus_b_over_c_minus_d(float a, float b, float c, float d)
{
    float den = c - d;
    return std::fabs(den) > 1e-6f ? (a - b) / den : 0.0f;
}

const std::vector<Formula> &table()
{
    static const std::vector<Formula> list = {
        {"NOT A", "DIGITAL  1 input", FormulaKind::Digital, 1, f_not, nullptr},
        {"A AND B", "DIGITAL  2 inputs", FormulaKind::Digital, 2, f_and, nullptr},
        {"A OR B", "DIGITAL  2 inputs", FormulaKind::Digital, 2, f_or, nullptr},
        {"A XOR B", "DIGITAL  2 inputs", FormulaKind::Digital, 2, f_xor, nullptr},
        {"A NAND B", "DIGITAL  2 inputs", FormulaKind::Digital, 2, f_nand, nullptr},
        {"A NOR B", "DIGITAL  2 inputs", FormulaKind::Digital, 2, f_nor, nullptr},
        {"A XNOR B", "DIGITAL  2 inputs", FormulaKind::Digital, 2, f_xnor, nullptr},
        {"A AND NOT B", "DIGITAL  2 inputs", FormulaKind::Digital, 2, f_a_not_b, nullptr},
        {"SR LATCH  A set, B reset", "DIGITAL  2 inputs", FormulaKind::Digital, 2, f_sr, nullptr},
        {"D LATCH  A data, B enable", "DIGITAL  2 inputs", FormulaKind::Digital, 2, f_d_latch, nullptr},
        {"A AND B AND C", "DIGITAL  3 inputs", FormulaKind::Digital, 3, f_and3, nullptr},
        {"A OR B OR C", "DIGITAL  3 inputs", FormulaKind::Digital, 3, f_or3, nullptr},
        {"A XOR B XOR C", "DIGITAL  3 inputs", FormulaKind::Digital, 3, f_xor3, nullptr},
        {"MAJORITY A, B, C", "DIGITAL  3 inputs", FormulaKind::Digital, 3, f_majority, nullptr},
        {"A GATED BY C", "DIGITAL  3 inputs", FormulaKind::Digital, 3, f_gated, nullptr},
        {"C ? B : A", "DIGITAL  3 inputs", FormulaKind::Digital, 3, f_mux, nullptr},
        {"A AND B AND C AND D", "DIGITAL  4 inputs", FormulaKind::Digital, 4, f_and4, nullptr},
        {"A OR B OR C OR D", "DIGITAL  4 inputs", FormulaKind::Digital, 4, f_or4, nullptr},
        {"PARITY A, B, C, D", "DIGITAL  4 inputs", FormulaKind::Digital, 4, f_parity4, nullptr},
        {"2 OF A, B, C, D", "DIGITAL  4 inputs", FormulaKind::Digital, 4, f_two_of_four, nullptr},
        {"(A AND B) OR (C AND D)", "DIGITAL  4 inputs", FormulaKind::Digital, 4, f_ab_or_cd, nullptr},
        {"-A", "ANALOG  1 input", FormulaKind::Analog, 1, nullptr, g_neg},
        {"|A|", "ANALOG  1 input", FormulaKind::Analog, 1, nullptr, g_abs},
        {"A x A", "ANALOG  1 input", FormulaKind::Analog, 1, nullptr, g_sq},
        {"A + B", "ANALOG  2 inputs", FormulaKind::Analog, 2, nullptr, g_add},
        {"A - B", "ANALOG  2 inputs", FormulaKind::Analog, 2, nullptr, g_sub},
        {"A x B", "ANALOG  2 inputs", FormulaKind::Analog, 2, nullptr, g_mul},
        {"A / B", "ANALOG  2 inputs", FormulaKind::Analog, 2, nullptr, g_div},
        {"AVG(A, B)", "ANALOG  2 inputs", FormulaKind::Analog, 2, nullptr, g_avg2},
        {"MIN(A, B)", "ANALOG  2 inputs", FormulaKind::Analog, 2, nullptr, g_min},
        {"MAX(A, B)", "ANALOG  2 inputs", FormulaKind::Analog, 2, nullptr, g_max},
        {"A + B + C", "ANALOG  3 inputs", FormulaKind::Analog, 3, nullptr, g_sum3},
        {"AVG(A, B, C)", "ANALOG  3 inputs", FormulaKind::Analog, 3, nullptr, g_avg3},
        {"(A - B) x C", "ANALOG  3 inputs", FormulaKind::Analog, 3, nullptr, g_a_minus_b_times_c},
        {"A + B + C + D", "ANALOG  4 inputs", FormulaKind::Analog, 4, nullptr, g_sum4},
        {"AVG(A, B, C, D)", "ANALOG  4 inputs", FormulaKind::Analog, 4, nullptr, g_avg4},
        {"A x B - C x D", "ANALOG  4 inputs", FormulaKind::Analog, 4, nullptr, g_ab_minus_cd},
        {"(A - B) / (C - D)", "ANALOG  4 inputs", FormulaKind::Analog, 4, nullptr, g_a_minus_b_over_c_minus_d},
    };
    return list;
}

int level_or_zero(const DigitalTrace *t, int64_t ns)
{
    if (!t) {
        return 0;
    }
    int l = t->level_at(ns);
    return l < 0 ? 0 : l;
}

}  // namespace

const std::vector<Formula> &formulas()
{
    return table();
}

int formula_count()
{
    return static_cast<int>(table().size());
}

void evaluate_digital(const Formula &formula, const DigitalTrace *const in[formula_inputs_max], int64_t t0, int64_t t1,
                      DigitalTrace &out)
{
    out.clear();
    if (formula.kind != FormulaKind::Digital || !formula.digital) {
        return;
    }
    int level[formula_inputs_max];
    size_t index[formula_inputs_max];
    size_t end[formula_inputs_max];
    for (int k = 0; k < formula_inputs_max; k++) {
        level[k] = level_or_zero(in[k], t0);
        index[k] = in[k] ? in[k]->lower_bound(t0 + 1) : 0;
        end[k] = in[k] ? in[k]->lower_bound(t1 + 1) : 0;
    }
    int state = 0;
    int current = formula.digital(level[0], level[1], level[2], level[3], state);
    out.add(t0, static_cast<uint8_t>(current));
    for (;;) {
        // The earliest next edge among the inputs.
        int which = -1;
        int64_t next = 0;
        for (int k = 0; k < formula_inputs_max; k++) {
            if (in[k] && index[k] < end[k]) {
                int64_t t = in[k]->at(index[k]).ns;
                if (which < 0 || t < next) {
                    which = k;
                    next = t;
                }
            }
        }
        if (which < 0) {
            break;
        }
        // Apply every edge at that instant before evaluating.
        for (int k = 0; k < formula_inputs_max; k++) {
            while (in[k] && index[k] < end[k] && in[k]->at(index[k]).ns == next) {
                level[k] = in[k]->at(index[k]).level;
                index[k]++;
            }
        }
        int value = formula.digital(level[0], level[1], level[2], level[3], state);
        if (value != current) {
            current = value;
            out.add(next, static_cast<uint8_t>(current));
        }
    }
}

void evaluate_analog(const Formula &formula, const AnalogTrace *const in[formula_inputs_max], int64_t t0, int64_t t1,
                     size_t samples, AnalogTrace &out)
{
    out.clear();
    if (formula.kind != FormulaKind::Analog || !formula.analog || samples < 2 || t1 <= t0) {
        return;
    }
    int64_t dt = std::max<int64_t>(1, (t1 - t0) / static_cast<int64_t>(samples - 1));
    std::vector<float> values;
    values.reserve(samples);
    for (size_t i = 0; i < samples; i++) {
        int64_t t = t0 + static_cast<int64_t>(i) * dt;
        float v[formula_inputs_max] = {0.0f, 0.0f, 0.0f, 0.0f};
        for (int k = 0; k < formula_inputs_max; k++) {
            if (in[k]) {
                in[k]->value_at(t, v[k]);
            }
        }
        values.push_back(formula.analog(v[0], v[1], v[2], v[3]));
    }
    out.add(t0, dt, values.data(), values.size());
}

}  // namespace core
