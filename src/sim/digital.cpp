// SPDX-License-Identifier: Apache-2.0
#include "sim/digital.h"

#include <algorithm>
#include <cmath>

namespace sim {

namespace {

constexpr double output_ohms = 400.0;      // of a gate
constexpr double clamp_ohms = 200.0;       // of the protection diodes of an input
constexpr double clamp_drop = 0.6;         // volts beyond the rail before they conduct
constexpr double leak = 1e-10;             // siemens to ground at every node of the island
constexpr double clock_hysteresis = 0.5;   // volts around zero, on phase 1 minus phase 2
constexpr double look_ahead = 40e-6;       // seconds an input is followed along its slope, and the engine runs ahead

// Inverts a small dense matrix in place (Gauss-Jordan with pivoting).
void invert(std::vector<double> &a, size_t n)
{
    std::vector<double> inv(n * n, 0.0);
    for (size_t i = 0; i < n; i++) {
        inv[i * n + i] = 1.0;
    }
    for (size_t col = 0; col < n; col++) {
        size_t pivot = col;
        for (size_t row = col + 1; row < n; row++) {
            if (std::fabs(a[row * n + col]) > std::fabs(a[pivot * n + col])) {
                pivot = row;
            }
        }
        if (pivot != col) {
            for (size_t k = 0; k < n; k++) {
                std::swap(a[col * n + k], a[pivot * n + k]);
                std::swap(inv[col * n + k], inv[pivot * n + k]);
            }
        }
        const double scale = a[col * n + col] != 0.0 ? 1.0 / a[col * n + col] : 0.0;
        for (size_t k = 0; k < n; k++) {
            a[col * n + k] *= scale;
            inv[col * n + k] *= scale;
        }
        for (size_t row = 0; row < n; row++) {
            const double factor = a[row * n + col];
            if (row == col || factor == 0.0) {
                continue;
            }
            for (size_t k = 0; k < n; k++) {
                a[row * n + k] -= factor * a[col * n + k];
                inv[row * n + k] -= factor * inv[col * n + k];
            }
        }
    }
    a.swap(inv);
}

}  // namespace

double DigitalEngine::value(int vector) const
{
    return vector >= 0 && static_cast<size_t>(vector) < values_.size() ? values_[static_cast<size_t>(vector)] : 0.0;
}

void DigitalEngine::set(const core::Digital &digital)
{
    bool same = digital.gates.size() == def_.gates.size() && digital.island.nodes == def_.island.nodes &&
                digital.island.parts.size() == def_.island.parts.size();
    for (size_t g = 0; same && g < digital.gates.size(); g++) {
        same = digital.gates[g].source == def_.gates[g].source;
    }
    // A delay line keeps its samples when it is still there.
    std::vector<Line> lines(digital.delays.size());
    for (size_t l = 0; l < digital.delays.size(); l++) {
        for (size_t old = 0; old < def_.delays.size() && old < lines_.size(); old++) {
            if (def_.delays[old].source == digital.delays[l].source && def_.delays[old].samples == digital.delays[l].samples) {
                lines[l] = std::move(lines_[old]);
            }
        }
        lines[l].ring.resize(static_cast<size_t>(std::max(digital.delays[l].samples, 1)), 0.0f);
        lines[l].head %= lines[l].ring.size();
        lines[l].rises.clear();
    }
    lines_ = std::move(lines);
    def_ = digital;
    inverses_.clear();
    if (!same) {
        gates_.assign(def_.gates.size(), Gate{});
        volts_.assign(def_.island.nodes.size(), 0.0);
        history_.clear();
        at_accept_.assign(columns(), 0.0);
        started_ = false;
    }
    map(names_);
}

void DigitalEngine::reset()
{
    gates_.assign(def_.gates.size(), Gate{});
    volts_.assign(def_.island.nodes.size(), 0.0);
    for (Line &line : lines_) {
        std::vector<float> ring = std::move(line.ring);
        std::fill(ring.begin(), ring.end(), 0.0f);
        line = Line{};
        line.ring = std::move(ring);
    }
    history_.clear();
    at_accept_.assign(columns(), 0.0);
    started_ = false;
    map(names_);
}

void DigitalEngine::map(const std::vector<std::string> &names)
{
    names_ = names;
    auto vector_of = [&](const std::string &name) {
        auto found = std::find(names_.begin(), names_.end(), name);
        return found == names_.end() ? -1 : static_cast<int>(found - names_.begin());
    };
    for (size_t g = 0; g < gates_.size(); g++) {
        gates_[g].input[0] = vector_of(def_.gates[g].input[0]);
        gates_[g].input[1] = vector_of(def_.gates[g].input[1]);
        gates_[g].high = vector_of(def_.gates[g].high);
        gates_[g].low = vector_of(def_.gates[g].low);
    }
    for (size_t l = 0; l < lines_.size(); l++) {
        lines_[l].input = vector_of(def_.delays[l].input);
        lines_[l].clock[0] = vector_of(def_.delays[l].clock[0]);
        lines_[l].clock[1] = vector_of(def_.delays[l].clock[1]);
        lines_[l].high = vector_of(def_.delays[l].high);
        lines_[l].low = vector_of(def_.delays[l].low);
    }
    fixed_.clear();
    for (const std::string &name : def_.island.fixed) {
        fixed_.push_back(vector_of(name));
    }
    fixed_volts_.assign(fixed_.size(), 0.0);
    slopes_ = false;
}

bool DigitalEngine::set_resistance(const std::string &name, double ohms)
{
    for (core::IslandPart &part : def_.island.parts) {
        if (!part.capacitor && part.name == name) {
            if (part.value != ohms) {
                part.value = ohms;
                inverses_.clear();
            }
            return true;
        }
    }
    return false;
}

// The inverse of the matrix of the island for a state of the input clamps
// (two bits per gate input: off, to the high rail, to the low rail). The
// capacitors are in it as conductances of one step.
const std::vector<double> &DigitalEngine::inverse(uint32_t clamps)
{
    for (const auto &entry : inverses_) {
        if (entry.first == clamps) {
            return entry.second;
        }
    }
    const size_t n = volts_.size();
    std::vector<double> a(n * n, 0.0);
    auto stamp = [&](int x, int y, double g) {
        if (x >= 0) {
            a[static_cast<size_t>(x) * n + static_cast<size_t>(x)] += g;
        }
        if (y >= 0) {
            a[static_cast<size_t>(y) * n + static_cast<size_t>(y)] += g;
        }
        if (x >= 0 && y >= 0) {
            a[static_cast<size_t>(x) * n + static_cast<size_t>(y)] -= g;
            a[static_cast<size_t>(y) * n + static_cast<size_t>(x)] -= g;
        }
    };
    for (const core::IslandPart &part : def_.island.parts) {
        stamp(part.a, part.b, part.capacitor ? part.value / step : 1.0 / std::max(part.value, 1e-3));
    }
    for (size_t g = 0; g < def_.gates.size(); g++) {
        stamp(def_.gates[g].node[2], -1, 1.0 / output_ohms);
        for (size_t k = 0; g < 8 && k < 2; k++) {
            if ((clamps >> (4 * g + 2 * k)) & 3U) {
                stamp(def_.gates[g].node[k], -1, 1.0 / clamp_ohms);
            }
        }
    }
    for (size_t i = 0; i < n; i++) {
        a[i * n + i] += leak;
    }
    invert(a, n);
    if (inverses_.size() > 64) {
        inverses_.clear();
    }
    inverses_.emplace_back(clamps, std::move(a));
    return inverses_.back().second;
}

// One step of the island: the capacitors remember, the outputs of the gates
// drive, the protection diodes of the inputs hold a node that goes beyond a
// rail.
void DigitalEngine::solve_island()
{
    const size_t n = volts_.size();
    if (n == 0) {
        return;
    }
    auto before = [&](int node) {
        if (node >= 0) {
            return volts_[static_cast<size_t>(node)];
        }
        return node == -1 ? 0.0 : fixed_volts_[static_cast<size_t>(-2 - node)];
    };
    std::vector<double> &next = next_;
    std::vector<double> &rhs = rhs_;
    next = volts_;
    // The clamps follow the voltage: once from where the node was, once
    // more from where it lands.
    for (int round = 0; round < 2; round++) {
        uint32_t clamps = 0;
        rhs.assign(n, 0.0);
        for (size_t g = 0; g < def_.gates.size() && g < 8; g++) {
            for (size_t k = 0; k < 2; k++) {
                const int node = def_.gates[g].node[k];
                if (node < 0) {
                    continue;
                }
                const double at = next[static_cast<size_t>(node)];
                if (at > gates_[g].volts[1] + clamp_drop) {
                    clamps |= 1U << (4 * g + 2 * k);
                    rhs[static_cast<size_t>(node)] += (gates_[g].volts[1] + clamp_drop) / clamp_ohms;
                } else if (at < gates_[g].volts[0] - clamp_drop) {
                    clamps |= 2U << (4 * g + 2 * k);
                    rhs[static_cast<size_t>(node)] += (gates_[g].volts[0] - clamp_drop) / clamp_ohms;
                }
            }
        }
        for (const core::IslandPart &part : def_.island.parts) {
            const double g = part.capacitor ? part.value / step : 1.0 / std::max(part.value, 1e-3);
            // What the node at the other end gives when it is not of the island.
            if (part.a >= 0 && part.b < 0) {
                rhs[static_cast<size_t>(part.a)] += g * before(part.b);
            }
            if (part.b >= 0 && part.a < 0) {
                rhs[static_cast<size_t>(part.b)] += g * before(part.a);
            }
            if (part.capacitor) {
                const double across = before(part.a) - before(part.b);
                if (part.a >= 0) {
                    rhs[static_cast<size_t>(part.a)] += g * across;
                }
                if (part.b >= 0) {
                    rhs[static_cast<size_t>(part.b)] -= g * across;
                }
            }
        }
        for (size_t g = 0; g < def_.gates.size(); g++) {
            const int node = def_.gates[g].node[2];
            if (node >= 0) {
                const double to = gates_[g].volts[gates_[g].level ? 1 : 0];
                const double from = gates_[g].volts[gates_[g].level ? 0 : 1];
                rhs[static_cast<size_t>(node)] += (to + gates_[g].boost * (to - from)) / output_ohms;
            }
        }
        const std::vector<double> &inv = inverse(clamps);
        for (size_t i = 0; i < n; i++) {
            double sum = 0.0;
            for (size_t k = 0; k < n; k++) {
                sum += inv[i * n + k] * rhs[k];
            }
            next[i] = sum;
        }
    }
    volts_.swap(next);
}

void DigitalEngine::substep()
{
    const double at = time_ + step;
    const double ahead = std::min(std::max(at - accepted_, 0.0), look_ahead);
    std::vector<char> &was = was_;
    was.resize(gates_.size());
    for (size_t g = 0; g < gates_.size(); g++) {
        was[g] = gates_[g].level ? 1 : 0;
    }
    // An input on the island crossed the middle of the supply somewhere in
    // the step before: how long ago. The gates that change now were due
    // then, and make up for it over this step, so that the time an
    // oscillator keeps does not go in jumps of a step.
    double late = 0.0;
    // A change goes through a chain of gates within the step.
    for (size_t round = 0; round <= gates_.size(); round++) {
        bool changed = false;
        for (size_t g = 0; g < gates_.size(); g++) {
            Gate &gate = gates_[g];
            bool in[2] = {false, false};
            for (size_t k = 0; k < 2; k++) {
                const int driver = def_.gates[g].driver[k];
                const int node = def_.gates[g].node[k];
                if (driver >= 0 && static_cast<size_t>(driver) < gates_.size()) {
                    in[k] = gates_[static_cast<size_t>(driver)].level;
                } else if (node >= 0) {
                    const double middle = 0.5 * (gate.volts[0] + gate.volts[1]);
                    const double here = volts_[static_cast<size_t>(node)];
                    in[k] = here > middle;
                    if (static_cast<size_t>(node) < next_.size()) {
                        const double there = next_[static_cast<size_t>(node)];   // a step ago
                        if ((there > middle) != in[k] && here != there) {
                            late = std::max(late, step * std::clamp((here - middle) / (here - there), 0.0, 1.0));
                        }
                    }
                } else {
                    in[k] = gate.now[k] + gate.slope[k] * ahead > 0.5;
                }
            }
            const bool level = !(in[0] || in[1]);
            changed = changed || level != gate.level;
            gate.level = level;
        }
        if (!changed) {
            break;
        }
    }
    for (size_t l = 0; l < lines_.size(); l++) {
        const int gate = def_.delays[l].clock_gate;
        if (gate >= 0 && static_cast<size_t>(gate) < gates_.size() && gates_[static_cast<size_t>(gate)].level &&
            !was[static_cast<size_t>(gate)] && lines_[l].rises.size() < 4096) {
            lines_[l].rises.push_back(time_ - late);
        }
    }
    for (size_t g = 0; g < gates_.size(); g++) {
        gates_[g].boost = gates_[g].level != (was[g] != 0) ? late / step : 0.0;
    }
    solve_island();
    for (const Gate &gate : gates_) {
        const double to = gate.volts[gate.level ? 1 : 0];
        history_.push_back(to + gate.boost * (to - gate.volts[gate.level ? 0 : 1]));
    }
    history_.insert(history_.end(), volts_.begin(), volts_.end());
    time_ = at;
}

void DigitalEngine::advance(double to)
{
    to = std::min(to, accepted_ + 2.0 * look_ahead);
    while (time_ + step <= to + 1e-12) {
        substep();
    }
}

void DigitalEngine::accept(double time, const std::vector<double> &values)
{
    if (!started_ || std::fabs(time_ - time) > 1e-3) {
        time_ = time;
        accepted_ = time;
        history_.clear();
        history_from_ = time;
        started_ = true;
        slopes_ = false;
    }
    values_ = values;
    advance(time);
    const double elapsed = time - accepted_;
    for (size_t g = 0; g < gates_.size(); g++) {
        Gate &gate = gates_[g];
        gate.volts[0] = value(gate.low);
        gate.volts[1] = std::max(value(gate.high), gate.volts[0]);
        const double swing = std::max(gate.volts[1] - gate.volts[0], 1e-3);
        for (size_t k = 0; k < 2; k++) {
            const double now = (value(gate.input[k]) - gate.volts[0]) / swing;
            gate.slope[k] = slopes_ && elapsed > 0.0 ? (now - gate.now[k]) / elapsed : 0.0;
            gate.now[k] = now;
        }
    }
    for (size_t k = 0; k < fixed_.size(); k++) {
        fixed_volts_[k] = value(fixed_[k]);
    }
    // The delay lines. The signal is taken on an even grid of 20
    // microseconds: the points of the simulator themselves while they come
    // at that pace, points read between them when they do not. A sample goes
    // in at each rise of the clock, read from 24 points of that grid around
    // the rise (a windowed sinc: what the grid repeats above its half must
    // not come down with the clock). The staircase that leaves the line
    // at the clock rate is averaged over stretches of an eighth of the grid
    // and low-passed down to it, so that what it has above the simulator
    // does not fold into the audio. A line runs some 350 microseconds
    // behind for all this.
    constexpr double pitch = 20e-6;
    constexpr double pi = 3.14159265358979323846;
    constexpr int taps = Line::taps;
    constexpr int back = taps / 2;   // the rises taken are between the points back - 1 and back, counted from the oldest
    constexpr int per_point = 8;
    static const std::vector<double> lowpass = [] {
        std::vector<double> h(Line::fine);
        double total = 0.0;
        for (int k = 0; k < Line::fine; k++) {
            const double x = static_cast<double>(k) - 0.5 * (Line::fine - 1);
            const double px = pi * x * 2.0 * 20e3 / (per_point / pitch);
            const double sinc = std::fabs(px) < 1e-9 ? 1.0 : std::sin(px) / px;
            const double turn = pi * x / (0.5 * Line::fine);
            h[static_cast<size_t>(k)] = sinc * (0.42 + 0.5 * std::cos(turn) + 0.08 * std::cos(2.0 * turn));
            total += h[static_cast<size_t>(k)];
        }
        for (double &v : h) {
            v /= total;
        }
        return h;
    }();
    for (size_t l = 0; l < lines_.size(); l++) {
        Line &line = lines_[l];
        const double low = value(line.low);
        const double in = std::clamp(value(line.input) - low, 0.0, std::max(value(line.high) - low, 0.0));
        if (def_.delays[l].clock_gate < 0) {
            const double clock = value(line.clock[0]) - value(line.clock[1]);
            const bool up = clock > (line.up ? -clock_hysteresis : clock_hysteresis);
            if (up && !line.up) {
                line.rises.push_back(time);
            }
            line.up = up;
        }
        if (!line.started || time < line.last_time) {
            line.started = true;
            line.grid = time;
            std::fill(std::begin(line.in), std::end(line.in), in);
            std::fill(std::begin(line.means), std::end(line.means), line.held);
            line.steps.clear();
            line.rises.clear();
            line.level = line.held;
            line.upto = line.grid - static_cast<double>(back - 1) * pitch;
            line.out = line.held;
        }
        while (time - line.grid >= pitch * 0.999) {
            // A point of the simulator a step after the last one is taken as it is.
            const double at = time - line.grid <= pitch * 1.001 ? time : line.grid + pitch;
            line.grid = at;
            const double part = time > line.last_time ? std::clamp((at - line.last_time) / (time - line.last_time), 0.0, 1.0) : 1.0;
            for (int k = 0; k + 1 < taps; k++) {
                line.in[k] = line.in[k + 1];
            }
            line.in[taps - 1] = line.last_in + (in - line.last_in) * part;
            // The rises between the two middle points.
            const double first = line.grid - static_cast<double>(back) * pitch;
            const double end = first + pitch;
            size_t taken = 0;
            for (; taken < line.rises.size() && line.rises[taken] <= end + 1e-12; taken++) {
                const double u = std::clamp((line.rises[taken] - first) / pitch, 0.0, 1.0);
                double sample = 0.0;
                double total = 0.0;
                for (int k = 0; k < taps; k++) {
                    const double px = pi * (u - static_cast<double>(k - back + 1));
                    const double sinc = std::fabs(px) < 1e-9 ? 1.0 : std::sin(px) / px;
                    const double turn = px / static_cast<double>(back);
                    const double window = 0.42 + 0.5 * std::cos(turn) + 0.08 * std::cos(2.0 * turn);
                    sample += line.in[k] * sinc * window;
                    total += sinc * window;
                }
                line.ring[line.head] = static_cast<float>(def_.delays[l].gain * sample / total);
                line.head = (line.head + 1) % line.ring.size();
                line.held = static_cast<double>(line.ring[line.head]);
                line.steps.emplace_back(std::max(line.rises[taken], line.upto), line.held);
                clocks_++;
            }
            line.rises.erase(line.rises.begin(), line.rises.begin() + static_cast<std::ptrdiff_t>(taken));
            // The output up to there, in short stretches.
            size_t used = 0;
            for (int k = 0; k < per_point; k++) {
                const double stop = line.upto + pitch / per_point;
                double area = 0.0;
                for (; used < line.steps.size() && line.steps[used].first <= stop; used++) {
                    area += line.level * (line.steps[used].first - line.upto);
                    line.upto = line.steps[used].first;
                    line.level = line.steps[used].second;
                }
                area += line.level * (stop - line.upto);
                line.upto = stop;
                line.means[line.mean_head] = area / (pitch / per_point);
                line.mean_head = (line.mean_head + 1) % Line::fine;
            }
            line.steps.erase(line.steps.begin(), line.steps.begin() + static_cast<std::ptrdiff_t>(used));
            double sum = 0.0;
            for (int k = 0; k < Line::fine; k++) {
                sum += lowpass[static_cast<size_t>(k)] * line.means[(line.mean_head + k) % Line::fine];
            }
            line.out = sum;
        }
        line.last_time = time;
        line.last_in = in;
    }
    // The steps up to this point are behind.
    const size_t width = columns();
    if (width > 0) {
        const size_t rows = history_.size() / width;
        const auto behind = std::min(rows, static_cast<size_t>(std::max(0.0, std::floor((time - history_from_) / step + 1e-6))));
        if (behind > 0) {
            at_accept_.assign(history_.begin() + static_cast<std::ptrdiff_t>((behind - 1) * width),
                              history_.begin() + static_cast<std::ptrdiff_t>(behind * width));
            history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(behind * width));
            history_from_ += static_cast<double>(behind) * step;
        }
    }
    accepted_ = time;
    slopes_ = true;
}

