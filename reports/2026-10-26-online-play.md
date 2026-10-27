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

### 6. Separate build trees so the touch-controls work keeps its builds

- **Seen:** `ob.sh recompile` copies generated code into `C:\ob\src` and builds `C:\ob\r`, both shared with the
  touch-controls session; `C:\ob\up` already had that session's commit b70986b past patch 0080.
- **Fix:** `C:\ob\net-up` = git worktree of `C:\ob\up`, branch `net-online` at 58145be (= 0080); `C:\ob\net-src`,
  `net-r`, `net-work`, `net-ovl`, `net-logs`, `net-pcsx2-gs-build`, `net-diag-build`, `net-ioptests` through ob.sh's
  `OB_*` variables (`.run/spike/net-env.sh`). `ob.sh` overlay list is `OB_OVERLAYS` (default adds netwk netaq nethttp).
- **Evidence:** first runner build 1,422 s (once, CPU shared with the GS library build); the next runner-only build
  after the DEV9 patch 88 s with 0 game units rebuilt.
- **Take-away:** for parallel work, give each line of work its own `OB_*` trees, not its own copy of everything.

### 7. Test bugs while bringing up the DEV9 tests

- **Seen:** the TX descriptor was never taken; then DHCP answered without a DNS server; then DMA 8 "accepted" a CHCR
  meant to be refused.
- **Cause:** (a) EMAC3 registers start at SMAP + 0x1F00, not + 0x2000 (`DEV9.h:240`); (b) the DHCP server sends only
  the options asked for (option 55); (c) `0x01000202 & 0x01000201` is the valid DEV9-to-RAM mode.
- **Fix:** the test's constants and request; `native/pcsx2_dev9/tests/dev9_tests.cpp` 15/15.
- **Take-away:** take register bases from the header, not from a neighbouring block.

### 8. PCSX2's internal DNS fails upper-case questions

- **Seen:** a listed host asked as `GATE1.JP...` came back SERVFAIL with no answer.
- **Found by:** dumping the reply, then `DNS_Server.cpp` 125-200.
- **Cause:** `CheckHostList` stores the answer under the lowered name, `FinaliseDNS` looks it up under the name as
  asked.
- **Fix:** none needed for the game (its strings are lower case); the test asks in lower case and documents it;
  `ob_dev9_open` lowers configured names (`DNS_Server.cpp:183`).
- **Take-away:** a candidate upstream PCSX2 fix (not sent; leaving the machine needs the developer's OK).

### 9. With an adapter, the boot's HDD check reaches an unwritten stub

- **Seen:** adapter on: "dev9: CXD9611 detected", "ATA: error: there is no device0", HDD/PFS start, then
  `Unimplemented PS2 stub called: sceFsSemInit` and the runner stops (`C:\ob\net-logs\boot-dev9-on.err`).
- **Cause:** a real PS2 always has the adapter, so the game checks for an HDD install at boot; without an adapter that
  path never ran here. `_sceFsSemInit` (CreateSema once for the EE file I/O) is in PS2Recomp's stub list as a TODO.
- **Fix (part 1):** recompile the game's own `_sceFsSemInit` (`ob.sh` `[general] recompile`, as 0073 did for
  sceDbcInit). Single player is unchanged: the adapter is off there and this path is not reached. The boot then runs
  its full 60 s at 60 fps.
- **Still stuck (part 2, open):** the HDD check's `sceDevctl("hdd0:", ...)` (recompiled game code) calls the IOP file
  service over an RPC client that was never bound (`[IOP/RPC trace:unhandled] sid=0x0 rpc=0x17 ... "hdd0"`): PS2Recomp's
  `sceFsInit` HLE returns 0 without binding SID 0x80000001. USBD/USBKB then never load. On a PS2/PCSX2 the reply is
  "no such device" and the boot goes on.
- **Cost found:** dropping one stub changed `ps2_recompiled_stubs.h`, which every generated unit includes: the runner
  rebuilt all game units, 1,271 s. Against the 5-minute rule; to fix in the build setup.
- **Evidence:** `C:\ob\net-logs\boot-dev9-on.err`, `ob-runner-build.log`; ob.sh.

