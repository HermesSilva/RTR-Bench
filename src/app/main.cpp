// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - Real-time Raspberry Bench. Entry point.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include "app/app.h"

namespace {

int run(int argc, char **argv)
{
    app::App app;
    const char *screenshot = nullptr;
    double delay = 5.0;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("rtr-bench %s\n", RTR_BENCH_VERSION);
            return 0;
        }
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (std::strcmp(argv[i], "--delay") == 0 && i + 1 < argc) {
            char *end = nullptr;
            delay = std::strtod(argv[++i], &end);
            if (end == argv[i] || delay < 0.0) {
                std::printf("rtr-bench: bad --delay value\n");
                return 2;
            }
        } else {
            std::printf("usage: rtr-bench [--version] [--screenshot FILE.png [--delay SECONDS]]\n"
                        "  --screenshot  writes a PNG of the rack (with alpha) after the delay and quits\n");
            return std::strcmp(argv[i], "--help") == 0 ? 0 : 2;
        }
    }
    if (screenshot) {
        app.screenshot(screenshot, delay);
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
