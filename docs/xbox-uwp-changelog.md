# Xbox UWP port: change log

What has changed in the Xbox (UWP, Dev Mode) port of NocturneDecomp, newest first within each section.
The decompilation itself is the work of NearlyTRex (https://github.com/NearlyTRex/NocturneDecomp); this
log covers only the Xbox port and its launcher.

## Launcher look

- **1930s pulp-horror theme.** The launcher is restyled as a case file: sepia-black cards with brass double
  rules and red corner brackets, a paper tab for the selected page, a rubber-stamp "Xbox port" mark, film grain,
  and the box art dimmed behind everything. Headings use IM Fell Double Pica (OFL) and body text Special Elite
  (Apache 2.0); both are bundled with their licences, replacing Poppins. Layout, tabs and controls are unchanged.

## Graphics options

- **New Graphics tab in the launcher** (saved in `launcher.ini`, read by the game when it starts):
  - **Output resolution, 1080p or 4K.** 1080p is the console's normal app window. 4K turns off the console's
    layout scaling so the app gets the TV's real pixel size (3840x2160 on a 4K TV set to 4K). A change needs a
    fresh window, so pressing Play after changing it restarts the app once and goes straight into the game.
    The size actually received is written to `stderr.log` ("EGL surface WxH").
  - **Upscaling, Off or FSR**, with a sharpness slider. FSR is an edge-aware upscale plus a contrast-adaptive
    sharpen in the final blit shader, after AMD FidelityFX Super Resolution 1 (MIT licence); the sharpen step
    is folded into the same pass. It only runs when the picture is being resized, and falls back to plain
    scaling if the shader will not build (`stderr.log` says which).
- **Screen shape** (same tab) for wide TVs: 4:3 (bars at the sides, the default), Smart (the middle of the picture keeps
  near-true proportions and the edges are stretched progressively more, drawn as a bent strip of 48 columns),
  Stretch (fills evenly), and Zoom (fills the width and crops the top and bottom). Done in the final present, so
  it works with FSR. True widescreen rendering (a wider view instead of a stretched one) is not done: the
  engine derives its picture width from the height as 4:3 in its camera set-up.
- The game's own picture size is unchanged: the engine's tables stop at 1600x1200, and a native 1920x1080 or
  4K software render was not enabled because it could write past those limits and would be too slow on the CPU.

## Performance

- **Optimised hot render code.** The console build compiles without optimisation by default, and
  profiling the running game put most CPU time in a few pixel and render loops. These now build at `-O2`
  (with `-fno-strict-aliasing`, as the project already does for its scanline rasterisers):
  `core/dstrender` (software lighting blends), `core/flame`, `core/door`, `core/dpart`, `engine/3d`, and the
  `shims/gl` and `shims/renderer` code (pixel conversion and upload). Result: about 20-25 fps became about
  55-60 fps at a high resolution, and a steady 60 at lower ones.
  Optimising the whole game was tried and reverted: it broke input handling (the X button also drew the
  guns), and did not help the real hot spots.
- **File lookups cached.** The port's case-insensitive path resolution ran several `std::filesystem` calls
  on every file open (about 40% of the game's own CPU time in the profile). Successful reads are now
  remembered; any write, delete, rename or touch clears the cache, so saves and settings behave as before.
- **Correct picture size.** SDL's cached window size on the console was stale after the game resized
  itself, which drew the picture as a tiny square. The real drawable size is now read from the EGL surface.

## Controls

- **B no longer closes the app.** The console treats B as "back" and closes apps that do not handle it.
  The port registers its own back handler (SDL only does so for newer build targets), ignores the quit
  request it can produce, and B no longer acts as the menu's Escape. Start is the menu cancel button.
- **View + Menu + left-stick click** in the game returns to the launcher (the app restarts, or closes if
  the console refuses a restart).
- **Torch.** In the original game, switching the torch on draws the guns and holstering switches it off,
  because the beam is drawn from the gun. That behaviour is kept as it was. (An attempt to separate the
  buttons was reverted: the torch cannot work without the guns out.)

## Launcher

- A launcher screen with tabs: Play, Game files, Settings, The game, About.
- **Game files.** Finds a `Nocturne` folder on a USB drive automatically, lets you pick any folder, or use
  the USB drive directly without copying (experimental). Checks the real layout: the flat `ACT1-5.POD` set,
  `ACT?SND.pod`, the other `.pod` archives, `System` and `video`.
- **Straight into the game.** Once the files are in place and Play has been pressed once, later launches
  go directly into the game with no launcher screen. If the files go missing the launcher shows again.
- **The game tab** has the box art, description and plot. The box art is bundled in the app.
- Game icon and tile logos generated from the supplied "Xbox Series X|S" artwork.
- Credit to NearlyTRex for the decompilation, in the launcher header, the About tab and the docs.

## Packaging and installing

- Sideloadable `.appx` and `.msix`, built by GitHub Actions on a Windows runner and signed with a fresh
  self-signed certificate each run (`NocturneDev.cer` ships beside the package).
- **Version `0.2.<run number>.0`**, so a newer build installs over an older one as an update and keeps the
  game files and settings (uninstalling deletes them).
- App-local C++ runtime DLLs are bundled; the app used to close silently without them.
- Launch fixes: a hang when pressing Play (a background USB probe was waited on at exit) and a crash
  handler that is looked up at run time because UWP does not declare it.

## Game files and paths

- Relative game paths resolve through data and user directories (`LocalState`, or the USB folder in in-place
  mode); `_chmod` and related calls resolve their paths first. This fixed the "please copy Nocturne to your
  hard drive" message.
- Writes always go to the app's own storage and are copied there first when the shipped file is read-only.

## Diagnostics (all optional)

- `startup.log`: start-up steps and game exit; `stderr.log`: the game's error output.
- `fps.log`: frame rate, frame time and swap wait every 5 seconds, fresh each launch.
- `profile.flag`: create an empty file with this name next to `startup.log` and restart to enable a
  sampling profiler that writes `perf.log` (which game functions the CPU spends time in). Off by default
  because it slows the game slightly.
- CI resolves profiler addresses to function names from the link map (`.github/scripts/map_symbols.py`).

## Keeping in sync with upstream

- Upstream v0.1.5 and v0.1.6 merged. The port adopted upstream's platform-file build and GL path, and its
  own changes sit on top (`cmake/platforms/uwp.cmake`, `cmake/toolchains/uwp-x64.cmake`).

## Not yet confirmed or not done

- Use-item on B: reported as not working at one point; the cause is unknown.
- Optional picture improvements (FSR-style sharpening upscale, anti-aliasing, integer or CRT scaling) are
  possible and not started. HDR is unlikely to be possible through this graphics layer.
- A true 16:9 render resolution depends on the game's own menus and layouts handling it; untested.
