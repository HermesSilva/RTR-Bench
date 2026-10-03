// SPDX-License-Identifier: Apache-2.0
#include "ui/window.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>
#endif
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

#include "ui/fonts.h"
#include "ui/png.h"

namespace ui {

namespace {

// Every GLFW callback runs with whichever ImGui context happens to be current,
// so each one selects the context of its own window before forwarding to the
// ImGui backend. The window pointer is stored by GLFW.
Window *window_of(GLFWwindow *window)
{
    return static_cast<Window *>(glfwGetWindowUserPointer(window));
}

template <typename Fn, typename... Args>
void forward(GLFWwindow *window, Fn fn, Args... args)
{
    Window *self = window_of(window);
    if (!self) {
        return;
    }
    ImGui::SetCurrentContext(self->imgui());
    fn(window, args...);
}

}  // namespace

bool platform_init()
{
    glfwSetErrorCallback([](int code, const char *description) {
        std::fprintf(stderr, "glfw error %d: %s\n", code, description);
    });
    if (!glfwInit()) {
        return false;
    }
    return true;
}

void platform_poll()
{
    glfwPollEvents();
}

void platform_shutdown()
{
    glfwTerminate();
}

bool platform_has_window_positions()
{
    return glfwGetPlatform() != GLFW_PLATFORM_WAYLAND;
}

bool platform_cursor(int &x, int &y)
{
#ifdef _WIN32
    POINT p;
    if (!GetCursorPos(&p)) {
        return false;
    }
    x = p.x;
    y = p.y;
    return true;
#elif defined(__linux__)
    if (glfwGetPlatform() != GLFW_PLATFORM_X11) {
        return false;
    }
    Display *display = glfwGetX11Display();
    if (!display) {
        return false;
    }
    ::Window root = DefaultRootWindow(display);
    ::Window ret_root = 0;
    ::Window ret_child = 0;
    int root_x = 0;
    int root_y = 0;
    int win_x = 0;
    int win_y = 0;
    unsigned int mask = 0;
    if (!XQueryPointer(display, root, &ret_root, &ret_child, &root_x, &root_y, &win_x, &win_y, &mask)) {
        return false;
    }
    x = root_x;
    y = root_y;
    return true;
#else
    (void)x;
    (void)y;
    return false;
#endif
}

Window::Window(const WindowSpec &spec)
{
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, spec.overlay ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_MOUSE_PASSTHROUGH, spec.overlay ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, spec.overlay ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUSED, spec.overlay ? GLFW_FALSE : GLFW_TRUE);
    overlay_ = spec.overlay;

    window_ = glfwCreateWindow(spec.width, spec.height, spec.title.c_str(), nullptr, nullptr);
    if (!window_) {
        return;
    }
    if (spec.x != -1 || spec.y != -1) {
        glfwSetWindowPos(window_, spec.x, spec.y);
    }
#ifdef _WIN32
    if (spec.overlay) {
        // No taskbar entry for the wires.
        HWND overlay_hwnd = glfwGetWin32Window(window_);
        LONG_PTR ex = GetWindowLongPtrW(overlay_hwnd, GWL_EXSTYLE);
        SetWindowLongPtrW(overlay_hwnd, GWL_EXSTYLE, (ex | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE) & ~WS_EX_APPWINDOW);
    }
#endif
    glfwSetWindowUserPointer(window_, this);
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);

    imgui_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(imgui_);
    ImPlot::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;  // the bench keeps its own settings
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

#ifdef _WIN32
    // The ImGui GLFW backend hooks the WndProc of the window to tell pen and
    // touch from mouse. The hook reads the *current* ImGui context, which is
    // wrong as soon as there is more than one window (Windows sends messages
    // to every window while another is created or destroyed). The bench has
    // no pen or touch: the hook is undone right after the backend sets it.
    HWND hwnd = glfwGetWin32Window(window_);
    LONG_PTR glfw_wndproc = GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
#endif
    ImGui_ImplGlfw_InitForOpenGL(window_, false);
#ifdef _WIN32
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, glfw_wndproc);
#endif
    install_callbacks(window_);
    ImGui_ImplOpenGL3_Init("#version 330");

    update_scale();
    fonts_ = load_fonts(scale_);
    if (!spec.overlay) {
        glfwShowWindow(window_);
    }
}

Window::~Window()
{
    if (!window_) {
        return;
    }
    // The callbacks must not reach this window any more: destroying it
    // still sends focus and size events through them.
    glfwSetWindowUserPointer(window_, nullptr);
    glfwMakeContextCurrent(window_);
    ImGui::SetCurrentContext(imgui_);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext(imgui_);
    ImGui::SetCurrentContext(nullptr);
    imgui_ = nullptr;
    glfwDestroyWindow(window_);
}

