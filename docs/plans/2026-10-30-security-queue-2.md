# Security queue 2: review 2's findings, highest priority first (2026-10-30)

From `reports/2026-10-29-security-review-2.md`. Same rule: plan, tests first, list what is touched; move on when green
with no warnings. P1's rewrite runs last, since it changes every commit ID; publishing is the developer's force push.

| # | Plan | Touches | Tests (written first) |
|---|---|---|---|
| P2 | `purge-check.sh` fails when git fails; searches file contents in all of history, not just commit fields | `tools/repo/purge-check.sh` | wrong path = exit non-zero; the current repo fails on contents |
| P1 | `scrub-history.py`: fast-export of a mirror, replace the mail and Tailscale addresses in file contents and messages, drop `.claude-state/`, `tools/__pycache__/`; fast-import | `tools/repo/`, every commit ID; developer force-pushes, local branches switch again | `purge-check.sh` on the copy, GitHub clone afterwards |
| M1 | Leave the slot on area/room change; stop handling packets once a client is closing | `lobby.cpp` | CREATESLOT + ENTERROOM frees the slot; LOGOUT + CREATESLOT in one read leaves none |
| F1 | Refuse 0.0.0.0/8 too (TCP), and loopback and 0/8 on UDP unless mapped; fan DNS must be a routable unicast address; hook parses strictly | PCSX2 patch 0006, `hook.cpp`, `net_choice.cpp`, `NetPlay.kt` | dev9: UDP to loopback never arrives; rule tests refuse 127/0/multicast/broadcast DNS |
| F2 | Stage driver imports internally. Android 10: settings and mods remain external (players manage them by USB), documented; the network choice is confirmed by nothing, so on API 29 the app ignores external `net_*` keys only if a move is impractical (decided at the step) | `Importer.kt`, `Settings.kt`, `OutbreakActivity.kt` | Kotlin rule tests |
| M2 | A separate 5 s handshake deadline, so a stalled peer costs less | `tls_server.cpp`, `web.h` | a peer stalling in the handshake is cut at the shorter deadline |
| M3 | Per-sender byte budget for what is passed on to others (chat, events, relay frames); the flooding sender is dropped, not the receiver | `lobby.cpp`, `relay.cpp` | a flooding sender is dropped; a normal rate is not |
| Licence | Top-level `COPYING` note and `THIRD_PARTY_NOTICES.md` packed into the APK; official GPL/AGPL texts come from the developer (no downloads) | root, `build.gradle.kts`, `CREDITS.md` | notices file lists every bundled library; asset present in the APK |
| L1 | `logSafe` on the room name; per-client budget on peer-triggered log lines | `lobby.cpp` | a newline in a room name is escaped; a flood stays under the budget |
| L2 | When full, evict the oldest entry | `beacon.cpp` | a new host gets in while 32 others keep sending |
| L3 | An invalid prefix refuses to host | `host_jni.cpp`, `host.cpp` | LocalHost with a bad prefix fails to start |
| L4 | The list shows logged-in players, not the host itself | `lobby.cpp`, `relay.cpp`, `host.cpp` | a connected, not logged-in client is not listed |
| L5 | Don't poll a closing client for reading | `lobby.cpp` | covered by the closing-client path |
| L6 | Close the listener after the thread has ended | `tls_server.cpp` | stop still returns promptly |
| F3 | `IniFile::set` strips line breaks | `ini_file.cpp` | a value with a newline stays one line |
| F4 | `FatWord` page index instead of pointer subtraction; size cap on the card read | `memory_card.cpp` | an oversized card file is refused |
| F5 | Hook parses with `isIpv4` | `hook.cpp` | with F1 |
| F6 | Trace stamp race: statistics only, documented | none | none |
| F7 | Count each zip entry against the budget | `ZipExtract.kt` | a zip of many empty files stops |
| H8 | mbedTLS check fails without `.git`, or on modified sources; no home paths in tools | `check_mbedtls.cmake`, tools | `check-pins.sh` |
| H9 | Settings: denying more push forms is the developer's call (agents don't edit their own permissions) | `.claude/settings.json` | none: proposed text only |
| Assets | The developer decides which third-party files leave history | none yet | none |

## Progress (2026-10-30)

| Item | State |
|---|---|
| P2 | Done: `purge-check.sh` fails on a failed git, searches contents |
| P1 | Scrubbed copy built and checked; **waiting for the developer's force push**, then local branches switch again |
| M1, F1, F3, F5, F7, L1, L2, L3, L5, L6 | Done, tests first where a test can fail |
| M2 | **Reduced, not eliminated:** an 8 s handshake limit; the site still serves one connection at a time, so a persistent peer can slow logins. A worker pool is the full fix |
| M3 | Done: the sender is dropped by its own budget (lobby 64 KB/s, relay 512 KB/s). The limits are estimates: check them in real play |
| F2 | **Partly:** driver imports staged internally. On Android 10 settings, mods and textures stay external (players manage them by USB), so another app with storage permission could change them. Raising minSdk to 30 would close it: the developer's call |
| F4 | Done; the size test passed before the cap, so it guards rather than proves |
| F6 | Left: statistics only, no memory hazard |
| L4 | Partly: the host's device can't be Removed; the list still shows connections for up to 15 s before login |
| H8 | Done: mbedTLS must be an unmodified pinned checkout; no home paths in tools. Not done: Gradle dependency verification metadata, hashes for FetchContent |
| H9 | **The developer's:** agents don't edit their own permissions. Proposed: deny `Bash(git -C * push*)`, `Bash(gh *)`, `Bash(bash tools/repo/*)` |
| Licensing | Done for COPYING and the notices in the APK; **the AGPL text, the source link and the texts marked "project repository" remain before a public release** |
| Assets | The developer decides |

Not run on a device yet: this batch. Full suite: ps2x 577/577, iop 5/5, gs_cache 45/45, ob_diag 24/24, pcsx2_vu 3/3, pcsx2_gs
92/92, spu2 7/7, ipu 22/22, sio2 20/20, ob_server 228/228, dev9 33/33, ob_ui 191/191, Kotlin 72/72; 0 warnings.
