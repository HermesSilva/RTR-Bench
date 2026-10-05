// SPDX-License-Identifier: Apache-2.0
#include "core/circuit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <numeric>

namespace core {

namespace {

const PartDef defs[part_kind_count] = {
    {PartKind::Ground, "GROUND", "GND", "GND", 1, {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}}, {"GND", "", "", "", ""}, 0.0,
     "", {-1, 0}, {1, 1}},
    {PartKind::Resistor, "RESISTOR", "R", "R", 2, {{-2, 0}, {2, 0}, {0, 0}, {0, 0}, {0, 0}}, {"A", "B", "", "", ""}, 1e3,
     "", {-2, -1}, {2, 1}},
    {PartKind::Capacitor, "CAPACITOR", "C", "C", 2, {{-2, 0}, {2, 0}, {0, 0}, {0, 0}, {0, 0}}, {"A", "B", "", "", ""},
     100e-9, "F", {-2, -1}, {2, 1}},
    {PartKind::Inductor, "INDUCTOR", "L", "L", 2, {{-2, 0}, {2, 0}, {0, 0}, {0, 0}, {0, 0}}, {"A", "B", "", "", ""}, 10e-3,
     "H", {-2, -1}, {2, 1}},
    {PartKind::Potentiometer, "POTENTIOMETER", "POT", "RV", 3, {{-2, 0}, {2, 0}, {0, -2}, {0, 0}, {0, 0}},
     {"A", "B", "W", "", ""}, 10e3, "", {-2, -2}, {2, 1}},
    {PartKind::Switch, "SWITCH", "SW", "SW", 2, {{-2, 0}, {2, 0}, {0, 0}, {0, 0}, {0, 0}}, {"A", "B", "", "", ""}, 0.0, "",
     {-2, -1}, {2, 1}},
    {PartKind::Diode, "DIODE", "D", "D", 2, {{-2, 0}, {2, 0}, {0, 0}, {0, 0}, {0, 0}}, {"A", "K", "", "", ""}, 0.0, "",
     {-2, -1}, {2, 1}},
    {PartKind::Led, "LED", "LED", "LED", 2, {{-2, 0}, {2, 0}, {0, 0}, {0, 0}, {0, 0}}, {"A", "K", "", "", ""}, 0.0, "",
     {-2, -1}, {2, 1}},
    // Transistors: collector, base, emitter.
    {PartKind::Npn, "NPN", "NPN", "Q", 3, {{1, -2}, {-2, 0}, {1, 2}, {0, 0}, {0, 0}}, {"C", "B", "E", "", ""}, 0.0, "",
     {-2, -2}, {2, 2}},
    {PartKind::Pnp, "PNP", "PNP", "Q", 3, {{1, -2}, {-2, 0}, {1, 2}, {0, 0}, {0, 0}}, {"C", "B", "E", "", ""}, 0.0, "",
     {-2, -2}, {2, 2}},
    // Operational amplifier: in+, in-, out, V+, V-.
    {PartKind::OpAmp, "OPAMP", "OPAMP", "U", 5, {{-3, 1}, {-3, -1}, {3, 0}, {0, -2}, {0, 2}},
     {"IN+", "IN-", "OUT", "V+", "V-"}, 0.0, "", {-3, -2}, {3, 2}},
    {PartKind::VSource, "VSOURCE", "V", "V", 2, {{-2, 0}, {2, 0}, {0, 0}, {0, 0}, {0, 0}}, {"+", "-", "", "", ""}, 9.0,
     "V", {-2, -1}, {2, 1}},
    {PartKind::VSine, "VSINE", "V", "V", 2, {{-2, 0}, {2, 0}, {0, 0}, {0, 0}, {0, 0}}, {"+", "-", "", "", ""}, 1.0, "V",
     {-2, -1}, {2, 1}},
    // The current goes from A through the source to B.
    {PartKind::ISource, "ISOURCE", "I", "I", 2, {{-2, 0}, {2, 0}, {0, 0}, {0, 0}, {0, 0}}, {"A", "B", "", "", ""}, 1e-3,
     "A", {-2, -1}, {2, 1}},
    // MOSFETs: drain, gate, source (the body is on the source).
    {PartKind::Nmos, "NMOS", "NMOS", "M", 3, {{1, -2}, {-2, 0}, {1, 2}, {0, 0}, {0, 0}}, {"D", "G", "S", "", ""}, 0.0, "",
     {-2, -2}, {2, 2}},
    {PartKind::Pmos, "PMOS", "PMOS", "M", 3, {{1, -2}, {-2, 0}, {1, 2}, {0, 0}, {0, 0}}, {"D", "G", "S", "", ""}, 0.0, "",
     {-2, -2}, {2, 2}},
    {PartKind::Voltmeter, "VOLTMETER", "VM", "VM", 2, {{-2, 0}, {2, 0}, {0, 0}, {0, 0}, {0, 0}}, {"+", "-", "", "", ""},
     0.0, "", {-2, -1}, {2, 1}},
    {PartKind::Ammeter, "AMMETER", "AM", "AM", 2, {{-2, 0}, {2, 0}, {0, 0}, {0, 0}, {0, 0}}, {"+", "-", "", "", ""}, 0.0,
     "", {-2, -1}, {2, 1}},
};

// The catalog. The first entry of each kind is its generic part. Model
// parameters of the real parts are the widely published ones of each type.
const CatalogEntry entry_table[] = {
    {"Ground", "Ground", "bench ground, node 0", PartKind::Ground, 0.0, nullptr, 0.0, 0.0, Glow::Red},
    {"Resistor", "Passive", "resistor", PartKind::Resistor, 0.0, nullptr, 0.0, 0.0, Glow::Red},
    {"Capacitor", "Passive", "capacitor", PartKind::Capacitor, 0.0, nullptr, 0.0, 0.0, Glow::Red},
    {"Inductor", "Passive", "inductor", PartKind::Inductor, 0.0, nullptr, 0.0, 0.0, Glow::Red},
    {"Potentiometer", "Passive", "potentiometer, turned with the wheel", PartKind::Potentiometer, 0.0, nullptr, 0.0, 0.0,
     Glow::Red},
    {"Switch", "Switches", "on-off switch, moved with a click", PartKind::Switch, 0.0, nullptr, 0.0, 0.0, Glow::Red},
    {"Diode", "Diodes", "signal diode", PartKind::Diode, 0.0, nullptr, 0.0, 0.0, Glow::Red},
    {"1N4148", "Diodes", "signal diode, 100 V 200 mA, fast", PartKind::Diode, 0.0,
     "is=2.52n rs=0.568 n=1.752 cjo=4p m=0.4 tt=20n bv=100 ibv=100u", 0.0, 0.0, Glow::Red},
    {"1N4007", "Diodes", "rectifier, 1000 V 1 A", PartKind::Diode, 0.0,
     "is=7.02767n rs=0.0341512 n=1.80803 eg=1.05743 xti=5 bv=1000 ibv=5e-08 cjo=1e-11 vj=0.7 m=0.5 fc=0.5 tt=1e-07", 0.0,
     0.0, Glow::Red},
    {"1N5819", "Diodes", "Schottky rectifier, 40 V 1 A", PartKind::Diode, 0.0,
     "is=31.7u rs=0.051 n=1.373 cjo=110p m=0.35 eg=0.69 xti=2 bv=40 ibv=1m", 0.0, 0.0, Glow::Red},
    {"Zener 3V3", "Diodes", "Zener diode, 3.3 V", PartKind::Diode, 0.0, "is=1n rs=5 n=1.5 cjo=150p m=0.33 bv=3.3 ibv=5m",
     0.0, 0.0, Glow::Red},
    {"Zener 5V1", "Diodes", "Zener diode, 5.1 V", PartKind::Diode, 0.0, "is=1n rs=3 n=1.5 cjo=120p m=0.33 bv=5.1 ibv=5m",
     0.0, 0.0, Glow::Red},
    {"Zener 12V", "Diodes", "Zener diode, 12 V", PartKind::Diode, 0.0, "is=1n rs=4 n=1.5 cjo=60p m=0.33 bv=12 ibv=5m",
     0.0, 0.0, Glow::Red},
    {"LED", "LEDs", "red LED, 1.9 V at 10 mA", PartKind::Led, 0.0, nullptr, 1e-18, 2.0, Glow::Red},
    {"LED green", "LEDs", "green LED, 2.1 V at 10 mA", PartKind::Led, 0.0, "is=2.3e-20 n=2 rs=3 cjo=20p", 2.3e-20, 2.0,
     Glow::Green},
    {"LED amber", "LEDs", "amber LED, 2.0 V at 10 mA", PartKind::Led, 0.0, "is=1.56e-19 n=2 rs=3 cjo=20p", 1.56e-19, 2.0,
     Glow::Amber},
    {"LED blue", "LEDs", "blue LED, 3.0 V at 10 mA", PartKind::Led, 0.0, "is=1.56e-19 n=3 rs=5 cjo=30p", 1.56e-19, 3.0,
     Glow::Blue},
    {"LED white", "LEDs", "white LED, 3.0 V at 10 mA", PartKind::Led, 0.0, "is=1.56e-19 n=3 rs=5 cjo=30p", 1.56e-19, 3.0,
     Glow::White},
    {"NPN", "Transistors", "NPN transistor, gain 200", PartKind::Npn, 0.0, nullptr, 0.0, 0.0, Glow::Red},
    {"2N3904", "Transistors", "NPN small signal, 40 V 200 mA", PartKind::Npn, 0.0,
     "is=6.734f xti=3 eg=1.11 vaf=74.03 bf=416.4 ne=1.259 ise=6.734f ikf=66.78m xtb=1.5 br=0.7371 nc=2 isc=0 ikr=0 rc=1 "
     "cjc=3.638p mjc=0.3085 vjc=0.75 fc=0.5 cje=4.493p mje=0.2593 vje=0.75 tr=239.5n tf=301.2p itf=0.4 vtf=4 xtf=2 rb=10",
     0.0, 0.0, Glow::Red},
    {"2N2222", "Transistors", "NPN switching, 40 V 800 mA", PartKind::Npn, 0.0,
     "is=14.34f xti=3 eg=1.11 vaf=74.03 bf=255.9 ne=1.307 ise=14.34f ikf=0.2847 xtb=1.5 br=6.092 nc=2 isc=0 ikr=0 rc=1 "
     "cjc=7.306p mjc=0.3416 vjc=0.75 fc=0.5 cje=22.01p mje=0.377 vje=0.75 tr=46.91n tf=411.1p itf=0.6 vtf=1.7 xtf=3 rb=10",
     0.0, 0.0, Glow::Red},
    {"BC547B", "Transistors", "NPN small signal, 45 V 100 mA", PartKind::Npn, 0.0,
     "is=2.39e-14 nf=1.008 ise=3.545e-15 ne=1.541 bf=294.3 ikf=0.1357 vaf=63.2 nr=1.004 isc=6.272e-14 nc=1.243 br=7.946 "
     "ikr=0.1144 var=25.9 rb=1 re=0.4683 rc=0.85 cje=1.358e-11 vje=0.65 mje=0.3279 tf=4.391e-10 xtf=120 vtf=2.643 "
     "itf=0.7495 cjc=3.728e-12 vjc=0.3997 mjc=0.2955 xcjc=0.6193 fc=0.9579",
     0.0, 0.0, Glow::Red},
    {"PNP", "Transistors", "PNP transistor, gain 200", PartKind::Pnp, 0.0, nullptr, 0.0, 0.0, Glow::Red},
    {"2N3906", "Transistors", "PNP small signal, 40 V 200 mA", PartKind::Pnp, 0.0,
     "is=1.41f xti=3 eg=1.11 vaf=18.7 bf=180.7 ne=1.5 ise=0 ikf=80m xtb=1.5 br=4.977 nc=2 isc=0 ikr=0 rc=2.5 cjc=9.728p "
     "mjc=0.5776 vjc=0.75 fc=0.5 cje=8.063p mje=0.3677 vje=0.75 tr=33.42n tf=179.3p itf=0.4 vtf=4 xtf=6 rb=10",
     0.0, 0.0, Glow::Red},
    {"BC557B", "Transistors", "PNP small signal, 45 V 100 mA", PartKind::Pnp, 0.0,
     "is=3.834e-14 nf=1.008 ise=1.219e-14 ne=1.528 bf=344.4 ikf=0.08039 vaf=21.11 nr=1.005 isc=2.852e-13 nc=1.28 br=14.84 "
     "ikr=0.047 var=32.02 rb=1 re=0.6202 rc=0.5713 cje=1.23e-11 vje=0.6106 mje=0.378 tf=5.595e-10 xtf=3.414 vtf=5.23 "
     "itf=0.1483 cjc=1.084e-11 vjc=0.1022 mjc=0.3563 xcjc=0.6288 fc=0.8027",
     0.0, 0.0, Glow::Red},
    {"Op amp", "Amplifiers", "operational amplifier, 1 MHz", PartKind::OpAmp, 0.0, nullptr, 0.0, 0.0, Glow::Red},
    {"LM358", "Amplifiers", "single supply, 1 MHz, output down to V-", PartKind::OpAmp, 0.0,
     "gbw=1meg hp=1.5 hn=0.02 isup=0.5m", 0.0, 0.0, Glow::Red},
    {"TL072", "Amplifiers", "JFET input, 3 MHz, audio", PartKind::OpAmp, 0.0, "gbw=3meg hp=1.5 hn=1.5 isup=1.4m", 0.0, 0.0,
     Glow::Red},
    {"NE5532", "Amplifiers", "low noise, 10 MHz, audio", PartKind::OpAmp, 0.0, "gbw=10meg hp=1.2 hn=1.2 isup=4m", 0.0, 0.0,
     Glow::Red},
    {"DC source", "Sources", "DC voltage source", PartKind::VSource, 0.0, nullptr, 0.0, 0.0, Glow::Red},
    {"Battery 9 V", "Sources", "9 V battery", PartKind::VSource, 9.0, nullptr, 0.0, 0.0, Glow::Red},
    {"Cell 1.5 V", "Sources", "1.5 V cell", PartKind::VSource, 1.5, nullptr, 0.0, 0.0, Glow::Red},
    {"Supply 5 V", "Sources", "5 V rail", PartKind::VSource, 5.0, nullptr, 0.0, 0.0, Glow::Red},
    {"Supply 12 V", "Sources", "12 V rail", PartKind::VSource, 12.0, nullptr, 0.0, 0.0, Glow::Red},
    {"AC source", "Sources", "sine voltage source: amplitude and frequency", PartKind::VSine, 0.0, nullptr, 0.0, 0.0,
     Glow::Red},
    {"Current source", "Sources", "DC current source", PartKind::ISource, 0.0, nullptr, 0.0, 0.0, Glow::Red},
    {"NMOS", "Transistors", "N-channel MOSFET, threshold 2 V", PartKind::Nmos, 0.0, nullptr, 0.0, 0.0, Glow::Red},
    {"NMOS logic level", "Transistors", "N-channel MOSFET, threshold 1 V", PartKind::Nmos, 0.0,
     "level=1 vto=1 kp=2 lambda=0.01 cbd=20p cbs=20p", 0.0, 0.0, Glow::Red},
    {"PMOS", "Transistors", "P-channel MOSFET, threshold -2 V", PartKind::Pmos, 0.0, nullptr, 0.0, 0.0, Glow::Red},
    {"PMOS logic level", "Transistors", "P-channel MOSFET, threshold -1 V", PartKind::Pmos, 0.0,
     "level=1 vto=-1 kp=1 lambda=0.01 cbd=20p cbs=20p", 0.0, 0.0, Glow::Red},
    {"Voltmeter", "Meters", "shows the voltage between its pins, 10 Mohm", PartKind::Voltmeter, 0.0, nullptr, 0.0, 0.0,
     Glow::Red},
    {"Ammeter", "Meters", "shows the current through it, in series", PartKind::Ammeter, 0.0, nullptr, 0.0, 0.0, Glow::Red},
};

// The models every netlist carries: a signal diode, a red LED, small-signal
// transistors and an operational amplifier with one pole, rail-limited.
const char *const library[] = {
    ".model rtr_d d(is=2.52n rs=0.568 n=1.752 cjo=4p m=0.4 tt=20n)",
    ".model rtr_led d(is=1e-18 n=2 rs=3 cjo=20p)",
    ".model rtr_npn npn(is=1e-14 bf=200 vaf=100 cje=8p cjc=4p tf=0.4n rb=10)",
    ".model rtr_pnp pnp(is=1e-14 bf=200 vaf=100 cje=8p cjc=4p tf=0.4n rb=10)",
    ".model rtr_nmos nmos(level=1 vto=2 kp=1 lambda=0.01 cbd=20p cbs=20p)",
    ".model rtr_pmos pmos(level=1 vto=-2 kp=0.5 lambda=0.01 cbd=20p cbs=20p)",
    // Gain-bandwidth product, headroom to each rail and supply current are
    // parameters: the amplifiers of the catalog are this one with their numbers.
    ".subckt rtr_opamp inp inn out vcc vee gbw=1meg hp=1.2 hn=1.2 isup=1.5m",
    "rin inp inn 10meg",
    "iq vcc vee {isup}",
    "b1 a 0 v = max(min(1e5*(v(inp)-v(inn)), v(vcc)-{hp}), v(vee)+{hn})",
    "r1 a b 1k",
    "c1 b 0 {15.9155/gbw}",
    "e1 c 0 b 0 1",
    "ro c out 75",
    ".ends",
    // A path to ground from every node: nothing floats, whatever is drawn.
    ".options rshunt=1e9",
};

int find_root(std::vector<int> &parent, int i)
{
    while (parent[static_cast<size_t>(i)] != i) {
        parent[static_cast<size_t>(i)] = parent[static_cast<size_t>(parent[static_cast<size_t>(i)])];
        i = parent[static_cast<size_t>(i)];
    }
    return i;
}

void unite(std::vector<int> &parent, int a, int b)
{
    a = find_root(parent, a);
    b = find_root(parent, b);
    if (a != b) {
        parent[static_cast<size_t>(std::max(a, b))] = std::min(a, b);
    }
}

// The voltage between two nodes in an expression; the ground node is not
// written (it is zero by definition).
std::string difference(const std::string &a, const std::string &b)
{
    if (a == "0" && b == "0") {
        return "0";
    }
    if (a == "0") {
        return "(-v(" + b + "))";
    }
    if (b == "0") {
        return "v(" + a + ")";
    }
    return "v(" + a + "," + b + ")";
}

// One string out of several pieces.
std::string cat(std::initializer_list<std::string> pieces)
{
    size_t size = 0;
    for (const std::string &piece : pieces) {
        size += piece.size();
    }
    std::string out;
    out.reserve(size);
    for (const std::string &piece : pieces) {
        out += piece;
    }
    return out;
}

std::string number_text(double value)
{
    char text[40];
    std::snprintf(text, sizeof(text), "%.9g", value);
    return text;
}

}  // namespace

