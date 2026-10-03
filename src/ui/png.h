// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - minimal PNG writer (RGBA, stored deflate), for screenshots of
// the windows with their transparent margins. No compression: the files are
// larger than they could be, but there is no dependency.
#pragma once

#include <cstdint>
#include <string>

namespace ui {

// `rgba` is width*height*4 bytes, top row first. Returns false on I/O error.
bool write_png(const std::string &path, int width, int height, const uint8_t *rgba);

}  // namespace ui
