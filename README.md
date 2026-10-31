# outbreak-modern

A native, statically recompiled build of Resident Evil Outbreak File #1 (SLPM-65428, JP v1.04 + English
fan patch v2.1) with modern controls: Windows first, then Linux and Android.

The game's code is recompiled from your own disc at build time on PS2Recomp (with our numbered patches in
`upstream/ps2recomp/`), and drawn by PCSX2's GS built in as a library (`native/pcsx2_gs/`). No game data
is in this repository.

- What each part of the repo is for: `docs/ARCHITECTURE.md`
- Goals, stack and early decisions: `PROJECT_CONTEXT.md`; roadmap: `docs/milestones/`
- Build, run and test: `docs/ops/build.md`
- The earlier Modern Camera patch for ARMSX2 that this grew out of: `legacy/README.md`

**Work in progress:** we are adding the **Modern Camera Mod (RE5 Style)** to the game.

## Setting up from the release

The release is the `dist/` folder, made by `build-all.bat`. It holds:

- `windows/OutbreakModern.exe` with the DLLs and `resources/` beside it. Keep the whole `windows` folder together.
- `android/outbreak-modern.apk` for Android phones and tablets.

The game disc is not included. You need your own copy of Resident Evil Outbreak File #1, the Japanese disc
(SLPM-65428, v1.04), as a `.iso` image. The English fan patch v2.1 is needed too; the disc check below says so
if it is missing. Play only discs you own.

**Windows**

1. Unzip or copy the `windows` folder anywhere you like, and double-click `OutbreakModern.exe`.
2. The launcher opens. Press **Choose your game disc** and pick your `.iso` file.
3. The launcher checks the disc in three steps. All three must pass before **Play** is enabled:
   - the file reads as a disc image (if not, it may be incomplete: dump or copy it again);
   - the disc boots SLPM_654.28 (other releases of Outbreak and other games are refused);
   - the game code matches this build (a different copy or patch version is refused, and the message names
     the version when it is known).
4. Press **Play**. The game starts with the settings you chose.

Your settings (`outbreak.ini`, `input.ini`), memory cards and mods are kept beside the exe. The game writes a log
and, if it crashes, a crash report into a `logs` folder beside the exe (or `%LOCALAPPDATA%\OutbreakModern\logs`
when that folder isn't writable). Send a crash report privately: it can contain your folder paths.

**Android**

1. Install `outbreak-modern.apk` (allow installs from this source when Android asks).
2. Open the app and pick your disc. It can be read where it is, or copied into the app's storage. The copy can be
   deleted from the Game page.
3. The same three disc checks run; **Play** starts once they pass.
4. Texture packs and mods are optional. Their folders are set on their own pages.
5. If the app crashes, open the launcher's Advanced page and choose **Save crash report** to get a `.zip` to send
   privately (it can contain server addresses).

## The settings menu

The same menu is in the launcher (shown at start-up) and in the game. It has these pages:

| Page | What it does | When it applies |
|---|---|---|
| Game | Disc, start-up (show the launcher or not), on Android the disc copy | Launcher only |
| Mods | Turn each PCSX2 `.pnach` mod on or off, import mods, don't load any | Next start |
| Controls | Keyboard or controller bindings, stick dead zone, reset to defaults | Immediately |
| Touch controls (Android) | Show on/off/auto, button layout, movement, size, opacity, vibration, edit layout | Immediately |
| Camera | Camera settings | Immediately |
| Display | Window mode, window size, render resolution, vsync, 16:9, scaling, show FPS | Immediately |
| Memory cards | Folder for your save files | Launcher only, next start |
| Audio | Volume and output device | Immediately |
| Network | Connection, and the server for online play (see below) | Next start |
| Graphics driver (Android) | System driver or a custom one | Launcher only, next start |
| Advanced | Renderer and VU1 engine; on Android, save a crash report | Launcher only, next start |

How it works:

- **Open it:** in the launcher it is on screen at once. In the game press **F1** or **Esc**, or on a controller
  hold **Back + Start** for 2 seconds. On Android use the touch menu's **Menu > Settings**.
- **Move:** use the page list on the left, and the arrow keys or the controller's stick and D-pad; **Enter** or
  **A** opens a setting. **Esc** or **B** goes back a level, and on the page list it closes the menu.
- **Changes save as you make them.** Nothing needs a confirm button. Your settings are kept in `outbreak.ini`
  beside the exe (Windows) or in the app's storage (Android), and a line you change is rewritten in place, so
  comments you add by hand are kept.
- **Rebinding keys:** on Controls, pick an action and press the new key or button. **Esc** cancels. **Reset to
  defaults** puts every binding back.
- **Start-up:** turn off **Show this screen at start-up** to boot straight into the game once the disc checks
  out. The Back button, F1 or Esc still opens the menu in the game.
- **Texture pack (optional):** the Game page's Texture pack option lets you import a texture pack. For 4K texture
  packs, see [HDRcade on Patreon](https://www.patreon.com/HDRcade/posts/resident-evil-1-114020125).
- **Settings that say "Next start" or "Launcher only"** take effect the next time the game starts. The menu
  says so beside them while you are in the game.

## Online and same-network play (planned, not built yet)

Online play uses the game's own network code. Research and design: `research/2026-10-26-network-crossplay.md`.

**First release: this port's clients only.** Windows and Android players of outbreak-modern play together, in any
mix. Playing with PCSX2 or real PS2s, and on the fan server (Obsrv), comes in the next release.

- **Pick a server** in the settings: a server you or someone else runs, or a game hosted on your network. The
  game has no LAN mode of its own; every match goes through a server.
- **Play on the same network:** press "Host game" on one Windows or Android device, and the others join it from a
  list or by its IP address.

Limitations:

- **This port's clients only** in the first release (above).
- **Hosting is for your local network.** Playing over the internet needs port forwarding or a VPN such as
  Tailscale, set up by you; the app doesn't do it.
- **The host server would be AGPL-3.0**, like the fan server it is based on, and ship as its own program or
  library.

Next release, when PCSX2 and PS2 players can join:

- **An Android host will serve only this port's players.** Android apps can't open ports below 1024, and the
  game's login check (DNAS) uses port 443, which a PS2 or PCSX2 can't be told to change. A Windows host will
  serve everyone.
- **Live play on Obsrv waits for its admins' agreement.**
- **Only the Japanese release is tested on the fan server.** Whether the English fan patch changes anything
  online is not known yet.

## Credits

- **Outbreak decomp repo:** [GalaxyNostalgia/Outbreak-decomp](https://github.com/GalaxyNostalgia/Outbreak-decomp) (CC0), the addresses, structures and functions this project is built on.
- **PS2Recomp:** [ran-j/PS2Recomp](https://github.com/ran-j/PS2Recomp) (GPL-3.0), the recompiler and runtime this build runs on.
- **Modern Camera Mod (RE5 Style):** by HeySnippy, the original *Resident Evil Outbreak Modern Camera* mod: [heysnippy.com/mods/outbreak-modern-camera](https://heysnippy.com/mods/outbreak-modern-camera). See `CREDITS.md`.
