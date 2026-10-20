# Phase 1 / Phase 2 Feasibility Report

**Project:** Resident Evil Outbreak Modern Camera on ARMSX2 (Android, AYN Odin 3 Max)
**Date:** 2026-10-19
**Status:** Desk research done. Nothing has been tested on a device yet.

Labels used: **Confirmed** (seen directly in source or files, with a reference),
**Partially verified** (evidence supports it but no device test), **Unverified**
(inference only), **Blocked** (can't go further without something we don't have).

Sources examined:

| Source | Version / hash |
|---|---|
| ARMSX2 repo `github.com/ARMSX2/ARMSX2` | `master` @ `c66d053fe` (2026-10-19); newest release tag `2.8.4` (`25e7084fd`, 2026-10-16) |
| HeySnippy mod page `heysnippy.com/mods/outbreak-modern-camera` | fetched 2026-10-19 |
| `Outbreak-ModernCam-v1.2.0.exe` | SHA-256 `e6c299dd…bfbb44`, which matches the published checksum |

The executable was only inspected statically: I listed the PyInstaller archive and
read strings and docstrings. It was **not run**. No code, addresses or assets from
it are copied into this repository.

---

## 1. Executive feasibility assessment

**Feasible. The most likely path is a native Android companion app that talks
PINE over loopback TCP. Two things must be proven on the device first.**

1. **ARMSX2 already has a working Android PINE server** (Confirmed). It listens on
   TCP `127.0.0.1:<slot>`, slot 28011 by default. The first release with it is
   **2.6.6.5**, so the earlier claim checks out. You turn it on in
   *Settings → Advanced → PINE (Remote Control) → Enable PINE*.
2. **The original mod speaks the same transport.** On Windows it connects to TCP
   `127.0.0.1:28011` (Confirmed from its strings). With `adb forward tcp:28011 tcp:28011`
   it should reach ARMSX2 without changes, at least at the socket level (Partially verified).
3. **The mod is not just "camera memory writes."** It patches the game's own MIPS
   code in EE RAM (NOPs and early `jr ra` returns) to freeze the engine's camera
   handlers. It then streams camera position and look-at data, plus movement and aim
   input, at roughly 200–250 Hz from Windows. It reads the controller with
   XInput/SDL2/Raw Input **on the PC** (Confirmed). This is the biggest obstacle,
   both for the Phase 2 test and for any port.
4. **The camera logic is closed source.** I found no public source, licence or
   implementation write-up. A native port therefore needs **the author's permission
   or collaboration**, or a clean-room rewrite. This is a project risk, not only a
   technical one.

---

## 2. ARMSX2 PINE findings

### 2.1 What ARMSX2 is built from — Confirmed
ARMSX2 is a full fork of the PCSX2 tree with ARM64 JITs added
(`pcsx2/arm64/iR5900-arm64.cpp`, `microVU-arm64.cpp`). It uses the PCSX2 core
unchanged in structure: `VMManager`, `vtlb`, `Patch`, `SIO/Pad`, `PINE`. The Android
frontend is Kotlin/Compose in `platforms/android/app/src/main/java/com/armsx2/`
with JNI in `platforms/android/app/src/main/cpp/native-lib.cpp`. Licence: GPL-3.0+
(`COPYING.GPLv3`).

### 2.2 Android PINE support — Confirmed

| Question | Answer | Evidence |
|---|---|---|
| Implemented on Android? | Yes | `pcsx2/PINE.cpp:95-97` defines `PINE_TCP_TRANSPORT` for `_WIN32 \|\| __ANDROID__` |
| Transport | TCP, bound to `INADDR_LOOPBACK` only | `PINE.cpp:650-690` (`htonl(INADDR_LOOPBACK)`, `htons(slot)`) |
| Port | `PINE_DEFAULT_SLOT 28011` | `pcsx2/PINE.h:13`; `Pcsx2Config.cpp:2223`; `Settings.kt:312` |
| Setting | `EmuCore/EnablePINE`, off by default; global only, not per game | `Settings.kt:301-312`, `ConfigStore.kt:355-367` |
| UI | Advanced tab → "PINE (Remote Control)" → "Enable PINE". Shows the `adb forward` command | `ui/settings/FixesTab.kt:551-573`; `i18n/I18n.kt:343,704-709` |
| Port editable? | Not in the UI, only in the INI (`EmuCore/PINESlot`) | `Settings.kt:307-311` |
| Start/stop | `VMManager::ReloadPINE()` restarts it live when the setting changes | `VMManager.cpp:5415-5425`, `:4088` |
| ADB forwarding | Intended use; the log prints `adb forward tcp:N tcp:N` | `PINE.cpp:685-687` |
| Added in | Commit `912b1d8f9` "PINE: listen on loopback TCP on Android" and `727ffd7c7` "Android: add a PINE toggle", both 2026-08-09 | `git log`; tag `2.6.6.4` contains neither, tag `2.6.6.5` contains both |
| Before that | PINE could never start on Android: the AF_UNIX bind to `/tmp` failed | commit message of `912b1d8f9` |

