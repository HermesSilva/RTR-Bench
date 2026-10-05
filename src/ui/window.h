// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - one operating-system window per instrument: undecorated, with a
// transparent framebuffer so the window is cut to the shape of the chassis.
// Each window owns a GLFW window, an OpenGL context and a Dear ImGui context.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/fonts.h"

struct GLFWwindow;
struct ImGuiContext;

namespace ui {

struct WindowSpec {
    std::string title;
    int width = 1280;
    int height = 720;
    int x = -1;  // -1: let the system choose
    int y = -1;
    // An overlay: always on top, lets the mouse through, takes no focus and
    // has no taskbar entry. Used for the wires drawn across the desktop.
    bool overlay = false;
};

class Window {
public:
    explicit Window(const WindowSpec &spec);
    ~Window();
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;

    // The instrument draws itself in this callback, between NewFrame and Render.
    void set_draw(std::function<void(Window &)> draw) { draw_ = std::move(draw); }

    // One frame: input, draw callback, render. Returns false when the window
    // asked to close.
    bool frame();

    void request_close() { close_requested_ = true; }
    bool close_requested() const { return close_requested_; }

    // Window moves driven by the chassis (drag by the panel): begin_move at
    // the press, move_with_cursor every frame while the button is held. The
    // cursor is tracked in screen coordinates, so moving the window under it
    // does not disturb the drag.
    void begin_move();
    void move_with_cursor();
    void position(int &x, int &y) const;
    void size(int &width, int &height) const;
    void set_size(int width, int height);
    void set_bounds(int x, int y, int width, int height);
    void minimize();
    bool minimized() const;
    void raise();   // bring to front and focus
    void raise_without_focus();   // bring to front, the focus stays where it is
    void show(bool visible);
    bool focused() const;
    // Keeps the window above every other one (the pin on the chassis).
    void set_on_top(bool on_top);
    bool on_top() const { return on_top_; }

    // Saves the next rendered frame as a PNG with alpha (the transparent
    // margins stay transparent). Returns through `done` when written.
    void capture(const std::string &path) { capture_path_ = path; }
    bool captured() const { return captured_; }
    // Same, but the pixels (RGBA, top row first) stay in memory for the
    // composite "bench" screenshot.
    void capture_memory() { capture_memory_ = true; }
    bool take_capture(std::vector<uint8_t> &rgba, int &width, int &height);

    // Pixels per logical unit on the monitor the window is on (HiDPI).
    float scale() const { return scale_; }

    GLFWwindow *handle() const { return window_; }
    ImGuiContext *imgui() const { return imgui_; }
    bool valid() const { return window_ != nullptr; }

private:
    static void install_callbacks(GLFWwindow *window);
    void update_scale();

    GLFWwindow *window_ = nullptr;
    ImGuiContext *imgui_ = nullptr;
    std::function<void(Window &)> draw_;
    bool close_requested_ = false;
    float scale_ = 1.0f;
    int move_origin_x_ = 0;   // window position at the press
    int move_origin_y_ = 0;
    double grab_x_ = 0.0;     // cursor position at the press, screen coordinates
    double grab_y_ = 0.0;
    std::string capture_path_;
    bool captured_ = false;
    bool capture_memory_ = false;
    std::vector<uint8_t> captured_pixels_;
    int captured_w_ = 0;
    int captured_h_ = 0;
    bool overlay_ = false;
    bool on_top_ = false;
    Fonts fonts_;
};

// Process-wide GLFW state.
bool platform_init();
void platform_poll();
void platform_shutdown();
// Whether windows know where they are on the desktop (not on Wayland): the
// wires across the desktop need it.
bool platform_has_window_positions();
// The mouse cursor in desktop coordinates, wherever it is; false when unknown.
bool platform_cursor(int &x, int &y);

}  // namespace ui
