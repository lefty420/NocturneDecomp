// =============================================================================
// LAUNCHER SETTINGS
// =============================================================================
//
// Stored as plain key=value lines in "launcher.ini" in the app's local folder.
// The game side reads them through the C functions at the bottom, falling back
// to its compile-time defaults when a key has never been written.
#pragma once
#include <string>

namespace launcher {

struct Settings {
    int resolution = 2;      // index into resolutions()
    int fit = 1;             // 0 = fill the screen, 1 = keep 4:3, 2 = whole-number scale
    bool introMovie = true;
    int musicVolume = 100;   // 0..100
    int movieVolume = 100;   // 0..100
    int lookSpeed = 5;       // 1..10 (5 = the game's default)
    int deadzone = 20;       // percent of stick travel ignored
    bool invertLook = false;
    bool showFps = false;
};

struct Resolution { int w, h; };
int resolution_count();
Resolution resolution_at(int index);

const char *fit_name(int fit);

// Loads from `<root>/launcher.ini`; missing file or keys keep the defaults.
Settings load_settings(const std::string &root);
bool save_settings(const std::string &root, const Settings &s);

}  // namespace launcher

#ifdef __cplusplus
extern "C" {
#endif
// For the game's shims: the value of a launcher setting, or `fallback` when the
// launcher has never written one. `root` is the app's data folder (with the
// trailing slash from nocturne_data_root()).
int nocturne_launcher_setting_int(const char *root, const char *key, int fallback);
#ifdef __cplusplus
}
#endif
