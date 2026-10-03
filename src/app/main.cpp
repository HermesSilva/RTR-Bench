// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - Real-time Raspberry Bench. Entry point.
#include <cstdio>
#include <cstring>

#include "app/app.h"

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("rtr-bench %s\n", RTR_BENCH_VERSION);
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0) {
            std::printf("usage: rtr-bench [--version]\n");
            return 0;
        }
    }
    app::App app;
    return app.run();
}
