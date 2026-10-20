// =============================================================================
// LAUNCHER SETTINGS — implementation
// =============================================================================
//
// See launcher_settings.h.

#include "core/launcher_settings.h"

#include "config/shim_config_input.h"
#include "core/data_root.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace {

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool parse_float(const std::string &v, float *out) {
    if (v.empty()) return false;
    char *end = nullptr;
    const float f = std::strtof(v.c_str(), &end);
    if (end == v.c_str() || *end != '\0') return false;
    *out = f;
    return true;
}

bool parse_bool(const std::string &v, int *out) {
    const std::string l = lower(v);
    if (l == "1" || l == "true" || l == "yes" || l == "on") { *out = 1; return true; }
    if (l == "0" || l == "false" || l == "no" || l == "off") { *out = 0; return true; }
    return false;
}

}  // namespace

extern "C" void nocturne_launcher_settings_defaults(NocturneLauncherSettings *out) {
    out->stick_deadzone = NOCTURNE_PAD_STICK_DEADZONE;
    out->turn_rate = NOCTURNE_PAD_TURN_RATE;
    out->look_rate = NOCTURNE_PAD_LOOK_RATE;
    out->invert_look = NOCTURNE_PAD_INVERT_LOOK ? 1 : 0;
}

extern "C" int nocturne_launcher_settings_parse(const char *text, NocturneLauncherSettings *inout) {
    if (text == nullptr || inout == nullptr) return 0;
    int applied = 0;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = lower(trim(line.substr(0, eq)));
        const std::string val = trim(line.substr(eq + 1));
        float f = 0.0f;
        int b = 0;
        if (key == "stick_deadzone" && parse_float(val, &f)) {
            inout->stick_deadzone = clampf(f, 0.05f, 0.40f); applied++;
        } else if (key == "turn_rate" && parse_float(val, &f)) {
            inout->turn_rate = clampf(f, 0.05f, 1.00f); applied++;
        } else if (key == "look_rate" && parse_float(val, &f)) {
            inout->look_rate = clampf(f, 0.05f, 1.00f); applied++;
        } else if (key == "invert_look" && parse_bool(val, &b)) {
            inout->invert_look = b; applied++;
        }
    }
    return applied;
}

extern "C" const NocturneLauncherSettings *nocturne_launcher_settings(void) {
    static const NocturneLauncherSettings settings = [] {
        NocturneLauncherSettings s;
        nocturne_launcher_settings_defaults(&s);
        std::ifstream f(nocturne_data_path("launcher.ini"), std::ios::binary);
        if (f) {
            std::stringstream ss;
            ss << f.rdbuf();
            nocturne_launcher_settings_parse(ss.str().c_str(), &s);
        }
        return s;
    }();
    return &settings;
}
