# Xbox (UWP, Dev Mode) port

**Status: groundwork only. There is no Xbox build yet, and nothing here has been run on a console.**

What exists today is the renderer half of the job: the GL layer can now run on OpenGL ES 3.0,
which is what ANGLE provides on UWP/Xbox (ES 3.0 over Direct3D 11). The rest is listed under
*Remaining work* below.

## CI status (`.github/workflows/xbox-uwp.yml`)

Runs on pushes to `xbox-uwp-gles-groundwork` and by hand. Syntax-level only: no game binary is
built or linked, and nothing runs.

| Job | What it checks | Latest result |
|---|---|---|
| `gles-syntax` | the GL files compile in desktop and ES modes | passes |
| `uwp-probe` | a Windows runner can compile (clang-cl, `WINAPI_FAMILY_APP`) and link (`/APPCONTAINER`, `WindowsApp.lib`) a small program | passes |
| `uwp-shim-compile` | every file under `shims/` compiles with clang-cl under `WINAPI_FAMILY_APP` in ES mode | 107 of 108 files clean in this job; the 108th, `win32/mci_video.cpp`, needs FFmpeg headers and compiles with 0 errors in the `ffmpeg-uwp` job, so all 108 compile |

| `uwp-game-compile` | the decompiled game sources (`src/`, `main/`, `checks/`: 6,604 files after the build's per-function source selection) compile under the same settings | a full run over all 6,604 files found 2 failures (editor `chdir`, `sleep`), now fixed; the default run samples every 20th file plus those two as regression guards, and is clean |
| `sdl-uwp-build` | SDL2 builds for UWP (x64 Release) from SDL's own `VisualC-WinRT/SDL-UWP.vcxproj` | builds: `SDL2.dll`, ~1.35 MB, saved as the `sdl2-uwp-x64` artifact. Needs toolset `v145` (the runner has Visual Studio 18); the project's own `v143` is not present there |

| `angle-uwp` (own workflow, `.github/workflows/angle-uwp.yml`) | Google's ANGLE built from source for UWP x64 (`target_os="winuwp"`) | builds: `libEGL.dll` (~46 KB) and `libGLESv2.dll` (~4.6 MB), with import libs and headers, saved as the `angle-uwp-x64` artifact. Takes about 15 minutes; runs only when that workflow file changes or by hand. Not run on a console |

ANGLE notes: Microsoft's `ANGLE.WindowsStore` NuGet package is built from an old ES 2.0 fork, so it is not used (the renderer needs ES 3.0). Chromium's UWP toolchain only drives MSVC `cl.exe`, so the build uses `is_clang=false` with `use_custom_libcxx=false`, points the pinned Windows SDK version at the one on the runner, and replaces the symbolic `NTDDI_VERSION` in Chromium's build config with a number.

| `sdl-ttf-uwp` (workflow `.github/workflows/libs-uwp.yml`) | SDL2_ttf 2.22.0 with its vendored FreeType, built static for UWP x64 (CMake `WindowsStore`, Visual Studio generator) | builds: `SDL2_ttf.lib` (~140 KB) and `freetype.lib` (~2.1 MB), saved as the `sdl2-ttf-uwp-x64` artifact. HarfBuzz is not enabled (SDL_ttf 2.22 leaves it off by default). Not linked or run |

| `ffmpeg-uwp` (workflow `libs-uwp.yml`) | FFmpeg 6.1.1 built static for UWP x64 (MSVC toolchain, `WINAPI_FAMILY_APP`, `-APPCONTAINER`), network/programs/devices off | builds: `libavcodec`, `libavformat`, `libavutil`, `libswscale`, `libswresample` (plus `libavfilter`, unused) and headers, saved as the `ffmpeg-uwp-x64` artifact; also checks `mci_video.cpp` compiles against those headers (0 errors). Not linked or run |

The jobs report results as run annotations, and the full logs are attached to each run as
artifacts. To re-run the game-source check over every file, start the workflow by hand
(Actions tab, `xbox-uwp`, Run workflow) with `stride` set to `1`; it takes about an hour.

All of this is compile-level only: nothing is linked, and nothing has run.

What it took to get there, for anyone hitting the same errors: the Watcom calling-convention
macros in `include/system/basetypes.h` were guarded by `_MSC_VER` and are now unconditional; the
shims' `tell()`/`chsize()` are renamed under MSVC to avoid the UCRT's; `kernel32.cpp` supplies
`ssize_t`, `S_ISDIR` and friends, 64-bit `off_t`/`lseek`, and uses `GetCurrentProcessId` instead of
`_getpid` (not available to UWP) when built with the Microsoft toolchain; `dump.cpp` uses
`_stricmp`; `watcom.cpp` provides `chdir()` (reports failure; a UWP app cannot change directory) and
`sleep()` for the Microsoft toolchain. A new `core/data_root` resolves the game's relative paths against the app's local
folder on UWP (unchanged elsewhere) and is used by the file-open, attribute, delete,
create-directory and file-search entry points and by `watcom_resolve_fs_path`.

## Why ES 3.0 and ANGLE

UWP has no desktop OpenGL, and a console in Dev Mode can only run UWP apps. The renderer
(`trigl`) is written against GL 3.3 core, but its shaders are plain `in`/`out` GLSL with no
3.3-only features, so the lowest-risk route is to keep its structure and target ES 3.0 through
ANGLE, rather than rewrite it for Direct3D 11.

## What changed

Everything is behind `NOCTURNE_GLES` (default **OFF**, so the existing Linux and Windows lanes
are unchanged).

| Area | With `NOCTURNE_GLES=1` |
|---|---|
| Headers | `<GLES3/gl3.h>` + `<GLES2/gl2ext.h>` instead of `SDL_opengl.h` |
| Context | `SDL_GL_CONTEXT_PROFILE_ES`, 3.0 (was core 3.3) |
| Shaders | `#version 300 es` plus `precision highp` lines (was `#version 150 core`) |
| `glClearDepth`, `glDepthRange` | routed to `glClearDepthf`, `glDepthRangef` |
| `glDrawBuffer` | routed to `glDrawBuffers(1, &mode)` |
| `glPolygonMode` | no-op (no caller in the tree) |
| `glGetTexImage` | left null (its only caller already checks) |
| BGRA uploads | internal format `GL_BGRA_EXT` (needs `GL_EXT_texture_format_BGRA8888`, which ANGLE exposes) |

Files: `shims/gl/gl_api.{h,cpp}`, `gl_blit.cpp`, `gl_present.cpp`, `shims/renderer/trigl_gl.cpp`,
and the `NOCTURNE_GLES` / `NOCTURNE_ANGLE_INCLUDE_DIR` options in `CMakeLists.txt`.

## Validate the ES path off-console first

The new `exe-linux-gles-x86_64` preset runs the same ES code path on Mesa, so GL-ES problems
show up on a desktop where they are easy to debug:

```sh
sudo apt install libgles-dev
cmake --preset exe-linux-gles-x86_64
cmake --build --preset exe-linux-gles-x86_64
```

Checked so far: the changed GL files pass `clang++ -fsyntax-only` in both modes (0 and 1).
Not checked: a full link, or a run, of either mode.

## Remaining work (in rough order)

1. **Build the libraries for UWP.** The build compiles SDL2, SDL2_ttf and FFmpeg from source
   for MSYS2/mingw (`cmake/bundledlibs.cmake`); none of that targets UWP. SDL2 (its WinRT port)
   now builds in CI (see above). ANGLE (ES 3.0) now builds too (see above). SDL2_ttf + FreeType build too. FFmpeg builds too. Still needed: checking that SDL's WinRT backend picks up this ANGLE.
2. **App shell.** UWP needs a WinRT app entry. SDL2's WinRT support does this with a C++/CX
   project compiled by MSVC (`/ZW`), so the realistic layout is an MSVC shell project that links
   a static library of the game built with `WINAPI_FAMILY_APP`. `packaging/uwp/Package.appxmanifest`
   is a starting template for that shell, not a working one.
3. **Compiler/toolchain.** Done at compile level: the platform layer and all decompiled game
   sources compile under clang-cl with the UWP API family (see CI status), apart from the one
   FFmpeg-dependent shim file that needs FFmpeg's headers. Linking has not been tried.
4. **Win32 API restrictions.** UWP blocks or removes many Win32 calls the shims currently use or
   will reach for (`SetCurrentDirectory`/`chdir`, `LoadLibrary` of arbitrary files, `CreateWindow`,
   `winmm` pieces, some `kernel32` calls). Each needs checking against the UWP API surface.
5. **File access and game data.** A UWP app cannot read arbitrary paths and cannot change its
   working directory. `core/data_root` now maps relative paths onto the app's local folder (where
   you copy your own game files through Device Portal) and is applied in the main file entry
   points. Still to do: audit the remaining ways the code touches files (direct `fopen`/`std::ofstream`
   in the ini, log and dump code) and confirm none bypass it, then test it for real on UWP.
