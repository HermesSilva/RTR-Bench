// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - a circuit: parts on a grid, wires between their pins, the
// points where the cables of the bench are plugged, and the SPICE netlist
// that describes all of it to the simulator. Pure: no drawing, no I/O.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace core {

enum class PartKind {
    Ground, Resistor, Capacitor, Inductor, Potentiometer, Switch, Diode, Led, Npn, Pnp, OpAmp,
    // Sources that are parts of the schematic (the bench outputs are the other way to power it),
    // MOSFETs, and the panel meters that show a reading on the schematic.
    VSource, VSine, ISource, Nmos, Pmos, Voltmeter, Ammeter,
    // A CMOS gate with its supply pins, and a bucket-brigade delay line.
    Nor, Bbd
};
constexpr int part_kind_count = 20;
constexpr int max_part_pins = 8;

struct GridPoint {
    int x;
    int y;
    bool operator==(const GridPoint &o) const { return x == o.x && y == o.y; }
    bool operator!=(const GridPoint &o) const { return !(*this == o); }
};

// What a kind of part is: its names, its pins and its body, in grid units at
// rotation 0.
struct PartDef {
    PartKind kind;
    const char *name;       // "RESISTOR", also the name in the saved file
    const char *key;        // on the palette: "R"
    const char *prefix;     // of the reference on the schematic: "R" of "R1"
    int pins;
    GridPoint pin[max_part_pins];
    const char *pin_name[max_part_pins];   // "A", "K", "IN+"
    double default_value;   // ohms, farads or henries; 0 when the part has no value
    const char *unit;       // after the value: "F", "H", "" for ohms
    GridPoint box_min;      // the body, for picking
    GridPoint box_max;
};
const PartDef &part_def(PartKind kind);
bool part_kind_from_name(const std::string &name, PartKind &kind);

struct Part {
    int id = 0;             // unique in the circuit, never reused
    int number = 0;         // of the reference: the 1 of "R1"
    PartKind kind = PartKind::Resistor;
    int x = 0;
    int y = 0;
    int rotation = 0;       // quarter turns clockwise
    double value = 0.0;     // delay line: its number of stages
    double value2 = 0.0;    // sine source: frequency in hertz
    double setting = 0.5;   // potentiometer: wiper position 0..1; switch: 0 open, 1 closed
    std::string model;      // the catalog entry it is ("2N3904"); empty for the generic part
};

// The catalog: what the ADD dialog offers. A generic part of each kind, and
// real parts that are a kind with the parameters of a SPICE model.
enum class Glow : uint8_t { Red, Green, Amber, Blue, White };

struct CatalogEntry {
    const char *name;          // "2N3904"; also what a part keeps in `model`
    const char *category;      // "Transistors"
    const char *description;   // "NPN small signal, 40 V 200 mA"
    PartKind kind;
    double value;              // 0: the default of the kind
    const char *model;         // model parameters ("is=... bf=..."), subcircuit parameters for an
                               // amplifier; null for the generic part
    double led_is;             // LEDs: saturation current and emission coefficient of the model,
    double led_n;              // for the brightness on the schematic
    Glow glow;
};
const std::vector<CatalogEntry> &catalog();
// The entry of a name, null when there is none.
const CatalogEntry *catalog_entry(const std::string &name);
// The entry a part is: its model, else the generic entry of its kind.
const CatalogEntry &catalog_entry(const Part &part);

struct CircuitWire {
    GridPoint a;
    GridPoint b;
};

// A cable of the bench plugged into the schematic.
struct Tap {
    int slot;       // the circuit port is numbered after it
    GridPoint at;
};

// An output of the bench (generator, supply) on a tap: a voltage source
// behind its output resistance.
struct Drive {
    int slot;
    double series_ohms;
};

// An ammeter of the bench in series: the current goes in at one tap and out
// at the other (the bench ground when there is none).
struct Shunt {
    int slot;
    int slot_com;   // -1: the bench ground
};

// What is plugged into a tap and takes current from it: an audio output of
// the computer, which is a line input seen from the circuit. The measuring
// inputs of the bench are ideal and are not here.
struct Load {
    int slot;
    double ohms;
};

GridPoint rotate(GridPoint p, int rotation);
GridPoint pin_position(const Part &part, int pin);
bool point_on_segment(GridPoint p, GridPoint a, GridPoint b);

