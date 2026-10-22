// The one function the game's startup calls before it creates its own window: shows the
// launcher (settings, game files) and reports whether the player chose Play.
#include "launcher_main.h"

#include "core/data_root.h"

#include <SDL.h>
#include <windows.h>

#include <cstdio>

#include <filesystem>
#include <string>

// Breadcrumbs and a crash note in <LocalState>\startup.log: a UWP app that dies at start-up shows
// nothing. Read it through Device Portal (File explorer > LocalAppData > the app > LocalState).
extern "C" void nocturne_boot_log(const char *line) {
    const std::string path = nocturne_data_path("startup.log");
    FILE *f = std::fopen(path.c_str(), "a");
    if (!f) return;
    std::fprintf(f, "%s\n", line);
    std::fclose(f);
}

static LONG CALLBACK boot_crash_handler(PEXCEPTION_POINTERS ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    static volatile LONG busy = 0;
    if ((code & 0xF0000000u) != 0xC0000000u || InterlockedExchange(&busy, 1)) return EXCEPTION_CONTINUE_SEARCH;
    char buf[200];
    const char *base = reinterpret_cast<const char *>(GetModuleHandleW(nullptr));
    std::snprintf(buf, sizeof buf, "EXCEPTION %08lx at exe+0x%llx (addr %p)", static_cast<unsigned long>(code),
                  static_cast<unsigned long long>(static_cast<const char *>(ep->ExceptionRecord->ExceptionAddress) - base),
                  ep->ExceptionRecord->ExceptionAddress);
    nocturne_boot_log(buf);
    InterlockedExchange(&busy, 0);
    return EXCEPTION_CONTINUE_SEARCH;
}

// AddVectoredExceptionHandler is not declared for UWP, so look it up at run time.
extern "C" void nocturne_boot_init(void) {
    // The game reports problems on stderr, which a UWP app has nowhere to show: keep it in a file.
    const std::string errpath = nocturne_data_path("stderr.log");
    if (std::freopen(errpath.c_str(), "a", stderr)) std::setvbuf(stderr, nullptr, _IONBF, 0);

    typedef PVOID(WINAPI * AddVehFn)(ULONG, PVECTORED_EXCEPTION_HANDLER);
    for (const wchar_t *mod : {L"kernelbase.dll", L"kernel32.dll"}) {
        HMODULE h = GetModuleHandleW(mod);
        AddVehFn fn = h ? reinterpret_cast<AddVehFn>(reinterpret_cast<void *>(GetProcAddress(h, "AddVectoredExceptionHandler"))) : nullptr;
        if (fn) { fn(1, boot_crash_handler); return; }
    }
    nocturne_boot_log("note: no exception handler available");
}

extern "C" int nocturne_uwp_run_launcher(void) {
    nocturne_boot_log("launcher: SDL_Init");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) return 1;  // no launcher: just start the game
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) { std::string m = std::string("launcher: SDL_Init failed: ") + SDL_GetError(); nocturne_boot_log(m.c_str()); }
    SDL_Window *w = SDL_CreateWindow("Nocturne", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                                     SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer *r = w ? SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (!w || !r) {
        { std::string m = std::string("launcher: window/renderer failed: ") + SDL_GetError(); nocturne_boot_log(m.c_str()); }
        if (r) SDL_DestroyRenderer(r);
        if (w) SDL_DestroyWindow(w);
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
        return 1;
    }
    nocturne_boot_log("launcher: window and renderer ready, showing UI");
    const char *base = SDL_GetBasePath();
    const std::filesystem::path fonts = std::filesystem::path(base ? base : "") / "fonts";
    if (base) SDL_free((void *)base);
    const nl::LaunchResult res = nl::run_launcher(w, r, fonts, std::filesystem::path(nocturne_data_root()));
    nocturne_boot_log("launcher: UI returned");
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(w);
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    return res == nl::LaunchResult::Play ? 1 : 0;
}
