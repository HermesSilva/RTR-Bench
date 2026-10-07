// SPDX-License-Identifier: Apache-2.0
// How clean a delay line clocked by an oscillator of gates is, without the
// analog simulator: a tone goes in at the points of the simulator, what the
// line gives comes out, and the tone is compared with everything else below
// 10 kHz (what a staircase at the clock rate has there is the tone alone).
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "core/circuit.h"
#include "sim/digital.h"

namespace {

constexpr double pi = 3.14159265358979323846;

// The oscillator of the echo circuit (R to the timing node, 1.5 nF, 10 k to
// the inputs), a buffer and a line of 256 samples on it.
core::Digital echo(double ohms)
{
    core::Digital d;
    d.island.nodes = {"a", "b", "c", "x"};
    d.island.sources = {"via", "vib", "vic", "vix"};
    d.island.parts.push_back(core::IslandPart{false, 1, 3, ohms, ""});
    d.island.parts.push_back(core::IslandPart{true, 2, 3, 1.5e-9, ""});
    d.island.parts.push_back(core::IslandPart{false, 3, 0, 10e3, ""});
    d.gates.push_back(core::LogicGate{{"a", "a"}, {-1, -1}, {0, 0, 1}, "vdd", "0", "vg1"});
    d.gates.push_back(core::LogicGate{{"b", "b"}, {0, 0}, {1, 1, 2}, "vdd", "0", "vg2"});
    d.gates.push_back(core::LogicGate{{"c", "c"}, {1, 1}, {2, 2, -1}, "vdd", "0", "vg3"});
    d.delays.push_back(core::DelayLine{"in", {"k", "0"}, 2, "vdd", "0", "vq", 256, 1.0});
    return d;
}

// Signal to everything else below 10 kHz, in decibels, by projection on the
// tone (the second half of the run).
double purity(const std::vector<double> &out, double hertz)
{
    const size_t from = out.size() / 2;
    double mean = 0.0;
    for (size_t i = from; i < out.size(); i++) {
        mean += out[i];
    }
    mean /= static_cast<double>(out.size() - from);
    // A low-pass first, with its first null at 10 kHz: a moving mean of 5
    // samples at 50 kS/s, four times over. The images of the tone around
    // the clock rate, which belong there, are well down after it.
    std::vector<double> low(out.begin() + static_cast<std::ptrdiff_t>(from), out.end());
    for (int pass = 0; pass < 4; pass++) {
        std::vector<double> next;
        for (size_t i = 4; i < low.size(); i++) {
            next.push_back((low[i] + low[i - 1] + low[i - 2] + low[i - 3] + low[i - 4]) / 5.0);
        }
        low.swap(next);
    }
    for (double &v : low) {
        v -= mean;
    }
    double s = 0.0;
    double c = 0.0;
    for (size_t i = 0; i < low.size(); i++) {
        const double phase = 2.0 * pi * hertz * static_cast<double>(i) * 20e-6;
        s += low[i] * std::sin(phase);
        c += low[i] * std::cos(phase);
    }
    s *= 2.0 / static_cast<double>(low.size());
    c *= 2.0 / static_cast<double>(low.size());
    double signal = 0.0;
    double rest = 0.0;
    for (size_t i = 0; i < low.size(); i++) {
        const double phase = 2.0 * pi * hertz * static_cast<double>(i) * 20e-6;
        const double tone = s * std::sin(phase) + c * std::cos(phase);
        signal += tone * tone;
        rest += (low[i] - tone) * (low[i] - tone);
    }
    return 10.0 * std::log10(signal / std::max(rest, 1e-30));
}

}  // namespace

TEST_CASE("a tone comes clean out of a delay line", "[quality]")
{
    for (double ohms : {14.7e3, 4.7e3, 54.7e3}) {
        for (double hertz : {440.0, 1000.0, 3000.0}) {
            sim::DigitalEngine engine;
            engine.set(echo(ohms));
            engine.map({"vdd", "in"});
            std::vector<double> out;
            const int points = 50000;   // one second
            for (int k = 0; k < points; k++) {
                const double t = k * 20e-6;
                engine.accept(t, {12.0, 6.0 + std::sin(2.0 * pi * hertz * t)});
                double volts = 0.0;
                REQUIRE(engine.source("vq", t + 20e-6, volts));
                out.push_back(volts);
            }
            if (const char *stem = std::getenv("RTR_DELAY_DUMP")) {
                char name[256];
                std::snprintf(name, sizeof(name), "%s.%d.%d.f32", stem, static_cast<int>(ohms), static_cast<int>(hertz));
                if (FILE *file = std::fopen(name, "wb")) {
                    std::vector<float> narrow(out.begin(), out.end());
                    std::fwrite(narrow.data(), sizeof(float), narrow.size(), file);
                    std::fclose(file);
                }
            }
            const double db = purity(out, hertz);
            std::printf("  R %.1fk, clock %.0f Hz, tone %.0f Hz: %.1f dB\n", ohms / 1e3, static_cast<double>(engine.clocks()),
                        hertz, db);
            // Below half the clock rate, with room: the line passes the tone.
            if (engine.clocks() > 20000 && hertz < static_cast<double>(engine.clocks()) / 4.0) {
                CHECK(db > 55.0);
            }
        }
    }
}