**Can another Android app reach it?** Partially verified. The server binds loopback
TCP, not an app-private Unix socket. On Android, all apps share the device's loopback
interface, so any app holding `android.permission.INTERNET` can normally connect to
`127.0.0.1:28011`. I haven't tested this on the Odin 3 Max's Android build. That test
is a Phase 3 gate.

### 2.3 Protocol surface — Confirmed (`PINE.cpp:196-232`, `:910-1290`)

| Opcode | Command | Notes |
|---|---|---|
| 0x00–0x03 | Read8/16/32/64 | `vtlb_ramRead`. Reads to MMIO/handler pages return FAIL (commit `b67e79328`) |
| 0x04–0x07 | Write8/16/32/64 | `vtlb_ramWrite`, a raw `memcpy` into guest RAM |
| 0x08 | Version | Returns **`"ARMSX2 <gitrev>"`**, not `"PCSX2 …"` (`PINE.cpp:1028-1038`) |
| 0x09 / 0x0A | Save / Load state | Marshalled to the CPU thread |
| 0x0B / 0x0C | Title / ID | ID = `VMManager::GetDiscSerial()`, e.g. `SLPM-65428` |
| 0x0D | UUID | Disc CRC as 8 hex chars (`PINE.cpp:1095-1107`) |
| 0x0E | GameVersion | Disc version |
| 0x0F | Status | 0 running, 1 paused, 2 shutdown |
| 0x10–0x18 | ARMSX2 extensions | stats JSON, get/set setting, frame advance, GS dump, UI automation |

- **No input opcodes.** PINE can't inject or read host controller state. It can only
  read and write the game's own pad buffers in EE RAM.
- **Address space:** EE virtual addresses through the vtlb (main RAM `0x00000000–0x01FFFFFF`).
- **Framing:** `[u32 total_len incl. header][opcode][args]…`, so several commands can
  be batched in one packet. Reply is `[u32 len][u8 0x00 OK / 0xFF FAIL][payload]`.
  The maximum reply is 450,000 bytes.
- **Connection lifecycle:** one thread named "PINE Server" accepts **one client at
  a time** and serves it until it disconnects (`PINE.cpp:811-826`). A second client
  completes the TCP connect through the backlog but gets no answers. Any malformed
  command fails the whole batch.
- **Thread safety:** memory commands run **on the PINE thread** with no lock against
  the EE thread. This is the same as upstream PCSX2. Writes can land mid-frame.
  Multi-word structures can tear, which is why the mod locks a whole structure into
  one transaction on its side.
- **Writes to code pages:** the ARM64 EE recompiler write-protects pages that hold
  compiled code (`mmap_MarkCountedRamPage`, `arm64/iR5900-arm64.cpp:3582,3608`). The
  page-fault handler then invalidates those blocks on a write (`vtlb.cpp:1602-1645`).
  So a PINE code patch *should* invalidate stale JIT blocks the same way it does on
  x86 PCSX2. Partially verified: the mechanism is in the source but I haven't tested
  it on the device. The mod itself warns that "PCSX2 kept a cached translation".

### 2.4 Controller path — Confirmed
`MainActivityRuntime.dispatchGenericMotionEvent` (`runtime/MainActivityRuntime.kt:4560`)
→ `NativeApp.setPadButton[ForPort]` → `applyPadButton()` (`native-lib.cpp:~1480-1562`)
→ `Pad::SetControllerState(port, key, value)`. Right-stick axes arrive as keycodes
110–123 with a 0..32767 magnitude.

Android delivers gamepad events **only to the focused activity**, which is ARMSX2.
That has two consequences:

- A background companion app can't read the Odin's sticks directly. Partially
  verified: this is standard Android behaviour; I haven't tested it on this device.
- ARMSX2 already feeds the right stick into the emulated DualShock 2, so the values
  end up in the game's pad buffer in EE RAM, where PINE can read them.

### 2.5 Runtime patching — Confirmed
PNACH patches and cheats are fully supported (`pcsx2/Patch.cpp`, Android
`ui/patches/PatchManager*`). The bundled `resources/patches.zip` already holds
widescreen PNACHes for both games:

| File | Label in file | CRC |
|---|---|---|
| File #1 | `[NTSC-J] (SLPM-65428)` | `03C8F393` |
| File #1 | `(ENGLISH PATCHED)` | `198EFDC1`, `32088394` |
| File #2 | `[NTSC-J] (SLPM-65692)` | `6FCA6FBD` |
| File #2 | `(ENGLISH PATCHED)` | `70C1FE97`, `DC0BFB60` |

