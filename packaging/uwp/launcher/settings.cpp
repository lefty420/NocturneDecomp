#include "settings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace launcher {

// Same ordered list as the game's own resolution selector (shims/gl/resolution.cpp).
static const Resolution kRes[] = {{320, 240}, {512, 384}, {640, 480}, {800, 600}, {1024, 768}, {1280, 1024}};

int resolution_count() { return (int)(sizeof kRes / sizeof kRes[0]); }
Resolution resolution_at(int i) {
    if (i < 0) i = 0;
    if (i >= resolution_count()) i = resolution_count() - 1;
    return kRes[i];
}

const char *fit_name(int fit) {
    switch (fit) {
        case 0: return "Fill screen";
        case 2: return "Whole-number scale";
        default: return "Keep 4:3";
    }
}

static std::string path_of(const std::string &root) {
    std::string r = root;
    if (!r.empty() && r.back() != '/' && r.back() != '\\') r += '/';
    return r + "launcher.ini";
}

static std::map<std::string, std::string> read_kv(const std::string &root) {
    std::map<std::string, std::string> kv;
    std::ifstream in(path_of(root));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos || line[0] == ';' || line[0] == '#') continue;
        kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return kv;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

Settings load_settings(const std::string &root) {
    Settings s;
    auto kv = read_kv(root);
    auto geti = [&](const char *k, int d) {
        auto it = kv.find(k);
        return it == kv.end() ? d : std::atoi(it->second.c_str());
    };
    s.resolution = clampi(geti("resolution", s.resolution), 0, resolution_count() - 1);
    s.fit = clampi(geti("fit", s.fit), 0, 2);
    s.introMovie = geti("introMovie", s.introMovie) != 0;
    s.musicVolume = clampi(geti("musicVolume", s.musicVolume), 0, 100);
    s.movieVolume = clampi(geti("movieVolume", s.movieVolume), 0, 100);
    s.lookSpeed = clampi(geti("lookSpeed", s.lookSpeed), 1, 10);
    s.deadzone = clampi(geti("deadzone", s.deadzone), 0, 40);
    s.invertLook = geti("invertLook", s.invertLook) != 0;
    s.showFps = geti("showFps", s.showFps) != 0;
    return s;
}

bool save_settings(const std::string &root, const Settings &s) {
    std::ofstream out(path_of(root), std::ios::trunc);
    if (!out) return false;
    out << "; written by the launcher\n"
        << "resolution=" << s.resolution << "\n"
        << "fit=" << s.fit << "\n"
        << "introMovie=" << (s.introMovie ? 1 : 0) << "\n"
        << "musicVolume=" << s.musicVolume << "\n"
        << "movieVolume=" << s.movieVolume << "\n"
        << "lookSpeed=" << s.lookSpeed << "\n"
        << "deadzone=" << s.deadzone << "\n"
        << "invertLook=" << (s.invertLook ? 1 : 0) << "\n"
        << "showFps=" << (s.showFps ? 1 : 0) << "\n";
    return (bool)out;
}

}  // namespace launcher

extern "C" int nocturne_launcher_setting_int(const char *root, const char *key, int fallback) {
    auto kv = launcher::read_kv(root ? root : "");
    auto it = kv.find(key ? key : "");
    return it == kv.end() ? fallback : std::atoi(it->second.c_str());
}