6. **Input.** The shims have gamepad support (`shims/game/`); it needs verifying through SDL's
   WinRT gamepad backend with an Xbox controller.
7. **Memory and performance.** Dev Mode apps have a fixed memory budget that differs between
   Xbox One and Series X|S. Check the current limits for your console in Microsoft's docs and
   measure the game's peak usage, including the FMV decoder.

## The launcher app (`packaging/uwp/launcher`)

A controller-driven front screen that runs before the game: **Play**, **Game files**, **Settings**, **About** (LB/RB switch tabs, A selects, B goes back). It is plain SDL2 (no game code), so it runs before the game's renderer exists.

- **Game files** checks the folders the game expects (`art`, `models`, `world`, `data`, `backdrop`, and the optional `video` intro) in the app's local folder, shows what is present or missing, and has an import flow with progress, cancel and plain-language errors. Play stays locked until the required folders are there.
- **Import routes** (C++/CX, `src/imports_platform_uwp.cpp`): the system folder picker and removable (USB) storage. Both go through WinRT storage rather than file paths, because Xbox only exposes picked folders through the broker. Microsoft documents folder pickers as unsupported on Xbox (see the next section), so each failure is reported on screen, and Device Portal's file explorer remains the route that needs no code.
- **Settings** are stored in `launcher.ini` and read by the game at run time through `shims/core/launcher_settings.*` (stick deadzone, turn speed, look speed, invert look, with a live stick preview).
- **CI** (`.github/workflows/launcher.yml`): one job runs the launcher on Linux with a software renderer, drives every screen and a full import against temporary folders, and saves screenshots (artifact `launcher-screenshots`); the other compiles it for UWP, including the C++/CX providers.