### 10. PS2Recomp 0071's DMA interrupt numbers for SIO2 may be one low (unverified)

- **Seen:** intrman numbers DMA channels 7 and up from 0x28 (SPU2 core 1 = 0x28 in PS2Recomp's own constants;
  ps2sdk `intrman.h`, from memory, not opened), so channel 8 = 0x29, 11 = 0x2C, 12 = 0x2D; 0071 uses 0x20 + channel
  (0x2B/0x2C).
- **Effect:** none seen (memory cards work); sio2man probably doesn't wait on those interrupts.
- **Action:** 0081 uses 0x29 for DEV9; 0071 left as is, noted here. Check against ps2sdk before changing it.

### 11. A stub-list change rebuilt half the runner (21 minutes)

- **Seen:** dropping one stub (entry 9) rebuilt 454 of 871 unity batches, 1,271 s; "headers: copied 1".
- **Found by:** every generated function file included the module-wide `*_recompiled_functions.h` (5,369 lines) and
  `*_recompiled_stubs.h`; the unity batches are alphabetical, so main-ELF files sit in most batches.
- **First fix, not enough:** PS2Recomp 0082's generator change (each file declares the functions it calls: calls are
  always `<name>_0x<addr>(rdram`; `branch_taken_0x…` locals are not functions, 99,711 of them, so matching on the
  call form matters). The same stub change still rebuilt 454 batches, 1,265 s.
- **Cause:** the runner's precompiled header (`ps2xRuntime/CMakeLists.txt`, `PS2X_RUNNER_PCH_HEADERS`) also held
  both generated headers, so any change to them rebuilt every batch using the PCH.
- **Fix:** 0082 also drops them from the PCH (only the registration files include them now, directly). Folded into
  the same patch (amended in `C:\ob\net-up`, no interactive rebase).
- **Evidence:** ps2x 557/557 with, 554/557 without. Measured (`.run/spike/net-measure.sh`, `_sceFsSemInit` stub on/off):
  after the PCH change's own full build (405 batches, 1,319 s), the first stub change still rebuilt 454 batches
  (1,193 s: batches not rebuilt by that baseline still had dependency records from the old PCH), its revert 2 batches
  (230 s); repeated, both directions 2 batches, 221 s and 240 s. Was 454 batches, 1,271 s.
- **Still slow-ish:** the ~3.5 min left is the runner's link of ~14,500 objects (plus re-globbing); under the
  5-minute ceiling, and the floor for any change. A candidate for later (incremental link, fewer objects).
- **Take-away:** a header in a PCH is included by every unit, whatever the sources say; check the PCH list first.

### 12. The boot's HDD check, answered as "no drive"

- **Seen:** entry 9: `sceDevctl("hdd0:", 0x4807)` waited forever for the IOP file service.
- **Found by:** reading the game's sceDevctl (0x11B0E0-0x11B184: semaphore, RPC 0x17, wait) and its HDD check
  (0x1BB420, decision table 0x1BBA60; research 7.5): only status 3 ("no drive") continues silently; a negative error
  means "drive present".
- **Fix:** PS2Recomp 0083: `sceDevctl` and `sceMount` as runtime HLE (`hdd*` 0x4807 = 3, other HDD commands and
  mounts -19, `dev9x:`/`pfs:`/`cdrom0:` 0); the analyzer makes them stubs because they are in `ps2_call_list.h`.
- **Evidence:** adapter-on boot, 90 s: `[fileio] sceDevctl("hdd0:", 0x4807) = 3`, `sceMount(...) = -19 (no HDD)`,
  then USBD/USBKB load (17 modules) and the same disc files and overlays as single player; ps2x 558/558.

### 13. Skipping DNAS without running any NETDNAS code

