// The one function the game's startup calls before it creates its own window: shows the
// launcher (settings, game files) and reports whether the player chose Play.
#include "launcher_main.h"

#include "core/data_root.h"

#include <SDL.h>

#include <filesystem>
#include <string>

extern "C" int nocturne_uwp_run_launcher(void) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) return 1;  // no launcher: just start the game
    SDL_Window *w = SDL_CreateWindow("Nocturne", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                                     SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer *r = w ? SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (!w || !r) {
        if (r) SDL_DestroyRenderer(r);
        if (w) SDL_DestroyWindow(w);
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
        return 1;
    }
    const char *base = SDL_GetBasePath();
    const std::filesystem::path fonts = std::filesystem::path(base ? base : "") / "fonts";
    if (base) SDL_free((void *)base);
    const nl::LaunchResult res = nl::run_launcher(w, r, fonts, std::filesystem::path(nocturne_data_root()));
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(w);
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    return res == nl::LaunchResult::Play ? 1 : 0;
}
