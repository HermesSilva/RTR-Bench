// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the Probe: what every instrument talks to. A probe observes
// the ports of a target and, when it can, drives them. Every event carries
// a time in nanoseconds of the probe clock.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace core {

enum class PortDirection : uint8_t { Unknown, Input, Output, Alternate };

struct PortInfo {
    int index;            // number on the probe (GPIO number on the Pi)
    std::string name;     // "GPIO 18"
    std::string pin;      // physical pin on the connector ("12"), empty if none
    bool digital;
    bool analog;
    bool drivable = false;    // the bench may set its level (an input of the target)
};

struct DigitalEvent {
    int64_t ns;
    uint16_t port;
    uint8_t level;        // 0 or 1
    uint8_t kind;         // DigitalEvent::Transition or Snapshot
    static constexpr uint8_t Transition = 0;
    static constexpr uint8_t Snapshot = 1;
};

// A block of analog samples of one port, at a fixed interval, in volts.
struct AnalogBlock {
    int64_t t0_ns;            // time of the first sample
    int64_t dt_ns;            // interval between samples
    uint16_t port;
    std::vector<float> volts;
};

struct ProbeCapabilities {
    std::string name;          // "Emulator (rtr-scope)"
    std::string target;        // "Raspberry Pi 4 (qemu-pi4)"
    bool observe = false;
    bool drive = false;
    bool analog = false;
    bool virtual_time = false; // the clock is the emulator's, not the wall's
    int64_t resolution_ns = 1;
};

enum class ProbeState { Disconnected, Connecting, Connected };

struct ProbeStats {
    uint64_t events = 0;
    uint64_t bytes = 0;
    uint64_t dropped = 0;      // events lost because the queue was full
    int64_t last_ns = -1;      // time of the last event
    int reconnects = 0;
    std::string error;         // last connection error, empty when none
};

class Probe {
public:
    virtual ~Probe() = default;

    virtual const ProbeCapabilities &capabilities() const = 0;
    virtual const std::vector<PortInfo> &ports() const = 0;

    virtual void connect() = 0;
    virtual void disconnect() = 0;
    virtual ProbeState state() const = 0;
    virtual ProbeStats stats() const = 0;

    // Moves the events received since the last call into `out` (appends).
    // Called from the interface thread once per frame.
    virtual void poll(std::vector<DigitalEvent> &out) = 0;
    // Same for the analog blocks; a probe without analog ports leaves `out` alone.
    virtual void poll_analog(std::vector<AnalogBlock> &out) { (void)out; }

    // Asks the target for the level of every port; the answer arrives as
    // Snapshot events.
    virtual void request_snapshot() = 0;

    // Forces the level of an input port. No effect on a probe that cannot drive.
    virtual void drive(int port, int level) = 0;
    // Drives a repeating pulse on an input port: high for `high_ns` every
    // `period_ns` (period 0 stops it and leaves the level). Generated at the
    // target side, so the socket latency does not shape the signal.
    virtual void drive_pattern(int port, int64_t period_ns, int64_t high_ns)
    {
        (void)port;
        (void)period_ns;
        (void)high_ns;
    }
};

}  // namespace core
