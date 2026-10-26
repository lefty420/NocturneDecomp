#include "settings_store.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace nl {

NocturneLauncherSettings load_settings(const std::filesystem::path &file) {
    NocturneLauncherSettings s;
    nocturne_launcher_settings_defaults(&s);
    std::ifstream in(file, std::ios::binary);
    if (in) {
        std::stringstream ss;
        ss << in.rdbuf();
        nocturne_launcher_settings_parse(ss.str().c_str(), &s);
    }
    return s;
}

bool save_settings(const std::filesystem::path &file, const NocturneLauncherSettings &s) {
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "# Written by the launcher. The game reads this when it starts.\n"
                  "stick_deadzone=%.2f\n"
                  "turn_rate=%.2f\n"
                  "look_rate=%.2f\n"
                  "invert_look=%d\n"
                  "upscaler=%d\n"
                  "fsr_sharpness=%.2f\n"
                  "output_height=%d\n",
                  s.stick_deadzone, s.turn_rate, s.look_rate, s.invert_look ? 1 : 0, s.upscaler ? 1 : 0,
                  s.fsr_sharpness, s.output_height >= 1500 ? 2160 : 1080);
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const std::filesystem::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << buf;
        if (!out) return false;
    }
    std::filesystem::rename(tmp, file, ec);
    return !ec;
}

}  // namespace nl
