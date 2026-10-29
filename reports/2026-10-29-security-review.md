# Security review: online play and the repository (2026-10-29)

Read-only review of the net-online work (worktree `netplay-research`, commit `9ab3106`) after local play landed, at the
developer's request ("prioritize security since we are now dealing with network connectivity"). Three reviewers read
the code in parallel: the server (`native/ob_server`), the client side (Android app, JNI, the emulated network card,
the Windows menu, upstream patches) and repository hygiene. Each finding below marked **verified** was checked
against the code or git by hand afterwards; the rest are the reviewers' reading, not re-checked.

Accepted constraints (not findings): the PS2 client only speaks SSL 3.0 / TLS 1.0 with 3DES/AES-CBC and has no real
login, so anyone on the same network can read or alter login traffic. The server compensates by staying on the local
network (guard.h) and holding nothing secret.

## Priority 1: repository

| # | Finding | Status |
|---|---|---|
| R1 | **Game data and a third-party .exe are in git history and on GitHub.** `bb0c282` "Added Game_Files to repo" (SLPM_654.28, prog/*.bin, IOP modules incl. `dnas280.img`), `e012448` (`Outbreak-ModernCam-v1.2.0 (1).exe`, 24.5 MB), `a8f3ff0`. `de60c78` only ignored the folder. The commits are on `origin/main` and `origin/netplay-research` (`https://github.com/JohnnyCollado/outbreak-modern`). | **Verified.** The GitHub repo is **private**, 0 forks, 0 stars (`gh repo view`, 2026-10-29). |

Fix: rewrite history without `game_files/` and the .exe (`git filter-repo`), then force-update GitHub. Needs the
developer's explicit go-ahead (a force push). Must happen before the repo is ever made public.

## Priority 2: the built-in server (any device on the same Wi-Fi while hosting)

| # | Sev. | Finding | Where | Status |
|---|---|---|---|---|
| S1 | Med-high | **One slow peer blocks all logins.** The login site serves one connection at a time; the timeout is per read, so a byte every 4 s holds it indefinitely; Stop waits for it (on Android under the JNI lock). The 10 s timeout is overwritten by 5 s two lines later. | `tls_server.cpp:163,168,227-387` | **Verified** (single thread, both timeouts) |
| S2 | Medium | **Output buffers grow without limit.** A lobby or relay client that never reads keeps receiving answers/frames; memory grows until the phone runs out. | `lobby.cpp:151`, `relay.cpp:86-88` | Reviewer |
| S3 | Medium | **A dropped lobby client that stops reading is never removed and spins the lobby thread at 100 %.** `dropClient` leaves `out` filled; the reap needs it empty. Its guard slot leaks. | `lobby.cpp:366-378,1006` | **Verified** (`dropClient` never clears `out`) |
| S4 | Medium | **No deadline before login.** The connection check skips clients not logged in, so silent connections hold slots forever: four addresses fill all 16. Relay's 60 s idle resets on any byte. | `lobby.cpp:889`, `relay.cpp` idle | **Verified** (`!c.loggedIn` skipped) |
| S5 | Medium | **Strings sent to other players have no length cap** (nickname, chat, room name/password). Packets over 65,535 bytes are silently truncated by `encode`, desyncing every other player's stream; long names reach the other games' code (effect unverified). | `lobby.cpp:461-491,606-680`, `protocol.cpp:16` | **Verified** (`encode` casts size to 16 bits) |
| S6 | Med-low | **A session number lets one player act as another.** Lobby/relay accept any valid session from any address and kick the owner; numbers are 8 digits from an mt19937 seeded by the clock, and travel in plain TCP. | `lobby.cpp:388-399`, `relay.cpp:63-81`, `accounts.cpp:8` | **Verified** (clock seed; accounts keyed by address) |
| S7 | Low | Beacon list can be filled with 32 spoofed hosts and never ages out until search restarts. | `beacon.cpp:188-190` | Reviewer |
| S8 | Low | Peer text logged raw (up to 64 KB, newlines); a peer can flood logcat; session numbers are logged. | `lobby.cpp:436,866`, `tls_server.cpp:357`, `ob_server.cpp:37` | Reviewer |
| S9 | Low | Missing null checks in lobby `connectedAddresses`/`removeAddress` and relay `removeAddress` (not reachable today). | `lobby.cpp:1035-1046`, `relay.cpp` | Reviewer |
| S10 | Low | Any private address is accepted, not just the host's own subnet (a hotel/campus 10.x network is "local"). | `guard.cpp:39-46` | **Verified** |

## Priority 3: the app and the emulated network card

| # | Sev. | Finding | Where | Status |
|---|---|---|---|---|
| C1 | Medium | **A malformed memory card can make the card writer read/write outside its buffer.** `LoadFormatted` checks only `ppc` and `allocEnd`; `alloc`, `root`, `ifc[]` and FAT values index memory unchecked. Reached at game start with online play on (writing BWNETCNF). | `memory_card.cpp:330-372,430-455` | **Verified** (no range checks) |
| C2 | Medium | On Android 10 only (unverified), other apps with storage permission may write the app's external folder, which holds `drivers/` (loaded as native code) and `net.env`. Android 11+ is safe. minSdk is 29. | `Settings.kt:14`, `gpu_driver_android.cpp:361-372` | minSdk 29 **verified**; Android 10 access unverified |
| C3 | Low | **Race on the network interrupt timer:** the receive thread and the IOP thread both write `s_irqIn` unsynchronised; a received-frame interrupt can be lost until the next frame. Also a latency suspect (entry 64). | `dev9_side.cpp` | **Verified** (plain `int64_t`, two threads) |
| C4 | Low | PCSX2 patch 0003 skips binding the adapter address for **any** 127.x destination, not only remapped ones: the emulated PS2 could reach the PC's loopback-only services if it ever sent there. | `TCP_Session_Out.cpp:168-170` | **Verified** |
| C5 | Low | Card save names and ini values passed to `NewStringUTF` unchecked (invalid UTF-8 aborts debug builds). | `android_menu.cpp:137,155` | Reviewer |
| C6 | Low | Zip import has no limit on the expanded size (zip bomb fills storage). | `Importer.kt:97-112` | Reviewer |

## Priority 4: hygiene

- mbedTLS 2.28.10 is end of life (`docs/ops/pins.md:27-30`); its source is checked only for `version.h`, not the pinned
  commit. PCSX2 clone by tag; Gradle wrapper without `distributionSha256Sum`.
- The Odin's Tailscale address in `docs/ops/android.md:134-138`; home LAN addresses in docs/tests; the personal email
  in five commits and in `upstream/*/*.patch` headers.
- `.gitignore` lacks `*.iso`, `*.elf`, `*.ps2`, `*.keystore`, `net.env`, logs (all currently kept outside the repo).
- `native/ob_server/LICENSE.md`: the AGPL text is still to be added; anyone hosting for others owes the source.

## Checked and fine (summary)

No private key or token anywhere in history; the TLS key is generated in memory at each start (nothing to steal from
the app). Debug keystore outside the repo with the standard public password. Every network listener checks the guard
before reading; admit/release balanced; Remove covers all three services. Stream splitters, session-digit decoding,
info chunks, web request parsing, beacon decoding: bounded. Android: only the launcher exported, `HostService` not
exported, immutable explicit PendingIntent, `allowBackup=false`, no cleartext config; zip slip blocked; every typed
network value validated in Kotlin and C++. PCSX2's DHCP/DNS servers open no host sockets. Build downloads in
`pcsx2-deps*.bat` are SHA-256 checked; Gradle repositories restricted to Google and Maven Central.

## Fixed (2026-10-29)

| # | Fix | Commit | Test first |
|---|---|---|---|
| S1 | The login site cuts each connection at 15 s and notices a stop within 250 ms; one read timeout (5 s) | `e8f2a29` | trickling peer not cut: 193/194 -> 194/194 |
| S2-S4 | Lobby and relay: 15 s login deadline, 256 KB pending-output cap, dropped lobby clients removed after 2 s and their input discarded | `7aa0fb3` | build failed without the settings; 181/181 |
| S5 | String caps (handle 6, nickname 32, chat 128, room name 32, password 16, event 8192 bytes; refused, not cut); `encode` refuses payloads over 65,535 | `fafe91a` | build failed without the cap argument; 184/184 |
| S6 | A session works only from the address its account is named for; sessions seeded from `std::random_device` | `cef4a89` | build failed without the setting; 192/192 |
| C1 | Card writer: layout fields and FAT values outside the card refused, nothing saved; bounded directory scans | `c547517` | a corrupted card crashed the test program; 173/173 |
| C3 | DEV9 interrupt requests from the receive thread posted atomically | `7250f0b` | no failing test possible for the race on MSVC; adapter 30/30 |

Full suite after the fixes: ps2x 577/577, iop 5/5, gs_cache 45/45, ob_diag 24/24, pcsx2_vu 3/3, pcsx2_gs 92/92, spu2 7/7,
ipu 22/22, sio2 20/20, ob_server 194/194, dev9 30/30, ob_ui 173/173, Kotlin 61/61. Not yet run on a device.
Then the queue (`docs/plans/2026-10-29-security-queue.md`): S7-S10, C2, C4-C6 and H1-H5 fixed with tests first; R1
prepared and verified on a copy (publishing needs a force push: the developer's call); H6 needs the official AGPL text
from the developer.
