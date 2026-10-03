# Third-party components

Fetched by CMake at configure time (`CMakeLists.txt` pins the versions):

| Component | Use | License |
|-----------|-----|---------|
| [GLFW](https://www.glfw.org/) | windows, OpenGL context, input | zlib/libpng |
| [Dear ImGui](https://github.com/ocornut/imgui) | immediate-mode GUI engine | MIT |
| [ImPlot](https://github.com/epezent/implot) | plotting on the instrument screens | MIT |
| [nlohmann/json](https://github.com/nlohmann/json) | settings files | MIT |
| [Catch2](https://github.com/catchorg/Catch2) | tests | BSL-1.0 |

Optional, from the system:

| Component | Use | License |
|-----------|-----|---------|
| [libm2k](https://github.com/analogdevicesinc/libm2k) | ADALM2000 probe (dynamic library) | LGPL-2.1 |

Fonts embedded in the executable (`resources/fonts`):

| Font | Use | License |
|------|-----|---------|
| [Inter](https://github.com/rsms/inter) | panel labels and keys | SIL OFL 1.1 |
| [JetBrains Mono](https://github.com/JetBrains/JetBrainsMono) | readouts and tables | SIL OFL 1.1 |
| [DSEG7](https://github.com/keshikan/DSEG) | seven-segment displays | SIL OFL 1.1 |

The emulator probe, `rtr_scope.c`, is a QEMU device and lives in the
[qemu-pi4 fork](https://github.com/HermesSilva/qemu-pi4) under GPL-2.0-or-later.