bool DigitalEngine::source(const std::string &name, double time, double &volts)
{
    for (size_t l = 0; l < lines_.size(); l++) {
        if (def_.delays[l].source == name) {
            volts = value(lines_[l].low) + lines_[l].out;
            return true;
        }
    }
    size_t column = 0;
    bool found = false;
    for (size_t g = 0; g < def_.gates.size() && !found; g++) {
        found = def_.gates[g].source == name;
        column = g;
    }
    for (size_t k = 0; k < def_.island.sources.size() && !found; k++) {
        found = def_.island.sources[k] == name;
        column = def_.gates.size() + k;
    }
    if (!found) {
        return false;
    }
    if (!started_) {
        volts = 0.0;
        return true;
    }
    advance(time);
    // The mean since the accepted point: between two points the simulator
    // draws a straight line, and the mean keeps the area of what happened.
    const size_t width = columns();
    const size_t rows = history_.size() / width;
    const auto upto = std::min(rows, static_cast<size_t>(std::max(0.0, std::floor((time - history_from_) / step + 1e-6))));
    if (upto == 0) {
        volts = column < at_accept_.size() ? at_accept_[column] : 0.0;
        return true;
    }
    double sum = 0.0;
    for (size_t row = 0; row < upto; row++) {
        sum += history_[row * width + column];
    }
    volts = sum / static_cast<double>(upto);
    return true;
}

}  // namespace sim
