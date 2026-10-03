// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - rtr-probe-dump: connects to the emulator probe and prints what
// it receives, for checking the emulator side without the bench.
//
//   rtr-probe-dump [host] [port] [seconds]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "probes/emulator_probe.h"

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? std::atoi(argv[2]) : 5555;
    int seconds = argc > 3 ? std::atoi(argv[3]) : 3;

    probes::EmulatorProbe probe(host, static_cast<uint16_t>(port));
    probe.connect();
    std::vector<core::DigitalEvent> events;
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    core::ProbeState last = core::ProbeState::Disconnected;
    size_t printed = 0;
    while (std::chrono::steady_clock::now() < end) {
        core::ProbeState s = probe.state();
        if (s != last) {
            std::printf("state: %s\n", s == core::ProbeState::Connected    ? "connected"
                                       : s == core::ProbeState::Connecting ? "connecting"
                                                                           : "disconnected");
            last = s;
        }
        events.clear();
        probe.poll(events);
        for (const core::DigitalEvent &e : events) {
            if (printed < 40) {
                std::printf("%s %lld ns  port %u  level %u\n",
                            e.kind == core::DigitalEvent::Snapshot ? "snapshot  " : "transition",
                            static_cast<long long>(e.ns), e.port, e.level);
                printed++;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    core::ProbeStats st = probe.stats();
    if (!st.error.empty()) {
        std::printf("error: %s\n", st.error.c_str());
    }
    std::printf("events %llu, bytes %llu, dropped %llu, reconnects %d, last %lld ns\n",
                static_cast<unsigned long long>(st.events), static_cast<unsigned long long>(st.bytes),
                static_cast<unsigned long long>(st.dropped), st.reconnects, static_cast<long long>(st.last_ns));
    probe.disconnect();
    return st.events > 0 ? 0 : 1;
}
