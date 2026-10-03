// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the application: owns the windows and runs the frame loop.
#pragma once

#include <memory>
#include <vector>

#include "ui/window.h"

namespace app {

class App {
public:
    App();
    ~App();

    // Runs until the last window closes. Returns the process exit code.
    int run();

private:
    void open_rack();

    std::vector<std::unique_ptr<ui::Window>> windows_;
};

}  // namespace app
