// =============================================================================
// LAUNCHER — the app's front screen
// =============================================================================
//
// Runs before the game starts: shows whether the game files are in place, lets
// the player add them, change settings, and press Play. Entirely controller
// driven (the console has no mouse); the keyboard works too for testing.
#pragma once
#include "gamefiles.h"
#include "importer.h"
#include "settings.h"

#include <SDL.h>
#include <memory>
#include <string>

namespace launcher {

enum class Button { Up, Down, Left, Right, A, B, X, Y, Start };

enum class Result { Play, Quit };

struct Fonts;   // opaque

class App {
public:
    // `dataRoot` is the folder holding the game files and launcher.ini.
    // `importer` may be null (the Import button then explains it is unavailable).
    App(SDL_Renderer *renderer, std::string dataRoot, std::unique_ptr<Importer> importer, std::string localFolderLabel);
    ~App();

    bool ok() const { return ok_; }
    // Feed one logical button press (already repeated by the caller if held).
    void press(Button b);
    // Advance animation/state by dt seconds.
    void update(float dt);
    void draw();

    bool wantsPlay() const { return wantPlay_; }
    bool wantsQuit() const { return wantQuit_; }
    const Settings &settings() const { return settings_; }

    // Test hooks.
    enum class Screen { Home, Files, Settings, About };
    void gotoScreen(Screen s) { setScreen(s); }
    void openImportSheet();
    void rescan();
    Screen screen() const { return screen_; }

    struct Impl;

private:
    std::unique_ptr<Impl> d_;
    bool ok_ = false;
    bool wantPlay_ = false, wantQuit_ = false;
    Screen screen_ = Screen::Home;
    Settings settings_;
    void setScreen(Screen s);
};

// Convenience loop for the real app: owns input, repeat and timing.
Result run(SDL_Window *window, SDL_Renderer *renderer, App &app);

}  // namespace launcher