const PartDef &part_def(PartKind kind)
{
    return defs[static_cast<size_t>(kind)];
}

const std::vector<CatalogEntry> &catalog()
{
    static const std::vector<CatalogEntry> list(std::begin(entry_table), std::end(entry_table));
    return list;
}

const CatalogEntry *catalog_entry(const std::string &name)
{
    for (const CatalogEntry &entry : catalog()) {
        if (name == entry.name) {
            return &entry;
        }
    }
    return nullptr;
}

const CatalogEntry &catalog_entry(const Part &part)
{
    const CatalogEntry *named = part.model.empty() ? nullptr : catalog_entry(part.model);
    if (named && named->kind == part.kind) {
        return *named;
    }
    for (const CatalogEntry &entry : catalog()) {
        if (entry.kind == part.kind) {
            return entry;   // the generic part of the kind comes first
        }
    }
    return catalog().front();
}

bool part_kind_from_name(const std::string &name, PartKind &kind)
{
    for (const PartDef &d : defs) {
        if (name == d.name) {
            kind = d.kind;
            return true;
        }
    }
    return false;
}

GridPoint rotate(GridPoint p, int rotation)
{
    switch (rotation & 3) {
    case 1:
        return GridPoint{-p.y, p.x};
    case 2:
        return GridPoint{-p.x, -p.y};
    case 3:
        return GridPoint{p.y, -p.x};
    default:
        break;
    }
    return p;
}

