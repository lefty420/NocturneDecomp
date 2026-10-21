// =============================================================================
// NOCTURNE CUSTOM ENTRY POINT
// =============================================================================
//
// Replaces the decompiled entry.c / ProcessInitTermHandlers /
// InitializeWinAppRuntime stack-probe scaffolding. Those functions only
// exist to set up the Watcom runtime — we don't need any of it when linking
// against a modern C/C++ runtime.
//
// Responsibilities:
//   1. Initialize the cross-platform Win32 shim function pointers.
//   2. Synthesize the HINSTANCE/cmdline arguments the decompiled winMain
//      expects.
//   3. Hand control to the decompiled winMain and propagate its exit code.
//

#include "nocturne.h"
#include "win32/shim_init.h"

#if defined(_WIN32)
#include <winapifamily.h>
#endif
#include <cstdio>
#include <cstring>

// Entries 0..155 are game static initializers (actor registration, vtable
// setup, etc.); entry 0 registers CDemonActor. Entries 156..166 are Watcom CRT
// init handlers that we skip since we link against a modern C/C++ runtime.
static void runStaticInitializers(void)
{
    for (int i = 0; i < 156; i++) {
        if (g_InitHandlers[i].func != (RUNTIME_HANDLER_FUNC *)0) {
            g_InitHandlers[i].func();
            g_InitHandlers[i].status = 0x02;
        }
    }
}

#ifdef NOCTURNE_UWP_LAUNCHER
extern "C" int nocturne_uwp_run_launcher(void);

// Breadcrumbs and a crash note in <LocalState>\startup.log, because a UWP app that dies at start-up
// shows nothing. Read it through Device Portal (File explorer > LocalAppData > the app > LocalState).
#include "core/data_root.h"
#include <windows.h>
extern "C" void nocturne_boot_log(const char *line)
{
    const std::string path = nocturne_data_path("startup.log");
    FILE *f = fopen(path.c_str(), "a");
    if (!f) return;
    fprintf(f, "%s\n", line);
    fclose(f);
}
static LONG CALLBACK bootCrashHandler(PEXCEPTION_POINTERS ep)
{
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    static volatile LONG busy = 0;
    if ((code & 0xF0000000u) != 0xC0000000u || InterlockedExchange(&busy, 1)) return EXCEPTION_CONTINUE_SEARCH;
    char buf[200];
    const char *base = (const char *)GetModuleHandleW(nullptr);
    snprintf(buf, sizeof buf, "EXCEPTION %08lx at exe+0x%llx (addr %p)", (unsigned long)code,
             (unsigned long long)((const char *)ep->ExceptionRecord->ExceptionAddress - base),
             ep->ExceptionRecord->ExceptionAddress);
    nocturne_boot_log(buf);
    InterlockedExchange(&busy, 0);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

// --version answers before anything is initialized: a bug report should be able
// to get the build's identity out of a binary that cannot open a window, load a
// POD or reach a GL context.
static int wantsVersion(int argc, char** argv)
{
    for (int i = 1; i < argc; i++) {
        if (argv[i] == nullptr) continue;
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) return 1;
    }
    return 0;
}

// UWP: the app is started by SDL's WinRT shell (SDL_winrt_main_NonXAML.cpp), which calls
// SDL_main on a worker thread once the window exists. Everywhere else it is plain main().
#if defined(_WIN32) && defined(WINAPI_FAMILY) && defined(WINAPI_FAMILY_APP) && WINAPI_FAMILY == WINAPI_FAMILY_APP
#define NOCTURNE_ENTRY_IS_SDL_MAIN 1
static int nocturne_main(int argc, char** argv);
extern "C" int SDL_main(int argc, char** argv) { return nocturne_main(argc, argv); }
static int nocturne_main(int argc, char** argv)
#else
int main(int argc, char** argv)
#endif
{
    if (wantsVersion(argc, argv)) {
        printf("%s\n", nocturne_version_line());
        return 0;
    }

#ifdef NOCTURNE_UWP_LAUNCHER
    // Launcher first: settings and adding the game files. Quit there means quit the app.
    AddVectoredExceptionHandler(1, bootCrashHandler);
    nocturne_boot_log("--- start: SDL_main entered, launcher next");
    if (!nocturne_uwp_run_launcher()) { nocturne_boot_log("launcher: player quit"); return 0; }
    nocturne_boot_log("launcher done, starting the game");
#endif
    shims_init_all();
    runStaticInitializers();
#ifdef NOCTURNE_UWP_LAUNCHER
    nocturne_boot_log("shims and static initializers done, entering the game");
#endif

    HINSTANCE hInstance     = (*g_GetModuleHandleAFunc)((LPCSTR)0);
    HINSTANCE hPrevInstance = (HINSTANCE)0;
    LPSTR     lpCmdLine     = (LPSTR)(argc > 1 ? argv[1] : "");
    const int nCmdShow      = 10;  // SW_SHOWDEFAULT, mirroring entry.c

    const int rc = wincore_winrun_cpp_winMain_FUN_005f3680(
        hInstance, hPrevInstance, lpCmdLine, nCmdShow);
#ifdef NOCTURNE_UWP_LAUNCHER
    { char b[64]; snprintf(b, sizeof b, "game returned %d", rc); nocturne_boot_log(b); }
#endif
    return rc;
}
