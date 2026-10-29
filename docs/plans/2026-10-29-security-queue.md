# Security queue: the open findings, highest priority first (2026-10-29)

From `reports/2026-10-29-security-review.md` (S1-S6, C1, C3 already fixed). The developer's rule for each step: a plan
of attack, the tests written before the code, a list of what the change touches and what must be tested; move on only
when every test is green, there are no warnings and the issue is addressed. Device runs wait for the Odin.

Order: R1 (high) -> C2 (medium) -> S10, S7, C4, C5, C6, S8, S9 (low) -> hygiene H1-H7.

---

## R1 (high): game data and a third-party .exe in git history (private GitHub repo)

- **Plan:** rewrite history on a **mirror copy** (`C:\ob\purge\outbreak-modern.git`), never the working repo:
  `git filter-repo --invert-paths` for `game_files/`, `Outbreak-ModernCam-v1.2.0 (1).exe`, with a mailmap turning the
  five commits authored with the personal address into the noreply one. Verify the copy, then hand over. **Replacing GitHub's history
  is a force push: only on the developer's explicit go-ahead.** Afterwards every worktree and checkout re-clones.
  Candidates for the developer to decide (not removed by default): `PS2Recomp-main/` Vita modules, `docs/assets/`
  Capcom artwork, `legacy/results/` game screenshots.
- **Touches:** every commit hash on every branch (`main`, `netplay-research`, `android-touch-controls`, the remote
  branches); worktrees under `.claude/worktrees/`; report references to commit hashes (they become stale).
- **Tests (script `tools/repo/purge-check.sh`, run on the copy):**
  1. no object path under `game_files/` and no `.exe` in `git rev-list --objects --all`;
  2. no personal address (passed to the script, never stored in the repo) in `git log --all --format=%ae%n%ce`;
  3. each branch's tree at its tip equals the original tip's tree minus the removed paths (`git diff --stat` empty
     apart from them);
  4. the same number of commits on each branch (none dropped by mistake).
- **Tool:** `git-filter-repo` is not installed (Python module missing). Installing it is a software install: needs
  the developer's go-ahead. Until then R1 stays "prepared".

## C2 (medium): Android 10 lets other apps write the app's external folder

- **Plan:**
  1. `net.env` is read only by a debuggable build (`ApplicationInfo.FLAG_DEBUGGABLE`): `gameEnv` gets `devAllowed`.
  2. Graphics drivers live in **internal** storage only: the importer writes `filesDir/drivers` (internal); the
     native loader stops syncing from the external folder (`gpu_driver_android.cpp` `ObGpuSelectDriver`); the
     Graphics driver page lists the internal folder. One-time move of existing external driver folders, **only on
     Android 11+** (where that folder is private); on Android 10 the player re-imports.
- **Touches:** `NetPlay.kt` `gameEnv`, `OutbreakActivity.kt`, `Importer.kt` (drivers destination), `Installed.kt`
  and the driver page (`SettingsModel.kt`), `DriverNames.kt` `repairDriverFolders` (folder), `gpu_driver_android.cpp`.
- **Tests (Kotlin, JVM):** `gameEnv` ignores net.env when not debuggable; `driversDir()` is the internal folder;
  `migrateDrivers` moves folders on API 30+, leaves them on API 29; (native) the loader has no external path left
  (code inspection + the Odin run: a driver imported after the change still loads).
- **Residual (documented):** outbreak.ini, mods and textures stay in the external folder (players manage them by
  USB); on Android 10 another app could change them. They don't run native code.

## S10 (low): any private address is accepted, not just the host's own network

- **Plan:** `ConnectionGuard` gets an optional subnet (address + prefix). `LocalHost` sets it from the Wi-Fi link
  (Android: `LocalLink.prefix` through JNI; Windows: `findLocalNetwork`). Loopback (this device) stays allowed.
- **Touches:** `guard.h/.cpp`, `host.h/.cpp`, `ob_server.h/.cpp` (limits carry the subnet), `host_jni.cpp`
  (`hostStart` gains `prefix`), `NativeBridge.kt`, `HostService.kt`, Windows `settings_ui.cpp` `StartHosting`.
