#include "settings.h"

#include "fs_util.h"

namespace nl {

namespace {
std::vector<Choice> onoff() { return {{"Off", 0}, {"On", 1}}; }
}  // namespace

Settings::Settings(std::string data_root) : root_(std::move(data_root)) {
    SettingDef res;
    res.label = "Resolution";
    res.help = "Game render resolution. Higher modes need the 3D renderer and cost more memory and speed.";
    res.resolution = true;
    res.applies_next_launch = true;
    for (auto wh : {std::pair<int, int>{320, 240}, {512, 384}, {640, 480}, {800, 600}, {1024, 768}, {1280, 1024}})
        res.choices.push_back({std::to_string(wh.first) + " x " + std::to_string(wh.second), wh.first * 10000 + wh.second});
    defs_.push_back(res);

    defs_.push_back({"Shadows", "Draw shadows under characters.", "Graphics", "shadowFlag", onoff(), false, false, true});
    defs_.push_back({"Blood", "Show blood in combat.", "Graphics", "bloodFlag", onoff(), false, false, true});
    defs_.push_back({"Strong language", "Allow strong language in dialogue.", "Graphics", "foulLanguageFlag", onoff(), false, false, true});
    defs_.push_back({"Always run", "Run by default instead of walking.", "Control", "alwaysRun", onoff(), false, false, true});
    defs_.push_back({"Auto-use health", "Use a health item automatically when health runs low.", "Control", "autoUseHealth", onoff(), false, false, true});

    SettingDef mv;
    mv.label = "Movie volume";
    mv.help = "Volume of cut-scene movies, as a percentage.";
    mv.section = "Sound";
    mv.key = "MovieVolume";
    for (int v = 0; v <= 100; v += 10) mv.choices.push_back({std::to_string(v) + "%", v});
    defs_.push_back(mv);

    defs_.push_back({"Menu font", "Size of the font used by the pause menu and pick lists.", "Graphics", "menuFont",
                     {{"Large", 0}, {"Small", 1}}, false, false, true});

    SettingDef as;
    as.label = "Start game automatically";
    as.help = "Skip this launcher and start the game as soon as its files are found. Hold B while the countdown runs to stay here.";
    as.section = "Launcher";
    as.key = "autoStart";
    as.choices = onoff();
    as.launcher_only = true;
    defs_.push_back(as);

    reload();
}

std::string Settings::game_ini_path() const { return find_ci(root_, "system\\nocturne.ini"); }
std::string Settings::launcher_cfg_path() const { return resolve_for_write(root_, "launcher.cfg"); }

void Settings::reload() {
    std::string p = game_ini_path();
    if (p.empty()) game_ini_ = IniFile();
    else game_ini_.load(p);
    launcher_cfg_.load(launcher_cfg_path());
}

int Settings::current_value(size_t i, bool *present) const {
    const SettingDef &d = defs_[i];
    if (d.resolution) {
        bool ok = game_ini_.has("Graphics", "gamePIXX") && game_ini_.has("Graphics", "gamePIXY");
        if (present) *present = ok;
        return game_ini_.get_int("Graphics", "gamePIXX", 640) * 10000 + game_ini_.get_int("Graphics", "gamePIXY", 480);
    }
    const IniFile &f = d.launcher_only ? launcher_cfg_ : game_ini_;
    if (present) *present = f.has(d.section, d.key);
    return f.get_int(d.section, d.key, 0);
}

int Settings::current_index(size_t i) const {
    bool present = false;
    int v = current_value(i, &present);
    if (!present) return defs_[i].launcher_only ? 0 : -1;   // launcher options start at their first choice (Off)
    const auto &ch = defs_[i].choices;
    for (size_t k = 0; k < ch.size(); ++k)
        if (ch[k].value == v) return (int)k;
    return -1;
}

std::string Settings::value_label(size_t i) const {
    int idx = current_index(i);
    if (idx >= 0) return defs_[i].choices[(size_t)idx].label;
    bool present = false;
    int v = current_value(i, &present);
    if (!present) return defs_[i].launcher_only ? "Off" : "Game default";
    if (defs_[i].resolution) return std::to_string(v / 10000) + " x " + std::to_string(v % 10000);
    return std::to_string(v);
}

bool Settings::adjust(size_t i, int step) {
    if (!editable(i)) return false;
    const SettingDef &d = defs_[i];
    int n = (int)d.choices.size();
    int idx = current_index(i);
    if (idx < 0) idx = step > 0 ? -1 : 0;
    idx = ((idx + step) % n + n) % n;
    int v = d.choices[(size_t)idx].value;
    if (d.resolution) {
        game_ini_.set_int("Graphics", "gamePIXX", v / 10000);
        game_ini_.set_int("Graphics", "gamePIXY", v % 10000);
        return game_ini_.save(game_ini_path());
    }
    if (d.launcher_only) {
        launcher_cfg_.set_int(d.section, d.key, v);
        return launcher_cfg_.save(launcher_cfg_path());
    }
    game_ini_.set_int(d.section, d.key, v);
    return game_ini_.save(game_ini_path());
}

bool Settings::autostart() const { return launcher_cfg_.get_int("Launcher", "autoStart", 0) != 0; }

}  // namespace nl
