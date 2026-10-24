// The one function the game's startup calls before it creates its own window: shows the
// launcher (settings, game files) and reports whether the player chose Play.
#include "launcher_main.h"
#include "game_files.h"

#include "core/data_root.h"

#include <SDL.h>
#include <windows.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>

#include <filesystem>
#include <string>

// Breadcrumbs and a crash note in <LocalState>\startup.log: a UWP app that dies at start-up shows
// nothing. Read it through Device Portal (File explorer > LocalAppData > the app > LocalState).
extern "C" void nocturne_uwp_claim_back_button(void);   // imports_platform_uwp.cpp (C++/CX)

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


// ---- Diagnostic sampler: where does the game thread spend its time? -------------------------------------------
// A helper thread looks at the game thread's instruction pointer ~every 2 ms and, every 5 s, writes to stderr.log
// how the samples split between modules (our code, ANGLE, Direct3D, ...) and the busiest spots in our own code
// (as offsets, to be matched against the link map). Nothing is allocated while the game thread is paused.
#include <algorithm>
#include <map>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {
void profiler_main(HANDLE target) {
    typedef DWORD(WINAPI * SuspendFn)(HANDLE);
    typedef BOOL(WINAPI * GetCtxFn)(HANDLE, LPCONTEXT);
    typedef DWORD(WINAPI * ModNameFn)(HMODULE, LPWSTR, DWORD);
    SuspendFn suspend = nullptr, resume = nullptr;
    GetCtxFn get_ctx = nullptr;
    ModNameFn mod_name = nullptr;
    for (const wchar_t *m : {L"kernelbase.dll", L"kernel32.dll"}) {
        HMODULE h = GetModuleHandleW(m);
        if (!h) continue;
        if (!suspend) suspend = reinterpret_cast<SuspendFn>(reinterpret_cast<void *>(GetProcAddress(h, "SuspendThread")));
        if (!resume) resume = reinterpret_cast<SuspendFn>(reinterpret_cast<void *>(GetProcAddress(h, "ResumeThread")));
        if (!get_ctx) get_ctx = reinterpret_cast<GetCtxFn>(reinterpret_cast<void *>(GetProcAddress(h, "GetThreadContext")));
        if (!mod_name) mod_name = reinterpret_cast<ModNameFn>(reinterpret_cast<void *>(GetProcAddress(h, "GetModuleFileNameW")));
    }
    if (!suspend || !resume || !get_ctx) { nocturne_boot_log("profiler: thread control not available"); return; }
    const uintptr_t exe_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    std::unordered_map<uintptr_t, std::string> names;   // module base -> file name
    std::map<std::string, int> by_module;
    std::unordered_map<uint32_t, int> by_rva;
    int total = 0, failed = 0;
    ULONGLONG window = GetTickCount64();
    for (;;) {
        Sleep(2);
        CONTEXT ctx;
        std::memset(&ctx, 0, sizeof(ctx));
        ctx.ContextFlags = 0x100001;   // CONTEXT_CONTROL (x64)
        uintptr_t rip = 0;
        if (suspend(target) != (DWORD)-1) {
            if (get_ctx(target, &ctx)) rip = static_cast<uintptr_t>(ctx.Rip);
            resume(target);
        }
        if (rip == 0) { ++failed; continue; }
        ++total;
        HMODULE hm = nullptr;
        std::string name = "?";
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(rip), &hm) && hm) {
            auto it = names.find(reinterpret_cast<uintptr_t>(hm));
            if (it == names.end()) {
                wchar_t buf[260] = {0};
                if (mod_name) mod_name(hm, buf, 260);
                std::string n;
                for (wchar_t c : std::wstring(buf)) n.push_back(c < 128 ? static_cast<char>(c) : '?');
                const size_t slash = n.find_last_of("\\/");
                if (slash != std::string::npos) n = n.substr(slash + 1);
                if (n.empty()) n = "module";
                it = names.emplace(reinterpret_cast<uintptr_t>(hm), n).first;
            }
            name = it->second;
            if (reinterpret_cast<uintptr_t>(hm) == exe_base) ++by_rva[static_cast<uint32_t>(rip - exe_base)];
        }
        ++by_module[name];
        if (GetTickCount64() - window >= 5000 && total > 0) {
            std::vector<std::pair<int, std::string>> mods;
            for (auto &kv : by_module) mods.push_back({kv.second, kv.first});
            std::sort(mods.rbegin(), mods.rend());
            std::string line = "prof:";
            for (size_t i = 0; i < mods.size() && i < 6; ++i)
                line += " " + mods[i].second + "=" + std::to_string(100 * mods[i].first / total) + "%";
            std::fprintf(stderr, "%s (n=%d)\n", line.c_str(), total);
            std::vector<std::pair<int, uint32_t>> top;
            for (auto &kv : by_rva) top.push_back({kv.second, kv.first});
            std::sort(top.rbegin(), top.rend());
            std::string hot = "prof-rva:";
            char tmp[48];
            for (size_t i = 0; i < top.size() && i < 80; ++i) {
                std::snprintf(tmp, sizeof(tmp), " %x:%d", top[i].second, top[i].first);
                hot += tmp;
            }
            std::fprintf(stderr, "%s\n", hot.c_str());
            by_module.clear(); by_rva.clear(); total = 0; failed = 0;
            window = GetTickCount64();
        }
    }
}

