// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - settings on disk: JSON files in the `.RTR-Bench` folder next
// to the executable (the program is portable and writes nowhere else). One
// file per subject: bench.json, scope.json, ...
#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace app {

// The folder, created on first use; empty when it cannot be found or made.
const std::string &settings_dir();

// Reads `name`.json; an empty object when the file is missing or broken.
nlohmann::json load_settings(const std::string &name);
// Writes `name`.json, pretty-printed. False on I/O error.
bool save_settings(const std::string &name, const nlohmann::json &value);

}  // namespace app
