# RTR-Bench — directives for Claude Code

RTR-Bench (Real-time Raspberry Bench) is a bench of virtual instruments in
C++17 with Dear ImGui + ImPlot on GLFW/OpenGL 3.3, for Windows and Linux. It
probes the RTR-OS emulator (qemu-pi4, `rtr-scope` device, TCP 127.0.0.1:5555)
and, later, the physical board through an ADALM2000 (libm2k).

The plan is `docs/PLANO.md` (Portuguese). Read the decisions (section 1) and
the requirements of the area you touch before changing anything.

## Working style

- **Plan before writing code.** Open-ended requests are discussed with the
  user first, one decision at a time; implement only what was agreed.
- **No flexibility that burdens the project.** One way of doing each thing.
- Commit and push only when the user asks. The repository is public
  (Apache-2.0); never commit secrets, personal data or conversation exports.
- **No attribution trailers in commits**: do not add `Co-Authored-By:` lines
  (or any other AI attribution) to commit messages or pull requests.

## Language

- Product in **English**: interface, console messages, code, comments,
  scripts, README, commit messages.
- Planning documents (`docs/PLANO.md`) in Portuguese. Conversation in
  Portuguese.

## Code rules

- C++17. `-Wall -Wextra -Werror -Wconversion -Wshadow -Wundef`; clang-tidy
  with `.clang-tidy` on our sources. Third-party code (fetched by CMake) is
  exempt and never edited in place.
- Layout: `src/app` (main, rack, settings), `src/core` (Probe, buffers,
  measurements, recording), `src/probes` (emulator, m2k, replay), `src/ui`
  (chassis, knobs, keys, screen, wires, overlay), `src/instruments/<name>`,
  `tests/`, `resources/` (fonts embedded at build time).
- Acquisition in its own thread; the interface thread never blocks on the
  probe. Lock-free queue between them.
- Every time value is `int64_t` nanoseconds of the probe clock. Every
  voltage is `float` volts. Units are converted only at display time.
- Nothing with the stock look of Dear ImGui reaches the user: all visible
  elements are drawn by `src/ui`. ImGui is the engine (input, text, draw
  lists), not the style.
- Three themes (Light, Dark, Amber) applied through `ui::Theme`; no colour
  literal outside `src/ui/theme.cpp` except the channel colours.
- Settings are JSON (nlohmann/json) in `.RTR-Bench/` next to the executable,
  one file per subject (`bench.json`, `scope.json`, ...). The program is
  portable: it writes nowhere else.

## Build and test

- `scripts\build.ps1` (Windows, clang + Ninja) and `scripts/build.sh`
  (Linux). Both must pass before a commit; Linux is checked in WSL Ubuntu.
- `scripts\test.ps1` / `scripts/test.sh` run the windowless tests (Catch2).
- A change in the emulator side (`rtr_scope.c`) belongs to the
  `HermesSilva/qemu-pi4` fork, not to this repository.

## Releases

- **Every release updates the screenshots** in `docs/screenshots/` for every
  interface that changed (rack, each instrument, each theme where relevant)
  and the README shows them. Use `scripts\screenshot.ps1` so the captures
  have the same size and theme set. A release with changed screens and old
  screenshots is not finished.
- Version is `0.1.<build>`: `build-number.txt` is incremented by every
  local build (`scripts\build.ps1`, `scripts/build.sh`) and committed; CI
  builds the committed number (`-KeepNumber` / `RTR_KEEP_BUILD_NUMBER=1`)
  and publishes release `v0.1.<build>` on every push to `master`
  (`.github/workflows/build.yml`). The README links to the latest release.
- The README "Current state" section says what the release does and does
  not do.
- Settings and the position of every window are persisted in `.RTR-Bench`;
  a new instrument must implement `save`/`load` and the rack must restore
  it at its place.