Status: the Linux run passes. The UWP compile passes for every launcher file including the C++/CX importers. Nothing has run on a console, so which import route actually works on your Xbox is not known yet.

## What Xbox UWP restricts (from Microsoft's docs and community reports)

Sources: Microsoft's [UWP limitations on Xbox](https://github.com/MicrosoftDocs/winrt-related/blob/docs/winrt-related-src/extension-sdks/uwp-limitations-on-xbox.md) page (dated 2020, so possibly stale), and a [Microsoft Q&A thread](https://learn.microsoft.com/en-in/answers/questions/573133/file-and-folder-picker-usage-policies-on-xbox-cons) (2021-2024). Nothing below has been tested by us on a console.

| Restriction | What it means for this port |
|---|---|
| "Scenarios related to folder pickers are not supported on Xbox" | The documented answer is no, but the Q&A thread reports pickers working on a retail Xbox One X and on dev-mode builds, with navigation limited (no arbitrary drives, Downloads usable). So an in-app folder or file picker is worth trying but cannot be the only way to add game files. |
| `broadFileSystemAccess` "not supported on Xbox" | Documented as unsupported; a community report says it still works in Developer Mode for sideloaded apps, and one project uses it to reach `D:\DevelopmentFiles` in dev mode. Declared as a restricted capability in the manifest. |
| `KnownFolders.RemovableDevices` | Needs the `removableStorage` capability plus at least one file type association in the manifest; only declared extensions are visible. Xbox support is not confirmed by Microsoft's page. Possible route for a USB drive. |
| Microsoft policy 10.13.4 | Store apps may not offer general browsing of the file system. Irrelevant for sideloading in Dev Mode, but it is why pickers are restricted. |
| HumanInterfaceDevice APIs not supported | Raw HID is out; controllers must come through the Windows.Gaming.Input path that SDL's WinRT backend uses. Needs checking with a real controller. |
| Roaming settings not synchronized; toasts limited; no tiles, no drag and drop | None of these affect the game. |
| "All APIs are callable, and will fail gracefully if unsupported" | Failures show up at run time, not compile time, so the app must report them on screen. |

Not covered by that page: memory limits, resolution limits, background suspension, and Dev Mode behaviour. Those need Microsoft's current Xbox developer documentation.

**Consequence for adding game files.** The in-app importer will try, in order: the system file/folder picker, a USB drive via removable storage, and a folder in `DevelopmentFiles`. The one route that does not depend on any of that is Device Portal's file explorer, which copies straight into the app's local folder. The app's Game Files screen is built to work with all of them: it rescans the local folder, shows exactly which files are present or missing, and reports plainly when a method is refused.

## Deploying, once there is a build

1. Enable Dev Mode on the console and note its IP address.
2. In Visual Studio, build the shell project for x64 / Release and create the app package, or
   deploy straight to the console from VS.
3. Open Device Portal (`https://<console-ip>:11443`) to install the package, and to copy your own
   copy of the game's data files into the app's local storage once item 5 exists.

No game data is distributed with this project; you need your own copy of Nocturne.

## Sideloadable package (CI: `uwp-link`)

The `uwp-link` workflow links `nocturne.exe` for UWP x64 (launcher included, shown before the game),
stages it with SDL2, ANGLE (`libEGL`, `libGLESv2`), `d3dcompiler_47.dll` and the launcher fonts, and
produces a test-signed `NocturneDecomp.msix` plus the matching `NocturneDev.cer` (artifact
`nocturne-uwp-package`). A fresh certificate is made on every run, so install the `.cer` that came
with the `.msix` you deploy.

**Status: builds and signs in CI. Never run on a console or on Windows.** Untested: that the package
installs, that SDL's WinRT backend finds this ANGLE, the launcher's three import routes, memory limits.

Deploy: Xbox in Dev Mode → Device Portal (`https://<console-ip>:11443`) → Add → choose the `.msix`
(add the `.cer` if asked) → Start. Game files: use the launcher's Game files screen, or Device Portal
→ File explorer → LocalAppData → the app → LocalState.
