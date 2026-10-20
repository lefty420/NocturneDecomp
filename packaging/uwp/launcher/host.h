// Runs the launcher in an SDL window: input (controller, keyboard), timing, drawing.
#pragma once
#include <string>

#include "importer.h"
#include "ui.h"

namespace nl {

// Shows the launcher until the user starts the game (Result::Play) or closes the
// window (Result::Quit). `assets_dir` holds fonts/. Needs an existing window and
// renderer; the caller keeps ownership of both.
Result run_launcher(SDL_Window *window, SDL_Renderer *renderer, ImportService &import, const LauncherConfig &cfg,
                    const std::string &assets_dir);

}  // namespace nl
