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
| `uwp-shim-compile` | every file under `shims/` compiles with clang-cl under `WINAPI_FAMILY_APP` in ES mode | 107 of 108 files clean; `win32/mci_video.cpp` fails only because FFmpeg headers are not on the runner |

That job reports results as run annotations, and the full log is attached to the run as the
`uwp-shim-compile-logs` artifact. It does **not** yet cover the decompiled game sources under
`src/`, only the platform layer.

What it took to get there, for anyone hitting the same errors: the Watcom calling-convention
macros in `include/system/basetypes.h` were guarded by `_MSC_VER` and are now unconditional; the
shims' `tell()`/`chsize()` are renamed under MSVC to avoid the UCRT's; `kernel32.cpp` supplies
`ssize_t`, `S_ISDIR` and friends, 64-bit `off_t`/`lseek`, and uses `GetCurrentProcessId` instead of
`_getpid` (not available to UWP) when built with the Microsoft toolchain; `dump.cpp` uses
`_stricmp`. A new `core/data_root` resolves the game's relative paths against the app's local
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
   for MSYS2/mingw (`cmake/bundledlibs.cmake`); none of that targets UWP. You need UWP builds of
   SDL2 (its WinRT port), SDL2_ttf, FFmpeg (for the FMV) and ANGLE.
2. **App shell.** UWP needs a WinRT app entry. SDL2's WinRT support does this with a C++/CX
   project compiled by MSVC (`/ZW`), so the realistic layout is an MSVC shell project that links
   a static library of the game built with `WINAPI_FAMILY_APP`. `packaging/uwp/Package.appxmanifest`
   is a starting template for that shell, not a working one.
3. **Compiler/toolchain.** The platform layer (`shims/`) now compiles under clang-cl with the UWP
   API family (see CI status). The decompiled game sources under `src/` have not been tried yet.
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

## Deploying, once there is a build

1. Enable Dev Mode on the console and note its IP address.
2. In Visual Studio, build the shell project for x64 / Release and create the app package, or
   deploy straight to the console from VS.
3. Open Device Portal (`https://<console-ip>:11443`) to install the package, and to copy your own
   copy of the game's data files into the app's local storage once item 5 exists.

No game data is distributed with this project; you need your own copy of Nocturne.
