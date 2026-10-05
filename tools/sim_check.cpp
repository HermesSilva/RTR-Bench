// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - console diagnostic of the circuit simulator: runs two small
// circuits through the ngspice session for a few seconds of wall time and
// prints what comes back (state, vectors, samples, speed against real time).
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "core/circuit.h"
#include "sim/ngspice.h"

namespace {

const char *state_name(sim::Ngspice::State state)
{
    switch (state) {
    case sim::Ngspice::State::Missing:
        return "missing";
    case sim::Ngspice::State::Idle:
        return "idle";
    case sim::Ngspice::State::Running:
        return "running";
    case sim::Ngspice::State::Failed:
        return "failed";
    }
    return "";
}

// Runs the loaded circuit for `seconds` of wall time at the pace of the
// clock, as the bench does, and reports.
bool run(const char *title, double seconds, size_t watch_count)
{
    sim::Ngspice &sim = sim::Ngspice::instance();
    std::printf("== %s\n", title);
    const double sim_start = sim.time();
    const auto start = std::chrono::steady_clock::now();
    std::vector<std::vector<float>> samples;
    std::vector<size_t> total(watch_count, 0);
    std::vector<float> lo(watch_count, 1e9f);
    std::vector<float> hi(watch_count, -1e9f);
    for (;;) {
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (elapsed >= seconds) {
            break;
        }
        sim.set_target(sim_start + elapsed);
        sim.drain(samples);
        for (size_t w = 0; w < samples.size() && w < watch_count; w++) {
            total[w] += samples[w].size();
            for (float v : samples[w]) {
                lo[w] = std::fmin(lo[w], v);
                hi[w] = std::fmax(hi[w], v);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    const double simulated = sim.time() - sim_start;
    std::printf("state %s, simulated %.3f s in %.3f s (%.2fx)\n", state_name(sim.state()), simulated, seconds,
                simulated / seconds);
    if (!sim.status().empty()) {
        std::printf("status: %s\n", sim.status().c_str());
    }
    std::vector<std::pair<std::string, double>> vectors;
    sim.snapshot(vectors);
    for (const auto &entry : vectors) {
        std::printf("  %-14s %12.6g\n", entry.first.c_str(), entry.second);
    }
    for (size_t w = 0; w < watch_count; w++) {
        std::printf("  watch %zu: %zu samples, %.4f .. %.4f\n", w, total[w], static_cast<double>(lo[w]),
                    static_cast<double>(hi[w]));
    }
    return sim.state() == sim::Ngspice::State::Running;
}

}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    sim::Ngspice &sim = sim::Ngspice::instance();
    if (!sim.available()) {
        std::printf("%s\n", sim.status().c_str());
        return 1;
    }

    // A generator into an RC low-pass: 1 kHz, 1 V, through 50 ohm, 1 k and 100 nF.
    core::Circuit rc;
    rc.add_part(core::PartKind::Resistor, 2, 0, 0);
    rc.add_part(core::PartKind::Capacitor, 4, 2, 1);
    rc.add_part(core::PartKind::Ground, 4, 4, 0);
    rc.taps.push_back(core::Tap{0, core::GridPoint{0, 0}});
    rc.taps.push_back(core::Tap{1, core::GridPoint{4, 0}});
    core::NetMap map = rc.nets();
    std::vector<core::Drive> drives = {core::Drive{0, 50.0}};
    std::vector<std::string> lines = core::netlist(rc, map, drives);
    for (const std::string &line : lines) {
        std::printf("  | %s\n", line.c_str());
    }
    core::WaveSpec sine;
    sine.kind = core::Waveform::Sine;
    sine.freq_hz = 1000.0;
    sine.amplitude_v = 1.0;
    sim.set_wave(core::drive_source(0), sine, true);
    sim.set_watches({map.name[static_cast<size_t>(map.tap_net[0])], map.name[static_cast<size_t>(map.tap_net[1])],
                     core::drive_source(0) + "#branch"});
    sim.load(lines, false);
    bool ok = run("RC low-pass, 1 kHz sine", 6.0, 3);

    // One of every part, powered: a supply, a potentiometer as a divider, a
    // switch into an LED, a transistor stage and an amplifier as a follower.
    core::Circuit all;
    all.add_part(core::PartKind::Potentiometer, 2, 0, 0);    // (0,0) (4,0) wiper (2,-2)
    all.add_part(core::PartKind::Ground, 4, 0, 0);
    all.add_part(core::PartKind::Switch, 2, 6, 0);           // (0,6) (4,6)
    all.add_part(core::PartKind::Resistor, 6, 6, 0);         // (4,6) (8,6)
    all.add_part(core::PartKind::Led, 10, 6, 0);             // (8,6) (12,6)
    all.add_part(core::PartKind::Ground, 12, 6, 0);
    all.add_part(core::PartKind::Npn, 20, 0, 0);             // c (21,-2) b (18,0) e (21,2)
    all.add_part(core::PartKind::Ground, 21, 2, 0);
    all.add_part(core::PartKind::Resistor, 21, -4, 1);       // (21,-6) (21,-2)
    all.add_part(core::PartKind::Resistor, 16, 0, 0);        // (14,0) (18,0)
    all.add_part(core::PartKind::OpAmp, 30, 0, 0);           // in+ (27,1) in- (27,-1) out (33,0) v+ (30,-2) v- (30,2)
    all.add_part(core::PartKind::Ground, 30, 2, 0);
    all.add_part(core::PartKind::Diode, 36, 0, 0);
    all.add_part(core::PartKind::Inductor, 40, 0, 0);
    all.add_part(core::PartKind::Pnp, 46, 0, 0);
    // Real parts of the catalog where there was a generic one, and a blue
    // LED through a Schottky diode and a Zener across the rail.
    for (core::Part &part : all.parts) {
        if (part.kind == core::PartKind::Npn) {
            part.model = "BC547B";
        } else if (part.kind == core::PartKind::Pnp) {
            part.model = "BC557B";
        } else if (part.kind == core::PartKind::OpAmp) {
            part.model = "LM358";
        } else if (part.kind == core::PartKind::Led) {
            part.model = "LED blue";
        } else if (part.kind == core::PartKind::Diode) {
            part.model = "1N5819";
        }
    }
    {
        size_t zener = all.add_part(core::PartKind::Diode, -4, 2, 3);   // cathode (-4,0), anode (-4,4)
        all.parts[zener].model = "Zener 5V1";
        size_t r = all.add_part(core::PartKind::Resistor, -2, 0, 0);    // (-4,0) (0,0)
        all.parts[r].value = 390.0;
        all.add_part(core::PartKind::Ground, -4, 4, 0);
        size_t q2 = all.add_part(core::PartKind::Npn, 50, 0, 0);
        all.parts[q2].model = "2N2222";
        size_t q3 = all.add_part(core::PartKind::Npn, 56, 0, 0);
        all.parts[q3].model = "2N3904";
        size_t q4 = all.add_part(core::PartKind::Pnp, 62, 0, 0);
        all.parts[q4].model = "2N3906";
        size_t d2 = all.add_part(core::PartKind::Diode, 68, 0, 0);
        all.parts[d2].model = "1N4007";
        size_t d3 = all.add_part(core::PartKind::Diode, 74, 0, 0);
        all.parts[d3].model = "1N4148";
        size_t u2 = all.add_part(core::PartKind::OpAmp, 82, 0, 0);
        all.parts[u2].model = "NE5532";
        all.taps.push_back(core::Tap{4, core::GridPoint{-4, 0}});   // Zener cathode
    }
    // The supply rail: (0,0), (0,6), (21,-6), (30,-2) and the wiper to the base resistor and to in+.
    all.wires.push_back(core::CircuitWire{{0, 0}, {0, 6}});
    all.wires.push_back(core::CircuitWire{{0, 0}, {0, -6}});
    all.wires.push_back(core::CircuitWire{{0, -6}, {30, -6}});
    all.wires.push_back(core::CircuitWire{{30, -6}, {30, -2}});
    all.wires.push_back(core::CircuitWire{{2, -2}, {14, -2}});
    all.wires.push_back(core::CircuitWire{{14, -2}, {14, 0}});
    all.wires.push_back(core::CircuitWire{{14, 0}, {14, 1}});
    all.wires.push_back(core::CircuitWire{{14, 1}, {27, 1}});
    all.wires.push_back(core::CircuitWire{{27, -1}, {27, -4}});
    all.wires.push_back(core::CircuitWire{{27, -4}, {33, -4}});
    all.wires.push_back(core::CircuitWire{{33, -4}, {33, 0}});
    all.taps.push_back(core::Tap{0, core::GridPoint{0, 0}});     // supply
    all.taps.push_back(core::Tap{1, core::GridPoint{33, 0}});    // follower output
    all.taps.push_back(core::Tap{2, core::GridPoint{21, -2}});   // collector
    all.taps.push_back(core::Tap{3, core::GridPoint{8, 6}});     // LED anode
    map = all.nets();
    drives = {core::Drive{0, 0.05}};
    lines = core::netlist(all, map, drives);
    for (const std::string &line : lines) {
        std::printf("  | %s\n", line.c_str());
    }
    core::WaveSpec supply;
    supply.kind = core::Waveform::Dc;
    supply.amplitude_v = 9.0;
    sim.set_wave(core::drive_source(0), supply, true);
    for (const core::Part &part : all.parts) {
        if (part.kind == core::PartKind::Potentiometer) {
            sim.set_constant(core::control_source(part), 0.25);
        }
        if (part.kind == core::PartKind::Switch) {
            sim.set_constant(core::control_source(part), 1.0);
        }
    }
    std::vector<std::string> watches;
    for (size_t t = 0; t < all.taps.size(); t++) {
        watches.push_back(map.tap_net[t] >= 0 ? map.name[static_cast<size_t>(map.tap_net[t])] : std::string());
    }
    watches.push_back(core::drive_source(0) + "#branch");
    sim.set_watches(watches);
    sim.load(lines, false);
    ok = run("one of every part, 9 V", 3.0, watches.size()) && ok;

    // Sources and meters that are parts: a 5 V source through an ammeter and
    // 1 k (5 mA), a MOSFET switching 1 k from the same rail, 1 mA into 1 k,
    // and a sine source on a divider.
    core::Circuit parts;
    parts.add_part(core::PartKind::VSource, 2, 0, 0);                    // + (0,0)  - (4,0)
    parts.parts.back().value = 5.0;
    parts.add_part(core::PartKind::Ground, 4, 0, 0);
    size_t meter = parts.add_part(core::PartKind::Ammeter, -2, 0, 2);    // rotated: + (0,0)  - (-4,0)
    parts.add_part(core::PartKind::Resistor, -6, 0, 0);                  // (-8,0) (-4,0)
    parts.add_part(core::PartKind::Ground, -8, 0, 0);
    parts.add_part(core::PartKind::Resistor, 0, -2, 1);                  // (0,-4) (0,0)
    parts.add_part(core::PartKind::Nmos, -1, -6, 0);                     // d (0,-8) g (-3,-6) s (0,-4)... source up
    parts.add_part(core::PartKind::ISource, 12, 0, 0);                   // a (10,0) b (14,0)
    parts.add_part(core::PartKind::Ground, 10, 0, 0);
    parts.add_part(core::PartKind::Resistor, 16, 0, 0);                  // (14,0) (18,0)
    parts.add_part(core::PartKind::Ground, 18, 0, 0);
    size_t sine_part = parts.add_part(core::PartKind::VSine, 22, 4, 0);  // + (20,4) - (24,4)
    parts.add_part(core::PartKind::Ground, 24, 4, 0);
    parts.add_part(core::PartKind::Voltmeter, 22, 8, 0);                 // (20,8) (24,8)
    parts.add_part(core::PartKind::Ground, 24, 8, 0);
    parts.wires.push_back(core::CircuitWire{{20, 4}, {20, 8}});
    parts.wires.push_back(core::CircuitWire{{-3, -6}, {-3, -8}});        // gate to drain: a diode-connected MOSFET
    parts.wires.push_back(core::CircuitWire{{-3, -8}, {0, -8}});
    parts.taps.push_back(core::Tap{0, core::GridPoint{14, 0}});          // 1 mA into 1 k: 1 V
    parts.taps.push_back(core::Tap{1, core::GridPoint{20, 4}});          // the sine
    parts.taps.push_back(core::Tap{2, core::GridPoint{0, -4}});          // MOSFET source
    map = parts.nets();
    lines = core::netlist(parts, map, {});
    for (const std::string &line : lines) {
        if (line[0] != '.' && line.size() < 60) {
            std::printf("  | %s\n", line.c_str());
        }
    }
    core::WaveSpec tone;
    tone.kind = core::Waveform::Sine;
    tone.freq_hz = parts.parts[sine_part].value2;
    tone.amplitude_v = parts.parts[sine_part].value;
    sim.set_wave(core::element_name(parts.parts[sine_part]), tone, true);
    watches.clear();
    for (size_t t = 0; t < parts.taps.size(); t++) {
        watches.push_back(map.tap_net[t] >= 0 ? map.name[static_cast<size_t>(map.tap_net[t])] : std::string());
    }
    watches.push_back(core::current_vector(parts.parts[meter]));
    sim.set_watches(watches);
    sim.load(lines, false);
    ok = run("sources, MOSFET and meters as parts", 3.0, watches.size()) && ok;

    sim.stop();
    return ok ? 0 : 1;
}
