// The one function the game's startup calls before it creates its own window: shows the
// launcher (settings, game files) and reports whether the player chose Play.
#include "launcher_main.h"
#include "game_files.h"
#include "settings_store.h"
#include "default_config.h"

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
extern "C" int nocturne_uwp_set_output_height(int height);   // imports_platform_uwp.cpp (C++/CX)

// The output size this run asked the console for at start-up (1080 or 2160), so Play can tell when the player
// has changed it since and the app needs to start again for it to apply.
static int g_boot_output_height = 1080;

extern "C" void nocturne_boot_log(const char *line) {
    const std::string path = nocturne_data_path("startup.log");
    FILE *f = std::fopen(path.c_str(), "a");
    if (!f) return;
    std::fprintf(f, "%s\n", line);
    std::fclose(f);
}

// Names the module an address falls in, as "name+0xRVA" (or "base+0xRVA" when the name is not available).
static void describe_addr(const void *addr, char *out, size_t cap) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!addr || !VirtualQuery(addr, &mbi, sizeof mbi) || !mbi.AllocationBase) {
        std::snprintf(out, cap, "%p", addr);
        return;
    }
    typedef DWORD(WINAPI * GetModNameFn)(HMODULE, LPWSTR, DWORD);
    static GetModNameFn get_name = [] {
        for (const wchar_t *mod : {L"kernelbase.dll", L"kernel32.dll"}) {
            HMODULE h = GetModuleHandleW(mod);
            if (h) {
                FARPROC f = GetProcAddress(h, "GetModuleFileNameW");
                if (f) return reinterpret_cast<GetModNameFn>(reinterpret_cast<void *>(f));
            }
        }
        return static_cast<GetModNameFn>(nullptr);
    }();
    wchar_t wname[260] = {0};
    char name[260] = {0};
    if (get_name && get_name(static_cast<HMODULE>(mbi.AllocationBase), wname, 260) > 0) {
        const wchar_t *slash = wname;
        for (const wchar_t *c = wname; *c; ++c)
            if (*c == L'\\' || *c == L'/') slash = c + 1;
        size_t n = 0;
        while (slash[n] && n < sizeof name - 1) { name[n] = static_cast<char>(slash[n] < 128 ? slash[n] : '?'); ++n; }
    }
    std::snprintf(out, cap, "%s@%p+0x%llx", name[0] ? name : "module", mbi.AllocationBase,
                  static_cast<unsigned long long>(static_cast<const char *>(addr) - static_cast<const char *>(mbi.AllocationBase)));
}

static DWORD g_main_tid = 0;

// ---- Shipped defaults (defaults\launcher.ini, defaults\nocturne.ini in the package) -------------------------
// See default_config.h. The launcher's own file can be seeded straight away; the game's System\nocturne.ini only
// exists once the game files are in place, so that one is tried at launch and again after the launcher closes.
static std::filesystem::path defaults_dir() {
    char *base = SDL_GetBasePath();
    std::filesystem::path d = std::filesystem::path(base ? base : "") / "defaults";
    if (base) SDL_free(base);
    return d;
}

static void note_defaults(const char *what, nl::ApplyResult r) {
    if (r == nl::ApplyResult::Applied) { std::string m = std::string("defaults: ") + what + " applied"; nocturne_boot_log(m.c_str()); }
    else if (r == nl::ApplyResult::Failed) { std::string m = std::string("defaults: ") + what + " could not be written"; nocturne_boot_log(m.c_str()); }
}

static void seed_launcher_defaults() {
    const std::filesystem::path root(nocturne_data_root());
    note_defaults("launcher.ini", nl::apply_preset(defaults_dir() / "launcher.ini", root / "launcher.ini", true, root / "defaults_launcher.version"));
}

static void seed_game_defaults() {
    const std::filesystem::path root(nocturne_data_root());
    const std::filesystem::path game = nl::effective_data_dir(root);
    const std::filesystem::path sys = nl::find_ci(game, "System");
    if (sys.empty()) return;
    const std::filesystem::path ini = nl::find_ci(sys, "nocturne.ini");
    if (ini.empty()) return;
    note_defaults("nocturne.ini", nl::apply_preset(defaults_dir() / "nocturne.ini", ini, false, root / "defaults_nocturne.version"));
}

