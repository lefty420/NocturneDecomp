// Opens the launcher in a normal window for hand-testing on a desktop (not built in CI).
//   demo <fonts_dir> <data_dir>
#include "../src/launcher_main.h"

#include <cstdio>

int main(int argc, char **argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: demo <fonts_dir> <data_dir>\n"); return 2; }
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    SDL_Window *w = SDL_CreateWindow("Nocturne launcher", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                                     SDL_WINDOW_RESIZABLE);
    SDL_Renderer *r = SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    const nl::LaunchResult res = nl::run_launcher(w, r, argv[1], argv[2]);
    std::printf("result: %s\n", res == nl::LaunchResult::Play ? "play" : res == nl::LaunchResult::Quit ? "quit" : "error");
    SDL_Quit();
    return 0;
}