void Window::install_callbacks(GLFWwindow *window)
{
    glfwSetWindowFocusCallback(window, [](GLFWwindow *w, int focused) {
        forward(w, ImGui_ImplGlfw_WindowFocusCallback, focused);
    });
    glfwSetCursorEnterCallback(window, [](GLFWwindow *w, int entered) {
        forward(w, ImGui_ImplGlfw_CursorEnterCallback, entered);
    });
    glfwSetCursorPosCallback(window, [](GLFWwindow *w, double x, double y) {
        forward(w, ImGui_ImplGlfw_CursorPosCallback, x, y);
    });
    glfwSetMouseButtonCallback(window, [](GLFWwindow *w, int button, int action, int mods) {
        forward(w, ImGui_ImplGlfw_MouseButtonCallback, button, action, mods);
    });
    glfwSetScrollCallback(window, [](GLFWwindow *w, double dx, double dy) {
        forward(w, ImGui_ImplGlfw_ScrollCallback, dx, dy);
    });
    glfwSetKeyCallback(window, [](GLFWwindow *w, int key, int scancode, int action, int mods) {
        forward(w, ImGui_ImplGlfw_KeyCallback, key, scancode, action, mods);
    });
    glfwSetCharCallback(window, [](GLFWwindow *w, unsigned int c) {
        forward(w, ImGui_ImplGlfw_CharCallback, c);
    });
    glfwSetWindowContentScaleCallback(window, [](GLFWwindow *w, float, float) {
        if (Window *self = window_of(w)) {
            self->update_scale();
        }
    });
    glfwSetWindowCloseCallback(window, [](GLFWwindow *w) {
        if (Window *self = window_of(w)) {
            self->request_close();
        }
    });
}

void Window::update_scale()
{
    float x = 1.0f;
    float y = 1.0f;
    glfwGetWindowContentScale(window_, &x, &y);
    scale_ = x > 0.0f ? x : 1.0f;
}

bool Window::frame()
{
    if (!window_ || close_requested_) {
        return false;
    }
    glfwMakeContextCurrent(window_);
    ImGui::SetCurrentContext(imgui_);
    set_fonts(fonts_);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (draw_) {
        draw_(*this);
    }

    ImGui::Render();
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    glViewport(0, 0, width, height);
    // Fully transparent background: whatever the chassis does not cover shows
    // the desktop behind the window.
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (!capture_path_.empty() || capture_memory_) {
        // Read the back buffer before the swap: straight RGBA, bottom row first.
        std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        std::vector<uint8_t> flipped(pixels.size());
        size_t row = static_cast<size_t>(width) * 4;
        for (int y = 0; y < height; y++) {
            std::copy(pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(height - 1 - y) * row),
                      pixels.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(height - y) * row),
                      flipped.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(y) * row));
        }
        if (!capture_path_.empty() && !write_png(capture_path_, width, height, flipped.data())) {
            std::fprintf(stderr, "rtr-bench: cannot write %s\n", capture_path_.c_str());
        }
        if (capture_memory_) {
            captured_pixels_ = std::move(flipped);
            captured_w_ = width;
            captured_h_ = height;
            capture_memory_ = false;
        }
        capture_path_.clear();
        captured_ = true;
    }
    glfwSwapBuffers(window_);
    return !close_requested_;
}

void Window::begin_move()
{
    glfwGetWindowPos(window_, &move_origin_x_, &move_origin_y_);
    double cx = 0.0;
    double cy = 0.0;
    glfwGetCursorPos(window_, &cx, &cy);
    grab_x_ = move_origin_x_ + cx;
    grab_y_ = move_origin_y_ + cy;
}

void Window::move_with_cursor()
{
    int wx = 0;
    int wy = 0;
    glfwGetWindowPos(window_, &wx, &wy);
    double cx = 0.0;
    double cy = 0.0;
    glfwGetCursorPos(window_, &cx, &cy);
    double dx = (wx + cx) - grab_x_;
    double dy = (wy + cy) - grab_y_;
    glfwSetWindowPos(window_, move_origin_x_ + static_cast<int>(dx),
                     move_origin_y_ + static_cast<int>(dy));
}

void Window::position(int &x, int &y) const
{
    glfwGetWindowPos(window_, &x, &y);
}

void Window::size(int &width, int &height) const
{
    glfwGetWindowSize(window_, &width, &height);
}

void Window::set_size(int width, int height)
{
    glfwSetWindowSize(window_, width, height);
}

void Window::set_bounds(int x, int y, int width, int height)
{
    int cx = 0;
    int cy = 0;
    int cw = 0;
    int ch = 0;
    glfwGetWindowPos(window_, &cx, &cy);
    glfwGetWindowSize(window_, &cw, &ch);
    if (cx != x || cy != y) {
        glfwSetWindowPos(window_, x, y);
    }
    if (cw != width || ch != height) {
        glfwSetWindowSize(window_, width, height);
    }
}

void Window::minimize()
{
    glfwIconifyWindow(window_);
}

bool Window::minimized() const
{
    return glfwGetWindowAttrib(window_, GLFW_ICONIFIED) != 0;
}

void Window::raise()
{
    glfwRestoreWindow(window_);
    glfwFocusWindow(window_);
}

void Window::raise_without_focus()
{
#ifdef _WIN32
    SetWindowPos(glfwGetWin32Window(window_), HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#elif defined(__linux__)
    if (glfwGetPlatform() == GLFW_PLATFORM_X11) {
        XRaiseWindow(glfwGetX11Display(), glfwGetX11Window(window_));
    }
#endif
}

bool Window::take_capture(std::vector<uint8_t> &rgba, int &width, int &height)
{
    if (captured_pixels_.empty()) {
        return false;
    }
    rgba = std::move(captured_pixels_);
    captured_pixels_.clear();
    width = captured_w_;
    height = captured_h_;
    return true;
}

bool Window::focused() const
{
#ifdef _WIN32
    // The system's own answer: GLFW's attribute can lag behind when the
    // overlay windows (floating, never focused) are shown and hidden.
    return GetForegroundWindow() == glfwGetWin32Window(window_);
#else
    return glfwGetWindowAttrib(window_, GLFW_FOCUSED) != 0;
#endif
}

void Window::show(bool visible)
{
    if (visible) {
        glfwShowWindow(window_);
    } else {
        glfwHideWindow(window_);
    }
}

}  // namespace ui