static void log_crash(const char *tag, PEXCEPTION_POINTERS ep) {
    char where[400], line[700];
    describe_addr(ep->ExceptionRecord->ExceptionAddress, where, sizeof where);
    const ULONG_PTR *info = ep->ExceptionRecord->ExceptionInformation;
    const char *kind = "";
    if (ep->ExceptionRecord->ExceptionCode == 0xC0000005 && ep->ExceptionRecord->NumberParameters >= 2)
        kind = info[0] == 0 ? " read" : (info[0] == 1 ? " write" : " execute");
    std::snprintf(line, sizeof line, "%s %08lx%s of %p at %s, thread %lu%s", tag,
                  static_cast<unsigned long>(ep->ExceptionRecord->ExceptionCode), kind,
                  ep->ExceptionRecord->NumberParameters >= 2 ? reinterpret_cast<void *>(info[1]) : nullptr, where,
                  static_cast<unsigned long>(GetCurrentThreadId()),
                  GetCurrentThreadId() == g_main_tid ? " (game thread)" : " (other thread)");
    nocturne_boot_log(line);
    // The call stack, as module+offset, so a system-library crash can be traced back to what called it.
    typedef USHORT(WINAPI * CaptureFn)(ULONG, ULONG, PVOID *, PULONG);
    CaptureFn capture = nullptr;
    for (const wchar_t *mod : {L"kernelbase.dll", L"kernel32.dll", L"ntdll.dll"}) {
        HMODULE h = GetModuleHandleW(mod);
        FARPROC f = h ? GetProcAddress(h, "RtlCaptureStackBackTrace") : nullptr;
        if (f) { capture = reinterpret_cast<CaptureFn>(reinterpret_cast<void *>(f)); break; }
    }
    if (capture) {
        void *frames[24];
        const USHORT n = capture(0, 24, frames, nullptr);
        for (USHORT i = 0; i < n; ++i) {
            char d[400];
            describe_addr(frames[i], d, sizeof d);
            std::snprintf(line, sizeof line, "  frame %u: %s", static_cast<unsigned>(i), d);
            nocturne_boot_log(line);
        }
    }
}

