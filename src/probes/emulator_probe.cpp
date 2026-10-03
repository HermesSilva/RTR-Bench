// SPDX-License-Identifier: Apache-2.0
#include "probes/emulator_probe.h"

#include <chrono>
#include <cstdio>

#include "core/scope_protocol.h"
#include "probes/tcp_client.h"

namespace probes {

namespace {

// The 40-pin header of the Raspberry Pi 4: physical pin of each GPIO (BCM).
const char *header_pin(int gpio)
{
    static const char *pins[28] = {"27", "28", "3",  "5",  "7",  "29", "31", "26", "24", "21",
                                   "19", "23", "32", "33", "8",  "10", "36", "11", "12", "35",
                                   "38", "40", "15", "16", "18", "22", "37", "13"};
    return gpio >= 0 && gpio < 28 ? pins[gpio] : "";
}

}  // namespace

core::PortDirection fsel_direction(int fsel)
{
    switch (fsel) {
    case 0:
        return core::PortDirection::Input;
    case 1:
        return core::PortDirection::Output;
    default:
        return fsel < 0 ? core::PortDirection::Unknown : core::PortDirection::Alternate;
    }
}

EmulatorProbe::EmulatorProbe(std::string host, uint16_t port)
    : host_(std::move(host)), port_(port)
{
    address_ = host_ + ":" + std::to_string(port_);
    capabilities_.name = "Emulator";
    capabilities_.target = "Raspberry Pi 4 (qemu-pi4, rtr-scope)";
    capabilities_.observe = true;
    capabilities_.drive = false;   // becomes true when a version 2 probe answers
    capabilities_.analog = false;
    capabilities_.virtual_time = true;
    capabilities_.resolution_ns = 1;

    // The 28 GPIOs of the header. The controller has 58 lines; the others
    // are the SD card, Ethernet and internal signals, not on the connector.
    for (int gpio = 0; gpio < 28; gpio++) {
        core::PortInfo info;
        info.index = gpio;
        info.name = "GPIO " + std::to_string(gpio);
        info.pin = header_pin(gpio);
        info.digital = true;
        info.analog = false;
        ports_.push_back(info);
    }
}

EmulatorProbe::~EmulatorProbe()
{
    stop_thread();
}

void EmulatorProbe::connect()
{
    if (running_.exchange(true)) {
        return;
    }
    thread_ = std::thread([this] { run(); });
}

void EmulatorProbe::disconnect()
{
    stop_thread();
}

void EmulatorProbe::stop_thread()
{
    if (!running_.exchange(false)) {
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    state_.store(core::ProbeState::Disconnected);
}

core::ProbeStats EmulatorProbe::stats() const
{
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void EmulatorProbe::poll(std::vector<core::DigitalEvent> &out)
{
    events_.drain(out);
}

void EmulatorProbe::poll_functions(std::vector<FunctionChange> &out)
{
    functions_.drain(out);
}

void EmulatorProbe::queue_command(const std::string &command)
{
    std::lock_guard<std::mutex> lock(command_mutex_);
    commands_ += command;
}

void EmulatorProbe::request_snapshot()
{
    // Version 1 answers any byte with a snapshot; version 2 wants "s".
    queue_command("s\n");
}

void EmulatorProbe::drive(int port, int level)
{
    if (!capabilities_.drive) {
        return;
    }
    queue_command("d " + std::to_string(port) + " " + std::to_string(level ? 1 : 0) + "\n");
}

void EmulatorProbe::handle_line(std::string_view line)
{
    core::ScopeMessage m;
    if (!core::parse_scope_line(line, m)) {
        return;
    }
    switch (m.kind) {
    case core::ScopeMessage::Event:
    case core::ScopeMessage::Snapshot: {
        core::DigitalEvent e;
        e.ns = m.ns;
        e.port = static_cast<uint16_t>(m.pin);
        e.level = static_cast<uint8_t>(m.level);
        e.kind = m.kind == core::ScopeMessage::Event ? core::DigitalEvent::Transition
                                                      : core::DigitalEvent::Snapshot;
        bool pushed = events_.push(e);
        if (m.kind == core::ScopeMessage::Snapshot && m.fsel >= 0) {
            functions_.push(FunctionChange{m.pin, m.fsel});
        }
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.events++;
        stats_.last_ns = m.ns;
        if (!pushed) {
            stats_.dropped++;
        }
        break;
    }
    case core::ScopeMessage::Function:
        functions_.push(FunctionChange{m.pin, m.fsel});
        break;
    case core::ScopeMessage::Version:
        protocol_version_ = m.version;
        capabilities_.drive = m.version >= 2;
        break;
    case core::ScopeMessage::Time: {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.last_ns = m.ns;
        break;
    }
    case core::ScopeMessage::None:
        break;
    }
}

void EmulatorProbe::run()
{
    TcpClient client;
    std::string buffer;
    char chunk[65536];

    while (running_.load()) {
        state_.store(core::ProbeState::Connecting);
        if (!client.connect(host_, port_, 500)) {
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.error = client.error();
            }
            // Not there yet: try again in a second, checking for stop meanwhile.
            for (int i = 0; i < 10 && running_.load(); i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }
        state_.store(core::ProbeState::Connected);
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.reconnects++;
            stats_.error.clear();
        }
        buffer.clear();
        protocol_version_ = 1;
        request_snapshot();

        while (running_.load() && client.connected()) {
            std::string pending;
            {
                std::lock_guard<std::mutex> lock(command_mutex_);
                pending.swap(commands_);
            }
            if (!pending.empty() && !client.send(pending.data(), pending.size())) {
                break;
            }
            int n = client.receive(chunk, sizeof(chunk), 50);
            if (n < 0) {
                break;
            }
            if (n == 0) {
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.bytes += static_cast<uint64_t>(n);
            }
            buffer.append(chunk, static_cast<size_t>(n));
            size_t start = 0;
            for (;;) {
                size_t nl = buffer.find('\n', start);
                if (nl == std::string::npos) {
                    break;
                }
                handle_line(std::string_view(buffer).substr(start, nl - start));
                start = nl + 1;
            }
            buffer.erase(0, start);
            if (buffer.size() > 4096) {
                buffer.clear();  // a line this long is not ours
            }
        }
        client.close();
        state_.store(core::ProbeState::Connecting);
    }
    client.close();
    state_.store(core::ProbeState::Disconnected);
}

}  // namespace probes