// Which net every pin, wire and tap is on. Net names are SPICE node names:
// "0" for a net with a ground symbol, "n1", "n2"... for the others.
struct NetMap {
    int count = 0;
    std::vector<int> pin_net;          // parts * max_part_pins, -1 for a pin the part has not
    std::vector<int> wire_net;
    std::vector<int> tap_net;          // -1: the tap is on nothing
    std::vector<std::string> name;     // per net
    bool grounded = false;             // the circuit has a ground symbol
};

class Circuit {
public:
    std::vector<Part> parts;
    std::vector<CircuitWire> wires;
    std::vector<Tap> taps;

    // Adds a part with the default value of its kind; returns its index.
    size_t add_part(PartKind kind, int x, int y, int rotation);
    const Part *part_by_id(int id) const;
    std::string reference(const Part &part) const;   // "R1"

    NetMap nets() const;
    // The net at a grid point (a pin or anywhere on a wire), -1 when none.
    int net_at(const NetMap &map, GridPoint p) const;

private:
    int next_id_ = 1;
};

// Names in the netlist. A switch is commanded through a source the bench
// sets while the simulation runs; a drive is a source too. A potentiometer
// is two resistors, which the bench alters as it is turned.
std::string element_name(const Part &part);      // "r12", "q7", "x3"
// The vector with the current through a part that is a source in the
// netlist (voltage sources and ammeters): "v12#branch"; empty for the others.
std::string current_vector(const Part &part);
std::string control_source(const Part &part);    // "vk12"
// The two halves of a potentiometer, 0 from A to the wiper and 1 from the
// wiper to B: the resistor and its ohms at the position the part is at.
std::string pot_resistor(const Part &part, int half);   // "rv12a"
double pot_ohms(const Part &part, int half);
std::string drive_source(int slot);              // "vd3"
std::string shunt_vector(int slot);              // "vs3#branch": the current into the tap

// What the bench computes outside the netlist, between the time points of
// the simulator: the logic gates, the delay lines, which keep their samples,
// and the island - the resistors and capacitors that hang only on gates (an
// oscillator), which leave the netlist and are solved with a step of their
// own. Each reads nodes of the netlist ("0" is the ground) and gives its
// output, in volts, to a source of it.
struct LogicGate {
    std::string input[2];
    int driver[2];          // the gate whose output is on that input, -1 when none is
    int node[3];            // the node of the island on each input and on the output, -1 when not of it
    std::string high;       // the supply
    std::string low;
    std::string source;     // of the output, when it is not of the island
};
struct DelayLine {
    std::string input;
    std::string clock[2];   // the two phases: a sample goes in when the first rises over the second
    int clock_gate;         // the gate that drives the first phase, -1 when none does
    std::string high;       // the supply
    std::string low;
    std::string source;     // receives the sample that leaves the line
    int samples;            // samples in the line: one every two stages
    double gain;            // from the input to the output
};
// A resistor or a capacitor of the island, between two of its nodes: a
// number of the island, -1 for the ground, -2 - k for the fixed node k.
struct IslandPart {
    bool capacitor;
    int a;
    int b;
    double value;
    std::string name;       // of a resistor that changes: half of a potentiometer
};
struct Island {
    std::vector<std::string> nodes;     // in the netlist
    std::vector<std::string> sources;   // one per node: receives its voltage
    std::vector<std::string> fixed;     // nodes the island takes as given: the supply of the gates
    std::vector<IslandPart> parts;
};
struct Digital {
    std::vector<LogicGate> gates;
    std::vector<DelayLine> delays;
    Island island;
};
// `drives` and `shunts` as given to `netlist`: a net an output of the bench
// or an ammeter is on stays with the simulator.
Digital digital(const Circuit &circuit, const NetMap &map, const std::vector<Drive> &drives = {},
                const std::vector<Shunt> &shunts = {});

// The element lines of the circuit, with models and options; no title, no
// analysis and no .end (the simulator session adds those).
std::vector<std::string> netlist(const Circuit &circuit, const NetMap &map, const std::vector<Drive> &drives,
                                 const std::vector<Shunt> &shunts = {}, const std::vector<Load> &loads = {});

// Values as written on a schematic: "4k7", "100n", "2.2M", "10mH", "470".
bool parse_value(const std::string &text, double &value);
std::string format_value(double value, const char *unit);

}  // namespace core
