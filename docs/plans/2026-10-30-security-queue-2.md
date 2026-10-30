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

## Progress

See the commit log; final state recorded at the end of the queue.
