// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - console diagnostic of the circuit simulator: runs small
// circuits through the ngspice session for a few seconds of wall time and
// prints what comes back (state, vectors, samples, speed against real time).
// With the file of a project of the circuit bench, runs that project
// instead: 12 V on the taps wired to the supply and a 200 Hz sine of 0.2 V
// on those wired to the generator. A second argument is the pace: how many
// simulated seconds are asked for per second (1 as the bench does, more to
// measure what the simulator can do); a third and a fourth are the volts
// and the hertz of the sine (0 hertz: noise, as rough as music gets); a
// fifth is a file name stem that receives the samples of every watch.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

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

double sine_volts = 0.2;   // what the generator gives a project
double sine_hertz = 200.0;
const char *dump = nullptr;   // a file name stem: every watch is written there, raw 32-bit floats at 50 kS/s
double project_seconds = 8.0;   // how long a project runs
double pace = 1.0;   // simulated seconds asked for per second: above 1 to measure what the simulator can do

// Runs the loaded circuit for `seconds` of wall time at the pace of the
// clock, as the bench does, and reports.
bool run(const char *title, double seconds, size_t watch_count)
{
    sim::Ngspice &sim = sim::Ngspice::instance();
    std::printf("== %s\n", title);
    const double sim_start = sim.time();
    const int64_t steps_start = sim.steps();
    const int64_t clocks_start = sim.clocks();
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
        sim.set_target(sim_start + elapsed * pace);
        sim.drain(samples);
        for (size_t w = 0; w < samples.size() && w < watch_count; w++) {
            total[w] += samples[w].size();
            if (dump && !samples[w].empty()) {
                std::ofstream out(std::string(dump) + "." + std::to_string(w) + ".f32", std::ios::binary | std::ios::app);
                out.write(reinterpret_cast<const char *>(samples[w].data()),
                          static_cast<std::streamsize>(samples[w].size() * sizeof(float)));
            }
            // The range is that of the second half of the run: the circuit has settled.
            for (float v : samples[w]) {
                if (elapsed < seconds / 2.0) {
                    continue;
                }
                lo[w] = std::fmin(lo[w], v);
                hi[w] = std::fmax(hi[w], v);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    const double simulated = sim.time() - sim_start;
    std::printf("state %s, simulated %.3f s in %.3f s (%.2fx), %lld steps\n", state_name(sim.state()), simulated, seconds,
                simulated / seconds, static_cast<long long>(sim.steps() - steps_start));
    if (sim.clocks() > clocks_start && simulated > 0.0) {
        std::printf("delay lines took %.1f samples per second, all together\n", static_cast<double>(sim.clocks() - clocks_start) / simulated);
    }
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

// Runs the project in a file as the circuit bench would.
bool run_project(const char *path)
{
    std::ifstream file(path);
    nlohmann::json in = nlohmann::json::parse(file, nullptr, false);
    if (!in.is_object() || !in.contains("parts")) {
        std::printf("%s: not a project\n", path);
        return false;
    }
    core::Circuit circuit;
    for (const nlohmann::json &j : in["parts"]) {
        core::PartKind kind;
        if (!core::part_kind_from_name(j.value("kind", ""), kind)) {
            std::printf("unknown part %s\n", j.value("kind", "").c_str());
            return false;
        }
        size_t index = circuit.add_part(kind, j.value("x", 0), j.value("y", 0), j.value("rotation", 0));
        core::Part &part = circuit.parts[index];
        part.id = j.value("id", part.id);
        part.value = j.value("value", part.value);
        part.value2 = j.value("value2", part.value2);
        part.setting = j.value("setting", part.setting);
        part.model = j.value("model", "");
    }
    for (const nlohmann::json &j : in["wires"]) {
        circuit.wires.push_back(core::CircuitWire{{j[0].get<int>(), j[1].get<int>()}, {j[2].get<int>(), j[3].get<int>()}});
    }
    for (const nlohmann::json &j : in["taps"]) {
        circuit.taps.push_back(core::Tap{j.value("slot", 0), core::GridPoint{j.value("x", 0), j.value("y", 0)}});
    }
    sim::Ngspice &sim = sim::Ngspice::instance();
    std::vector<core::Drive> drives;
    std::vector<core::Load> loads;
    for (const nlohmann::json &j : in.value("cables", nlohmann::json::array())) {
        const std::string instrument = j.value("instrument", "");
        const int slot = j.value("slot", 0);
        core::WaveSpec wave;
        if (instrument == "AUDIO") {
            loads.push_back(core::Load{slot, 47e3});   // an audio output; one on an input is replaced below
        }
        if (instrument == "PSU") {
            wave.kind = core::Waveform::Dc;
            wave.amplitude_v = 12.0;
            drives.push_back(core::Drive{slot, 0.05});
        } else if (instrument == "GEN") {
            wave.kind = sine_hertz > 0.0 ? core::Waveform::Sine : core::Waveform::Noise;
            wave.freq_hz = sine_hertz;
            wave.amplitude_v = sine_volts;
            drives.push_back(core::Drive{slot, 50.0});
        } else {
            continue;
        }
        sim.set_wave(core::drive_source(slot), wave, true);
    }
    core::NetMap map = circuit.nets();
    std::vector<std::string> lines = core::netlist(circuit, map, drives, {}, loads);
    for (const std::string &line : lines) {
        if (line[0] != '.' && line.size() < 100) {
            std::printf("  | %s\n", line.c_str());
        }
    }
    const core::Digital digital = core::digital(circuit, map, drives);
    for (const core::Part &part : circuit.parts) {
        if (part.kind == core::PartKind::Switch) {
            sim.set_constant(core::control_source(part), part.setting);
        }
    }
    std::vector<std::string> watches;
    for (size_t t = 0; t < circuit.taps.size(); t++) {
        watches.push_back(map.tap_net[t] >= 0 ? map.name[static_cast<size_t>(map.tap_net[t])] : std::string());
    }
    for (const core::DelayLine &delay : digital.delays) {
        watches.push_back(delay.clock[0]);
    }
    sim.set_watches(watches);
    sim.set_digital(digital);
    sim.load(lines, false);
    bool ok = run(path, project_seconds, watches.size());
    sim.stop();
    return ok;
}

}  // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    sim::Ngspice &sim = sim::Ngspice::instance();
    if (!sim.available()) {
        std::printf("%s\n", sim.status().c_str());
        return 1;
    }
    if (argc > 2) {
        pace = std::atof(argv[2]);
    }
    if (argc > 4) {
        sine_volts = std::atof(argv[3]);
        sine_hertz = std::atof(argv[4]);
    }
    if (argc > 5) {
        dump = argv[5];
    }
    if (argc > 6) {
        project_seconds = std::atof(argv[6]);
    }
    if (argc > 1) {
        return run_project(argv[1]) ? 0 : 1;
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
    all.parts.back().setting = 0.25;
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
