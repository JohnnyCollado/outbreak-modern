# Online play, first release (our clients on one network)

- **Date:** 2026-10-26 (started)
- **Who:** developer + Claude Code session on branch `netplay-research`
- **Goal:** Windows and Android clients of outbreak-modern play online together on one network, as planned in
  `docs/plans/2026-10-26-online-play.md` (S0-S8).
- **Outcome:** in progress: research and plan done; S0 started.
- **Versions:** outbreak-modern `main` f386a8a + this branch; PS2Recomp patches 0001-0080; PCSX2 source in
  `C:\ob\pcsx2` (DEV9 read only so far).

## Summary

Research found the game is state sync (each client sends its own character; each room's first player runs its
enemies), so Windows and Android don't need identical maths. The plan keeps the game's own network code and
emulates the network adapter with PCSX2's DEV9. Struggles below are logged as they happen.

## Struggles

### 1. "No old-cipher TLS needed for our clients" was wrong

- **Seen:** while planning, the login site turned out to be HTTPS too.
- **Found by:** the game's own strings (`https://www01.kddi-mmbb.jp/00000002/CRS-top.jsp` in the network overlays'
  data) and the fan-server guide (corbin.zip: OpenSSL 1.0.2q with weak ciphers for DNAS).
- **Cause:** the game encrypts DNAS and login itself, so whoever answers must speak the PS2's ciphers, even when
  the adapter is ours.
- **Fix:** plan P4: a TLS library spike (S0.2), with a fallback hook that skips both steps for our clients.
- **Evidence:** `docs/plans/2026-10-26-online-play.md` P4; `research/2026-10-26-network-crossplay.md` section 5b.
- **Take-away:** check who does the encryption before assuming a layer can skip it.

### 2. Splat lists only 30 NETDNAS functions

- **Seen:** `splat_functions.py` gave 30 functions, 0x6C0040-0x6C2A04, of a 0x840C0-byte text.
- **Found by:** byte entropy and opcode mix per 16 KB of the text (scratch script): ordinary MIPS almost
  everywhere, ~1,300 JAL targets past splat's end.
- **Cause:** the decomp's splat config treats the rest of NETDNAS as one data block.
- **Fix:** `tools/fill_functions.py` (+ `tools/test_fill_functions.py`, 2/2): new starts from JAL targets and from
  stack prologues right after a return; 1,835 functions.
- **Evidence:** `C:\ob\net-ovl\netdnas-filled.csv`, `recomp-netdnasf.log`.
- **Take-away:** a short splat list for a big overlay means "not split", not "little code".

### 3. NETDNAS has code scrambled at rest

- **Seen:** with the filled list, 2,216 recompile errors, all "unhandled instruction".
- **Found by:** bucketing error addresses: they sit in 0x6C4000-0x6CC000 and 0x728000-0x744000, the two windows
  with byte entropy 6.3-7.2 (code elsewhere: 5.1-5.9).
- **Cause:** Sony's DNAS code unscrambles parts of itself at run time (inferred from the entropy; not traced).
- **Fix:** plan P4 result: skip DNAS in our client by a hook; real DNAS stays a server-side job.
- **Evidence:** `C:\ob\net-ovl\recomp-netdnasf.log`; plan P4 result.
- **Take-away:** static recompilation can't take code that only exists at run time; find the API boundary instead.

### 4. The worktree session can't run `cmd /c` scripts directly

- **Seen:** the session refused `cmd /c <bat>` and `run.sh -- cmd //c ...` ("too complex to verify ... git").
- **Found by:** the refusal message.
- **Cause:** this session is isolated in a git worktree; commands it can't see into are refused.
- **Fix:** a bash wrapper in `.run/spike/` (ignored) that calls the batch file, run by `tools/run.sh`.
- **Evidence:** `.run/spike/mbedtls-build.sh`.
- **Take-away:** in a worktree session, wrap Windows batch steps in a bash script.

### 5. mbedTLS 2.28 doesn't configure with the NDK toolchain

- **Seen:** `if given arguments: "hwaddress" "IN_LIST" "ANDROID_SANITIZE" Unknown arguments` (NDK `flags.cmake:46`).
- **Found by:** `C:\ob\logs\mbedtls-android-configure.log`.
- **Cause:** mbedTLS 2.28 declares an old CMake minimum, so policy CMP0057 (`IN_LIST`) is off, which NDK r27's
  toolchain needs.
- **Fix:** `-DCMAKE_POLICY_DEFAULT_CMP0057=NEW`; then libmbedtls.a and `ssl_server2` build for arm64.
- **Evidence:** `.run/spike/mbedtls-android.sh`, `C:\ob\logs\mbedtls-android-build.log`.
- **Take-away:** old CMake projects under the NDK toolchain: set CMP0057 NEW.

## Mistakes and corrections

| Claimed | What was true | How it was caught | Corrected in |
|---|---|---|---|
| Answering DNAS/login inside the adapter avoids old TLS (research 5b) | The game does the TLS itself; the answering side must speak it | Game strings + corbin.zip guide while planning | plan P4 |
| The in-game network code is in NETWK (milestone docs) | It is NETAQ (0x820000); NETWK is the lobby | Reading the overlays' code and strings | research 7.1 |
| mbedTLS 2.28 needs `MBEDTLS_REMOVE_3DES_CIPHERSUITES` undefined for 3DES (said in the session) | That define only drops 3DES from the default list; a suite set explicitly still negotiates | The control handshake on the stock build negotiated it | plan P4 result |

## Still open

- S0.1-S0.3 done (research 7.4; plan P4 result). The overlays are recompiled but not booted: needs a runner tree
  apart from `C:\ob\src` (shared with the touch-controls work).
- mbedTLS on a real device and against the PS2's actual ClientHello: S3 / S6.

## Useful for other developers

- In-game network messages: `research/2026-10-26-network-crossplay.md` section 7.1 (types, sizes, room owner).
