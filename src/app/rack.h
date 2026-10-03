// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - the mini rack: the ports of the target as jacks with live
// LEDs, the probe selector and its state, the keys that open the
// instruments and the theme keys.
#pragma once

namespace ui {
class Window;
}

namespace app {

class App;

class Rack {
public:
    explicit Rack(App &app) : app_(app) {}
    void draw(ui::Window &window);

private:
    App &app_;
    int selected_port_ = -1;   // jack clicked, start of a wire (stage 3)
};

}  // namespace app