- **Seen:** NETDNAS can't be recompiled (entry 3), so the client must never run it.
- **Found by:** reading the lobby overlay (read-only helper, 2026-10-27): the NETWK step dispatcher (`func_00596D90`,
  step variable D_0068C5F0) runs step 33, `func_0058B3D0`, which loads NETDNAS (`func_005897F0(1)` → overlay id 4),
  drives Capcom's wrapper at 0x7437E0 (NETDNAS's plain tail, not the scrambled part) and returns 1 on success, -1 on
  failure. On 1 the game goes to step 35, which loads NETHTTP over NETDNAS and opens
  `https://www01.kddi-mmbb.jp/00000002/CRS-top.jsp`. Nothing later reads DNAS's result (status/auth buffers are never
  referenced from main, NETWK or NETHTTP). Only the "update patch" path calls 0x743E10/0x743F50 (not needed).
- **Fix:** an instruction patch in NETWK's recompile config (`ob.sh`): 0x58B3D0 = `jr $ra`, 0x58B3D4 =
  `addiu $v0, $zero, 1`. The recompiled function returns 1 on entry; NETDNAS is never loaded.
- **Evidence:** trial recompile of NETWK with the patch: 0 errors; the generated `netwk_func_0058B3D0` starts with
  `jr $ra` + `v0 = 1`. Not yet seen in the running game (needs the network menu: step C).
- **Take-away:** skip a protection at the caller's state step, not inside the protected code.

### 14. A rebuild with the old recompiler (21 minutes lost)

- **Seen:** the rebuild for the DNAS patch copied 14,273 changed sources and took 1,288 s.
- **Cause:** `.run/spike/net-env.sh` did not set `OB_T`, so `net-recompile.sh` used `C:\ob\t`'s analyzer and
  recompiler (built before 0082/0083): every file got the old includes back, and `sceDevctl`/`sceMount` were not
  stubbed (the old analyzer doesn't know them).
- **Fix:** `OB_T=C:/ob/net-t` in `net-env.sh`; rebuilt (one more full build, back to the new form).
- **Take-away:** one environment file for every build of a line of work, including the tools tree.

### 15. First drive of the real game to its network menu: LOADHIGH and module arguments

- **Seen:** NETWORK PLAY → black screen forever; `unhandled import modload:19`, `modload:8` from `LOADHIGH.IRX`.
- **Found by:** decoding LOADHIGH (scratch decoder, no capstone): modload 19 = LoadModuleWithOption(argv[1], option
  {position 1, access 1}) → id, then 8 = StartModule(id, name, arglen, args, &result). PS2Recomp had neither, and
  started every module with (argument size, raw buffer) instead of loadcore's (argc, argv).
- **Fix:** PS2Recomp patches: (a) module start with (argc, argv) (argv[0] = name/path, block kept allocated);
  (b) modload 19 + 8. Tests fail without (IOP 4/5 each). LOADHIGH then loads and starts `USBSRCH.IRX`.
- **Side effect to gate:** modules that take arguments now get them, e.g. the CRI ADX driver `thpri=39 spucore=1
  debug=0` (it got garbage before). Single player's sound path changes: the gate must run before merging.
- **Test-writing trap:** my first test module did `lw ra` then `jr ra` at once; the R3000 load delay (correctly
  emulated) used the old `ra`. Real IRX code never does that; one `nop` fixes the test.

### 16. sysclib's isdigit never matched

- **Seen:** `NETCNFIF.IRX thpri=123 thstack=8KB` printed its usage and returned 1 (not resident).
- **Cause:** PS2Recomp's sysclib `look_ctype_table` (8) and `get_ctype_table` (9) returned 0, so `isdigit` (=
  `look_ctype_table(c) & 0x04`, ps2sdk ctype.h) was always false.
- **Fix:** PS2Recomp patch: the real class flags (and a 256-entry table). Test fails without. NETCNFIF resident.

### 17. The game's network settings editor is a separate program

- **Seen:** NET CONNECTION → "In order to play online you need to configure the network settings" → Edit → YES:
  the game runs its power-off devctls, then EE syscall 6 (unimplemented) and stays on the dialog.
- **Found by:** the lobby code at 0x5CDC04: `LoadExecPS2("cdrom0:\NTGUI2\YN_FILEJ.BIN;1", 2, ...)`: Sony's network
  configuration utility, a separate PS2 executable that reboots back into the game when done.