GridPoint pin_position(const Part &part, int pin)
{
    GridPoint p = rotate(part_def(part.kind).pin[static_cast<size_t>(pin)], part.rotation);
    return GridPoint{part.x + p.x, part.y + p.y};
}

bool point_on_segment(GridPoint p, GridPoint a, GridPoint b)
{
    long long cross = static_cast<long long>(b.x - a.x) * (p.y - a.y) - static_cast<long long>(b.y - a.y) * (p.x - a.x);
    if (cross != 0) {
        return false;
    }
    return p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x) && p.y >= std::min(a.y, b.y) &&
           p.y <= std::max(a.y, b.y);
}

size_t Circuit::add_part(PartKind kind, int x, int y, int rotation)
{
    Part part;
    part.kind = kind;
    part.x = x;
    part.y = y;
    part.rotation = rotation & 3;
    part.value = part_def(kind).default_value;
    part.value2 = kind == PartKind::VSine ? 1000.0 : 0.0;
    part.setting = kind == PartKind::Switch ? 0.0 : 0.5;
    // The next free number among the parts with the same reference prefix.
    int number = 1;
    for (const Part &p : parts) {
        next_id_ = std::max(next_id_, p.id + 1);
        if (std::strcmp(part_def(p.kind).prefix, part_def(kind).prefix) == 0) {
            number = std::max(number, p.number + 1);
        }
    }
    part.id = next_id_++;
    part.number = number;
    parts.push_back(part);
    return parts.size() - 1;
}