- **Tests:** guard with subnet 192.168.0.0/24: 192.168.0.9 admitted, 192.168.1.5 refused, 10.0.0.2 refused,
  127.0.0.1 admitted; no subnet set = today's rule; `LocalHost` with prefix 24 refuses another subnet's connection.

## S7 (low): the nearby-games list can be filled with fake hosts

- **Plan:** the listener drops entries older than `maxAge` before checking the 32 cap, so stale fakes age out.
- **Touches:** `beacon.cpp` `BeaconListener` receive loop.
- **Tests:** fill the list with 32 beacons from 127.0.0.2..33, wait past maxAge, a beacon from 127.0.0.1 is listed.

## C4 (low): PCSX2 patch 0003 lets the game reach this PC's loopback-only services

- **Plan:** skip binding the adapter's address for a loopback destination **only when a remap entry produced it**
  (`HostRemap::Apply` reports whether it matched).
- **Touches:** `upstream/pcsx2/0003` (and 0004 on top), `C:\ob\pcsx2` tree, `native/pcsx2_dev9` (rebuild).
- **Tests (pcsx2_dev9):** a TCP SYN from the game to 127.0.0.1:port **without** a remap entry never reaches a
  loopback listener; with an entry (existing test) it does.

## C5 (low): invalid text handed to Android's `NewStringUTF`

- **Plan:** a core helper turns any bytes into valid Modified UTF-8 (invalid sequences and 4-byte characters become
  `?`); every `NewStringUTF` of card or ini text goes through it.
- **Touches:** `native/ob_ui/core` (new helper), `android_menu.cpp` (:137, :155 and any other text from files),
  `host_jni.cpp` (beacon names are already ASCII; passed through anyway).
- **Tests (ob_ui):** ASCII unchanged; valid 2- and 3-byte UTF-8 unchanged; lone 0x80, truncated sequences, overlong
  forms, 0xFF and a 4-byte emoji become `?`; a NUL byte becomes `?`.

## C6 (low): zip import has no limit on the unpacked size

- **Plan:** a byte budget while extracting: stop when the unpacked total passes the free space or 100x the zip's size
  (whichever is smaller); the partial folder is removed.
- **Touches:** `Importer.kt` zip loop (factored into a testable `extractZip(input, target, budget)`).
- **Tests (Kotlin JVM, in-memory zips):** a normal zip extracts whole; a 1 KB zip that unpacks to 10 MB with a 1 MB
  budget stops and leaves nothing; the existing zip-slip refusals still hold.

## S8 (low): peer text logged raw, log flooding, session numbers in logs

- **Plan:** `logSafe(text, 64)`: printable ASCII, other bytes as `\xNN`, cut at 64 with `...`; used for every peer
  string in a log line (version string, web request line, names). Unhandled-question lines limited to 10 per client.
  Session numbers logged as `****1234`.
- **Touches:** `lobby.cpp` (CHECKVERSION, unhandled), `tls_server.cpp` (request line), `ob_server.cpp` (quick login),
  a new helper in `protocol` or `guard`.
- **Tests:** `logSafe` cases (newline, 0x00, 0xFF, 200 bytes); a lobby client sending 50 unknown questions makes at
  most 10 log lines; the quick-login line has no full session number.

## S9 (low): missing null checks (not reachable today)

- **Plan:** `LobbyServer::connectedAddresses/removeAddress`, `RelayServer::removeAddress` return safely when stopped.
- **Tests:** calling them on a never-started and a stopped server does not crash.

## Hygiene