// Sees every exception first, including ones the code handles itself, so a line here is not always
// fatal. "FATAL" below comes from the unhandled-exception filter and means the app is going down.
static LONG CALLBACK boot_crash_handler(PEXCEPTION_POINTERS ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    static volatile LONG busy = 0;
    if ((code & 0xF0000000u) != 0xC0000000u || InterlockedExchange(&busy, 1)) return EXCEPTION_CONTINUE_SEARCH;
    log_crash("EXCEPTION", ep);
    InterlockedExchange(&busy, 0);
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI boot_fatal_filter(PEXCEPTION_POINTERS ep) {
    log_crash("FATAL", ep);
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
    FILE *out = std::fopen(nocturne_data_path("perf.log").c_str(), "w");
    if (!out) out = stderr;
    std::setvbuf(out, nullptr, _IONBF, 0);
    std::fprintf(out, "profiler started\n");
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
    std::map<std::string, int> by_wait;   // "<caller rva>><its caller rva>" for samples outside our code
    int total = 0, failed = 0;
    ULONGLONG window = GetTickCount64();
    for (;;) {
        Sleep(2);
        CONTEXT ctx;
        std::memset(&ctx, 0, sizeof(ctx));
        ctx.ContextFlags = 0x100001;   // CONTEXT_CONTROL (x64)
        uintptr_t rip = 0;
        uintptr_t stack_copy[192];
        bool have_stack = false;
        if (suspend(target) != (DWORD)-1) {
            if (get_ctx(target, &ctx)) {
                rip = static_cast<uintptr_t>(ctx.Rip);
                // Copy the top of the stack while the thread is paused, to find who called into the wait.
                // Only copy when the whole range is committed, readable memory: the crash notes showed this copy
                // faulting (and the __except around it not catching it) when the sample landed at an odd moment.
                MEMORY_BASIC_INFORMATION mbi;
                const uintptr_t sp = static_cast<uintptr_t>(ctx.Rsp);
                if (sp > 0x10000 && VirtualQuery(reinterpret_cast<const void *>(sp), &mbi, sizeof mbi) &&
                    mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                    sp + sizeof(stack_copy) <= reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
                    std::memcpy(stack_copy, reinterpret_cast<const void *>(sp), sizeof(stack_copy));
                    have_stack = true;
                }
            }
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
        if (have_stack && rip != 0 && !(rip >= exe_base && rip < exe_base + 0x1100000)) {
            uint32_t found[2] = {0, 0};
            int nf = 0;
            for (size_t i = 0; i < 192 && nf < 2; ++i) {
                const uintptr_t v = stack_copy[i];
                if (v < exe_base + 0x1000 || v >= exe_base + 0x1097090) continue;
                const uint8_t *c = reinterpret_cast<const uint8_t *>(v);
                bool call_before = false;
                __try {
                    call_before = c[-5] == 0xE8 || c[-6] == 0xFF || c[-2] == 0xFF || c[-3] == 0xFF;
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
                if (call_before) found[nf++] = static_cast<uint32_t>(v - exe_base);
            }
            char key[40];
            std::snprintf(key, sizeof(key), "%x>%x", found[0], found[1]);
            ++by_wait[key];
        }
        if (GetTickCount64() - window >= 5000 && total > 0) {
            std::vector<std::pair<int, std::string>> mods;
            for (auto &kv : by_module) mods.push_back({kv.second, kv.first});
            std::sort(mods.rbegin(), mods.rend());
            std::string line = "prof:";
            for (size_t i = 0; i < mods.size() && i < 6; ++i)
                line += " " + mods[i].second + "=" + std::to_string(100 * mods[i].first / total) + "%";
            std::fprintf(out, "%s (n=%d)\n", line.c_str(), total);
            std::vector<std::pair<int, uint32_t>> top;
            for (auto &kv : by_rva) top.push_back({kv.second, kv.first});
            std::sort(top.rbegin(), top.rend());
            std::string hot = "prof-rva:";
            char tmp[48];
            for (size_t i = 0; i < top.size() && i < 80; ++i) {
                std::snprintf(tmp, sizeof(tmp), " %x:%d", top[i].second, top[i].first);
                hot += tmp;
            }
            std::fprintf(out, "%s\n", hot.c_str());
            std::vector<std::pair<int, std::string>> waits;
            for (auto &kv : by_wait) waits.push_back({kv.second, kv.first});
            std::sort(waits.rbegin(), waits.rend());
            std::string wl = "prof-wait:";
            for (size_t i = 0; i < waits.size() && i < 24; ++i) wl += " " + waits[i].second + ":" + std::to_string(waits[i].first);
            std::fprintf(out, "%s\n", wl.c_str());
            by_wait.clear();
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
    // Output resolution (launcher.ini, Graphics tab). Read the file directly rather than through the game's
    // cached settings, which must not be loaded before the launcher has had its say.
    seed_launcher_defaults();
    {
        const NocturneLauncherSettings ls =
            nl::load_settings(std::filesystem::path(nocturne_data_root()) / "launcher.ini");
        g_boot_output_height = ls.output_height >= 1500 ? 2160 : 1080;
        const int took = nocturne_uwp_set_output_height(g_boot_output_height);
        char line[96];
        std::snprintf(line, sizeof line, "output: asked for %dp native pixels=%s (result %d)", g_boot_output_height,
                      g_boot_output_height >= 1500 ? "yes" : "no", took);
        nocturne_boot_log(line);
    }
    // The game reports problems on stderr, which a UWP app has nowhere to show: keep it in a file.
    const std::string errpath = nocturne_data_path("stderr.log");
    if (std::freopen(errpath.c_str(), "a", stderr)) std::setvbuf(stderr, nullptr, _IONBF, 0);
    // The sampler pauses the game thread many times a second, so it only runs when asked for: put a file
    // called profile.flag in the app data folder (next to startup.log) and restart.
    // One launch only: the flag is removed as it is used, so a forgotten profile.flag cannot keep the sampler
    // (which pauses the game thread many times a second) running on every start.
    if (FILE *f = std::fopen(nocturne_data_path("profile.flag").c_str(), "rb")) {
        std::fclose(f);
        std::remove(nocturne_data_path("profile.flag").c_str());
        start_profiler();
    }

    g_main_tid = GetCurrentThreadId();
    {
        typedef LPTOP_LEVEL_EXCEPTION_FILTER(WINAPI * SetFilterFn)(LPTOP_LEVEL_EXCEPTION_FILTER);
        for (const wchar_t *mod : {L"kernelbase.dll", L"kernel32.dll"}) {
            HMODULE h = GetModuleHandleW(mod);
            FARPROC f = h ? GetProcAddress(h, "SetUnhandledExceptionFilter") : nullptr;
            if (f) { reinterpret_cast<SetFilterFn>(reinterpret_cast<void *>(f))(boot_fatal_filter); break; }
        }
    }
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

static int run_launcher_impl();

extern "C" int nocturne_uwp_run_launcher(void) {
    seed_game_defaults();                  // game files already in place from an earlier run
    const int r = run_launcher_impl();
    if (r) seed_game_defaults();           // files may have just been imported
    return r;
}

static int run_launcher_impl() {
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
    if (res == nl::LaunchResult::Play) {
        // A changed output size only takes effect for a fresh window. Start the app again; the Play flag is
        // already written, so it comes straight back into the game.
        const NocturneLauncherSettings ls =
            nl::load_settings(std::filesystem::path(nocturne_data_root()) / "launcher.ini");
        const int want = ls.output_height >= 1500 ? 2160 : 1080;
        if (want != g_boot_output_height) {
            nocturne_boot_log("launcher: output size changed, restarting the app to apply it");
            nocturne_uwp_restart_app();   // returns only if the console refused the restart
            nocturne_boot_log("launcher: restart refused, starting the game at the old output size");
        }
    }
    return res == nl::LaunchResult::Play ? 1 : 0;
}
