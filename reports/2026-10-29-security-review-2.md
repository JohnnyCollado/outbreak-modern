# Security review 2: after the fixes and the history rewrite (2026-10-29)

Second read-only review (three reviewers in parallel: the server, the app and runtime, the repository and build), at
the developer's request after the security queue (`docs/plans/2026-10-29-security-queue.md`). **Verified** = checked
by hand against the code or git afterwards; the rest is the reviewers' reading.

**Correction to R1:** the history rewrite changed commit authors only. The personal mail address is still inside old
versions of tracked files (19 commits; e.g. `.claude-state/journal/*`, `phase-ends/*/logs/T3.c1.md`, old patch
headers, an old version of the queue plan), and the Odin's Tailscale address in 2 commits of `docs/ops/android.md`.
`purge-check.sh` checked only commit metadata, and it printed "ok" when git itself failed. Both **verified**.

## Ranked findings

| # | Sev. | Finding | Where | Status |
|---|---|---|---|---|
| P1 | High | Personal mail address (and the Tailscale address) in old file contents across history, on the private GitHub repo | history: `.claude-state/`, `phase-ends/`, `upstream/*` old versions, `docs/ops/android.md` | **Verified** |
| P2 | Medium | `purge-check.sh` says "ok" when git fails (e.g. a wrong path) | `tools/repo/purge-check.sh` | **Verified** |
| M1 | Medium | A peer locks every lobby game slot until restart: area/room changes don't leave the slot (CREATESLOT then ENTERROOM); packets after LOGOUT are still handled | `lobby.cpp:573-599`, `:995` | **Verified** (no leaveSlot on area/room change) |
| F1 | Medium | Fan-server DNS: its replies now reach the game's own resolver; a malicious DNS could point the game at 0.0.0.0 (loopback on Android/Linux) or, over UDP, at 127.x (0005 covers TCP 127.x only) | `dev9_side.cpp`, PCSX2 `UDP_Session.cpp:143` | UDP path **verified** (no loopback check); 0.0.0.0 reachability unverified |
| F2 | Medium | Android 10 only: outbreak.ini (incl. the network choice), mods, textures and the import staging folder are still in external storage; a driver import is staged there before the copy inside | `Settings.kt:14-16`, `Importer.kt` staging | Staging **verified** |
| M2 | Medium | The login site is still one connection at a time: a peer reconnecting endlessly keeps others waiting 15 s per connection | `tls_server.cpp:225-407` | Reviewer |
| M3 | Med-low | The output cap drops the receiver: a sender flooding chat/event data/relay frames can get another player kicked | `lobby.cpp:158`, `relay.cpp:95` | Reviewer |
| L-licence | Medium | No top-level licence file; the APK ships GPL/Apache/BSD code without the notices; CREDITS misses mbedTLS, zstd, shaderc, sse2neon, raylib, libpng/zlib, libzip, AndroidX | repo root, `CREDITS.md`, APK | Reviewer |
| L1 | Low | One log line still writes the room name raw; several per-packet log lines have no cap | `lobby.cpp:726` and others | **Verified** (`s->name` raw) |
| L2 | Low | Nearby list: one peer re-sending 32 fake beacons every 10 s still keeps real hosts off | `beacon.cpp:190` | Reviewer |
| L3 | Low | An invalid Wi-Fi prefix falls back to "any private address" | `host_jni.cpp:67` | Reviewer |
| L4 | Low | The host's player list counts not-yet-logged-in connections and the host itself (the host could Remove itself) | `lobby.cpp:1045`, `relay.cpp:249` | Reviewer |
| L5 | Low | A closing lobby client whose peer hung up spins the loop for up to 2 s | `lobby.cpp:985` | Reviewer |
| L6 | Low | Login site stop frees the listen socket while accept may use it (benign race) | `tls_server.cpp:211` | Reviewer |
| F3 | Low | `IniFile::set` writes a value with a line break as an extra line (all callers validate today) | `ini_file.cpp:246` | **Verified** |
| F4 | Low | Card writer: pointer subtraction across arrays when bad; whole file read without a size cap | `memory_card.cpp:381`, `:74` | Reviewer |
| F5 | Low | `PS2X_NET_DNS` parsing looser than the UI's; 127/0/broadcast/multicast accepted as a fan DNS | `hook.cpp:374`, `fanServerEnv` | Reviewer |
| F6 | Low | Latency trace timestamp race (statistics only) | `dev9_side.cpp:89` | Reviewer |
| F7 | Low | A zip of millions of empty files passes the byte budget | `ZipExtract.kt` | Reviewer |
| H8 | Low | Home paths `C:/Users/johnn` in 9 tracked files; pins: mbedTLS check falls back to a version string without `.git`; no Gradle dependency verification metadata | `tools/ob.sh:55` etc. | Reviewer |
| H9 | Low | `.claude/settings.json` denies only `git push:*` (not `git -C … push`, `gh`); `bash tools/*` allowed lets an agent run the history rewrite unprompted | `.claude/settings.json` | Reviewer |
| Assets | Decide | Third-party files in history: Vita `.suprx` modules (~3.5 MB), a 12 MB symbol header, fonts, `.pyc` with home paths; in HEAD: 3 icon PNGs (AI-generated per metadata), a game screenshot, `.pnach` patches | history / `docs/assets/`, `legacy/results/` | Reviewer |

## Verified correct (reviewers)

S1-S10, C1-C6 fixes hold as claimed; no crash or iterator bug introduced; PS2Recomp 0105 has no use-after-free path
(stacks freed only for dormant threads whose invocations are cleared); `javaSafeText` always yields valid Modified
UTF-8; every NewStringUTF wrapped; zip traversal checks hold; no secrets or keys in history; pins for PCSX2,
libadrenotools, ARMSX2 and the deps scripts hold; HEAD has no personal data.