| # | Item | Plan | Tests |
|---|---|---|---|
| H1 | mbedTLS 2.28.10 is end of life; only `version.h` is checked | Stay on 2.28 (3.x removed TLS 1.0, which the PS2 needs: the review's note). CMake checks the source is the pinned commit `2fc8413...` (`docs/ops/pins.md`) when it is a git checkout, else the version string | configure with the right tree passes; a script check (`tools/net/check-pins.sh`) reports the commit |
| H2 | PCSX2 cloned by tag only | `ob.sh` checks `rev-parse HEAD` against the pinned commit after cloning | `check-pins.sh` |
| H3 | Gradle wrapper without `distributionSha256Sum` | Add the checksum of gradle-9.7.1-bin.zip (from the zip already downloaded, cross-checked against gradle.org's published sum) | the package step still builds (Gradle verifies the sum) |
| H4 | Personal data in tracked files | Replace the Odin's Tailscale address in `docs/ops/android.md` with `<odin-ip>:<port>`; the personal address in `upstream/*/*.patch` headers becomes the noreply address | `tools/repo/personal-data-check.sh` |
| H5 | `.gitignore` gaps | Add `*.iso`, `*.elf`, `*.ps2`, `*.keystore`, `*.jks`, `net.env`, `*.log` outside `gate/evidence`, `build/` | `git check-ignore` on sample paths; `git status` unchanged |
| H6 | AGPL text missing for `native/ob_server` | Add `native/ob_server/COPYING.AGPL` (the official text) and a source note in the app's About/Network page | file present; needs the official text from a trusted local copy (no download: research safety rule) |
| H7 | `tools/net/up-without.sh` deletes and resets outside the repo | Left as is (by design, guarded); documented | none |

## Progress (2026-10-29)

| Item | State | Commit | Test first -> green |
|---|---|---|---|
| R1 | **Prepared and verified; waiting for the developer.** `tools/repo/purge-history.sh` (built-in filter-branch, no install) built a clean mirror in `C:\ob\purge\outbreak-modern.git` (41 -> 15.6 MB); `purge-check.sh` all ok: no game_files/.exe, no personal address, every branch's tree and commit count unchanged. Publishing = force push + every checkout re-clones: needs the go-ahead, run again right before | - | check failed on the real repo, passes on the copy |
| C2 | Done: drivers internal only (migration on Android 11+), net.env only in debuggable builds. Driver load still to see on the Odin | `f1d64a5` | Kotlin unresolved -> 64/64 |
| S10 | Done: only the host's own subnet (and loopback) may join | `7501ece` | build failed -> 200/200 |
| S7 | Done: quiet hosts age out of the 32-entry list | `3455a31` | 201/202 -> 202/202 |
| S9 | Done: lobby/relay safe when not running | `016aad1` | crash -> 204/204 |
| S8 | Done: logSafe, 10 unknown-question lines per client, masked sessions | `2199f77` | build failed -> 212/212 |
| C4 | Done: PCSX2 0005 refuses unmapped loopback connections | `e2f199f` | 30/31 -> 31/31 |
| C5 | Done: javaSafeText before every NewStringUTF of file text | `24c2c8b` | helper missing -> ob_ui 182/182 |
| C6 | Done: extractZip with a byte budget and canonical-path check | `b736a20` | unresolved -> Kotlin 69/69 |
| H1 | Done: the build accepts only the pinned mbedTLS commit | `5a8a8f2` | check missing -> 3/3 |
| H2 | Already in place: `ob.sh` checks the PCSX2 clone and its base | - | `check-pins.sh` ok |
| H3 | Done: Gradle distribution SHA-256 pinned (two gradle.org sources agree); checked on the next fresh download | `43720fa` | build still runs |
| H4 | Done: no Tailscale address or personal mail address tracked; scripts take the address as OLD_EMAIL | `dc6d204` | `personal-data-check.sh` failed -> ok |
| H5 | Done: .gitignore covers discs, ELFs, cards, keys, net.env, logs, build dirs | `178f0ab` | 9 fails -> all ok |
| H6 | **Waiting for the developer:** the official AGPL text (no local copy; no downloads by rule) as `native/ob_server/COPYING.AGPL`; the source link for players once the source's public home is decided | - | - |
| H7 | No change (by design), documented | - | - |

Full suite after the queue: ps2x 577/577, iop 5/5, gs_cache 45/45, ob_diag 24/24, pcsx2_vu 3/3, pcsx2_gs 92/92, spu2 7/7,
ipu 22/22, sio2 20/20, ob_server 212/212, dev9 31/31, ob_ui 182/182, Kotlin 69/69; Windows and Android builds 0
warnings. Device checks (Odin): driver load from internal storage, local play and fan server with all fixes.
