cmake_minimum_required(VERSION 3.24)
project(rtr-bench VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(RTR_BENCH_TIDY "Run clang-tidy on our sources" ON)
option(RTR_BENCH_TESTS "Build the windowless tests" ON)

# ---------------------------------------------------------------------------
# Dependencies, fetched at configure time with pinned versions.
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
    GIT_SHALLOW TRUE)

FetchContent_Declare(implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG v0.17
    GIT_SHALLOW TRUE)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(glfw imgui implot json)

# Dear ImGui and ImPlot ship no CMake project: one static library for both,
# with the GLFW + OpenGL 3 backends. Third-party code: no warnings, no tidy.
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp)
target_include_directories(imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${implot_SOURCE_DIR})
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(imgui PUBLIC glfw)

# ---------------------------------------------------------------------------
# Our code.
# ---------------------------------------------------------------------------
find_package(OpenGL REQUIRED)

# Core and probes: no window, so the tests link them too.
add_library(rtr-core STATIC
    src/core/scope_protocol.cpp
    src/core/port_state.cpp
    src/probes/tcp_client.cpp
    src/probes/emulator_probe.cpp)
target_include_directories(rtr-core PUBLIC src)
target_compile_definitions(rtr-core PUBLIC $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN>)
if(WIN32)
    target_link_libraries(rtr-core PUBLIC ws2_32)
endif()
find_package(Threads REQUIRED)
target_link_libraries(rtr-core PUBLIC Threads::Threads)

add_executable(rtr-bench
    src/app/main.cpp
    src/app/app.cpp
    src/app/rack.cpp
    src/ui/theme.cpp
    src/ui/window.cpp
    src/ui/chassis.cpp
    src/ui/widgets.cpp)
target_include_directories(rtr-bench PRIVATE src)
target_link_libraries(rtr-bench PRIVATE rtr-core imgui glfw OpenGL::GL nlohmann_json::nlohmann_json)
target_compile_definitions(rtr-bench PRIVATE
    RTR_BENCH_VERSION="${PROJECT_VERSION}"
    $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN>)

foreach(target rtr-core rtr-bench)
    if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Werror -Wconversion -Wshadow -Wundef
            -Wno-unused-command-line-argument)
    endif()
endforeach()

if(WIN32)
    # A GUI program: no console window. The entry point is still main().
    set_target_properties(rtr-bench PROPERTIES WIN32_EXECUTABLE TRUE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_link_options(rtr-bench PRIVATE -Wl,/ENTRY:mainCRTStartup)
    else()
        target_link_options(rtr-bench PRIVATE /ENTRY:mainCRTStartup)
    endif()
    target_link_libraries(rtr-bench PRIVATE dwmapi)
endif()

# Console diagnostic: prints what the emulator probe sends.
add_executable(rtr-probe-dump tools/probe_dump.cpp)
target_link_libraries(rtr-probe-dump PRIVATE rtr-core)

if(RTR_BENCH_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set_target_properties(rtr-bench rtr-core PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    else()
        message(WARNING "clang-tidy not found: static checks skipped")
    endif()
endif()

if(RTR_BENCH_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
