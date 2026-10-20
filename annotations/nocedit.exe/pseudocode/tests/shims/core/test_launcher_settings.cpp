// =============================================================================
// LAUNCHER SETTINGS — what a launcher.ini can and cannot do
// =============================================================================
//
// The parser is tested on strings. What is pinned: defaults survive an empty or
// junk file, recognised keys apply, out-of-range numbers are clamped rather than
// trusted, and a bad line never disturbs a good one.

#include "nocturne_test.h"

#include "core/launcher_settings.h"

namespace {

NocturneLauncherSettings defaults() {
    NocturneLauncherSettings s;
    nocturne_launcher_settings_defaults(&s);
    return s;
}

}  // namespace

NOCTURNE_TEST(launcher_settings_empty_text_changes_nothing) {
    NocturneLauncherSettings s = defaults();
    const NocturneLauncherSettings before = s;
    CHECK(nocturne_launcher_settings_parse("", &s) == 0);
    CHECK(nocturne_launcher_settings_parse("nonsense\n# c\n[x]\n=\n", &s) == 0);
    CHECK(s.stick_deadzone == before.stick_deadzone);
    CHECK(s.invert_look == before.invert_look);
}

NOCTURNE_TEST(launcher_settings_applies_known_keys) {
    NocturneLauncherSettings s = defaults();
    const int n = nocturne_launcher_settings_parse(
        "stick_deadzone = 0.30\nTurn_Rate=0.5 ; trailing text is not a comment\nlook_rate=0.1\ninvert_look=yes\n", &s);
    CHECK(n == 3);  // the turn_rate value has trailing text, so it is rejected
    CHECK(s.stick_deadzone == 0.30f);
    CHECK(s.look_rate == 0.1f);
    CHECK(s.invert_look == 1);
}

NOCTURNE_TEST(launcher_settings_clamps_out_of_range) {
    NocturneLauncherSettings s = defaults();
    nocturne_launcher_settings_parse("stick_deadzone=0.9\nturn_rate=0\nlook_rate=50\n", &s);
    CHECK(s.stick_deadzone == 0.40f);
    CHECK(s.turn_rate == 0.05f);
    CHECK(s.look_rate == 1.00f);
}

NOCTURNE_TEST(launcher_settings_bad_line_does_not_disturb_good_ones) {
    NocturneLauncherSettings s = defaults();
    nocturne_launcher_settings_parse("stick_deadzone=abc\ninvert_look=maybe\nlook_rate=0.4\n", &s);
    CHECK(s.stick_deadzone == defaults().stick_deadzone);
    CHECK(s.invert_look == defaults().invert_look);
    CHECK(s.look_rate == 0.4f);
}

NOCTURNE_TEST_MAIN()