const Part *Circuit::part_by_id(int id) const
{
    for (const Part &p : parts) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

std::string Circuit::reference(const Part &part) const
{
    if (part.kind == PartKind::Ground) {
        return "";
    }
    return std::string(part_def(part.kind).prefix) + std::to_string(part.number);
}

NetMap Circuit::nets() const
{
    NetMap map;
    const size_t pin_slots = parts.size() * max_part_pins;
    const size_t total = pin_slots + wires.size();
    std::vector<int> parent(total);
    std::iota(parent.begin(), parent.end(), 0);
    std::vector<GridPoint> at(pin_slots, GridPoint{0, 0});
    std::vector<bool> used(pin_slots, false);
    for (size_t i = 0; i < parts.size(); i++) {
        for (int j = 0; j < part_def(parts[i].kind).pins; j++) {
            size_t slot = i * max_part_pins + static_cast<size_t>(j);
            at[slot] = pin_position(parts[i], j);
            used[slot] = true;
        }
    }
    // Pins at the same point, pins on a wire, wires that touch with an end.
    for (size_t a = 0; a < pin_slots; a++) {
        if (!used[a]) {
            continue;
        }
        for (size_t b = a + 1; b < pin_slots; b++) {
            if (used[b] && at[a] == at[b]) {
                unite(parent, static_cast<int>(a), static_cast<int>(b));
            }
        }
        for (size_t w = 0; w < wires.size(); w++) {
            if (point_on_segment(at[a], wires[w].a, wires[w].b)) {
                unite(parent, static_cast<int>(a), static_cast<int>(pin_slots + w));
            }
        }
    }
    for (size_t a = 0; a < wires.size(); a++) {
        for (size_t b = a + 1; b < wires.size(); b++) {
            const CircuitWire &wa = wires[a];
            const CircuitWire &wb = wires[b];
            if (point_on_segment(wa.a, wb.a, wb.b) || point_on_segment(wa.b, wb.a, wb.b) ||
                point_on_segment(wb.a, wa.a, wa.b) || point_on_segment(wb.b, wa.a, wa.b)) {
                unite(parent, static_cast<int>(pin_slots + a), static_cast<int>(pin_slots + b));
            }
        }
    }
    // Roots become nets, numbered in order of appearance.
    std::vector<int> net_of_root(total, -1);
    auto net_of = [&](size_t node) {
        int root = find_root(parent, static_cast<int>(node));
        if (net_of_root[static_cast<size_t>(root)] < 0) {
            net_of_root[static_cast<size_t>(root)] = map.count++;
        }
        return net_of_root[static_cast<size_t>(root)];
    };
    map.pin_net.assign(pin_slots, -1);
    for (size_t a = 0; a < pin_slots; a++) {
        if (used[a]) {
            map.pin_net[a] = net_of(a);
        }
    }
    map.wire_net.resize(wires.size());
    for (size_t w = 0; w < wires.size(); w++) {
        map.wire_net[w] = net_of(pin_slots + w);
    }
    std::vector<bool> ground(static_cast<size_t>(map.count), false);
    for (size_t i = 0; i < parts.size(); i++) {
        if (parts[i].kind == PartKind::Ground) {
            ground[static_cast<size_t>(map.pin_net[i * max_part_pins])] = true;
            map.grounded = true;
        }
    }
    map.name.resize(static_cast<size_t>(map.count));
    int number = 1;
    for (int n = 0; n < map.count; n++) {
        map.name[static_cast<size_t>(n)] = ground[static_cast<size_t>(n)] ? "0" : "n" + std::to_string(number++);
    }
    map.tap_net.resize(taps.size());
    for (size_t t = 0; t < taps.size(); t++) {
        map.tap_net[t] = net_at(map, taps[t].at);
    }
    return map;
}

int Circuit::net_at(const NetMap &map, GridPoint p) const
{
    for (size_t i = 0; i < parts.size(); i++) {
        for (int j = 0; j < part_def(parts[i].kind).pins; j++) {
            size_t slot = i * max_part_pins + static_cast<size_t>(j);
            if (slot < map.pin_net.size() && pin_position(parts[i], j) == p) {
                return map.pin_net[slot];
            }
        }
    }
    for (size_t w = 0; w < wires.size() && w < map.wire_net.size(); w++) {
        if (point_on_segment(p, wires[w].a, wires[w].b)) {
            return map.wire_net[w];
        }
    }
    return -1;
}

std::string element_name(const Part &part)
{
    const char *letter = "";
    switch (part.kind) {
    case PartKind::Resistor:
        letter = "r";
        break;
    case PartKind::Capacitor:
        letter = "c";
        break;
    case PartKind::Inductor:
        letter = "l";
        break;
    case PartKind::Diode:
    case PartKind::Led:
        letter = "d";
        break;
    case PartKind::Npn:
    case PartKind::Pnp:
        letter = "q";
        break;
    case PartKind::OpAmp:
        letter = "x";
        break;
    case PartKind::Potentiometer:
    case PartKind::Switch:
        letter = "b";
        break;
    case PartKind::VSource:
    case PartKind::VSine:
    case PartKind::Ammeter:
        letter = "v";
        break;
    case PartKind::ISource:
        letter = "i";
        break;
    case PartKind::Nmos:
    case PartKind::Pmos:
        letter = "m";
        break;
    case PartKind::Voltmeter:
        letter = "r";
        break;
    case PartKind::Ground:
        break;
    }
    return letter + std::to_string(part.id);
}

std::string current_vector(const Part &part)
{
    bool source = part.kind == PartKind::VSource || part.kind == PartKind::VSine || part.kind == PartKind::Ammeter;
    return source ? element_name(part) + "#branch" : std::string();
}

std::string control_source(const Part &part)
{
    return "vk" + std::to_string(part.id);
}

std::string drive_source(int slot)
{
    return "vd" + std::to_string(slot);
}

std::string shunt_vector(int slot)
{
    return "vs" + std::to_string(slot) + "#branch";
}

namespace {

// The name of the model of a catalog entry in the netlist: "m_2n3904".
std::string model_name(const CatalogEntry &entry)
{
    std::string name = "m_";
    for (const char *c = entry.name; *c; c++) {
        bool letter = (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9');
        if (*c >= 'A' && *c <= 'Z') {
            name += static_cast<char>(*c - 'A' + 'a');
        } else {
            name += letter ? *c : '_';
        }
    }
    return name;
}

}  // namespace

std::vector<std::string> netlist(const Circuit &circuit, const NetMap &map, const std::vector<Drive> &drives,
                                 const std::vector<Shunt> &shunts)
{
    std::vector<std::string> lines;
    std::vector<const CatalogEntry *> models;   // the real parts in use: one model card each
    auto model_of = [&](const Part &part, const char *generic) {
        const CatalogEntry &entry = catalog_entry(part);
        if (!entry.model) {
            return std::string(generic);
        }
        if (std::find(models.begin(), models.end(), &entry) == models.end()) {
            models.push_back(&entry);
        }
        return model_name(entry);
    };
    for (size_t i = 0; i < circuit.parts.size(); i++) {
        const Part &part = circuit.parts[i];
        auto node = [&](int pin) {
            int net = map.pin_net[i * max_part_pins + static_cast<size_t>(pin)];
            return net >= 0 ? map.name[static_cast<size_t>(net)] : std::string("0");
        };
        const std::string name = element_name(part);
        const std::string id = std::to_string(part.id);
        switch (part.kind) {
        case PartKind::Ground:
            break;
        case PartKind::Resistor:
            lines.push_back(name + " " + node(0) + " " + node(1) + " " + number_text(std::max(part.value, 1e-3)));
            break;
        case PartKind::Capacitor:
            lines.push_back(name + " " + node(0) + " " + node(1) + " " + number_text(std::max(part.value, 1e-15)));
            break;
        case PartKind::Inductor:
            lines.push_back(name + " " + node(0) + " " + node(1) + " " + number_text(std::max(part.value, 1e-12)));
            break;
        case PartKind::Potentiometer: {
            // Two resistances set by the wiper position, which is the voltage
            // of a source the bench commands: turning it needs no new netlist.
            const std::string k = "k" + id;
            const std::string r = number_text(std::max(part.value, 1.0));
            lines.push_back(control_source(part) + " " + k + " 0 dc 0 external");
            lines.push_back(cat({name, "a ", node(0), " ", node(2), " i = ", difference(node(0), node(2)), " / (1 + ", r,
                                 "*v(", k, "))"}));
            lines.push_back(cat({name, "b ", node(2), " ", node(1), " i = ", difference(node(2), node(1)), " / (1 + ", r,
                                 "*(1 - v(", k, ")))"}));
            break;
        }
        case PartKind::Switch: {
            const std::string k = "k" + id;
            lines.push_back(control_source(part) + " " + k + " 0 dc 0 external");
            lines.push_back(cat({name, " ", node(0), " ", node(1), " i = ", difference(node(0), node(1)),
                                 " * (1e-9 + 100*v(", k, "))"}));
            break;
        }
        case PartKind::Diode:
            lines.push_back(name + " " + node(0) + " " + node(1) + " " + model_of(part, "rtr_d"));
            break;
        case PartKind::Led:
            lines.push_back(name + " " + node(0) + " " + node(1) + " " + model_of(part, "rtr_led"));
            break;
        case PartKind::Npn:
            lines.push_back(name + " " + node(0) + " " + node(1) + " " + node(2) + " " + model_of(part, "rtr_npn"));
            break;
        case PartKind::Pnp:
            lines.push_back(name + " " + node(0) + " " + node(1) + " " + node(2) + " " + model_of(part, "rtr_pnp"));
            break;
        case PartKind::VSource:
            lines.push_back(name + " " + node(0) + " " + node(1) + " dc " + number_text(part.value));
            break;
        case PartKind::VSine:
            // The bench gives the wave: its phase goes on from run to run.
            lines.push_back(name + " " + node(0) + " " + node(1) + " dc 0 external");
            break;
        case PartKind::ISource:
            lines.push_back(name + " " + node(0) + " " + node(1) + " dc " + number_text(part.value));
            break;
        case PartKind::Nmos:
            lines.push_back(cat({name, " ", node(0), " ", node(1), " ", node(2), " ", node(2), " ", model_of(part, "rtr_nmos")}));
            break;
        case PartKind::Pmos:
            lines.push_back(cat({name, " ", node(0), " ", node(1), " ", node(2), " ", node(2), " ", model_of(part, "rtr_pmos")}));
            break;
        case PartKind::Voltmeter:
            lines.push_back(name + " " + node(0) + " " + node(1) + " 10meg");
            break;
        case PartKind::Ammeter:
            lines.push_back(name + " " + node(0) + " " + node(1) + " dc 0");   // its branch current is the reading
            break;
        case PartKind::OpAmp: {
            // The amplifiers are one subcircuit; a real one passes its parameters.
            const CatalogEntry &entry = catalog_entry(part);
            lines.push_back(cat({name, " ", node(0), " ", node(1), " ", node(2), " ", node(3), " ", node(4), " rtr_opamp",
                                 entry.model ? " " : "", entry.model ? entry.model : ""}));
            break;
        }
        }
    }
    for (const Drive &drive : drives) {
        for (size_t t = 0; t < circuit.taps.size(); t++) {
            if (circuit.taps[t].slot != drive.slot || map.tap_net[t] < 0) {
                continue;
            }
            const std::string slot = std::to_string(drive.slot);
            lines.push_back(drive_source(drive.slot) + " d" + slot + " 0 dc 0 external");
            lines.push_back(cat({"rd", slot, " d", slot, " ", map.name[static_cast<size_t>(map.tap_net[t])], " ",
                                 number_text(std::max(drive.series_ohms, 1e-3))}));
        }
    }
    // The ammeters of the bench: a source of zero volts, whose current is
    // the reading, and the 10 milliohm of the meter.
    auto tap_node = [&](int slot) {
        for (size_t t = 0; t < circuit.taps.size(); t++) {
            if (circuit.taps[t].slot == slot && map.tap_net[t] >= 0) {
                return map.name[static_cast<size_t>(map.tap_net[t])];
            }
        }
        return std::string();
    };
    for (const Shunt &shunt : shunts) {
        const std::string in = tap_node(shunt.slot);
        const std::string out = shunt.slot_com >= 0 ? tap_node(shunt.slot_com) : std::string("0");
        if (in.empty() || out.empty()) {
            continue;
        }
        const std::string slot = std::to_string(shunt.slot);
        lines.push_back(cat({"vs", slot, " ", in, " s", slot, " dc 0"}));
        lines.push_back(cat({"rs", slot, " s", slot, " ", out, " 0.01"}));
    }
    for (const CatalogEntry *entry : models) {
        const char *type = "d";
        switch (entry->kind) {
        case PartKind::Npn:
            type = "npn";
            break;
        case PartKind::Pnp:
            type = "pnp";
            break;
        case PartKind::Nmos:
            type = "nmos";
            break;
        case PartKind::Pmos:
            type = "pmos";
            break;
        default:
            break;
        }
        lines.push_back(cat({".model ", model_name(*entry), " ", type, "(", entry->model, ")"}));
    }
    for (const char *line : library) {
        lines.emplace_back(line);
    }
    return lines;
}

bool parse_value(const std::string &text, double &value)
{
    const char *start = text.c_str();
    while (*start == ' ') {
        start++;
    }
    // The digits before the multiplier, read by hand: strtod would take the
    // "e" of an exponent, which nobody writes on a schematic.
    const char *p = start;
    double number = 0.0;
    bool digits = false;
    bool point = false;
    double place = 0.1;
    for (; (*p >= '0' && *p <= '9') || *p == '.' || *p == ','; p++) {
        if (*p == '.' || *p == ',') {
            if (point) {
                return false;
            }
            point = true;
            continue;
        }
        digits = true;
        if (point) {
            number += (*p - '0') * place;
            place *= 0.1;
        } else {
            number = number * 10.0 + (*p - '0');
        }
    }
    if (!digits) {
        return false;
    }
    while (*p == ' ') {
        p++;
    }
    double multiplier = 1.0;
    bool has_multiplier = true;
    if ((p[0] == 'M' || p[0] == 'm') && (p[1] == 'e' || p[1] == 'E') && (p[2] == 'g' || p[2] == 'G')) {
        multiplier = 1e6;
        p += 3;
    } else if (static_cast<unsigned char>(p[0]) == 0xC2 && static_cast<unsigned char>(p[1]) == 0xB5) {
        multiplier = 1e-6;   // the micro sign in UTF-8
        p += 2;
    } else {
        switch (*p) {
        case 'p':
            multiplier = 1e-12;
            break;
        case 'n':
            multiplier = 1e-9;
            break;
        case 'u':
            multiplier = 1e-6;
            break;
        case 'm':
            multiplier = 1e-3;
            break;
        case 'k':
        case 'K':
            multiplier = 1e3;
            break;
        case 'M':
            multiplier = 1e6;
            break;
        case 'G':
            multiplier = 1e9;
            break;
        case 'R':
        case 'r':
            multiplier = 1.0;
            break;
        default:
            has_multiplier = false;
            break;
        }
        if (has_multiplier) {
            p++;
        }
    }
    // "4k7": the digits after the multiplier are the decimals.
    if (has_multiplier && !point) {
        double decimal = 0.1;
        for (; *p >= '0' && *p <= '9'; p++) {
            number += (*p - '0') * decimal;
            decimal *= 0.1;
        }
    }
    // What is left may only be a unit ("F", "H", "ohm").
    for (; *p; p++) {
        bool letter = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == ' ';
        if (!letter) {
            return false;
        }
    }
    value = number * multiplier;
    return value > 0.0;
}

std::string format_value(double value, const char *unit)
{
    static const struct {
        double multiplier;
        const char *prefix;
    } table[] = {{1e9, "G"}, {1e6, "M"}, {1e3, "k"}, {1.0, ""}, {1e-3, "m"}, {1e-6, "u"}, {1e-9, "n"}, {1e-12, "p"}};
    if (value <= 0.0) {
        return std::string("0") + unit;
    }
    for (const auto &entry : table) {
        if (value >= entry.multiplier * 0.9995 || entry.multiplier == 1e-12) {
            char text[32];
            std::snprintf(text, sizeof(text), "%.3g", value / entry.multiplier);
            return std::string(text) + entry.prefix + unit;
        }
    }
    return std::string("0") + unit;
}

}  // namespace core
