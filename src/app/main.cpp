// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - Real-time Raspberry Bench. Entry point.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#include "app/app.h"

namespace {

const char *usage =
    "usage: rtr-bench [options]\n"
    "  --probe NAME             probe to start with: emulator (default) or demo\n"
    "  --open NAME              open an instrument at start (scope)\n"
    "  --wire NAME:CH=PORT      wire channel CH of an instrument to a port (--wire scope:1=18)\n"
    "  --math N=FORMULA         enable math channel N (1 or 2) of the scope with a formula name\n"
    "  --screenshot NAME=FILE   write a PNG with alpha of a window (rack, scope) or of the whole\n"
    "                           bench (every window and cable at its place), then quit\n"
    "  --delay SECONDS          wait before the screenshots (default 5)\n"
    "  --version\n";

bool parse_instrument(const std::string &name, app::Instrument &kind)
{
    if (name == "scope") {
        kind = app::Instrument::Scope;
        return true;
    }
    return false;
}

int run(int argc, char **argv)
{
    app::App app;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        std::string value = i + 1 < argc ? argv[i + 1] : "";
        if (arg == "--version") {
            std::printf("rtr-bench %s\n", RTR_BENCH_VERSION);
            return 0;
        }
        if (arg == "--help") {
            std::printf("%s", usage);
            return 0;
        }
        if (value.empty()) {
            std::printf("%s", usage);
            return 2;
        }
        i++;
        if (arg == "--probe") {
            if (value == "emulator") {
                app.probe_at_start(app::App::ProbeKind::Emulator);
            } else if (value == "demo") {
                app.probe_at_start(app::App::ProbeKind::Demo);
            } else {
                std::printf("rtr-bench: unknown probe '%s'\n", value.c_str());
                return 2;
            }
        } else if (arg == "--open") {
            app::Instrument kind;
            if (!parse_instrument(value, kind)) {
                std::printf("rtr-bench: unknown instrument '%s'\n", value.c_str());
                return 2;
            }
            app.open_at_start(kind);
        } else if (arg == "--wire") {
            size_t colon = value.find(':');
            size_t eq = value.find('=');
            app::Instrument kind;
            if (colon == std::string::npos || eq == std::string::npos || eq < colon ||
                !parse_instrument(value.substr(0, colon), kind)) {
                std::printf("rtr-bench: bad --wire '%s'\n", value.c_str());
                return 2;
            }
            std::string ch_text = value.substr(colon + 1, eq - colon - 1);
            std::string port_text = value.substr(eq + 1);
            char *ch_end = nullptr;
            char *port_end = nullptr;
            long channel = std::strtol(ch_text.c_str(), &ch_end, 10);
            long port = std::strtol(port_text.c_str(), &port_end, 10);
            if (ch_text.empty() || port_text.empty() || *ch_end != '\0' || *port_end != '\0' || channel < 1 ||
                port < 0 || port > 255) {
                std::printf("rtr-bench: bad --wire '%s'\n", value.c_str());
                return 2;
            }
            app.wire_at_start(kind, static_cast<int>(channel) - 1, static_cast<int>(port));
        } else if (arg == "--math") {
            size_t eq = value.find('=');
            if (eq == std::string::npos || (value[0] != '1' && value[0] != '2')) {
                std::printf("rtr-bench: bad --math '%s' (use 1=\"A AND B\")\n", value.c_str());
                return 2;
            }
            app.math_at_start(value[0] - '0', value.substr(eq + 1));
        } else if (arg == "--screenshot") {
            size_t eq = value.find('=');
            if (eq == std::string::npos) {
                std::printf("rtr-bench: bad --screenshot '%s'\n", value.c_str());
                return 2;
            }
            std::string window = value.substr(0, eq);
            for (char &c : window) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            // "rack" and "bench" are the application's own names; the rest are
            // instrument names (SCOPE, ...).
            if (window == "RACK") {
                window = "rack";
            } else if (window == "BENCH") {
                window = "bench";
            }
            app.screenshot(window, value.substr(eq + 1));
        } else if (arg == "--delay") {
            char *end = nullptr;
            double delay = std::strtod(value.c_str(), &end);
            if (end == value.c_str() || delay < 0.0) {
                std::printf("rtr-bench: bad --delay value\n");
                return 2;
            }
            app.set_screenshot_delay(delay);
        } else {
            std::printf("%s", usage);
            return 2;
        }
    }
    return app.run();
}

}  // namespace

int main(int argc, char **argv)
{
    try {
        return run(argc, argv);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "rtr-bench: %s\n", e.what());
        return 1;
    }
}
