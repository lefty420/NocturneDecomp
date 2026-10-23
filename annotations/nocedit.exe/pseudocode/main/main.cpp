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
//   0. On Windows, catch an unhandled fault and leave a crash report
//      (core/crash_report.h) rather than vanishing.
//   1. Choose the data and user directories (core/path_roots.h), then
//      initialize the cross-platform Win32 shim function pointers.
//   2. Synthesize the HINSTANCE/cmdline arguments the decompiled winMain
//      expects.
//   3. Hand control to the decompiled winMain and propagate its exit code.
//

#include "nocturne.h"
#include "win32/shim_init.h"
#include "core/path_roots.h"
#include "core/crash_report.h"

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

// Breadcrumbs and a crash note in <LocalState>\startup.log (defined in launcher_entry.cpp).
extern "C" void nocturne_boot_log(const char *line);
extern "C" void nocturne_boot_init(void);
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

    nocturne_install_crash_handler(nocturne_version_line());
    nocturne_init_path_roots(argc, argv);
#ifdef NOCTURNE_UWP_LAUNCHER
    // Launcher first: settings and adding the game files. Quit there means quit the app.
    nocturne_boot_init();
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
