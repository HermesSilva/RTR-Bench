// SPDX-License-Identifier: Apache-2.0
// RTR-Bench - Real-time Raspberry Bench. Entry point.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#include "app/app.h"
#include "ui/theme.h"

namespace {

const char *usage =
    "usage: rtr-bench [options]\n"
    "  --probe NAME             probe to start with: emulator (default) or demo\n"
    "  --open NAME              open an instrument at start (scope, logic, gen, psu, dmm, lab)\n"
    "  --gen N=on               output N of the generator starts switched on\n"
    "  --psu N=VOLTS            output N of the supply starts switched on at VOLTS\n"
    "  --patch A=B              audio jack A of the rack (an input or PLAYING) straight into audio jack B (an output)\n"
    "  --dmm N=FUNCTION         tip N of the multimeter starts on a function (vdc, vac, vpp, freq, period,\n"
    "                           duty, width, count, level, adc, aac)\n"
    "  --link OUT:CH=IN:CH      wire an output of an instrument into an input of another (gen:1=scope:1)\n"
    "  --place NAME=X,Y         where a window opens (rack, scope, logic, gen, psu, dmm, lab)\n"
    "  --tile on                lay the windows out in columns, none over another\n"
    "  --wire NAME:CH=PORT      wire channel CH of an instrument to a port (--wire scope:1=18);\n"
    "                           20000 + N is the cable N of the circuit (its \"slot\" in the file)\n"
    "  --circuit FILE           the circuit bench opens with the circuit of FILE (as saved in lab.json)\n"
    "  --math N=FORMULA         enable math channel N (1 or 2) of the scope with a formula name\n"
    "  --screenshot NAME=FILE   write a PNG with alpha of a window (rack, scope) or of the whole\n"
    "                           bench (every window and cable at its place), then quit\n"
    "  --delay SECONDS          wait before the screenshots (default 5)\n"
    "  --version\n";

bool parse_instrument(const std::string &name, app::Instrument &kind)
{
    const struct {
        const char *name;
        app::Instrument kind;
    } table[] = {{"scope", app::Instrument::Scope},   {"logic", app::Instrument::Logic},
                 {"gen", app::Instrument::Generator}, {"psu", app::Instrument::Supply},
                 {"dmm", app::Instrument::Multimeter}, {"lab", app::Instrument::Circuit},
                 {"audio", app::Instrument::Audio}};
    for (const auto &entry : table) {
        if (name == entry.name) {
            kind = entry.kind;
            return true;
        }
    }
    return false;
}

int run(int argc, char **argv)
{
    app::App app;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        std::string value = i + 1 < argc ? argv[i + 1] : "";
        if (arg == "--version") {
            std::printf("rtr-bench %s\n", RTR_BENCH_VERSION);
            return 0;
        }
        if (arg == "--help") {
            std::printf("%s", usage);
            return 0;
        }
        if (value.empty()) {
            std::printf("%s", usage);
            return 2;
        }
        i++;
        if (arg == "--probe") {
            if (value == "emulator") {
                app.probe_at_start(app::App::ProbeKind::Emulator);
            } else if (value == "demo") {
                app.probe_at_start(app::App::ProbeKind::Demo);
            } else {
                std::printf("rtr-bench: unknown probe '%s'\n", value.c_str());
                return 2;
            }
        } else if (arg == "--theme") {
            if (value == "light") {
                app.theme_at_start(ui::ThemeKind::Light);
            } else if (value == "dark") {
                app.theme_at_start(ui::ThemeKind::Dark);
            } else if (value == "amber") {
                app.theme_at_start(ui::ThemeKind::Amber);
            } else {
                std::printf("rtr-bench: unknown theme '%s'\n", value.c_str());
                return 2;
            }
        } else if (arg == "--open") {
            app::Instrument kind;
            if (!parse_instrument(value, kind)) {
                std::printf("rtr-bench: unknown instrument '%s'\n", value.c_str());
                return 2;
            }
            app.open_at_start(kind);
        } else if (arg == "--wire") {
            size_t colon = value.find(':');
            size_t eq = value.find('=');
            app::Instrument kind;
            if (colon == std::string::npos || eq == std::string::npos || eq < colon ||
                !parse_instrument(value.substr(0, colon), kind)) {
                std::printf("rtr-bench: bad --wire '%s'\n", value.c_str());
                return 2;
            }
            std::string ch_text = value.substr(colon + 1, eq - colon - 1);
            std::string port_text = value.substr(eq + 1);
            char *ch_end = nullptr;
            char *port_end = nullptr;
            long channel = std::strtol(ch_text.c_str(), &ch_end, 10);
            long port = std::strtol(port_text.c_str(), &port_end, 10);
            if (ch_text.empty() || port_text.empty() || *ch_end != '\0' || *port_end != '\0' || channel < 1 ||
                port < 0 || port > 65535) {
                std::printf("rtr-bench: bad --wire '%s'\n", value.c_str());
                return 2;
            }
            app.wire_at_start(kind, static_cast<int>(channel) - 1, static_cast<int>(port));
        } else if (arg == "--tile") {
            app.tile_at_start();
        } else if (arg == "--circuit") {
            app.circuit_at_start(value);
        } else if (arg == "--link") {
            // "--link gen:1=scope:1": output 1 of the generator into channel 1 of the scope.
            size_t eq = value.find('=');
            size_t c1 = value.find(':');
            size_t c2 = eq == std::string::npos ? std::string::npos : value.find(':', eq);
            app::Instrument out;
            app::Instrument in;
            if (eq == std::string::npos || c1 == std::string::npos || c2 == std::string::npos || c1 > eq ||
                !parse_instrument(value.substr(0, c1), out) || !parse_instrument(value.substr(eq + 1, c2 - eq - 1), in)) {
                std::printf("rtr-bench: bad --link '%s' (use OUT:N=IN:M)\n", value.c_str());
                return 2;
            }
            char *e1 = nullptr;
            char *e2 = nullptr;
            std::string n1 = value.substr(c1 + 1, eq - c1 - 1);
            std::string n2 = value.substr(c2 + 1);
            long o = std::strtol(n1.c_str(), &e1, 10);
            long i2 = std::strtol(n2.c_str(), &e2, 10);
            if (n1.empty() || n2.empty() || *e1 != '\0' || *e2 != '\0' || o < 1 || i2 < 1) {
                std::printf("rtr-bench: bad --link '%s' (use OUT:N=IN:M)\n", value.c_str());
                return 2;
            }
            app.link_at_start(out, static_cast<int>(o) - 1, in, static_cast<int>(i2) - 1);
        } else if (arg == "--place") {
            // "--place scope=40,280": where a window opens (rack or an instrument).
            size_t eq = value.find('=');
            size_t comma = value.find(',');
            if (eq == std::string::npos || comma == std::string::npos || comma < eq) {
                std::printf("rtr-bench: bad --place '%s' (use NAME=X,Y)\n", value.c_str());
                return 2;
            }
            char *end_x = nullptr;
            char *end_y = nullptr;
            std::string xs = value.substr(eq + 1, comma - eq - 1);
            std::string ys = value.substr(comma + 1);
            long px = std::strtol(xs.c_str(), &end_x, 10);
            long py = std::strtol(ys.c_str(), &end_y, 10);
            if (xs.empty() || ys.empty() || *end_x != '\0' || *end_y != '\0') {
                std::printf("rtr-bench: bad --place '%s' (use NAME=X,Y)\n", value.c_str());
                return 2;
            }
            app.place_at_start(value.substr(0, eq), static_cast<int>(px), static_cast<int>(py));
        } else if (arg == "--gen") {
            // "--gen 1=on": output 1 of the generator starts on (for screenshots).
            size_t eq = value.find('=');
            if (eq == std::string::npos || value.substr(eq + 1) != "on") {
                std::printf("rtr-bench: bad --gen '%s' (use N=on)\n", value.c_str());
                return 2;
            }
            char *end = nullptr;
            long n = std::strtol(value.c_str(), &end, 10);
            if (end == value.c_str() || n < 1 || n > 8) {
                std::printf("rtr-bench: bad --gen '%s' (use N=on)\n", value.c_str());
                return 2;
            }
            app.generator_on_at_start(static_cast<int>(n));
        } else if (arg == "--psu") {
            // "--psu 1=3.8": output 1 of the supply starts on, at 3.8 V.
            size_t eq = value.find('=');
            long n = eq == std::string::npos ? 0 : std::strtol(value.c_str(), nullptr, 10);
            char *vend = nullptr;
            double volts = eq == std::string::npos ? -1.0 : std::strtod(value.c_str() + eq + 1, &vend);
            if (n < 1 || n > 6 || volts < 0.0 || volts > 30.0 || vend == value.c_str() + eq + 1) {
                std::printf("rtr-bench: bad --psu '%s' (use N=VOLTS)\n", value.c_str());
                return 2;
            }
            app.supply_on_at_start(static_cast<int>(n), static_cast<float>(volts));
        } else if (arg == "--patch") {
            size_t eq = value.find('=');
            long a = eq == std::string::npos ? 0 : std::strtol(value.c_str(), nullptr, 10);
            long b = eq == std::string::npos ? 0 : std::strtol(value.c_str() + eq + 1, nullptr, 10);
            if (a < 1 || b < 1 || a > 64 || b > 64) {
                std::printf("rtr-bench: bad --patch '%s' (use A=B)\n", value.c_str());
                return 2;
            }
            app.audio_patch_at_start(static_cast<int>(a), static_cast<int>(b));
        } else if (arg == "--dmm") {
            // "--dmm 1=adc": tip 1 of the multimeter starts measuring DC current.
            const char *const names[] = {"vdc", "vac", "vpp", "freq", "period", "duty", "width", "count", "level", "adc", "aac"};
            size_t eq = value.find('=');
            long n = eq == std::string::npos ? 0 : std::strtol(value.c_str(), nullptr, 10);
            int function = -1;
            for (int k = 0; eq != std::string::npos && k < 11; k++) {
                if (value.substr(eq + 1) == names[k]) {
                    function = k;
                }
            }
            if (n < 1 || n > 8 || function < 0) {
                std::printf("rtr-bench: bad --dmm '%s' (use N=FUNCTION)\n", value.c_str());
                return 2;
            }
            app.multimeter_function_at_start(static_cast<int>(n), function);
        } else if (arg == "--math") {
            size_t eq = value.find('=');
            if (eq == std::string::npos || (value[0] != '1' && value[0] != '2')) {
                std::printf("rtr-bench: bad --math '%s' (use 1=\"A AND B\")\n", value.c_str());
                return 2;
            }
            app.math_at_start(value[0] - '0', value.substr(eq + 1));
        } else if (arg == "--screenshot") {
            size_t eq = value.find('=');
            if (eq == std::string::npos) {
                std::printf("rtr-bench: bad --screenshot '%s'\n", value.c_str());
                return 2;
            }
            std::string window = value.substr(0, eq);
            for (char &c : window) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            // "rack" and "bench" are the application's own names; the rest are
            // instrument names (SCOPE, ...).
            if (window == "RACK") {
                window = "rack";
            } else if (window == "BENCH") {
                window = "bench";
            }
            app.screenshot(window, value.substr(eq + 1));
        } else if (arg == "--delay") {
            char *end = nullptr;
            double delay = std::strtod(value.c_str(), &end);
            if (end == value.c_str() || delay < 0.0) {
                std::printf("rtr-bench: bad --delay value\n");
                return 2;
            }
            app.set_screenshot_delay(delay);
        } else {
            std::printf("%s", usage);
            return 2;
        }
    }
    return app.run();
}

}  // namespace

int main(int argc, char **argv)
{
    try {
        return run(argc, argv);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "rtr-bench: %s\n", e.what());
        return 1;
    }
}
