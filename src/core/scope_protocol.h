// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the text protocol of the rtr-scope probe (qemu-pi4 device).
//
// Version 1 (current device): one line per output transition,
//     <ns> <pin> <level>
// and any byte sent to the device asks for a snapshot (same line format).
//
// Version 2 (planned, same device in the HermesSilva/qemu-pi4 fork):
//     V 2 <machine> <pins>            first line after connecting
//     E <ns> <pin> <level>            transition of an output pin
//     F <ns> <pin> <fsel>             function change (0 in, 1 out, 2-7 alt)
//     S <ns> <pin> <fsel> <level>     one line of a snapshot
//     T <ns>                          virtual clock
// Bench to probe: "s" snapshot, "d <pin> <level>" drive an input, "t [ms]".
//
// The parser is pure: no I/O, so it is tested without a probe.
#pragma once

#include <cstdint>
#include <string_view>

namespace core {

struct ScopeMessage {
    enum Kind { None, Event, Function, Snapshot, Version, Time } kind = None;
    int64_t ns = 0;
    int pin = 0;
    int level = 0;
    int fsel = -1;
    int version = 1;
    int pins = 0;
};

// Parses one line without its terminator. Returns false on anything it does
// not understand (the line is ignored, the stream goes on).
bool parse_scope_line(std::string_view line, ScopeMessage &out);

}  // namespace core
