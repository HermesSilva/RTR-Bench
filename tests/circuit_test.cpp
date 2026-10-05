// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "core/circuit.h"
#include "core/probe.h"

using Catch::Approx;

namespace {

bool has_line(const std::vector<std::string> &lines, const std::string &wanted)
{
    return std::find(lines.begin(), lines.end(), wanted) != lines.end();
}

}  // namespace

TEST_CASE("values are read as written on a schematic")
{
    double v = 0.0;
    REQUIRE(core::parse_value("470", v));
    CHECK(v == Approx(470.0));
    REQUIRE(core::parse_value("4k7", v));
    CHECK(v == Approx(4700.0));
    REQUIRE(core::parse_value("2.2M", v));
    CHECK(v == Approx(2.2e6));
    REQUIRE(core::parse_value("100n", v));
    CHECK(v == Approx(100e-9));
    REQUIRE(core::parse_value("10mH", v));
    CHECK(v == Approx(10e-3));
    REQUIRE(core::parse_value("4u7F", v));
    CHECK(v == Approx(4.7e-6));
    REQUIRE(core::parse_value("1Meg", v));
    CHECK(v == Approx(1e6));
    CHECK_FALSE(core::parse_value("", v));
    CHECK_FALSE(core::parse_value("k", v));
    CHECK_FALSE(core::parse_value("0", v));
    CHECK_FALSE(core::parse_value("1.2.3", v));
}

TEST_CASE("values are written short")
{
    CHECK(core::format_value(4700.0, "") == "4.7k");
    CHECK(core::format_value(100e-9, "F") == "100nF");
    CHECK(core::format_value(1e6, "") == "1M");
    CHECK(core::format_value(10e-3, "H") == "10mH");
    CHECK(core::format_value(470.0, "") == "470");
}

TEST_CASE("pins rotate with the part")
{
    core::Circuit c;
    size_t r = c.add_part(core::PartKind::Resistor, 10, 5, 1);
    CHECK(core::pin_position(c.parts[r], 0) == core::GridPoint{10, 3});
    CHECK(core::pin_position(c.parts[r], 1) == core::GridPoint{10, 7});
}

TEST_CASE("a divider has three nets and a netlist")
{
    core::Circuit c;
    // R1 from (0,0) to (4,0), R2 from (4,0) down to (4,4), ground at (4,4).
    size_t r1 = c.add_part(core::PartKind::Resistor, 2, 0, 0);
    size_t r2 = c.add_part(core::PartKind::Resistor, 4, 2, 1);
    c.add_part(core::PartKind::Ground, 4, 4, 0);
    c.taps.push_back(core::Tap{0, core::GridPoint{0, 0}});
    c.taps.push_back(core::Tap{1, core::GridPoint{4, 0}});
    core::NetMap map = c.nets();
    REQUIRE(map.count == 3);
    CHECK(map.grounded);
    int top = map.pin_net[r1 * core::max_part_pins];
    int middle = map.pin_net[r1 * core::max_part_pins + 1];
    int bottom = map.pin_net[r2 * core::max_part_pins + 1];
    CHECK(middle == map.pin_net[r2 * core::max_part_pins]);
    CHECK(map.name[static_cast<size_t>(bottom)] == "0");
    CHECK(map.tap_net[0] == top);
    CHECK(map.tap_net[1] == middle);
    CHECK(c.reference(c.parts[r1]) == "R1");
    CHECK(c.reference(c.parts[r2]) == "R2");

    std::vector<core::Drive> drives = {core::Drive{0, 50.0}};
    std::vector<std::string> lines = core::netlist(c, map, drives);
    const std::string a = map.name[static_cast<size_t>(top)];
    const std::string b = map.name[static_cast<size_t>(middle)];
    CHECK(has_line(lines, "r1 " + a + " " + b + " 1000"));
    CHECK(has_line(lines, "r2 " + b + " 0 1000"));
    CHECK(has_line(lines, "vd0 d0 0 dc 0 external"));
    CHECK(has_line(lines, "rd0 d0 " + a + " 50"));
}

TEST_CASE("wires join what they touch and only that")
{
    core::Circuit c;
    size_t r1 = c.add_part(core::PartKind::Resistor, 2, 0, 0);    // pins (0,0) and (4,0)
    size_t r2 = c.add_part(core::PartKind::Resistor, 12, 6, 0);   // pins (10,6) and (14,6)
    c.wires.push_back(core::CircuitWire{{4, 0}, {10, 0}});
    c.wires.push_back(core::CircuitWire{{10, 0}, {10, 6}});
    // Crosses the first wire without an end on it: not joined.
    c.wires.push_back(core::CircuitWire{{7, -3}, {7, 3}});
    core::NetMap map = c.nets();
    CHECK(map.pin_net[r1 * core::max_part_pins + 1] == map.pin_net[r2 * core::max_part_pins]);
    CHECK(map.wire_net[0] == map.wire_net[1]);
    CHECK(map.wire_net[2] != map.wire_net[0]);
    CHECK(c.net_at(map, core::GridPoint{7, 0}) == map.wire_net[0]);
    CHECK(c.net_at(map, core::GridPoint{20, 20}) == -1);
}

