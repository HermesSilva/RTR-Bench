// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the demo probe: synthetic signals, no target. Shows the bench
// working without the emulator or the ADALM2000, and is the stand-in for
// the analog channels until the ADALM2000 arrives. Ports:
//   D0..D7   an 8-bit counter at 10 kHz (D0 toggles at 5 kHz)
//   D8       a 1 kHz PWM at 30 %
//   A1       sine, 1 kHz, 2 Vpp
//   A2       triangle, 500 Hz, 3 Vpp, offset 0.5 V
//   A3       ramp, 250 Hz, 1 Vpp
//   A4       sine with noise, 2 kHz, 1 Vpp
// Analog sampling at 1 MS/s. Time is the wall clock from the start.
#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "core/event_queue.h"
#include "core/probe.h"

namespace probes {

class DemoProbe : public core::Probe {
public:
    DemoProbe();
    ~DemoProbe() override;

    const core::ProbeCapabilities &capabilities() const override { return capabilities_; }
    const std::vector<core::PortInfo> &ports() const override { return ports_; }

    void connect() override;
    void disconnect() override;
    core::ProbeState state() const override { return state_.load(); }
    core::ProbeStats stats() const override;

    void poll(std::vector<core::DigitalEvent> &out) override;
    void poll_analog(std::vector<core::AnalogBlock> &out) override;
    void request_snapshot() override;
    void drive(int port, int level) override;

    static constexpr int digital_ports = 9;
    static constexpr int analog_ports = 4;
    static constexpr int64_t analog_dt_ns = 1000;   // 1 MS/s

private:
    void run();
    void generate(int64_t from_ns, int64_t to_ns);

    core::ProbeCapabilities capabilities_;
    std::vector<core::PortInfo> ports_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<core::ProbeState> state_{core::ProbeState::Disconnected};
    core::EventQueue<core::DigitalEvent> events_{1u << 16};

    std::mutex analog_mutex_;
    std::vector<core::AnalogBlock> analog_;   // blocks waiting for the interface

    mutable std::mutex stats_mutex_;
    core::ProbeStats stats_;
    int64_t generated_ns_ = 0;
    uint64_t pushed_ = 0;
    int levels_[digital_ports] = {};
    bool snapshot_pending_ = true;
};

}  // namespace probes
