// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the digital side of a circuit: the logic gates, the delay
// lines and the island (the resistors and capacitors that hang only on
// gates, an oscillator for instance), computed here with a step of half a
// microsecond, between the time points of the analog simulator. A clock of
// gates runs at its real rate, whatever the step of the simulator is. Pure:
// it reads the node voltages it is given and answers for its sources.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/circuit.h"

namespace sim {

class DigitalEngine {
public:
    // The gates, lines and island of the circuit. What was there with the
    // same shape keeps its state.
    void set(const core::Digital &digital);
    // Everything starts again: gates settle, delay lines are empty.
    void reset();
    // The names of the vectors `accept` receives, in their order.
    void map(const std::vector<std::string> &names);
    // A time point of the analog simulator and the voltages at it.
    void accept(double time, const std::vector<double> &values);
    // The voltage of one of its sources at a time, at or after the last
    // accepted point; false when the source is not one of them.
    bool source(const std::string &name, double time, double &volts);
    // The value of a resistor of the island; false when it is not there.
    bool set_resistance(const std::string &name, double ohms);
    // Samples the delay lines have taken.
    int64_t clocks() const { return clocks_; }

    static constexpr double step = 0.5e-6;   // seconds

private:
    struct Gate {
        int input[2] = {-1, -1};   // vectors, -1 for the ground
        int high = -1;
        int low = -1;
        double now[2] = {0.0, 0.0};     // an input from the circuit, as a fraction of the supply
        double slope[2] = {0.0, 0.0};   // and how it was moving, per second
        double volts[2] = {0.0, 0.0};   // of the output, low and high
        bool level = false;
        double boost = 0.0;             // for one step after a change: how much of a step ago it was due
    };
    struct Line {
        std::vector<float> ring;
        size_t head = 0;                // where the next sample goes: also the oldest one
        int input = -1;
        int clock[2] = {-1, -1};
        int high = -1;
        int low = -1;
        bool up = false;                // the clock, when no gate drives it
        std::vector<double> rises;      // times of the clock rises not yet taken
        static constexpr int taps = 24;
        static constexpr int fine = 64;
        bool started = false;
        double last_time = 0.0;         // the last point of the simulator, and the signal there
        double last_in = 0.0;
        double grid = 0.0;              // the time of the newest point of the even grid the signal is on
        double in[taps] = {};           // the signal at the last points of that grid, the newest last
        std::vector<std::pair<double, double>> steps;   // the output: when it changes, and to what
        double level = 0.0;             // the output at `upto`
        double upto = 0.0;              // up to where the output went into `means`
        double means[fine] = {};        // the output over the last short stretches, a ring
        int mean_head = 0;
        double held = 0.0;              // the sample at the output
        double out = 0.0;               // what its source gives over the coming step, above the low rail
    };

    void advance(double to);
    void substep();
    void solve_island();
    const std::vector<double> &inverse(uint32_t clamps);
    double value(int vector) const;
    size_t columns() const { return def_.gates.size() + def_.island.nodes.size(); }

    core::Digital def_;
    std::vector<Gate> gates_;
    std::vector<Line> lines_;
    std::vector<double> volts_;          // of the island nodes
    std::vector<int> fixed_;             // vectors of the nodes the island takes as given
    std::vector<double> fixed_volts_;
    std::vector<std::pair<uint32_t, std::vector<double>>> inverses_;   // per state of the input clamps
    std::vector<double> values_;         // the voltages of the last accepted point
    std::vector<std::string> names_;
    std::vector<char> was_;              // scratch of a step
    std::vector<double> next_;
    std::vector<double> rhs_;
    std::vector<double> history_;        // one row per step since the last accepted point: gates, then nodes
    std::vector<double> at_accept_;      // the row at the last accepted point
    double history_from_ = 0.0;
    double time_ = 0.0;                  // up to where it has been computed
    double accepted_ = 0.0;
    bool started_ = false;
    bool slopes_ = false;                // the inputs have a point before the last
    int64_t clocks_ = 0;
};

}  // namespace sim
