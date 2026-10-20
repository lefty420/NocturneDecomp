#pragma once

// =============================================================================
// LAUNCHER SETTINGS — controller tuning the player can change at run time
// =============================================================================
//
// An addition, not a reconstruction. The controller feel (deadzone, turn and
// look speed, inverted look) used to be fixed when the game was built. On a
// console there is no rebuilding, so the launcher app writes these into
// "launcher.ini" in the data root and the game reads them here.
//
// The file is plain "key=value" lines; '#' and ';' start comments; unknown keys
// and unparseable values are ignored, and numbers are clamped to a sane range, so
// a hand-edited file cannot make the controller unusable. A missing file means
// the build-time defaults from shim_config_input.h.

struct NocturneLauncherSettings {
    float stick_deadzone;   // 0.05 .. 0.40   radial deadzone for both sticks
    float turn_rate;        // 0.05 .. 1.00   left/right look speed
    float look_rate;        // 0.05 .. 1.00   up/down look speed
    int   invert_look;      // 0 or 1
};

#ifdef __cplusplus
extern "C" {
#endif

// The build-time defaults.
void nocturne_launcher_settings_defaults(NocturneLauncherSettings *out);

// Apply the lines in `text` on top of whatever `inout` already holds. Returns how
// many keys were recognised and applied.
int nocturne_launcher_settings_parse(const char *text, NocturneLauncherSettings *inout);

// The settings in effect: defaults, then "launcher.ini" from the data root if it
// exists. Loaded once, on first use.
const NocturneLauncherSettings *nocturne_launcher_settings(void);

#ifdef __cplusplus
}
#endif
