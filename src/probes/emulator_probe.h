// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the emulator probe: the rtr-scope device of qemu-pi4, reached
// over TCP (127.0.0.1:5555 by default). An acquisition thread reads the
// stream, parses it and pushes events to a lock-free queue; the interface
// drains the queue once per frame. The thread reconnects by itself.
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/event_queue.h"
#include "core/probe.h"

namespace probes {

// GPFSEL value of the BCM2711 to a port direction.
core::PortDirection fsel_direction(int fsel);

class EmulatorProbe : public core::Probe {
public:
    EmulatorProbe(std::string host = "127.0.0.1", uint16_t port = 5555);
    ~EmulatorProbe() override;

    const core::ProbeCapabilities &capabilities() const override { return capabilities_; }
    const std::vector<core::PortInfo> &ports() const override { return ports_; }

    void connect() override;
    void disconnect() override;
    core::ProbeState state() const override { return state_.load(); }
    core::ProbeStats stats() const override;

    void poll(std::vector<core::DigitalEvent> &out) override;
    void request_snapshot() override;
    void drive(int port, int level) override;

    // Direction changes reported by a version 2 probe (port, fsel), drained
    // with the events.
    struct FunctionChange {
        int port;
        int fsel;
    };
    void poll_functions(std::vector<FunctionChange> &out);

    const std::string &address() const { return address_; }

private:
    void stop_thread();
    void run();
    void handle_line(std::string_view line);
    void queue_command(const std::string &command);

    core::ProbeCapabilities capabilities_;
    std::vector<core::PortInfo> ports_;
    std::string host_;
    uint16_t port_;
    std::string address_;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<core::ProbeState> state_{core::ProbeState::Disconnected};
    core::EventQueue<core::DigitalEvent> events_{1u << 16};
    core::EventQueue<FunctionChange> functions_{256};

    mutable std::mutex stats_mutex_;
    core::ProbeStats stats_;

    std::mutex command_mutex_;
    std::string commands_;   // bytes to send on the next loop
    int protocol_version_ = 1;
};

}  // namespace probes