These CRCs are community labels, so they count as **Partially verified** until we
read the CRC from a real dump (`pine_diag.py` prints it). `GameIndex.yaml` lists
both JP serials (`:43829`, `:45333`) without CRCs.

---

## 3. HeySnippy Modern Camera — dependency analysis

Unless marked otherwise, everything here comes from the mod page and from module
docstrings and strings inside v1.2.0.

| Question | Finding | Label |
|---|---|---|
| Source available? | No repo, licence or design doc published | Confirmed (none found) |
| Packaging | PyInstaller one-file, Python 3.12, SDL2.dll, pywebview + pythonnet UI, local `bottle` HTTP backend | Confirmed |
| Modules | `ro_pine`, `ro_patch`, `ro_camera`, `ro_camera_f2`, `ro_collision`, `ro_input`, `ro_pad`, `ro_addresses`, `ro_addresses_f2`, `ro_autopilot`, `ro_config`, `crosshair_overlay`, `server`, `launcher_ui` | Confirmed |
| Supported games | `SLPM-65428` (File #1 JP) and `SLPM-65692` (File #2 JP) | Confirmed |
| Game detection | Checks PINE `MsgID`, prints "expected …, got …". No CRC check found | Partially verified |
| Emulator detection | Calls `MsgVersion` but only seems to display it. No "must be PCSX2" check found | Partially verified |
| Transport | Windows: TCP `127.0.0.1:<slot>` with TCP_NODELAY. Linux/macOS: AF_UNIX `pcsx2.sock` | Confirmed |
| PINE ops used | Read8/16/32/64, Write8/16/32/64 (batched), Version, Title, ID, GameVersion, Status | Confirmed |
| Update loop | "runs as fast as PINE answers (200+ Hz)", camera thread at ~250 Hz, frame-rate-independent smoothing | Confirmed (docstrings) |
| Continuous writes? | Yes: writes the camera source buffer (`CAM_SRC`) every iteration | Confirmed |
| Injects PS2 code? | Yes, two kinds. (1) Patches existing code: NOPs and early `jr ra` returns on engine camera handlers (`ro_patch.CodePatcher`). (2) An injected **button code cave** (`VBTN_CAVE`) that the game's pad mapper runs on every read: it ANDs a keep mask (suppress) and ORs a press mask into the raw pad word. That is how the mod strips the native dash and presses its own synthetic one. *Corrected 2026-10-19; the first version of this report said no new routines were injected.* | Confirmed (docstrings) |
| Persists after exit? | Restores originals on Stop. If it crashes or the connection drops, the patches **stay in EE RAM** until you load a state or reboot. Originals are saved under `%LOCALAPPDATA%` for recovery | Confirmed |
| Input capture | Host devices: XInput, SDL2 GameController, Raw Input mouse, `GetAsyncKeyState`. It does **not** read the PS2 pad from RAM for camera control | Confirmed |
| Game-state gating | Takes over "only after a live character is confirmed", hands back on menus and loads (v1.1 changelog) | Confirmed (changelog) |
| Movement/aim | Writes movement input into game memory (`set_move_input`). Writes raw SCE pad words (`ro_pad`, "discovered live on SLPM-65428"). Suppresses game reactions to the right stick | Confirmed (docstrings) |
| Window dependency | Looks for the **PCSX2 window/process** (`GetForegroundWindow`, "is this a PCSX2 build", title fallback) to decide `pcsx2_focused` and mouse capture | Confirmed |
| Extras | Wall collision, "nearest authored angle" cut matching, crosshair overlay window, FOV/clip-plane writes | Confirmed |

---

## 4. Can the original Windows executable work through ADB forwarding?

| Layer | Expectation | Label |
|---|---|---|
| Socket transport | Mod → TCP `127.0.0.1:28011` → `adb forward` → ARMSX2 loopback 28011. Same family, same port | Partially verified |
| Wire protocol | Same PCSX2 PINE opcodes and framing | Confirmed (both sides' source/strings) |
| App identity | Version string says "ARMSX2". No gate on it found | Partially verified |
| Game identity | Serial comes from the same `GetDiscSerial()` code path as PCSX2 | Confirmed (ARMSX2 side) |
| **Controller** | The mod reads a pad attached **to the PC**. The Odin's built-in controls are invisible to Windows. For the test, plug a pad into the PC | Confirmed limitation |
| **"PCSX2 focused" gate** | No PCSX2 window will exist. Keyboard/mouse capture won't engage. It's unclear whether pad binds are also gated; if they are, the mod won't respond | **Unverified, possible blocker** |
| Latency | Every transaction crosses USB through adbd. The mod serialises several transactions per loop, so its rate drops and the camera may lag. The mod uses dt-scaled smoothing, so expect lag rather than breakage | Unverified, measure with `pine_diag.py --bench` |
| JIT invalidation of code patches | Same mechanism as x86 (§2.3) | Partially verified |
| Crash safety | A USB disconnect mid-session leaves the camera handlers NOPed. Make a save state before testing | Confirmed risk |
| English-patched ISOs | Same serial, different CRC. The mod's addresses may not match | Unverified, test retail JP only |

---

## 5. Technical obstacles (ranked)

1. **Closed-source camera logic and licensing.** Porting it means either the author's
   cooperation or a clean-room rewrite. Decompiling and re-hosting their bytecode
   would raise legal and ethical problems.
2. **Input acquisition on Android.** No focus means no direct stick access for a
   companion app. Workaround: read the game's raw pad words from EE RAM through PINE.
   ARMSX2 already writes the right stick there. The game's own right-stick reactions
   (ad-libs) must then be suppressed, which the mod already does on PCSX2.
3. **Unsynchronised writes.** PINE writes race the EE thread. This is acceptable on
   PCSX2, per the mod's own experience, but it can tear multi-word camera data.
4. **Code patching under the JIT.** Must be verified on device. Crash-safe restore
   must keep its originals in the companion app's storage.
5. **PINE is single-client.** The companion app takes the port, so it can't run
   alongside other PINE tools.
6. **No address documentation.** Every field in §6.1 of the brief is TBD for us.
   Nothing gets invented.
7. **Fork churn** (if Option B). ARMSX2 publishes nightly tags every day.

---

## 6. Native implementation alternatives

| Criterion | A: Android companion over PINE | B: Integrated ARMSX2 module | C: PS2-side MIPS patch |
|---|---|---|---|
| Fidelity to original | High: same architecture as the original mod (external rig + code freeze + streamed camera) | High | Medium: rig, collision and cut-matching must be rewritten in MIPS |
| Feasibility | Good, needs 2 device checks (§2.2 loopback, §2.4 pad read) | Good | Low–medium: no decompilation of the engine exists (per the mod's own notes), needs code caves, hooks and float math |
| Performance | Loopback round trip on device, likely sub-millisecond (unmeasured). One extra process | Best: runs in-process, can sync to vsync | Best: runs inside the game's own frame |
| Standalone (no PC) | Yes | Yes | Yes |
| Maintainability | Independent of ARMSX2 releases; depends only on the stable PINE ABI | Must be rebased onto fast-moving ARMSX2 | Independent of emulator, tied to game CRC |
| Controller integration | Indirect, via the game's pad RAM; tuning UI lives in the companion | Direct (`applyPadButton` hook) | Via the game's pad RAM |
| Future ARMSX2 compat | High | Low–medium | Highest (works in any emulator) |
| Complexity | Medium | Medium–high (C++/JNI/Compose + GPL fork) | High |
| Uninstall / disable | Uninstall the app | Rebuild without it | Toggle the PNACH |

---

## 7. Recommended architecture

> **Superseded on 2026-10-19 by §13.** A CC0 decompilation of both games turned up. It
> makes Option C (in-game MIPS) the primary route. The text below is kept as the original reasoning.

**Option A: a native Android companion app (Kotlin) using ARMSX2's loopback PINE
server.** Keep **Option B as the fallback** if either of these turns out true:

- Android can't reach the loopback port across apps, or
- reading pad state from RAM gives unacceptable latency or conflicts.

Why A:
- It reuses an interface ARMSX2 ships and supports (rule 2) and needs **zero
  emulator changes** (rule 3).
- It mirrors the original mod's own proven architecture, an external PINE client.
  So behaviour learned in Phase 2 carries over directly.
- Game-specific code stays fully outside the emulator (rule 4). Unrelated games are
  untouched, because the app only engages on the two whitelisted serial+CRC pairs.

Option C is worth revisiting later only for the *static* part, freezing the camera
handlers. That would become a PNACH instead of runtime patching, which removes the
crash-left-patched risk. It should not try to carry the whole rig.

**Precondition (non-technical):** contact HeySnippy (Discord or email listed on the
mod page) before any port. Their permission or collaboration decides whether the
Android version reuses their logic and address map or has to be a clean-room rewrite.

---

## 8. First proof-of-concept test (concrete steps)

**Automated:** `python tools/phase2_check.py` runs steps 2–7 (plus an on-device
loopback probe from the `shell` uid and an ARMSX2 version check). It then asks you
the step 9 questions and writes `results/phase2-<timestamp>.{json,md}`. The manual
steps below are what the script does, for reference or for running by hand.

Prerequisites: ARMSX2 **≥ 2.6.6.5** on the Odin 3 Max (check *About*; the PINE
section exists only in those builds). Also a Windows PC with platform-tools, a USB
cable, a pad attached **to the PC**, and a retail JP File #1 image.

1. ARMSX2: *Settings → Advanced → PINE (Remote Control) → Enable PINE*. The row
   should read `127.0.0.1:28011` and show the `adb forward` command.
2. Enable USB debugging on the Odin. On the PC: `adb devices` must list it.
3. `adb forward tcp:28011 tcp:28011`
4. Optional: `adb logcat | findstr PINE` should show `PINE: Listening on 127.0.0.1:28011`.
5. Boot File #1, reach a controllable character, then **make a save state** (it's
   your recovery point).
6. `python tools\pine_diag.py`. Expected: `ARMSX2 …`, `running`, `SLPM-65428`, and a
   CRC. **Record the CRC.**
7. `python tools\pine_diag.py --bench 2000` records the round-trip latency over ADB.
   Run the same command on-device later (Termux) to get the loopback baseline.
8. Close `pine_diag` (single client), then start `Outbreak-ModernCam.exe`, choose
   File 1, press Start.
9. Record, separately:
   - (a) whether the light turns green (connection + game ID),
   - (b) whether the camera moves behind the character (code patch + camera writes),
   - (c) whether the PC pad's right stick rotates it (input path, `pcsx2_focused` gate),
   - (d) the camera's smoothness under ADB latency.
10. Press Stop. Confirm the original fixed cameras come back. Unplug USB during a
    second run to observe the crash-left-patched case, then recover with the save state.
11. Repeat with File #2 (`SLPM-65692`).

Exit condition for Phase 2: each of 9a–9d is documented as works, fails (with the
reason), or blocked.

---

## 9. Files requiring inspection or modification

**ARMSX2 (inspect; modify only for Option B):**
- `pcsx2/PINE.cpp`, `pcsx2/PINE.h`: server, opcodes, transport
- `pcsx2/VMManager.cpp` (`ReloadPINE`, `GetDiscSerial`, `GetDiscCRC`)
- `pcsx2/vtlb.cpp` (`vtlb_ramRead/Write`, `HandlePageFault`, `mmap_ClearCpuBlock`)
- `pcsx2/arm64/iR5900-arm64.cpp`: code-page protection
- `platforms/android/app/src/main/cpp/native-lib.cpp` (`applyPadButton`): input hook point for B
- `platforms/android/app/src/main/java/com/armsx2/runtime/MainActivityRuntime.kt` (`dispatchGenericMotionEvent`)
- `platforms/android/app/src/main/java/com/armsx2/config/Settings.kt`, `ui/settings/FixesTab.kt`: PINE toggle
- `platforms/android/app/src/main/AndroidManifest.xml`: ARMSX2 holds `INTERNET`
- `pcsx2/Patch.cpp`, `resources/patches.zip`: for an Option C freeze PNACH

**This repository:**
- `tools/phase2_check.py`: automated Phase 2 runner (added). Tested only with a fake `adb` and a mock server.
- `tools/pine_diag.py`: read-only diagnostic client (added). I checked it against a
  local mock server written from `PINE.cpp`'s framing. It hasn't been run against real ARMSX2.
- Later: `docs/memory-map.md` (verified addresses only), and the companion app module.

---

## 10. Answers to §11 open questions

| # | Question | Answer | Label |
|---|---|---|---|
| 1 | Does current ARMSX2 Android expose usable PINE? | Yes: loopback TCP 28011, since 2.6.6.5 | Confirmed (source); device test pending |
| 2 | Does the exe work over ADB-forwarded PINE? | Transport and protocol match; input and focus gating are open | Partially verified |
| 3 | Extra PCSX2-specific checks? | Window/process "is PCSX2" check for focus/capture. No version gate found | Partially verified |
| 4 | Camera logic public? | No | Confirmed (none found) |
| 5 | Continuous memory manipulation required? | Yes, ~200–250 Hz camera writes plus one-time code patches | Confirmed |
| 6 | Controller input without Windows app? | Possibly, by reading the game's pad RAM via PINE (A) or hooking `applyPadButton` (B) | Unverified |
| 7 | Injected routines to port? | Code patches (NOP / `jr ra`) **and** the `VBTN_CAVE` button-mask cave | Confirmed (docstrings) |
| 8 | Can a native Android app access the IPC endpoint? | Expected yes (loopback TCP plus INTERNET permission) | Partially verified |
| 9 | Could a game-side patch replace the controller? | Only the camera freeze, realistically; the rig would be a MIPS rewrite | Unverified |
| 10 | Fork maintenance cost? | High: daily nightlies, large active tree. Avoided by Option A | Confirmed (tag history) |
| 11 | Licensing constraints? | ARMSX2 GPL-3.0+ (a fork must publish source). The mod is closed and unlicensed, so reuse needs permission | Confirmed |
| 12 | Breaks networking/events? | The author says it is view-only online (obsrv). Untested on ARMSX2 DEV9 | Unverified |

## 11. Unresolved questions
- Actual CRCs of the user's dumps (from step 6).
- Whether `pcsx2_focused` gates pad binds as well as keyboard/mouse (from step 9c).
- Real ADB and loopback latency numbers (from step 7).
- Whether a second app on the Odin's Android build can connect to `127.0.0.1:28011`
  (needs a trivial test APK or Termux).
- HeySnippy's position on an Android port.
- Whether File #2 addresses are fully calibrated: one mod docstring marks a File #2
  field `[TODO] calibrate live`.

---

## 12. Phase 2 field observations (2026-10-19, user's Odin 3 Max, ARMSX2 2.8.4, Android 15)

| Observation | Result | Label |
|---|---|---|
| PC → `adb forward` → ARMSX2 PINE | Works after the on-device probe fix (earlier failures came from this repo's own probe holding PINE's single client slot) | Confirmed (user run) |
| Mod connects and drives the camera | Yes, but described as "wonky". Symptom not classified yet | Partially verified |
| Running / sprint | **Does not work** | Confirmed (user report) |

**Running: probable cause (Partially verified, from mod docstrings).** The mod strips the
game's native dash bit through `VBTN_CAVE`. It then presses a synthetic dash only while
its sprint toggle is on **and** it sees the player moving. It judges "moving" from the
**host (PC) pad's** left stick and D-pad. In this setup, movement comes from the Odin's
stick straight into the game, so the mod thinks the player is standing still. The
synthetic dash never fires, and the native one is suppressed. The mod's own notes
describe the same failure for D-pad players before v1.1.

Confirming test: move with the PC pad's left stick and check whether sprint works.

**Design consequence for Option A:** the companion app must source *all* player input
(left stick, D-pad, buttons, right stick) from the game's **raw** pad word in EE RAM,
read before the cave applies its masks. Reading a host device won't work. On ARMSX2
that raw word already carries the Odin's built-in controls.

---

## 13. The Outbreak decompilation changes the recommendation (2026-10-19)

Source: [GalaxyNostalgia/Outbreak-decomp](https://github.com/GalaxyNostalgia/Outbreak-decomp),
cloned at `eb6af38` (2026-10-19). Licence **CC0 1.0** (public domain). It contains no
game code; you build it from your own disc.

**Coverage.** It targets File #1 `SLPM-65428` VER **1.04** and File #2 `SLPM-65692` VER **1.03**,
plus the English fan patch v2.1. Every target rebuilds byte-identical from asm. File #1 has
238 functions decompiled and matching, plus 77 written but not yet matching. **File #2's C is
not started** (`config/SLPM_656.92/symbol_addrs.txt` is empty), though about half its code is
shared with File #1.

### 13.1 Facts it gives us (File #1 VER 1.04)

| Field | Address / offset | Type | Decomp source | Live-verified |
|---|---|---|---|---|
| Camera | `0x00306320` | `Camera` struct | `include/camera.h` | Yes (live run 2026-10-19, 598 samples) |
| … position / target / up | `+0x18` / `+0x24` / `+0x3C` | `f32[3]` | same | Yes (live run 2026-10-19, 598 samples): 272 distinct positions, up = (0,1,0), target at the player's head |
| … far / near / FOV | `+0x58` / `+0x5C` / `+0x60` | `f32` (FOV in degrees) | same | Yes (live run 2026-10-19, 598 samples): 50000 / 1.0 / 55 |
| … roll | `+0x6C` | `s32` | same | No |
| View matrix | `0x003062E0` | `Mat44` | same | No |
| **Camera → renderer, once per frame** | `func_00345740` (SUBMAIN overlay); builds the view in `func_00345870` | code | `src/.../submain/ingame.c:79-113` | No |
| Raw pad, this frame | `0x00292D40` | `PadData`: buttons `u16 +2` (active low), sticks `u8[4] +4` | `include/pad.h` | Sticks yes (127 at rest, 65 distinct). Button encoding not yet understood |
| Controls per pad | `0x00306410` | `Ctrl` (0x30): stick angle/magnitude, button history | `include/ctrl.h` | No |
| Players | `0x00476DD0`, stride `0x10E0`, count at `0x0023BE14` | `DrawObjFull[]` | `include/object.h`, `game/talk.c` | Yes (live run 2026-10-19, 598 samples): count 3 offline |
| … position / heading / own controls | `+0x38` / `+0x92` / `+0xF78` | `f32[3]` / `s16` (0x10000 per turn) / `Ctrl` | `include/object.h` | Position and heading yes (254 / 203 distinct). Controls pending analysis |
| Camera cut this frame | `0x003AEF54` | `u8[]` | `game/charmove.c:2455` | No |
| Room cameras | `RoomCam` (0x198 bytes), 32 per room, type 8 chains into paths | struct | `game/roomcams.c` | No |

The game also has built-in "keep the stick direction across a camera cut" logic
(`DrawObjFull.unkFA8/unkFAA`, `game/charmove.c:2560-2580`). Camera-relative movement would hook in there.

### 13.2 Revised recommendation: Option C first, with a PC prototyping loop

1. **Verify** the table above on the user's disc (`tools/f1_live.py`, read-only).
2. **Prototype from the PC over PINE** (Python): compute an over-the-shoulder camera from
   player 0's position and heading plus the right stick in the raw pad. To avoid racing the
   engine, install one small hook at the top of `func_00345740`. When an "override" flag is
   set, it copies our camera block into `0x00306320` just before the game hands the camera
   to the renderer. The PC then writes only the override block, and timing no longer matters.
3. **Move the camera math into the hook** as MIPS (C compiled for R5900, loaded with a PNACH).
   The finished mod then runs entirely inside the game.

Why this beats both earlier options:
- No companion app, so Android can't freeze it (field-observed in §12).
- No ADB or PINE latency.
- It updates exactly once per rendered frame.
- It reads the Odin's real controls, because the game itself reads them.
- No ARMSX2 changes, and it works on any PS2 emulator.
- No dependence on HeySnippy's code. Everything we use is CC0 or our own.

### 13.3 New risks

| Risk | Label |
|---|---|
| Decomp addresses assume VER 1.04 and an unmodified ELF. Fan-patched (English) builds change `SLPM_654.28` and `GAME.BIN` | Confirmed (`config/versions.json`) |
| Online (obsrv) the server pushes a 0x7AA0-byte v1.01 code patch, so code addresses may move online | Confirmed (`config/versions.json` note); impact unverified |
| The hook lives in the SUBMAIN overlay, so it must be re-applied or checked whenever that overlay loads (conditional PNACH) | Partially verified |
| Free EE RAM for the code cave isn't identified yet | Unverified |
| File #2 has no decompiled C or symbols yet | Confirmed |

### 13.4 First live run (results/f1live-20261019-191951.json, 598 samples / 60 s)

- **Build:** `SLPM-65428`, disc version 1.04, CRC **`32088394`**, which the bundled PNACH labels as
  *English patched*. The camera, pad and player data addresses still matched (§13.1), so the
  English patch leaves that main-ELF data where it was. The patch does change the ELF code,
  `DEMO.BIN`, `GAME.BIN` and `NETWK.BIN`, but **not `SUBMAIN.BIN`**, which holds the camera hook
  (`config/versions.json`). `tools/f1_hookcheck.py` checks the hook site in the running build.
- **Heading convention (Confirmed, device + data):** characters face along the heading field,
  `heading_rad = atan2(dx, dz)` (0 = +Z, 0x4000 = +X, 0x10000 per turn). On the device,
  `f1_camtest run --yaw-offset 0` shows the character's back and `180` shows the face. *History:
  an interim "faces heading + 180°" correction was wrong. It came from an early test that was
  probably a stale `run` process, before the tool printed its settings.*
- **Right-stick angle (Partially verified, few samples):** `Ctrl.rangle` is 0 = up, 0x8000 = down,
  counter-clockwise positive. The game computes it from the raw stick, so it can drive the
  camera directly.
- **Raw button field (Confirmed):** reads 0 at rest, so as *stored* it is active-high. The header's
  "active low as read" describes the hardware read before the game flips it. Bit-to-button mapping not done yet.
- **Camera:** 465 units from the player (median, fixed room cameras), target ~100 above the
  player's origin. 10 cuts in 60 s. The "camera cut" flag at `0x3AEF54` read 0 throughout:
  **Unverified** (wrong address, or a one-frame pulse that 10 Hz sampling misses).
- **PINE through ADB:** one full sample (7 batched reads) took about 5 ms.

### 13.5 First write test: render-time override hook (`tools/f1_camtest.py`)

- **Hook site:** `func_00345740` (SUBMAIN), verified in the user's build by `f1_hookcheck.py`:
  prologue `0x27BDFFF0`, calls in the order the decomp gives.
- **Mechanism:** the first two instructions are relocated into a cave, and the site becomes
  `j cave ; nop`, written as one aligned 8-byte store. The cave does three things:
  - increments a call counter;
  - when its flags say so, copies our pos/target (and optionally FOV) into the camera at `0x306320`;
  - replays the two original instructions and jumps to `func_00345740 + 8`.
- **Registers:** the cave uses only `t0`–`t3`. The function takes no arguments, and the MIPS
  calling convention doesn't preserve `t` registers across a call.
- **Where the cave lives:** `freemem` samples `0x80000`–`0x100000` during play and picks a
  region that stayed zero. That this region is unused is **measured, not assumed**.
- **Verification before the device:**
  - every encoding is cross-checked against LLVM `llvm-mc`;
  - the cave runs on an interpreter for all four flag combinations (`tools/test_mips.py`);
  - the full freemem/install/status/run/uninstall flow runs against a mock PINE with 32 MB RAM.
- **Not yet tested on the device.**

### 13.6 Device results for the override hook (2026-10-19)

| Check | Result | Label |
|---|---|---|
| Hook installs and is called every frame | `install` at `0x00084000`, about 60 calls/s | Confirmed (device) |
| Our camera reaches the screen | Yes | Confirmed (device, user) |
| Whose camera | `0x48BF7D` ("our player number") = 0 offline, so slot 0 was the user's character all along | Confirmed (device) |
| Facing vs heading field | `--yaw-offset 0` shows the character's back, `180` the face. He **faces along the heading field** (now the default) | Confirmed (device, user) |
| Framing | Over-the-shoulder view confirmed in J's Bar with defaults at `--yaw-offset 0` (camera over the **left** shoulder; `--shoulder -35` gives the right). Right-stick orbit and recenter fixes (`97fe870`) not yet tested on the device | Confirmed (device, screenshot) |
| Room collision data (`0x3938A8`, CollTri layout) | `walls`: two 9 x 5 grids of 300 x 300 cells; 181 triangles read, **0 failed the plane check**; wall distances 101–505 around the player, one direction clear | Confirmed (device) |
| Camera jerk (stale absolute camera) | Gone after the hook started adding the player's position every frame (`489948e`) | Confirmed (device, user) |
| Aim state and target | Aiming = action 2 (`+0x08`) confirmed: `aiming`/`aim released` track the weapon. Auto-aim targets (`+0x24`) seen: all `type 2 (enemy), kind 0x01` in the thing pool (`0x3FFCA0`), slots 47/49; no partner targeted in that run. Type map 0 player / 1 NPC / 2 enemy: the enemy case is confirmed, player/NPC not yet observed | Partially verified (device) |
| Hook call rate | ~60/s normally; ~120/s in some states (called twice per frame, harmless); 0/s for a few seconds during loads | Confirmed (device) |
| Ceilings in the collision data | Stairs probe: straight down hits the step (grid 1), straight up and every behind-and-rising ray are clear, so **ceilings are not in the collision data**. Grid 0 = walls (kind 0), grid 1 = floors/stairs (kinds 0 and 1). The camera is held within `--headroom` (190) of the floor under it instead | Confirmed (device) |
| Aim elevation field (`f1_aimscan`) | Player `+0x0BC8` (s16, 0x10000 per turn): level 0, up +8191 (≈ +45°), down −8191. Also `+0x0BCC` (0x100 / 0 / 0x200: pose index?) and `+0x0CD2` (0x20 / 0 / 0x70). `+0xF80`–`0xF8C` matched too but are the controls (`Ctrl` at `+0xF78`: 0x400 aim, 1 up, 2 down). How it changes over time and which code writes it are not yet known | Confirmed (device, scan) |
| Aim elevation servo (`f1_aimscan watch`) | Holding up/down moves `+0xBC8` by ±1170 (6.4°) per logic tick, clamped at ±8191 (45°); released, it returns toward 0 by 2730 (15°) per tick; a tick every ~33 ms (30 Hz logic). `+0xBCC` is the pose index (0 up, 1 level, 2 down). A written value would be dragged back each tick, so the servo must be neutralised | Confirmed (device) |
| Code touching `+0xBC8` (`f1_aimscan writers`, user's English-patched build) | 54 loads/stores in the GAME overlay. Servo candidates: a load/modify/store cluster at `0x556860`–`0x5569E8`, a similar one at `0x648C78`–`0x648CA4`. Many `sh $zero` resets; readers (pose/shot candidates) at `0x5A6B98`, `0x5C5170`, `0x646C04`, … Addresses are for this build only (GAME.BIN differs in the English patch) | Partially verified (device, static) |
| Free EE RAM | `0x00084000`–`0x00100000` stayed zero for 720 passes (60 s of play) | Confirmed (measured) |

This agrees with the f1_live travel fit (`atan2(dx, dz)`), so the "back-step" reinterpretation
in an interim commit was wrong. At offset 0 the right stick felt reversed ("southpaw"):
`--invert-x` is being checked.