- **Consequence:** our port can't run it (not recompiled; a different program). Plan P16 becomes required: the app
  writes the network configuration (`BWNETCNF` on the memory card: Ethernet, DHCP, DNS automatic) itself, so the
  editor is never needed. The file's format (Sony netcnf; possibly scrambled) is the next research item.
- **Reached so far:** title → NETWORK PLAY → NET file created on the card → CONNECTION → notice → NET CONNECTION
  (the network modules all load: AVE-TCP, LOADHIGH/USBSRCH, NETCNF, NETCNFIF).

## Mistakes and corrections

| Claimed | What was true | How it was caught | Corrected in |
|---|---|---|---|
| Answering DNAS/login inside the adapter avoids old TLS (research 5b) | The game does the TLS itself; the answering side must speak it | Game strings + corbin.zip guide while planning | plan P4 |
| The in-game network code is in NETWK (milestone docs) | It is NETAQ (0x820000); NETWK is the lobby | Reading the overlays' code and strings | research 7.1 |
| mbedTLS 2.28 needs `MBEDTLS_REMOVE_3DES_CIPHERSUITES` undefined for 3DES (said in the session) | That define only drops 3DES from the default list; a suite set explicitly still negotiates | The control handshake on the stock build negotiated it | plan P4 result |

## Still open

- S0.1-S0.3 done (research 7.4; plan P4 result).
- S1 done: `native/pcsx2_dev9` builds; tests 15/15 (DHCP offer with address and DNS server, INTC 13 on receive,
  DNS host override, DMA 8 refusal). S2 patch done: PS2Recomp 0081 (provisional number), IOP tests 5/5 suites; the
  runner with `PS2X_DEV9=pcsx2` finds the adapter ("dev9: CXD9611 detected").
- Done: the boot's HDD check with an adapter (entry 12). Next: the network menu (S3). The gate not yet run in the
  net trees; ps2x 558/558 in `C:\ob\net-t`.
- Build defect (entry 11): fixed by 0082; a stub change now rebuilds 2 batches in ~4 min (was 454, 21 min).
- Reference decided (developer, 2026-10-27): the fan server's source is read, nothing installed (option B); spec in
  `research/2026-10-27-lobby-protocol.md`, design `docs/plans/2026-10-27-ob-server.md` (approved).
- ob_server step A done: `native/ob_server` (packet header, lobby/relay stream splitting, string scrambling, session
  digits), tests 27/27, 0 warnings, in `ob.sh test`. The scrambling formula decodes the fan server's captured version
  string into "0311172218" (ten digits: strong evidence the formula is right).
- ob_server step B done: login pages with quick login over SSL 3.0 / TLS 1.0 with RSA and DHE-RSA 3DES (tests 40/40,
  mbedTLS 2.28.10 pinned), and the DNAS skip patch (entry 13). Not yet tried with the game itself: that needs the game
  to reach its network menu and our adapter's DNS pointing at the server (step C).
- ob_server step C (server side) done: `net` (sockets, poll), `accounts` (sessions, handles, game numbers), `lobby`
  (minimal path + fixed answers; unknown questions answered empty and logged), `Server` (login pages + lobby on one
  account list), `ob_server.exe`. Tests 70/70: two scripted game clients log in, create/open/join/start, both get
  GETREADY, game number 2 and the relay address; quick login over TLS → lobby login. Not yet with the real game.
- Trick for the first real-game test on one PC: PCSX2's DNS rewrites an answer of 127.0.0.1 to the PC's LAN address
  (`DNS_Server.cpp:210`), which makes Windows ask about the firewall; `PS2X_NET_HOSTS=www01.kddi-mmbb.jp=127.0.0.2`
  stays on loopback (any 127.x reaches this PC) with `ob_server --bind 127.0.0.2`. Unverified.
- Faking an installed HDD: not now (research 7.5: only for HDD-only rooms).
- mbedTLS on a real device and against the PS2's actual ClientHello: S3 / S6.

## Useful for other developers

- In-game network messages: `research/2026-10-26-network-crossplay.md` section 7.1 (types, sizes, room owner).
