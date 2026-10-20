#pragma once
// Runs the launcher on an existing SDL window and renderer until the player chooses Play
// or closes the app. The UWP shell calls this before starting the game.

#include <SDL.h>

#include <filesystem>

namespace nl {

enum class LaunchResult { Play, Quit, Error };

LaunchResult run_launcher(SDL_Window *window, SDL_Renderer *renderer, const std::filesystem::path &fonts_dir,
                          const std::filesystem::path &data_root);

}  // namespace nl
