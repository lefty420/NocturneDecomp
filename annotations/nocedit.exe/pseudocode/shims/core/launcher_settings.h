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

    // Display. Added with the Xbox port; the shipped game has no equivalent.
    int   upscaler;         // 0 = plain scaling (default), 1 = FSR-style edge-aware upscale + sharpen
    float fsr_sharpness;    // 0.0 .. 1.0   how hard the sharpening pass works (FSR only)
    int   output_height;    // 1080 (default) or 2160: the size of the picture the console is asked for
    int   screen_shape;     // on a screen wider than the game's 4:3: 0 = bars at the sides (default),
                            // 1 = widescreen (smart stretch: centre barely changed, edges stretched), 2 = stretch to fill,
                            // 3 = zoom to fill (crops top and bottom)
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
