// The launcher's Settings screen: a small table of options, each stored either in
// the game's own system\nocturne.ini (so the game reads it) or in launcher.cfg.
#pragma once
#include <string>
#include <vector>

#include "ini_file.h"

namespace nl {

struct Choice {
    std::string label;
    int value;
};

struct SettingDef {
    std::string label;
    std::string help;
    std::string section, key;     // ini location (ignored for resolution, which uses two keys)
    std::vector<Choice> choices;
    bool launcher_only = false;   // stored in launcher.cfg rather than the game's ini
    bool resolution = false;      // gamePIXX + gamePIXY
    bool applies_next_launch = false;
};

class Settings {
public:
    explicit Settings(std::string data_root);
    void reload();

    const std::vector<SettingDef> &defs() const { return defs_; }
    bool game_ini_present() const { return game_ini_.loaded(); }
    bool editable(size_t i) const { return defs_[i].launcher_only || game_ini_.loaded(); }

    // Index into defs()[i].choices, or -1 when the key is absent (game default) or custom.
    int current_index(size_t i) const;
    std::string value_label(size_t i) const;
    // Steps the choice by +1/-1 (wrapping), writes the file. False when it could not save.
    bool adjust(size_t i, int step);

    bool autostart() const;

private:
    int current_value(size_t i, bool *present) const;
    std::string game_ini_path() const;
    std::string launcher_cfg_path() const;

    std::string root_;
    std::vector<SettingDef> defs_;
    IniFile game_ini_, launcher_cfg_;
};

}  // namespace nl