void start_profiler() {
    HANDLE self = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &self, 0, FALSE,
                         DUPLICATE_SAME_ACCESS)) {
        nocturne_boot_log("profiler: could not get the game thread handle");
        return;
    }
    try {
        std::thread(profiler_main, self).detach();
    } catch (...) {
        nocturne_boot_log("profiler: could not start");
    }
}
}  // namespace

// AddVectoredExceptionHandler is not declared for UWP, so look it up at run time.
extern "C" void nocturne_boot_init(void) {
    // On Xbox the B button is the system "back" action, and an app that does not say it handles it is
    // closed by the console. Tell SDL to claim it (it then arrives as an ordinary key event). Must be
    // set before SDL creates the window.
    SDL_SetHint("SDL_WINRT_HANDLE_BACK_BUTTON", "1");
    nocturne_uwp_claim_back_button();
    // The game reports problems on stderr, which a UWP app has nowhere to show: keep it in a file.
    const std::string errpath = nocturne_data_path("stderr.log");
    if (std::freopen(errpath.c_str(), "a", stderr)) std::setvbuf(stderr, nullptr, _IONBF, 0);
    start_profiler();

    typedef PVOID(WINAPI * AddVehFn)(ULONG, PVECTORED_EXCEPTION_HANDLER);
    for (const wchar_t *mod : {L"kernelbase.dll", L"kernel32.dll"}) {
        HMODULE h = GetModuleHandleW(mod);
        AddVehFn fn = h ? reinterpret_cast<AddVehFn>(reinterpret_cast<void *>(GetProcAddress(h, "AddVectoredExceptionHandler"))) : nullptr;
        if (fn) { fn(1, boot_crash_handler); return; }
    }
    nocturne_boot_log("note: no exception handler available");
}

extern "C" void nocturne_uwp_restart_app(void);   // imports_platform_uwp.cpp (C++/CX)

// View + Menu + L3 in the game: the game owns the window and the graphics context, so the launcher cannot be
// drawn over it. Leave a note, then restart the app (or just close it); the next start shows the launcher.
extern "C" void nocturne_uwp_return_to_launcher(void) {
    static bool once = false;
    if (once) return;
    once = true;
    nocturne_boot_log("chord: returning to the launcher");
    if (FILE *f = std::fopen(nocturne_data_path("show_launcher.flag").c_str(), "wb")) std::fclose(f);
    nocturne_uwp_restart_app();   // returns only if the console refused the restart
    std::exit(0);
}

extern "C" int nocturne_uwp_run_launcher(void) {
    // After the player has pressed Play once with the game files in place, later starts go straight into
    // the game. View + Menu + L3 in the game leaves show_launcher.flag and restarts the app, which brings the
    // launcher back for that one start (and Play there turns the shortcut on again).
    const std::string skip_flag = nocturne_data_path("skip_launcher.flag");
    const std::string show_flag = nocturne_data_path("show_launcher.flag");
    bool want_launcher = false;
    if (FILE *f = std::fopen(show_flag.c_str(), "rb")) {
        std::fclose(f);
        std::remove(show_flag.c_str());
        want_launcher = true;
    }
    if (!want_launcher) {
        if (FILE *f = std::fopen(skip_flag.c_str(), "rb")) {
            std::fclose(f);
            if (nl::scan_game_files(nl::effective_data_dir(std::filesystem::path(nocturne_data_root()))).ready) {
                nocturne_boot_log("launcher: skipped (game files ready, Play chosen before)");
                return 1;
            }
        }
    }
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
    if (res == nl::LaunchResult::Play) {
        if (FILE *f = std::fopen(skip_flag.c_str(), "wb")) std::fclose(f);
    }
    nocturne_boot_log("launcher: UI returned");
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(w);
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    return res == nl::LaunchResult::Play ? 1 : 0;
}
