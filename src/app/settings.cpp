// SPDX-License-Identifier: Apache-2.0
#include "app/settings.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace app {

namespace {

std::filesystem::path executable_dir()
{
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(buffer).parent_path();
#else
    std::error_code ec;
    std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        return {};
    }
    return self.parent_path();
#endif
}

}  // namespace

const std::string &settings_dir()
{
    static const std::string dir = [] {
        std::filesystem::path base = executable_dir();
        if (base.empty()) {
            return std::string();
        }
        std::filesystem::path folder = base / ".RTR-Bench";
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        if (ec) {
            std::fprintf(stderr, "rtr-bench: cannot create %s\n", folder.string().c_str());
            return std::string();
        }
        return folder.string();
    }();
    return dir;
}

nlohmann::json load_settings(const std::string &name)
{
    if (settings_dir().empty()) {
        return nlohmann::json::object();
    }
    std::filesystem::path file = std::filesystem::path(settings_dir()) / (name + ".json");
    std::ifstream in(file);
    if (!in) {
        return nlohmann::json::object();
    }
    nlohmann::json value = nlohmann::json::parse(in, nullptr, false);
    if (value.is_discarded() || !value.is_object()) {
        std::fprintf(stderr, "rtr-bench: %s is not valid, ignored\n", file.string().c_str());
        return nlohmann::json::object();
    }
    return value;
}

bool save_settings(const std::string &name, const nlohmann::json &value)
{
    if (settings_dir().empty()) {
        return false;
    }
    std::filesystem::path file = std::filesystem::path(settings_dir()) / (name + ".json");
    std::ofstream out(file);
    if (!out) {
        return false;
    }
    out << value.dump(2) << '\n';
    return static_cast<bool>(out);
}

}  // namespace app
