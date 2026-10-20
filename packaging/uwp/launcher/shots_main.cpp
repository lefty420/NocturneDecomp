// Headless screenshot tool: renders each launcher screen with a software renderer and
// saves BMPs, so the UI can be checked without a display. Also exercises the import path.
//   shots <assets_dir> <data_root> <out_dir> [import_source_dir]
#include <cstdio>
#include <string>

#include "importer.h"
#include "ui.h"

using namespace nl;

static void shot(SDL_Surface *surf, SDL_Renderer *r, Gfx &gfx, Launcher &ui, const std::string &out, const char *name) {
    ui.tick(400);   // finish the fade
    SDL_SetRenderDrawColor(r, 5, 6, 12, 255);
    SDL_RenderClear(r);
    ui.render();
    SDL_RenderPresent(r);
    std::string path = out + "/" + name + ".bmp";
    SDL_SaveBMP(surf, path.c_str());
    std::printf("wrote %s\n", path.c_str());
}

int main(int argc, char **argv) {
    if (argc < 4) return 2;
    std::string assets = argv[1], root = argv[2], out = argv[3];
    std::string import_src = argc > 4 ? argv[4] : "";
    SDL_Init(0);
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, 1920, 1080, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = SDL_CreateSoftwareRenderer(surf);
    Gfx gfx;
    if (!gfx.init(r, assets + "/fonts/")) return 3;
    auto svc = make_folder_import_service(root, import_src);
    LauncherConfig cfg;
    cfg.data_root = root;
    cfg.version = "0.1";
    cfg.build_note = "test build";
    Launcher ui(gfx, *svc, cfg);
    PadState pad;
    pad.connected = true;
    pad.name = "Xbox Wireless Controller";
    pad.a = true;
    pad.lx = 0.6f;
    pad.ly = -0.3f;
    pad.rt = 0.7f;
    pad.left = true;
    ui.set_pad(pad);

    shot(surf, r, gfx, ui, out, "home");
    ui.debug_goto(Screen::Files);
    shot(surf, r, gfx, ui, out, "files");
    ui.debug_open_import_help();
    shot(surf, r, gfx, ui, out, "help");
    ui.handle(Action::Back);
    ui.debug_goto(Screen::Settings);
    ui.debug_focus(2);
    shot(surf, r, gfx, ui, out, "settings");
    ui.debug_goto(Screen::About);
    shot(surf, r, gfx, ui, out, "about");
    ui.debug_goto(Screen::Home);
    ui.handle(Action::Accept);   // Play with missing files -> modal, or starts if ready
    shot(surf, r, gfx, ui, out, ui.result() == Result::Play ? "play" : "notready");

    if (!import_src.empty()) {
        ui.debug_goto(Screen::Files);
        ui.debug_focus(0);
        ui.handle(Action::Accept);   // start import
        for (int i = 0; i < 200 && ui.in_modal(); ++i) {
            SDL_Delay(20);
            ui.tick(20);
            if (svc->progress().state == ImportState::Done || svc->progress().state == ImportState::Failed) break;
        }
        ui.tick(20);
        shot(surf, r, gfx, ui, out, "import_result");
    }
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(surf);
    SDL_Quit();
    return 0;
}
