## Nocturne for Xbox Series X|S — alpha

An unofficial community port of the decompiled game to a sideloaded Xbox app, with a launcher in front of it.
**This is an alpha**: it runs well on the author's console, but it has had little testing beyond that.

**No game data is included.** You need your own copy of Nocturne (the retail PC game). The app copies your
files in from its *Game files* tab.

### Install

1. Put the console in **Dev Mode** and open **Device Portal** (`https://<console-ip>:11443`).
2. **Add** → choose `NocturneDecomp.appx` (add `NocturneDev.cer` too if it asks for the certificate).
3. Start the app. In the launcher, open **Game files** and add your Nocturne folders, then press **Play**.

A new version installs over an old one and keeps your saves and settings.

### Getting back to the launcher

After you press **Play** once, the app starts straight into the game. To reopen the launcher (graphics, controls,
game files), hold **View + Menu + left-stick click** together in the game. The app restarts and shows the launcher.
B does not close the app; use **Start** to back out of the game's menus.

### What it has

- Launcher: game-file import that resumes if interrupted, controller feel, and graphics options.
- Screen shape: **4:3** (bars at the sides), **Widescreen** (edges stretched more than the middle), **Stretch**
  (fills evenly) or **Zoom** (fills, trims the top and bottom).
- Output: **1080p** or **4K** (3840x2160; only useful on a 4K TV).
- **FSR upscaling** with a sharpness slider: scales the game's 640x480 picture up with cleaner edges than a plain stretch.
- Your controls and graphics start from the author's defaults the first time (change them in the game as usual).
- Based on the community decompilation by NearlyTRex (NocturneDecomp).

### Known issues

- 4K with FSR is the heaviest setting. If the frame rate drops, turn FSR off or use 1080p.
- The controller lines in the game's own Control Options screen do not change anything on the console; use the
  launcher's Settings tab.
- Changing the output resolution restarts the app.
- Only tested on one console. Please report problems with the app's `startup.log` and `stderr.txt` (Device Portal →
  File explorer → LocalAppData → the app → LocalState).

The `.cer` is a test certificate generated for this build. Only the `.cer` is published, never its private key.
