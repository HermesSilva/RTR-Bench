// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "core/circuit.h"
#include "sim/digital.h"

namespace {

// Two gates as inverters, a resistor from the first output and a capacitor
// from the second to the timing node, and a resistor from there to the
// inputs of the first: the classic CMOS oscillator, 1 / (2.2 R C).
core::Circuit oscillator(double ohms, double farads)
{
    core::Circuit c;
    c.add_part(core::PartKind::Nor, 0, 0, 0);               // A (-3,-1) B (-3,1) Y (3,0) V+ (0,-2) V- (0,2)
    c.add_part(core::PartKind::Nor, 6, 1, 0);               // A (3,0) B (3,2) Y (9,1) V+ (6,-1) V- (6,3)
    size_t r = c.add_part(core::PartKind::Resistor, 3, 6, 1);     // (3,4) (3,8)
    size_t cap = c.add_part(core::PartKind::Capacitor, 9, 5, 1);  // (9,3) (9,7)
    size_t rs = c.add_part(core::PartKind::Resistor, -5, 4, 1);   // (-5,2) (-5,6)
    c.add_part(core::PartKind::Ground, 0, 2, 0);
    c.add_part(core::PartKind::Ground, 6, 3, 0);
    c.parts[r].value = ohms;
    c.parts[cap].value = farads;
    c.parts[rs].value = 10.0 * ohms;
    const int wires[][4] = {{0, -2, 6, -2}, {6, -2, 6, -1},                   // the supply
                            {3, 0, 3, 2},   {3, 2, 3, 4},                     // first output: second gate, resistor
                            {9, 1, 9, 3},   {9, 7, 9, 8},   {9, 8, 3, 8},     // second output: capacitor, timing node
                            {-5, 6, -5, 8}, {-5, 8, 3, 8},                    // timing node: series resistor
                            {-5, 2, -5, 0}, {-5, 0, -3, 0}, {-3, -1, -3, 1}}; // and the inputs of the first gate
    for (const auto &w : wires) {
        c.wires.push_back(core::CircuitWire{{w[0], w[1]}, {w[2], w[3]}});
    }
    c.taps.push_back(core::Tap{0, core::GridPoint{0, -2}});
    return c;
}

}  // namespace

TEST_CASE("an oscillator of gates is an island, out of the netlist")
{
    core::Circuit c = oscillator(100e3, 1e-9);
    core::NetMap map = c.nets();
    std::vector<core::Drive> drives = {core::Drive{0, 0.05}};
    core::Digital digital = core::digital(c, map, drives);
    REQUIRE(digital.gates.size() == 2);
    CHECK(digital.island.nodes.size() == 4);
    CHECK(digital.island.parts.size() == 3);
    CHECK(digital.island.fixed.empty());
    CHECK(digital.gates[0].node[0] >= 0);
    CHECK(digital.gates[0].node[2] >= 0);
    CHECK(digital.gates[1].driver[0] == 0);

    std::vector<std::string> lines = core::netlist(c, map, drives);
    auto starts = [&](const std::string &prefix) {
        return std::count_if(lines.begin(), lines.end(), [&](const std::string &l) { return l.compare(0, prefix.size(), prefix) == 0; });
    };
    CHECK(starts("vi") == 4);    // the nodes of the island, shown to the simulator
    CHECK(starts("r3 ") == 0);   // its parts are not there
    CHECK(starts("c4 ") == 0);
    CHECK(starts("vg") == 0);    // nor the outputs of its gates
}

TEST_CASE("the island oscillates at the rate of its parts")
{
    for (double ohms : {100e3, 10e3}) {
        core::Circuit c = oscillator(ohms, 1e-9);
        core::NetMap map = c.nets();
        std::vector<core::Drive> drives = {core::Drive{0, 0.05}};
        core::Digital digital = core::digital(c, map, drives);
        const std::string supply = map.name[static_cast<size_t>(map.pin_net[3])];
        const std::string out = digital.island.sources[static_cast<size_t>(digital.gates[1].node[2])];

        sim::DigitalEngine engine;
        engine.set(digital);
        engine.map({supply});
        // The simulator comes with the supply at 12 V; the output is read a
        // step of the island after each point.
        const double seconds = 0.02;
        const double pace = 2e-6;
        int rises = 0;
        bool high = false;
        for (int k = 0; k * pace < seconds; k++) {
            engine.accept(k * pace, {12.0});
            double volts = 0.0;
            REQUIRE(engine.source(out, k * pace + sim::DigitalEngine::step, volts));
            rises += volts > 8.0 && !high ? 1 : 0;
            high = volts > 8.0 ? true : (volts < 4.0 ? false : high);
        }
        const double expected = seconds / (2.2 * ohms * 1e-9);
        CHECK(rises > expected * 0.85);
        CHECK(rises < expected * 1.15);
    }
}