TEST_CASE("a real part brings its model")
{
    core::Circuit c;
    size_t q = c.add_part(core::PartKind::Npn, 0, 0, 0);
    size_t d = c.add_part(core::PartKind::Diode, 10, 0, 0);
    size_t u = c.add_part(core::PartKind::OpAmp, 20, 0, 0);
    c.add_part(core::PartKind::Npn, 30, 0, 0);
    c.parts[q].model = "2N3904";
    c.parts[d].model = "2N3904";   // not a diode: the generic diode it is
    c.parts[u].model = "TL072";
    CHECK(std::string(core::catalog_entry(c.parts[q]).name) == "2N3904");
    CHECK(std::string(core::catalog_entry(c.parts[d]).name) == "Diode");
    std::vector<std::string> lines = core::netlist(c, c.nets(), {});
    int cards = 0;
    bool transistor = false;
    bool generic = false;
    bool diode = false;
    bool amplifier = false;
    for (const std::string &line : lines) {
        cards += line.rfind(".model m_2n3904 npn(", 0) == 0 ? 1 : 0;
        transistor = transistor || (line.rfind("q1 ", 0) == 0 && line.find(" m_2n3904") != std::string::npos);
        generic = generic || (line.rfind("q4 ", 0) == 0 && line.find(" rtr_npn") != std::string::npos);
        diode = diode || (line.rfind("d2 ", 0) == 0 && line.find(" rtr_d") != std::string::npos);
        amplifier = amplifier || (line.rfind("x3 ", 0) == 0 && line.find("rtr_opamp gbw=3meg") != std::string::npos);
    }
    CHECK(cards == 1);
    CHECK(transistor);
    CHECK(generic);
    CHECK(diode);
    CHECK(amplifier);
    CHECK(core::catalog_entry("no such part") == nullptr);
}

TEST_CASE("sources, MOSFETs and meters are in the netlist")
{
    core::Circuit c;
    size_t v = c.add_part(core::PartKind::VSource, 2, 0, 0);    // (0,0) (4,0)
    size_t a = c.add_part(core::PartKind::Ammeter, 6, 0, 0);    // (4,0) (8,0)
    size_t s = c.add_part(core::PartKind::VSine, 20, 0, 0);
    size_t m = c.add_part(core::PartKind::Nmos, 30, 0, 0);
    c.parts[m].model = "NMOS logic level";
    c.add_part(core::PartKind::Voltmeter, 40, 0, 0);
    c.add_part(core::PartKind::ISource, 50, 0, 0);
    CHECK(c.parts[v].value == Approx(9.0));
    CHECK(c.parts[s].value2 == Approx(1000.0));
    CHECK(c.reference(c.parts[v]) == "V1");
    CHECK(c.reference(c.parts[s]) == "V2");
    CHECK(core::current_vector(c.parts[a]) == "v2#branch");
    CHECK(core::current_vector(c.parts[m]).empty());
    std::vector<std::string> lines = core::netlist(c, c.nets(), {});
    CHECK(has_line(lines, "v1 n1 n2 dc 9"));
    CHECK(has_line(lines, "v2 n2 n3 dc 0"));
    CHECK(has_line(lines, "v3 n4 n5 dc 0 external"));
    CHECK(has_line(lines, "m4 n6 n7 n8 n8 m_nmos_logic_level"));
    CHECK(has_line(lines, "r5 n9 n10 10meg"));
    CHECK(has_line(lines, "i6 n11 n12 dc 0.001"));
    CHECK(has_line(lines, ".model m_nmos_logic_level nmos(level=1 vto=1 kp=2 lambda=0.01 cbd=20p cbs=20p)"));
}

TEST_CASE("an ammeter of the bench goes in series")
{
    core::Circuit c;
    c.add_part(core::PartKind::Resistor, 2, 0, 0);    // (0,0) (4,0)
    c.add_part(core::PartKind::Resistor, 10, 0, 0);   // (8,0) (12,0)
    c.taps.push_back(core::Tap{0, core::GridPoint{4, 0}});
    c.taps.push_back(core::Tap{1, core::GridPoint{8, 0}});
    c.taps.push_back(core::Tap{2, core::GridPoint{12, 0}});
    core::NetMap map = c.nets();
    std::vector<core::Shunt> shunts = {core::Shunt{0, 1}, core::Shunt{2, -1}};
    std::vector<std::string> lines = core::netlist(c, map, {}, shunts);
    CHECK(has_line(lines, "vs0 n2 s0 dc 0"));
    CHECK(has_line(lines, "rs0 s0 n3 0.01"));
    CHECK(has_line(lines, "vs2 n4 s2 dc 0"));
    CHECK(has_line(lines, "rs2 s2 0 0.01"));
    CHECK(core::shunt_vector(2) == "vs2#branch");
}

TEST_CASE("a wave is a function of time")
{
    core::WaveSpec s;
    s.kind = core::Waveform::Sine;
    s.freq_hz = 1000.0;
    s.amplitude_v = 2.0;
    s.offset_v = 1.0;
    CHECK(core::waveform_volts(s, 0.0) == Approx(1.0).margin(1e-9));
    CHECK(core::waveform_volts(s, 0.00025) == Approx(3.0).margin(1e-6));
    s.kind = core::Waveform::Dc;
    CHECK(core::waveform_volts(s, 5.0) == Approx(3.0));
    s.kind = core::Waveform::Clock;
    CHECK(core::waveform_volts(s, 0.0001) == Approx(core::logic_high_volts));
    CHECK(core::waveform_volts(s, 0.0006) == Approx(0.0));
    s.kind = core::Waveform::Noise;
    CHECK(core::waveform_volts(s, 0.123) == core::waveform_volts(s, 0.123));
}
