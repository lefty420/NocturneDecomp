// Test harness: renders each launcher screen to a BMP with SDL's software renderer.
// Not part of the app. Usage: launcher_shot <dataRoot> <outDir> [importSource]
#include "launcher.h"

#include <cstdio>
#include <filesystem>

using namespace launcher;

static void save(SDL_Renderer *r, const std::string &path) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 1920, 1080, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    SDL_SaveBMP(s, path.c_str());
    SDL_FreeSurface(s);
}

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    std::string root = argv[1], out = argv[2];
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *w = SDL_CreateWindow("shot", 0, 0, 1920, 1080, 0);
    SDL_Renderer *r = SDL_CreateRenderer(w, -1, SDL_RENDERER_SOFTWARE);
    if (!r) { std::fprintf(stderr, "no renderer: %s\n", SDL_GetError()); return 1; }
    std::filesystem::create_directories(out);

    std::vector<DirImporter::Source> src;
    if (argc > 3) src.push_back({{"folder", "USB drive or shared folder", "Looks for your Nocturne folder in " + std::string(argv[3])}, argv[3]});
    src.push_back({{"picker", "Choose a folder...", "Opens the system file picker"}, "/nonexistent"});
    App app(r, root, std::make_unique<DirImporter>(root, src), "LocalState (this app's private folder)");
    if (!app.ok()) { std::fprintf(stderr, "init failed\n"); return 1; }

    auto frame = [&](const char *name) {
        for (int i = 0; i < 12; ++i) app.update(0.05f);
        app.draw();
        save(r, out + "/" + name + ".bmp");
    };
    SDL_RenderSetLogicalSize(r, 1920, 1080);
    frame("01_home");
    app.press(Button::A); frame("03_files");
    app.press(Button::X); frame("04_import_sheet");
    app.press(Button::Down);                                  // second method
    app.press(Button::Up); app.press(Button::A);              // start the import from the first method
    for (int i = 0; i < 4; ++i) { SDL_Delay(30); app.update(0.05f); }
    frame("05_import_progress_or_done");
    for (int i = 0; i < 40; ++i) { SDL_Delay(50); app.update(0.05f); }
    frame("06_import_result");
    app.press(Button::A);                                     // dismiss, rescans
    frame("07_files_after_import");
    app.press(Button::B);                                     // back to home
    app.press(Button::Down); app.press(Button::A);            // settings
    for (int i = 0; i < 3; ++i) app.press(Button::Down);
    app.press(Button::Right); frame("08_settings_music");
    for (int i = 0; i < 6; ++i) app.press(Button::Down);      // wrap to the bottom row
    frame("09_settings_bottom");
    app.press(Button::B); app.press(Button::Down); app.press(Button::A); frame("10_about");
    app.press(Button::B); frame("11_home_ready");
    SDL_Quit();
    return 0;
}
