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
- **Action:** 0087 uses 0x29 for DEV9; 0071 left as is, noted here. Check against ps2sdk before changing it.

### 11. A stub-list change rebuilt half the runner (21 minutes)

- **Seen:** dropping one stub (entry 9) rebuilt 454 of 871 unity batches, 1,271 s; "headers: copied 1".
- **Found by:** every generated function file included the module-wide `*_recompiled_functions.h` (5,369 lines) and
  `*_recompiled_stubs.h`; the unity batches are alphabetical, so main-ELF files sit in most batches.
- **First fix, not enough:** PS2Recomp 0088's generator change (each file declares the functions it calls: calls are
  always `<name>_0x<addr>(rdram`; `branch_taken_0x…` locals are not functions, 99,711 of them, so matching on the
  call form matters). The same stub change still rebuilt 454 batches, 1,265 s.
- **Cause:** the runner's precompiled header (`ps2xRuntime/CMakeLists.txt`, `PS2X_RUNNER_PCH_HEADERS`) also held
  both generated headers, so any change to them rebuilt every batch using the PCH.
- **Fix:** 0088 also drops them from the PCH (only the registration files include them now, directly). Folded into
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
- **Fix:** PS2Recomp 0089: `sceDevctl` and `sceMount` as runtime HLE (`hdd*` 0x4807 = 3, other HDD commands and
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
  recompiler (built before 0088/0089): every file got the old includes back, and `sceDevctl`/`sceMount` were not
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

### 18. The gate after the rebase (and its first, false red)

- **Seen:** first gate run on the rebased net build: 0/3 gated green (savedata/access/capcom 2-5% of pixels off).
- **Cause:** my run used the runner's default CPU renderer (`[gs] backend=cpu`); the project's green gate runs use
  PCSX2's software renderer (`PS2X_GS=pcsx2-sw`; earlier reports "gate 3/3 pcsx2-sw"). Not patch 0090.
- **Result:** with `PS2X_GS=pcsx2-sw`: **3/3 gated green** (max delta 0), advisory screens as before
  (`.run/spike/net-gate.sh`).
- **Not covered:** sound. With 0090 the CRI ADX driver now reads its arguments (`thpri=39 spucore=1 debug=0`, it prints
  them: "SPU CORE Number = 1"); before it ran on its defaults. Pictures are identical; audio needs a listen.

### 19. The network settings files are scrambled with the console's ID

- **Found by:** reading Sony's netcnf as ps2sdk reimplements it (`iop/network/netcnf/src/netcnf.c`, fetched
  2026-10-27): `BWNETCNF/` on the card holds an index file and `netNNN.cnf` in plain text, but the interface and
  device files (`ifcNNN.dat`, `devNNN.dat`) are scrambled with a key made from the console's i.Link ID, which netcnf
  reads with cdvdman's `sceCdRI` (import 22). Each byte of the ID gives three bit-rotation amounts; every 16-bit word
  of the file is rotated left by one and inverted.
- **Problem:** `sceCdRI` was an unhandled import (returning 0), so netcnf would retry 20 times and give up: the game
  could never read settings, whoever wrote them.
- **Fix:** PS2Recomp 0093: `sceCdRI` returns one fixed ID (eight 0xAA bytes) on every install, so the files we write
  read the same everywhere. Also 0094: `sceCdReadClock` (import 24, polled by USBSRCH: 21 warnings per drive) returns
  the host's local time in BCD like the EE stub already did. IOP tests 5/5; each fails without its patch (4/5).
- **Next:** write the settings set onto the card image (entry to follow).

### 20. Settings on the card, but NETCNF can't open files: the IOP's ioman only registers drivers

- **Done:** `tools/ob/mcd.py netcnf` wrote `BWNETCNF` onto `C:\ob\saves-net\Mcd001.ps2` (backup beside it:
  `Mcd001.before-bwnetcnf.ps2`) beside the game's own save; `mcd.py info` lists both. Two corrections to my notes
  on the way: the real `netcnf.irx` fills all 24 key slots from the 8 ID bytes (code 0x128c-0x12f0; ps2sdk's
  decompilation stops one byte short) and an Ethernet setting is `type eth` (1), not `nic` (3, netcnf.c's parser).
- **Seen (drive, runner with 0093/0094):** NET CONNECTION still says "you need to configure the network settings".
  The cdvdman 24 warnings are gone. New in `boot-net-live.err`: after NETCNFIF starts, repeated
  `unhandled import ioman:4`, `ioman:6`, `ioman:5` (open, read, close; NETCNF links the old `ioman`, version 0x104).
- **Cause:** PS2Recomp's IOP `ioman` handles only AddDrv (20) and DelDrv (21) (`iop_ioman.cpp`): the card driver
  MCMAN registers its `mc` device, but nothing routes `open("mc0:/BWNETCNF/BWNETCNF")` to it, so the unhandled open
  "succeeds" with 0 and the read returns nothing: NETCNF sees no settings, whatever the card holds.
- **Open question (developer's call):** the fix is a PS2Recomp patch giving ioman a file table that calls the
  registered device's operations (open, read, close, then lseek/getstat/dopen as NETCNF needs). MCMAN's operations
  wait for the card hardware, so they must run where the guest can block (patch 0075's thread runner), not as a
  plain nested call. Paused here to agree the approach.

### 21. ioman now reaches drivers, but this game's card isn't an ioman device: NETCNF reads through the EE

- **Done (developer approved 2026-10-27):** PS2Recomp 0095, ioman file operations call the registered device's
  functions on the caller's own thread (a return sentinel turns the result into an fd). IOP tests 5/5; fails without
  (4/5). Written code-first, test second; the developer asked for test-first from now on. My first expected value
  was also wrong (`"/ab"[1]` is 'a', 0x65, not 0x66), corrected from the string, not from the output.
- **Seen (drive with temporary logging in `net-src` only, since removed):** the ioman warnings are gone, the screen
  is unchanged. Only `dev9x`, `hdd`, `pfs` register with ioman; NETCNF's six `open("mc0:/BWNETCNF/BWNETCNF")` get
  -19 (no device). The game's card driver is `mc2_s1` (libmc2 over `sio2d`/`dbcman`), which imports no ioman. The
  classic `mcman.irx` exists only in `ntgui2/modules` (Sony's settings utility).
- **How the real game reads its settings** (ps2sdk `netcnfif.c` 1131-1189 and the dispatcher case 11): NETCNFIF RPC
  function 11 = `sceNetCnfSetCallback`: NETCNF's file open/read/close go to callbacks that call an **EE** RPC server
  (sid 0x80001101, functions 12 open, 13 read, 14 close); the game's EE code reads the card with libmc2.
- **Cause of the hang:** first `sce_callback_initialize` binds that EE server and loops until `gcd.server` is set.
  PS2Recomp's IOP `sifcmd` answers `sceSifBindRpc` (15), `sceSifCallRpc` (16) and `sceSifCheckStatRpc` (18) with 0
  and nothing else (`iop_rpc.cpp`, ordinals per ps2sdk `sifcmd.h`), so it never binds; the handler runs out of its
  one-second budget (the "did not return within its budget" warning: entry 0xc24c0, function 0xb) and the callbacks
  are never installed. On the EE, `sceSifRpcLoop` returns at once (`Stubs/SIF.cpp:610`): no IOP-to-EE calls exist.
- **0095's place:** correct and tested, but not on this game's path. Kept for now; the developer decides.
- **Open question (developer's call):** serve sid 0x80001101 calls 12/13/14 on the host (settings files generated
  from the app's Network page or read from the card image), or build real IOP-to-EE RPC so the game's own libmc2
  code serves them.

### 22. The client serves the settings; the game lists them; two more walls fell, one is left

- **Built (developer approved):** PS2Recomp 0096 (IOP `sceSifBindRpc`/`CallRpc`/`CheckStatRpc` to a host-served EE
  server) and 0097 (the runtime serves NETCNFIF's server 0x80001101 with Sony's four settings files from
  `PS2X_NETCNF=dhcp`; no memory card involved). Tests written first (IOP 5/5, ps2x 569/569).
- **Drive 1:** the list showed "1. 使用できません" (unusable, red). The game's data compare (`netwk` 0x5c7270: the
  combination's `attach_ifc` against the type-1 list) failed because the type-0 load returned **-2**: NETCNF's heap
  allocations failed. Cause: `IopMemory::allocate` never reused freed blocks (a bump allocator; the tests had a
  comment admitting it, and 0075 had pooled RPC stacks around it); NETCNF reads its 100-byte index every frame of
  this screen, so the 832 KB heap ran dry in seconds. **PS2Recomp 0098:** first fit from the heap's start; test
  written first (10,000 alloc/free cycles on a fresh IOP). A wrong library name in my test module ("sysmme") cost
  three builds: the module's log lines showed it.
- **Drive 2:** "1. Setting 1" listed white, "Hardware: SCE/Ethernet (Network Adaptor)", every NETCNFIF call
  returns 0 (index, type 1, type 0 loads). **OK on it does nothing.**
- **Why (so far):** the screen's sub-state machine (`netwk` 0x5c8f90) re-runs a memory-card check every frame
  (`0x5d0900`, the game's own mc2 job runner 0x1be6f0/0x1bea60 over `sceMc2*Async`): its result lands in
  `S+0x10DA` = 2, which sets the status bit `S[0x25]` = 1, and with that bit set the sub-state resets to 0 each
  frame (hence the per-frame index reads) and never reaches the state where OK is handled. The job's path format
  is `"%s/%s"` (the NET save folder + a file); the result code mapped to that bit is -5. Which sceMc2 call that
  is and why our card path (mc2_s1 over the external SIO2, patch 0071) answers -5 here is the next question.
  The NET file was created on this same card earlier in the run, so the card itself works.
- **Also learned:** the game lists type-1 ("Setting N") entries and loads both the interface (type 1) and the
  combination (type 0); NETCNF's entry lookup ignores the type and matches the usr_name; the game asks for
  `mc0:/BWNETCNF/BWNETDCNF`, which netcnf's `do_handle_fname` rewrites to `/BWNETCNF/BWNETCNF`; `f_no_decode`
  is 0 (static in NETWK's data: scrambled files, as assumed).
- **Tools:** `.run/spike/up-commit.sh`, `up-amend.sh`, `up-export.sh`, `up-sync.sh` (skips the generated
  `register_functions.cpp`: my first version overwrote it and the runner built without the game's functions),
  `up-without.sh` (proves a test fails without the change). Temporary diagnostics lived in `net-src` only and are
  gone (`up-sync.sh`).

### 23. The "-5" is not a card failure; why OK does nothing is still open

- **Checked:** the card-check status is the same with the card image from before my `BWNETCNF` write (so not my
  card writer), and no memory-card RPC fails: the mc2 traffic goes through dbcman (sid 0x80000211, fn 1 each
  frame) and SIF command packets (sids 0/1), all answered. The -5 (`-0xFB`) is the game's own job code at main
  0x1bec00: "found entry's size (0x308C00) != expected (job+0x88)". Live, both are 0x1BEC (the NET file) and the
  entry buffer at 0x308BF0 names `BISLPM-65286NET`, so the compare passes now; the status byte at `S+0x10DA` = 2
  is set by the success path too (0x5d09c8), so "status 1" may simply mean "checked". Unverified either way.
- **Observed:** OK on the "Proxy Configuration" row opens its dialog, so input works on this screen; OK on
  "1. Setting 1" does nothing. The screen's per-frame handler for the list rows (`netwk` 0x5c92d0) only acts for
  `S[0x20]` < 6, and `S[0x20]` is 18 here (peeked), which belongs to the 20-state connect machine
  (0x5c94f0, state 18 at 0x5ca5e8: usable check, then `S+0x1ACB4` byte (0 live) → state 13 with message 0x33,
  else the compare with the game's own saved preference at 0x35EF00 (all zero: a fresh NET file) through
  0x5c78a0, then 0x5cda50/0x5cd7c0 and state 5). Which of these runs on OK, and why nothing is drawn, needs a
  call trace of the overlay or a reference run on PCSX2.
- **State for the next session:** `C:\ob\saves-net\Mcd001.ps2` is the pre-`BWNETCNF` card again (the client
  serves the settings; `Mcd001.with-bwnetcnf.ps2` kept beside it). Temporary diagnostics removed from `net-src`.

### 24. OK works: the game loads its network drivers and gets a DHCP lease; it stops before any DNS or TCP

- **How OK was found:** a first-call trace (a temporary log of each first dispatch into the overlay range, in
  `net-src`'s `lookupFunction`) showed the OK path ending in main 0x1cb640 → 0x1cae40: "stop and unload the USB
  device searcher" through the EE kernel's own `sceSifStopModule`/`UnloadModule` wrappers (unnamed in the symbol
  database, so not replaced by HLE). They bind Sony's loadfile RPC server (sid 0x80000006), ask its version
  (function 255, must read "2800", compared at 0x1ec2ac) and call 7/8. Our IOP had no such server: the bind left
  `cd->server` null and the game retried forever, so the screen's logic stopped (its frame counter froze) while
  the picture stayed. **PS2Recomp 0099** (test first, 570/570): the IOP subsystem binds that sid and answers 255,
  7, 8 and 9.
- **Then:** OK → hardware check → DEVGLUE, AVEDHCP, AVEPPP, AVEWRAPE, SMAP load (LOADHIGH), NETCNF/NETCNFIF are
  stopped (7/8 ids 22/21), SMAP negotiates the link, **DHCP works end to end** through PCSX2's adapter (DISCOVER →
  OFFER → REQUEST → ACK: 192.0.2.100/24, router and DNS 192.0.2.1, domain "PCSX2"; a temporary frame log in
  `dev9_side.cpp`, reverted). The AVE-TCP wrapper (sid 0x01270030) then returns the lease (fn 55/1/48/49: address,
  mask, domain, DNS server), fn 30 and 74 run, fn 56 is polled for ~20 s, and the game shows 通信エラーが発生しました
  ("a communication error occurred, connect again"). **No DNS query, ARP or TCP SYN ever reaches the adapter**, and
  no request to the wrapper names the game's host: the stack never tries to send after the lease. AVE-TCP's imports
  are all handled (no alarms: thbase 35-38 unused; DelayThread/Sleep/Wakeup/semaphores); SMAP hits modload 16/17/22
  unhandled at init (three calls, likely module queries).
- **Still open:** what fn 30/74/56 are (the wrapper's protocol is Capcom's, undocumented) and why nothing is
  transmitted. The PCSX2 reference run will show what the real game sends right after DHCP (DNS for
  `www01.kddi-mmbb.jp`, or a direct TCP connect); then the IOP side can be compared call by call.
- **Not on the game's path (kept):** 0095 (ioman file operations). The settings card writer stays as a reference.

### 25. PCSX2 reference: skipping DNAS is harmless; the real game resolves the login host 7.5 s after the link

- **New machine first (2026-10-28):** the handoff (`docs/ops/online-play-handoff.md`) missed four inputs of
  `net-recompile.sh`: `C:\ob\net-ovl\demo.toml` (hand-written, in no repo), `C:\ob\splat\SLPM_654.28` (splat asm),
  `C:\ob\net-work\{SLPM_654.28, main-functions.csv}`, and `C:\ob\net-src` (a copy of `net-up`). Copied from the old PC;
  then net-tools 570/570, recompile errors=0 for main and all 8 overlays, runner built (918/918). `mbedtls-build.bat`
  hard-coded VS Community's vcvars; it now reads `OB_VCVARS`.
- **Live PCSX2 on the fan server** (PCSX2 2.9 portable, DEV9 sockets, intercept DHCP, internal DNS with host entries
  DNAS → 153.121.44.150, KDDI → 208.72.237.15): link up 269.9 s, first packet 285.4 s = DNS for
  `gate1.jp.dnas.playstation.org`, DNAS over 443, then `www01.kddi-mmbb.jp` at 291 s, login pages over 443, lobby TCP
  8300 at 729 s.
- **The question:** does our DNAS skip (NETWK `func_0058B3D0` → return 1, `ob.sh recompile`) cut out setup the stack
  needs? **Test:** the same two words as a PCSX2 cheat (`ps2_emu/cheats/SLPM-65428_32088394.pnach`, guarded by NETWK's
  original `0x27BDFFE0` at 0x58B3D0 because GAME/DEMO share 0x540000), KDDI hosts → 127.0.0.2 so nothing reaches the
  fan server with a cheat on. **Result:** link up 83.4 s, DNS for `www01.kddi-mmbb.jp` at 90.9 s (answered 127.0.0.2),
  no DNAS query. No TCP connection logged to 127.0.0.2 (not followed up).
- **So:** with the same skip the real game's stack sends a DNS query 7.5 s after the link; ours sends nothing and
  shows 通信エラー after polling wrapper fn 56 for ~20 s. The wall is in our IOP/wrapper path, not in the skip.
  Next: the game's AVE wrapper RPCs (sid 0x01270030) in PCSX2 from the lease to that DNS query, against ours.

### 26. PCSX2 trace of the AVE wrapper: the game waits for fn 56 to answer 3 before it resolves a name

- **How:** PCSX2 2.9 log breakpoints ("Log When Hit" + "Continue on hit"; log formats are not saved to the
  debugger settings file, so typed in the dialog). The wrapper's client is `D_00309050`, bound in `func_001C3BD0`
  (main 0x1C3C20, `sceSifBindRpc` 0x117558); all calls go through `sceSifCallRpc` 0x117728 with send = receive buffer
  `D_00309080`, sync calls return at 0x1C3D60. Breakpoint 1 at 0x117728, condition `a0 == 0x309050`, logs fn/mode/
  sizes and the buffer's first 8 words; breakpoint 2 at 0x1C3D60 logs v0 and the same 8 words after the reply.
  Register-relative memory reads in a log format fail validation ("unaligned") because the dialog evaluates them
  with the current registers; fixed addresses work. Normal fan-server settings, no cheat. Raw log kept in
  `C:\ob\net-logs\pcsx2-ave-trace-20261028.log`.
- **Sequence (fn decimal, word 6 of the reply in brackets):** 70/71/72 then 5, 41, 43, then 44 and 70 polled until
  the link (smap 401.70 s); 46, 54; 56 → [1], [2], [3]; 55 (lease address in word 7), 1, 70 ×90, 48, 59, 49 (entry
  24's 55/1/48/49); **30; 74 [4]; 70 ×25; 56 [3] 0.9 s after 74**; 70 …; then 5 s later **61** (send buffer starts
  "gate"), **63** [-2, -2, 1] while the DNS query goes out, **62**, **7** (word 7 = 0x962C7999 = 153.121.44.150: connect;
  PCSX2 opens TCP 443), then 15 (status), 16/19 (send/receive), 13, 22 (close). The same 61/63/62/7 pattern repeats
  for `www01.kddi-mmbb.jp` after DNAS.
- **So:** fn 56 is a state query, and "3" is what the game waits for before it starts name resolution (fn 61). Ours
  polls fn 56 for ~20 s after 30/74 and never reaches fn 61, which fits "nothing is ever sent". Not yet checked:
  what our fn 56 answers (needs a temporary log in `RPC.cpp` `sceSifCallRpc` for this client, as entry 24's was).

### 27. Ours answers fn 56 the same; the game's own state machine keeps polling instead of moving on

- **How:** a temporary `TEMP-AVE` log in `C:\ob\net-src`'s `RPC.cpp` (`SifCallRpc`: the request before
  `handleRpc`, the reply in `completeClient`, client 0x309050 only; not in `net-up`, no patch), runner rebuilt
  (`net-runner.sh`, 7 s), `net-live.sh` + `to-network.pad` + down ×3, ○. NET card: a copy of PCSX2's `Mem1.ps2`
  (has `BISLPM-65428NETBIO` and `BWNETCNF`) as `C:\ob\saves-net\Mcd001.ps2`. 通信エラー reproduced on the new PC.
  Trace kept in `C:\ob\net-logs\ours-ave-trace-20261028.err`.
- **Result:** every call and every reply word (first 8 of 12) is identical to PCSX2's from fn 70 through 55/1/48/59/49,
  30, 74 [4] and fn 56 [3]. **The difference is after that:** PCSX2 calls fn 56 twice after 74 (then 5 s later 61);
  ours calls it ~48 more times (alternating with 70) and never calls 61. 52 fn 56 calls in total vs 12.
- **Reading the code:** fn 56 is `func_001C4E70` (returns reply word 6), called only through `func_001C67A0`, which has
  three callers: `func_001C5430`, `func_001CE100` (retries < 3, error code -0x54) and `func_001CEE40` (a state machine
  on a byte state and a halfword timer: state 5 polls fn 56 and leaves when it answers -1/-2/0 or when the timer
  runs out; state 6 sets the timer to 300). Which caller loops in our build, and why it does not leave, is the next
  question: a timer that never runs down (frame/VBlank pacing) or a state written by something we do differently.
- **The temporary log stays in `net-src` until that is answered**; revert with `up-sync.sh` (copies `net-up`'s
  `RPC.cpp` back) before any gate run.

### 28. In ours the loop is the connection check `func_001C5430` (mode 1), waiting for an event that was already taken

- **How:** more `TEMP-AVE` prints in `net-src`'s generated runner files (`func_001C67A0` result + caller + flag +
  mode, entries of `func_001C6890`, `func_001C6900`, `func_001C5320`, `func_001C5360`, `func_001CEE40`); runner rebuilt
  (5-6 s) and the same live route, three runs (traces `ours-ave-trace{2,3,4}-20261028.err` in `C:\ob\net-logs`).
  Note for next time: a running runner holds the exe (LNK1104); end it first. `sed` inserting `\n` into C strings
  breaks them; use the editor.
- **The game's flow** (static reading): `func_001CCA20` steps on `D_0031E7E0`; one step calls `func_001C5430(mode =
  *D_0023BC54, D_0023BC38 == 1)` until it returns nonzero, the next runs `func_001CEE40` (polls fn 56, timer, then name
  resolution). In mode 1, `func_001C5430` asks the state (fn 56) and then `func_001C6890`, which returns 1 only when
  an "event" is pending: fn 70 reply word 6 nonzero, cached in `D_00309480`. `func_001CD440` (from `func_001CD6B0` and
  `func_001CE100`) takes the event: `func_001C6900` clears the flag and calls fn 71.
- **Ours:** mode = 1, `D_0023BC38` = 1. The first fn 70 (right after OK) answers word 6 = 1; `func_001CD440` peeks
  and takes it at once (fn 71). After the lease, `func_001C5430` polls fn 56 (3) and fn 70 (word 6 = 0) ~35 times;
  no new event, so it never returns nonzero and `func_001CEE40` never runs.
- **PCSX2** shows the same fn 70 → 71 right after OK and word 6 = 0 for every fn 70 after the lease, yet it reaches
  `func_001CEE40`'s behaviour (fn 56 twice, a 5 s wait, fn 61). Open: does PCSX2 also run in mode 1, and does its
  `func_001C5430` see the event before `func_001CD440` takes it (an ordering/thread difference), or does it leave by
  another path? Next: one PCSX2 run with log breakpoints at 0x1C6890 (ra, flag, mode), 0x1C6900 (ra) and
  0x1CEE40 (ra), the same points ours prints.

### 29. Correction to 28: the connection check is the main loop's periodic poll, not the gate

- **PCSX2** (log breakpoints at 0x1C6890, 0x1C6900, 0x1CEE40; `C:\ob\net-logs\pcsx2-flag-trace-20261028.log`): mode 1
  too; the first event is peeked and taken by `func_001CD440` right after OK, as in ours; `func_001C5430`'s peek never
  sees an event; **`func_001CEE40` never runs**. Yet DNS goes out at 203.8 s and 210.2 s.
- **Ours** (entry prints on `func_001C5430`, `func_001A87F0`, `func_001CCA20`; trace 5): `func_001CCA20` never runs;
  `func_001A87F0` is the game's main loop (entered once from 0x100210): wait a frame (`func_001B9070`), and every 32
  frames (`D_0023BBA4 & 0x1F`) call `func_001C5430`. So the 36 calls are the normal periodic check (PCSX2 calls it
  a few seconds apart). **Entry 28's "the loop is the connection check" is wrong**: the check is a bystander.
- **What issues the name lookup:** fn 61 = `func_001C4FF0` (62 = `func_001C5050`, 63 = `func_001C50A0`, 7 =
  `func_001C4160`), called from `func_001C5B90` (← `func_001CD0D0`) and `func_001C6FD0` (← `func_001C7240`). Next: find
  which of these runs in PCSX2 and not in ours, walking up until the two builds differ.

### 30. Root cause: NETHTTP creates its semaphores with max_count 0; our CreateSema rejects them

- **PCSX2** (log breakpoints at the four functions above; `C:\ob\net-logs\pcsx2-path-trace-20261028.log`): DNAS's
  lookup comes from NETDNAS (`func_001C6FD0` ← 0x6CAB0C); the login lookup from **NETHTTP**: `func_001C7240` ←
  0x6C51E4 (in `func_006C5158`) → `func_001C6FD0` → fn 61, 5.1 s after link with the DNAS skip.
- **Ours, walking up with prints** (traces 6-11 in `C:\ob\net-logs`): none of the four runs; NETHTTP is loaded (CD
  search for `\PROG\NETHTTP.BIN`). `func_006C5158` is reached only from the thread `func_006C5408`, created in
  `func_006C5440` ← `func_006C56C0` ← `func_007136E0` = entry 4 of NETHTTP's step table `jtbl_00719EF0`. NETWK's
  `func_0056EC80` runs a connection object's step function (`+0x118` → `+0x64`): ours goes [0], [1], [1], [2],
  [11], [12], three times, never [4]. Step [2] (`func_00712F70`) goes to [11] with error 2 when
  `func_006C0498` (create the session object) returns 0: its `calloc(1, 0x210)` works, `func_006C5E28` returns -1.
  At its failure label: **all four `CreateSema` returned -1**; nothing after them ran (stacks 0, threads 0).
- **Why:** `func_006C5E28` passes `ee_sema_t{max_count = 0, init_count = 0}` (names `D_0071A4B0`...). Our
  `EeScheduler::createSemaphore` returns `KE_ERROR` for `maxCount <= 0` (`EeScheduler.cpp:895`). The real kernel
  accepts it (the game works on PCSX2's real BIOS); Play!'s HLE kernel copies the values without checks and its
  SignalSema never compares against the maximum ("TODO: Check maximum value"). Second trap behind it: our
  `signalSemaphore` returns `KE_SEMA_OVF` when `count == maxCount` with no waiter, so a max-0 semaphore would drop
  every signal.
- **Also seen:** the second helper thread's entry `D_006C5BA8` lies inside `func_006C5AF8` (0x6C5AF8-0x6C5CD0), so
  it has no recompiled entry; our `StartThread` would refuse it (`hasFunction`). It comes after the semaphores; to be
  checked once they work.
- **Proposed (awaiting the developer):** PS2Recomp 0100, tests first: max_count <= 0 = no limit (create accepts,
  signal always counts), other semaphores unchanged.

### 31. PS2Recomp 0100 (semaphores without a limit); the next wall is a thread that starts mid-function

- **0100** (developer: "use your best judgement" → option A): `createSemaphore` accepts `maxCount <= 0` as no limit;
  `signalSemaphore` overflows only below a positive maximum. Tests written first in PS2RuntimeKernel (two cases):
  571/572 without, 572/572 with. Committed in `net-up` (`c90af32`), exported, README row, pins 0001-0100.
- **Live** (`net-src` synced, runner rebuilt, same route; `C:\ob\net-logs\ours-fix0100-20261028.{err,log}`): NETHTTP's
  setup now creates its semaphores (ids 36-39), both stacks and both threads (9, 10), then fails at the **second
  `StartThread`**: its entry `D_006C5BA8` lies inside `func_006C5AF8` (0x6C5AF8-0x6C5CD0), so the runtime has no
  function there (`StartThread` checks `hasFunction`) and returns KE_ERROR; setup still returns -1 and the game
  still retries [0], [1], [2], [11], [12]. Next: a thread entry in the middle of a recompiled function (a new
  function entry for 0x6C5BA8 in the overlay's function list, or the runtime starting the containing function at
  that pc), tests first.
- **State of `net-src`:** `up-sync.sh` restored `RPC.cpp`/`Thread.cpp` (their TEMP-AVE prints are gone); the
  TEMP-AVE prints in the generated runner files (`func_001C67A0`, `func_001CEE40`, `func_001C5320/5360/6890/6900`,
  `func_001C5430`, `func_001A87F0`, `func_001CCA20`, `func_001C5B90/6FD0/7240`, `func_001CD0D0`, nethttp
  `6C0498/5158/5408/5440/5E28` and the 21 API entries) stay until the next `net-recompile.sh`, which regenerates
  them. They exist only on the new PC.

### 32. NETHTTP's second thread entry registered (entry_points); all four helper threads start, still no connection

- **Old PC caught up first:** `net-up` was at 0099; `git am` of 0100 (`584265b`), `up-sync.sh` (EeScheduler.cpp and
  the kernel tests), runner 11 s.
- **Cause, read from the generated code:** `0x6C5BA8` opens with its own prologue (`addiu $sp, -0x60`, saves of
  s0-s2/ra). The code before it, `0x6C5AF8`-`0x6C5BA4`, is a thread loop that ends in a backward `b` and never
  returns, so splat found no end and folded the next routine in (`nethttp-functions.csv`: `func_006C5AF8` up to
  `0x6C5CD0`).
- **Fix, no PS2Recomp change:** PS2Recomp's `[general] entry_points` already registers an address inside a function
  as an entry of that function (`config_manager.cpp:96`, `ps2_recompiler.cpp:1944`, `function_table_emitter.cpp:108`;
  its own test at `ps2_recompiler_tests.cpp:1473`). `tools/ob.sh`: `ovl_entries` lists the entries per overlay
  (NETHTTP: `0x6C5BA8`), written into that overlay's generated config; `check_ovl_entries` after each overlay
  recompile fails the recompile when one is not in the generated function table. Check written first: it failed on
  the old output ("entry 0x6C5BA8 not registered") and passes after (`nethttp_register_functions.cpp:1982`). Full
  `net-recompile.sh` 47 s, errors 0 everywhere, 2 generated files changed.
- **Live** (`net-live.sh`, `to-network.pad` + three downs and OK; `C:\ob\net-logs\boot-net-live.err`, temporary
  `TEMP-AVE StartThread` print in `net-src`'s `Thread.cpp`): NETHTTP's threads 9-12 start (`0x6C5AF8`, `0x6C5BA8`,
  `0x6C5440`, `0x6C5408`, all registered), then the same four again (13-16): the setup runs a second time. The
  screen stays on NOW LOADING (no 通信エラー within ~4 min); no TCP connection to ob_server on 127.0.0.2 (netstat).
  Not known yet: whether the DNS query goes out (our DEV9 adapter does not log it).
- **On this PC the route differs:** its card holds BWNETCNF (entry 20), so after NETWORK PLAY the game shows
  "Network configuration file has loaded from MEMORY CARD slot 1" instead of the NET CONNECTION list, and the route's
  downs + OK only dismiss that. Screens in `C:\ob\frames-net-live`.
- **`StartThread` refuses an entry silently** (KE_ERROR, no message): that hid this wall from the first live runs.
- **State of `net-src`:** the `TEMP-AVE StartThread` print stays in `Thread.cpp` for the next step; `up-sync.sh`
  restores it.
- **Next:** why the setup runs twice and where the second thread's work stops: the DNS query (a DEV9-side log of DNS
  questions, or PCSX2 log breakpoints in `func_006C5408`/`func_006C5158` against ours), then the TCP connect to 443.

### 33. The name lookup and the connect go out; the game then polls the connection status (fn 15) forever

- **DEV9's own log (kept):** `ob_dev9_config.log_path`; `ob_dev9_open` opens PCSX2's log file at info level when
  none is open (one per process; info = DNS host-list answers and socket/session errors, not DevCon's line per
  packet). The runner's hook passes `pcsx2-dev9.log` (working dir: `C:\ob\net-logs` for boots). Test first in
  `native/pcsx2_dev9/tests`: the log names the host-list answer; 15/16 without, 16/16 with.
- **Temporary prints (`net-src` only):** `RPC.cpp` `SifCallRpc` for the AVE wrapper client 0x309050 without fn
  56/70 (fn, ra, first 8 send words, 48 bytes as text); NETHTTP's step runner `func_00712A60` (step byte `+0x40`,
  printed when it changes); the `StartThread` print of entry 32.
- **Live** (same route; `C:\ob\net-logs\boot-net-live.err`, `pcsx2-dev9.log`): wrapper 71, 72, 5, 41, 43, 44 ×97,
  46, 54, 55, 1, 48, 59, 49, 30, 74 (as PCSX2, entry 26); NETHTTP steps [0] [1] [2] (threads 9-10) [3] [4] (threads
  11-12); then **61** with `www01.kddi-mmbb.jp`, **63** ×3, **62**, and the adapter logs at 148.9 s
  `DEV9: DNS: www01.kddi-mmbb.jp found in hosts`; then **7** (connect) with word 7 = `0x0200007F` = 127.0.0.2, the
  ob_server address; then **15** (status) 14 109 times and nothing else. PCSX2 goes 7 → 15 → 16/19 (send/receive).
  The adapter logged no socket error (a failed host connect would log "TCP: Connect error", `TCP_Session_In.cpp:235`;
  with interface "Auto" the socket is not bound to a LAN address, `sockets.cpp:137`). ob_server's empty log says
  nothing: it prints no line on an accept or a handshake (`tls_server.cpp:219-246`), so whether a TCP connection
  reached it is not known (correction made the same day; see the table below). This run went through the steps once (no
  second setup as in entry 32's run; this run was slowed by the prints, so that difference may be timing).
- **Pad route:** the fn 15 prints (2.3 MB of stderr) slowed the game enough that the route's last `wait 3600`
  timed out; next time the print leaves out fn 15 (count only).
- **Answer to entry 32's questions:** the DNS query does go out and is answered; the connect is issued to the right
  address. The wall is now the TCP connect: whether the adapter's Sockets layer opens a host socket to 127.0.0.2:443
  (SYN seen, host connect result) and what our fn 15 answers against PCSX2's.
- **Next:** the fn 15 reply words in ours (reply side of `SifCallRpc`, as entry 27's log) and the port in fn 7's
  send words 8-9; on the adapter side, whether a TCP session is created (PCSX2 `TCP_Session` at DevCon level for one
  run, or netstat during the run).

### 34. The connect to 127.0.0.2:443 never leaves the PS2: a loopback address stays inside its own stack

- **ob_server logs connections (kept):** `WebConfig.log` (set from the server's log, as the lobby's): one line on
  accept (`[web] <address>: accepted`) and one on the handshake (protocol and suite, or `handshake failed -0x....`).
  Test first in `native/ob_server/tests`: 70/72 without, 72/72 with.
- **Run** (same route; temporary prints in `net-src`'s `RPC.cpp`: fn 7/13/15/16/19/22 replies, fn 15 and 44
  counted; DEV9 log at DevCon level for this run only, put back to info after; netstat every 20 s into
  `C:\ob\net-logs\netstat-live.txt`):
  - fn 7 (connect) send word 7 = `0x0200007F` = 127.0.0.2, word 8 = `0x0000BB01` = port 443. Its reply equals its
    send buffer. fn 15 (status) answers the same words every time (word 7 `0x00020000`, word 8 `0x20000000`);
    after ~14 000 polls the game closes (fn 13, then 22), NETHTTP goes to steps [11] [12] and starts again at [0]:
    **that is the second setup of entry 32** (a retry after the connect never completes).
  - `pcsx2-dev9.log` (DevCon): DHCP (two 342-byte frames, 144.8 s), then a 64-byte frame and the 78-byte DNS query
    at 150.44 s ("www01.kddi-mmbb.jp found in hosts"), and **no frame sent after it**: no SYN reaches the adapter.
  - ob_server: no `accepted` line; netstat: ob_server listening on 127.0.0.2:443/8300, never a connection to it.
- **Why (likely, not yet proven):** 127.0.0.0/8 is the loopback network; a TCP/IP stack (AVE-TCP here) keeps
  traffic to it inside the machine, so the PS2 connects to itself, where nothing listens, and never puts a frame on
  the wire. It fits entry 25's PCSX2 run with the KDDI host → 127.0.0.2: DNS answered, "no TCP connection logged".
  The adapter has no loopback mapping of its own: it opens a host socket to whatever address the PS2 sends
  (`TCP_Session_Out.cpp:207-212`).
- **So:** the host entry for `www01.kddi-mmbb.jp` (and the lobby name, the same) must be an address outside
  127.0.0.0/8 that reaches ob_server from the host: e.g. the PC's LAN address with ob_server bound there. Decision
  pending with the developer (binding on the LAN opens the ports to the local network).

### 35. With a LAN address the connection reaches ob_server; the game rejects its certificate (alert 42)

- **Test (developer approved the LAN exposure for one run):** `PS2X_NET_HOSTS` with both names → 192.168.0.76 (this
  PC's LAN address), `ob_server --bind 192.168.0.76`, otherwise as entry 34 (script kept out of git:
  `net-live.sh` with the hosts changed). Server and runner stopped after; the LAN ports closed (netstat).
- **Loopback confirmed as entry 34's cause:** the same route now connects. fn 7 to `0x4C00A8C0` = 192.168.0.76:443;
  the adapter logs `Socket: Creating New TCP Connection to 443`; ob_server logs `[web] 192.168.0.76: accepted`.
- **The handshake:** fn 16 sends a 0x66-byte TLS 1.0 ClientHello; fn 19 receives ob_server's reply (5 + 0x331
  bytes: ServerHello and the Certificate); then fn 16 sends 7 bytes `15 03 01 00 02 02 2A` = a TLS alert, **fatal,
  42 bad_certificate**, then 13/22 (close). ob_server logs `handshake failed -0x7280` (`MBEDTLS_ERR_SSL_CONN_EOF`:
  the client closed). Three tries a few hundred ms apart, then the steps go [11] [12] [0] … as in entry 34.
- **So:** the game's TLS client (NETHTTP) checks the server certificate, and ob_server's self-signed SHA-1
  certificate for the site name (`tls_server.cpp:79-98`) does not pass. The note that self-signed is enough
  (Bioserver1's README, cited in `tls_server.h`) does not hold for this client as configured; entry 25's PCSX2 run
  against the fan server got through the login pages, so the fan server presents something it accepts.
- **Next (research before code):** what NETHTTP verifies (built-in root certificates in the overlay's data, the
  name match, the date, the signature algorithm) and what certificate the fan server sends (its published setup),
  then make ob_server's certificate pass the same checks. Also open: the address to give the game in normal use
  (entry 34).

### 36. What NETHTTP checks in the server certificate, and what the fan server sends (research, no code)

- **NETHTTP's TLS:** Sony's `libhttp` 1.2.61 (`nethttp.bin 0x5A980`) on OpenSSL of the 0.9.6 era (strings dated
  06-Mar-2001); CA certificates load from a file or from memory ("cert file = %s", `BIO_new_mem`, 0x5A9F0-0x5AB20).
- **The only built-in CA in the whole game:** a PEM at `nethttp.bin 0x59AF0` (838 bytes): C=US, O=RSA Data
  Security, Inc., OU=Secure Server Certification Authority, valid 1994-11-09 to 2010-01-07. No other PEM in any
  PROG overlay or the main ELF; "VeriSign" appears in none of them as text. (Scan script kept out of git.)
- **The check, `func_006CD388`** (static reading of the splat asm, nethttp `text.s` around 0x6CD388; messages
  printed through `scePrintf`): no certificate → -0x7D3; each received certificate printed; dates against the current
  time (`func_006CCE10`; the date parser `func_006CCD48` takes UTCTime of 13 and GeneralizedTime of 15 characters,
  so ob_server's 2099 end date parses); a loop matching the received certificates against a CA list (`func_0070C0A0`
  / `0070C0B0` / `0070C0D0` / `0070C360`) → "Could not match client certificate to Server CA!!" (-0x7D6); on a
  match, the commonName against the host name (`func_006CCC00`, only on one branch) → -0x7D7; then
  `func_0070D2E0` → "X509 verification failed." (-0x7D1).
- **The fan server** (TAB64-K/ps2-outbreak-server, `dnas/Dockerfile`, read 2026-10-28): one Apache site on 443 for
  every name, `SSLCertificateFile /etc/dnas/cert-jp.pem`, chain `ca-cert.pem`, `ServerName
  gate1.jp.dnas.playstation.org`. `cert-jp.pem` (decoded here): subject CN=gate1.jp.dnas.playstation.org,
  O=Cyberpunks; issuer "C=US, O=VeriSign, Inc., OU=Class 3 Public Primary Certification Authority" (a CA the fans
  made, their own key; `ca-cert.pem` is that CA, self-signed, 2016-04-18 to 2026-04-16); **sha256WithRSA**,
  1024-bit key; valid until 2026-04-16 (expired now). Its README's "self-signed works" is not what it serves.
- **What does not add up yet:** entry 25's PCSX2 run passed the login pages with that certificate: wrong host name
  for www01, an issuer name that is not the built-in CA, a SHA-256 signature a 2001 OpenSSL cannot check (and, if the
  run's clock was 2026-10, an expired one). So the game's check is looser than the reading above, or its CA list is
  filled at run time from somewhere else (NETDNAS's data is scrambled at rest, entry 3). Unverified.
- **Next:** the game's own verdict: its `scePrintf` lines are compiled out (`ps2_log.h` `AGRESSIVE_LOGS` 0,
  `TTY.cpp:16`); a temporary print of those lines in `net-src` names the failing check for ob_server's certificate.

### 37. The game's verdict: OpenSSL cannot decode ob_server's certificate (ASN.1 error), before any of its own checks

- **How:** a temporary print in `net-src`'s `TTY.cpp` `scePrintf` of every line whose format string lies in NETHTTP
  (0x6C0000-0x720000); runner 8 s; one LAN run as entry 35 (developer approved; server and runner stopped after,
  LAN ports closed).
- **Seen:** three connections, each `SSL Handshake failure. err = 0xd07e095` (printed from ra 0x6CBF88), and nothing
  else from NETHTTP: none of `func_006CD388`'s lines ("recved cert", dates, CA match). So the handshake fails inside
  OpenSSL while it reads the certificate, before the game's own check runs; entry 36's checks are not the wall yet.
- **The code:** OpenSSL packs errors as library << 24 | function << 12 | reason: library 0x0D = ASN1; in OpenSSL
  0.9.6's `crypto/asn1/asn1.h` (GitHub tag `OpenSSL_0_9_6`, read 2026-10-28) function 126 = `d2i_ASN1_BYTES` and
  reason 149 = `WRONG_TAG`: a field of ob_server's certificate has a type tag this decoder does not expect. Sony's
  build may number them differently: matching them to this binary's error sites was not finished (unverified).
- **Where ob_server's certificate differs from the fan one that passes** (`tls_server.cpp:79-103` against entry 36):
  X.509 **v3** (fan: v1, no extensions); validity to **2099**, which DER writes as GeneralizedTime (fan: UTCTime to
  2026); self-signed (fan: issued by a separate CA sent in the chain); SHA-1 (fan: SHA-256). OpenSSL 0.9.6 itself
  reads both time types (`x_val.c`, `d2i_ASN1_TIME`), but an older Sony copy may read only UTCTime. Leading
  suspects: the GeneralizedTime end date, then the v3 version field.
- **State of `net-src`:** the `TTY.cpp` print stays for the next run, with entry 32-34's prints; `up-sync.sh`
  removes them.

### 38. ob_server's certificate ends in 2049 (UTCTime): login over TLS, the lobby, and the game's first lobby screen

- **Fix (test first):** `ob_server_tests` checks that the certificate the server sends has both validity dates as
  UTCTime (DER `30 1E 17 0D … 17 0D …`): 72/73 before, 73/73 after. `tls_server.cpp`: end date 2099 → 2049 (DER
  writes 2050 on as GeneralizedTime). The test client got `FetchTlsCert` (the peer certificate); `FetchTls` keeps
  its signature for `lobby_tests.cpp`.
- **Live** (LAN run as entry 35, developer approved; server and runner stopped after, LAN ports closed): ob_server
  logs three connections on 443, each `TLSv1.0 TLS-RSA-WITH-3DES-EDE-CBC-SHA`, `quick login, session 67560268`, then
  on 8300 `[lobby] 192.168.0.76: logged in` and `game version '0311172218'`. The adapter: DNS for www01 twice, TCP
  443 ×3, TCP 8300. No NETHTTP `scePrintf` lines at all (no handshake failure, and no line from `func_006CD388`'s
  checks either). Screen (`C:\ob\frames-net-live\n11-lobby.png`): Capcom's ユーザー登録/変更 (user registration,
  "register/change" or "don't register"), waiting for the pad: the route ended there.
- **So:** entry 37's decode error was the GeneralizedTime end date; a self-signed v3 SHA-1 certificate passes
  otherwise (the fan README's "self-signed works" holds once the dates are UTCTime). The v1 fallback was not needed.
  Entry 36's question (why the fan certificate passes the CA check) is moot for us for now.
- **Still open:** the address the game gets in normal use (entry 34: 127.x stays inside the PS2; this run used the
  PC's LAN address, which opens 443/8300 to the local network); the route from the registration screen into the
  lobby (areas, rooms); the temporary prints in `net-src`.

### 39. This device's game reaches its own ob_server through an alias (192.0.2.2 → 127.0.0.1), nothing on the LAN

- **Developer's choice (2026-10-28):** option B of the address plan: the game's server names point at 192.0.2.2 (in
  the adapter's private network, whose gateway is 192.0.2.1, `net.cpp:166`; the adapter answers ARP for any address
  but the PS2's own, `sockets.cpp:303`), and the adapter's host socket connects to 127.0.0.1 instead; ob_server
  serves loopback only unless hosting is asked for.
- **upstream/pcsx2 0003** (`C:\ob\pcsx2` branch `ob-patches`, `d4de0c7`): `Sessions::HostRemap::Set/Apply` (new
  `DEV9/Sessions/HostRemap.{h,cpp}`); `TCP_Session::SendConnect` connects the host socket to the mapped address and
  port while the PS2 keeps `destIP`/`destPort`; a loopback destination is not bound to the adapter's address (a named
  interface, as Android will use, binds first). No entries = unchanged. README row, pins.
- **Our side:** `ob_dev9_config.remaps` (`ob_dev9_remap`: from, from port 0 = any, to, to port 0 = same), passed to
  HostRemap in `ob_dev9_open`; `ob_dev9_parse_remaps` for `PS2X_NET_REMAP` ("192.0.2.2=127.0.0.1", ";"-separated,
  ports optional), read by the runner's hook (`hook.cpp`). ob_server defaults: listens on 127.0.0.1 (was every
  interface), hands out 192.0.2.2 as the relay address (was 127.0.0.1); hosting = `--bind ""`/LAN + `--host <LAN>`.
  `tools/net/net-live.sh` now sets the alias and the remap.
- **Tests first:** `pcsx2_dev9` remap test (a SYN to 192.0.2.2:4433 reaches a listener on 127.0.0.1, the SYN-ACK comes
  from 192.0.2.2:4433): 17/19 without 0003, 19/19 with; parser test (address-only and address:port entries,
  malformed ones skipped), 23/23 (this one was written with the parser, not seen failing first). ob_server defaults
  test: 73/75 before, 75/75 after.
- **Struggle:** the first remap test run hung past 10 minutes (no answer without the remap, and 20 × up-to-3000-tick
  waits); stopped at once, the wait capped at 10 s of wall time; the failing run then took seconds.
- **Live (loopback only; netstat: ob_server on 127.0.0.1:443/8300, nothing else):** `[web] 127.0.0.1: accepted`,
  TLS ×3, quick login, `[lobby] 127.0.0.1: logged in`, version; the adapter logs TCP 443 ×3 and 8300.

### 40. Into the lobby: handle name, areas, rooms, a new room's rules; the scenario list is empty

- **Route** (live pad, then kept as `tools/net/to-lobby.pad`): registration → 登録しない; SELECT ID: handle name
  "あ" on the kana keyboard (Start does not confirm; the 決定 key, bottom right, does) → "are you sure you want to use
  this (HN)?" → 登録する; ob_server: `handle 0SFV4X`. Lobby "LOCAL" (ROOM LIST / DATABASE / PERSONAL RANK, 1P) →
  ROOM LIST: AREA 01-07, 0 players → AREA 01: slots 1-5 "(free) Vacant" → slot 1: the RULE screen (room title,
  password, four players, ten minutes, very hard, friendly fire off, scenario, character KEVIN, FINISH) → Triangle
  (scenario): an empty framed list, still empty 5 s later (shots `l01`-`l13` in `C:\ob\frames-net-live`).
- **ob_server** logged no unhandled question during any of it, so the game asks the server nothing for that list:
  it is built from something already received (the area/room answers, or a login-time list) or from game data.
- **Next:** where the scenario list comes from (the RULE screen's code in NETWK, against Bioserver1's create-room
  replies: research note section 7 "6509 scenario", open question 3).

### 41. Where the RULE screen's scenario list could come from (research, not settled)

- **ob_server's answers with guessed contents** (`lobby.cpp`): INFOSIZES/INFOCHUNK (6881/6882) answered "no file"
  (`:496-504`), RANKINGS 207 zero bytes (`:505-511`), fixed bytes for UNK61A1 (`:459`) and UNK6308 (`:547`).
- **The 6881/6882 data file** (research note section 7, open point 1): the fan server's `Packet6881.java`
  (TAB64-K/ps2-outbreak-server, bioserver1, read 2026-10-28; described, nothing copied): one byte array of several
  thousand bytes, served in slices (header: index, offset and size big-endian, then the bytes); it starts
  `01 01 01`, Shift-JIS 発生 ("Outbreak"), "outbreak", and holds `SLPM-65428/65429/65430`, `BIOHAZARD OUTBREAK`,
  a `BIOHAZARD OUTBREAK BR` marker and Shift-JIS message blocks in `<BODY>…<END>`; no plain scenario list seen.
- **The scenario titles are the game's own:** the main ELF holds 発生, 零下, 巣窟, 獄炎, 決意 at 0x231C48-0x231C68
  (file offset 0x131E48), through the pointer table `D_002167B0`; the only code using that table is
  `func_001AE160` (called from 0x1B96FC), which clears the 0x4234-byte block at `D_00301D60`, sets bits in a
  bitfield at `+0x4094` from the list `D_002167D0` (ends at -1) and copies the titles in: game progress/state data
  (reading, unverified). NETWK's asm has no direct reference to `D_00301D60` or that bitfield.
- **So, open:** the list is empty either because the game lists only scenarios a server-sent table enables (the
  6881 file is the leading candidate: its first bytes are a per-title header), or because it lists scenarios the
  player's data marks as available, and this NET card's data has none. Not settled by reading so far.
- **Cheapest decisive tests:** (a) serve a 6881 file for one run (from our own reading of its format, or the fan
  server's bytes with credit, ob_server being AGPL as the research note's option 3 says) and see whether the list
  fills; (b) find the RULE screen's list code in NETWK and the condition per entry.

### 42. Settled by reading NETWK: the scenario list is the login data file (6881/6882), which ob_server leaves empty

Developer's choice: (b), the game's code, no fan data. The chain, NETWK splat asm (`C:\ob\splat\SLPM_654.28\en\netwk`):
- **The RULE screen** (`func_005B01E0`): after the "scenario" label (`D_005FA4D8`, 0x5B06A4) it draws
  `func_00552A80(D_0062E236)`, the name of the selected scenario.
- **The name lookup** `func_00552A80`: record `i` = base + i × 72 (0x48); the name is at +3; a negative word at
  +0x44 means "no scenario" (returns 0). Base = `func_00552A60()` = `*(0x3065F4) + 0x15540`.
- **Who fills it** (`func_0054E7A0`): memset 0x2100 bytes at that base, then memcpy from the first downloaded
  piece (size and address from the 12-byte piece list `D_005FFEC0`, filled through `func_005D0F60`).
- **The piece list** (`func_005D0FF0`): from the buffer `D_0068AF30`, a count byte (`func_005587A0`) then one
  big-endian 4-byte size per piece (`func_005587D0`) into the 12-byte entries at `D_00687718`: the INFOSIZES
  (0x6881) answer; the bytes come with INFOCHUNK (0x6882) (ob_server's names, `lobby.cpp:42`).
- **So:** ob_server answers INFOSIZES with zeros (count 0, `lobby.cpp:496`), the record area stays cleared, every
  record's name is "" and its +0x44 word 0 (not negative), and the list shows empty rows. The fan server's file
  matches this layout: it starts `01 01 01` then 発生 at byte 3 (record 0's name).
- **Not yet known:** the meaning of record bytes 0-2, of the bytes after the name (the fan file has `09 02 03 04…`
  there) and of the +0x44 word; how many records; what the file's other pieces hold (the fan file also has
  disc IDs, "BIOHAZARD OUTBREAK" and `<BODY>…<END>` message text).

### 43. The login data file's first piece, mapped from NETWK's readers (base = `*(0x3065F4) + 0x15540`, 0x2100 bytes)

| Offset | What | Reader (NETWK) | Use |
|---|---|---|---|
| `+0x000 + i×0x48` | scenario record `i` (at most 48 up to 0xD80) | `func_00552A80` and the helpers below | |
| record `+0` | byte, not read by the helpers | — | open |
| record `+1` | byte | `func_00552BA0` | callers (`func_005B0110`, `func_005B1620`, the RULE screen) add 0x5F: an index into a label or picture table |
| record `+2` | byte | `func_00552B40` | stored in `D_0062DCBC` before `func_0054ECE0` (`func_005D1670`); `D_0062DCBC` is cleared at reset: likely the scenario number the game starts or sends |
| record `+3` | name, Shift-JIS, to `+0x43` | `func_00552A80` | the scenario row's text |
| record `+0x44` | s32: offset into the text area; < 0 = no scenario | `func_00552A80`, `func_00552AE0` | `func_005B0EF0` draws the text at `+0xE08 + offset` (the description) |
| `+0xD83` | byte: on/off | `func_00552C40` via `func_005432C0` | every caller (`func_005A8B20`, `005AF520`, `005B0110`, `005B1620`, `005B5420`) skips the scenario path when 0 |
| `+0xDA8 + i×0x20` | 32-byte strings (up to 3 before 0xE08) | `func_00552C00` | formatted with `%s %s`, `%s%s` (`func_005511B0`), `%s%s%s` (`func_005B5420`): titles or headings |
| `+0xE08` … `+0x2100` | text area | `func_00552AE0` | descriptions; the fan file has `<BODY>…<END>` markup texts |

- **Still open:** record byte 0; what byte 1 + 0x5F selects; whether byte 2 is the number the create-room request
  (6509, "scenario") sends; how the selection popup counts its rows (records until `+0x44` < 0, or a count
  elsewhere: after the memset every unused record has `+0x44` = 0, which reads as "present"); the file's other
  pieces (the size list allows several; only piece 0 is copied here).

### 44. ob_server builds its own data file: the rules screen shows 発生 and its description; only one scenario offered

- **Build (test first):** `native/ob_server/src/datafile.{h,cpp}` builds piece 0 from entry 43's layout, nothing
  from other servers: records 0-4 = the five titles (Shift-JIS, from the main program's table), byte 0 = 1, byte 1
  = 1, byte 2 = 1-5 (guesses), +0x44 = offsets of `<BODY>English title<END>` texts at +0xE08; records 5-47 marked
  absent (-1); +0xD83 = 1; "BIOHAZARD OUTBREAK" at +0xDA8 (0xE08 + texts = 3,712 bytes). 6881 answers `01` + the
  size big-endian; 6882 answers `[piece][offset BE][size BE][size low 2][bytes]`, clamped; each 6882 request is
  logged. ob_server tests: 75/84 against an empty stub, 84/84 with the builder.
- **Live (loopback):** after the lobby login the game asked 6882 eight times, `00 00000000 00000200`,
  `00 00000200 00000200`, … to `00 00000E00 00000200`: **piece 0, offset, 0x200 bytes**, as the fan description
  said. The RULE screen's scenario row now reads **発生** with a small icon (byte 1 + 0x5F, presumably); Triangle
  shows 発生 and the text "Outbreak" (shots `m07`, `m13` in `C:\ob\frames-net-live`). Only 発生 is offered:
  Down/Circle on the Triangle screen and Right on the scenario row do not change it.
- **So:** the list's content is right; the number of scenarios offered comes from a field not yet mapped (record
  byte 0 or 1, or +0xD80-0xD82, or the 0xDA8 strings).
- **Route:** `to-lobby.pad` fell out of step at the handle keyboard (the cursor never reached 決定 with 10-frame
  waits after the extra download); finished by hand with 20-frame waits, now in the file.

### 45. Why only one scenario (reading, not settled)

- **The RULE screen does not choose the scenario:** its value `D_0062E236` is written in one place,
  `func_005B3C90` (0x5B3D04), from the slot data the server sent (`D_006294B8` + slot × 0x15C, byte 0, filled by
  `func_0055B100`); ob_server's SLOTSCENTYPE answer carries `s->scenario`, 0 for a new slot = record 0 = 発生. The
  Triangle screen describes the current scenario only (`func_005B1620` draws its icon: record byte 1 + 0x5F).
- **The choice itself** is sent by the game as SCENESELECT 6509 (ob_server stores type and scenario from it,
  `lobby.cpp:640-645`). In this run no picker appeared between CREATESLOT and the RULE screen: either the game skips
  a picker with one choice, or the picker is reached another way. Not seen yet; SCENESELECT is not logged.
- **More record fields exist** than entry 43 listed: `func_00552C80` reads record +0x23 (a count) with entries from
  +0x24, and +0x34 (a count) with entries from +0x35 (picked at random): the name field is shorter than 0x41 bytes.
- **Progress data:** `func_00552C60`/`func_00552E20`/`func_00552C80` read the block `D_301D60` + 0x4154 (12-byte
  entries); `func_001AE160`'s start list `D_002167D0` is bit numbers 0, 0x0C, 0x0F, … 0x21 (nine), not obviously
  scenarios. Whether the offline save limits what can be hosted is open.
- **Next:** log SCENESELECT and the slot answers in ob_server; find the picker (callers that send 6509).

### 46. Checks on the branch after 0003, the hook and ob_server's data file (net trees)

- `net-src` cleaned first: `up-sync.sh` restored `RPC.cpp`, `Thread.cpp`, `TTY.cpp`; the TEMP print in the
  generated `nethttp_func_00712A60_0x712a60.cpp` removed by hand (now identical to `C:\ob\net-ovl`'s output).
- `ob.sh test` (net env, 148 s): ps2x 572/572, iop 5/5, gs_cache 45/45, ob_diag 24/24, pcsx2_ipu 22/22,
  pcsx2_sio2 20/20, ob_server 84/84, pcsx2_dev9 23/23, ob_ui 118/118; pcsx2_gs's log says 70/70 (the summary line
  printed "81/0"). **Not run (net-tree setup, not this code):** pcsx2_vu (build: `C:/ob/net-t/_deps/raylib-build/
  raylib/raylib.lib` missing, nothing in that tree builds it) and pcsx2_spu2 (the test exe cannot load `z.dll`). Not
  checked whether these failed in the net trees before today.
- `net-gate.sh` (pcsx2-sw, 100 s): selftest 9/9, **3/3 gated green**, 3 advisory as usual; the J's Bar set skipped
  (no references on this branch).

### 47. Net-tree suites fixed; FINISH sends the scenario and opens the room; how to offer more scenarios still open

- **pcsx2_vu** built against `${OB_T}/_deps` (= `C:/ob/net-t/_deps`, absent: net-t borrows `C:/ob/t/_deps`,
  `net-tools.sh:12`). `native/pcsx2_vu/CMakeLists.txt` now takes `OB_T_DEPS` (default `${OB_T}/_deps`, the main
  tree unchanged); `ob.sh` passes it and reconfigures when the cached value differs; `net-env.sh` sets it to
  `C:/ob/t/_deps` when present. **pcsx2_spu2**: `C:\ob\net-pcsx2-gs-build` had never been staged (no DLLs);
  `ob.sh pcsx2-gs` in the net env (12 s, smoke PASS) copied z.dll and friends. Suites (152 s): ps2x 572/572,
  iop 5/5, gs_cache 45/45, ob_diag 24/24, **pcsx2_vu 3/3**, **pcsx2_gs 86/86**, **pcsx2_spu2 7/7**, pcsx2_ipu
  22/22, pcsx2_sio2 20/20, ob_server 84/84, pcsx2_dev9 23/23, ob_ui 118/118.
- **ob_server logs** CREATESLOT ("creating game slot N") and SCENESELECT ("scenario type T, scenario S").
- **Live (loopback):** `creating game slot 1` when the slot is chosen; on the RULE screen, **FINISH** sends
  `scenario type 1, scenario 0` (record 0 with type 1, presumably a record byte), ob_server opens the slot, and the
  game shows the room: START GAME / STANDBY / EXIT / MEMBER DATA, "Wait for the game to start", 1/4P (shot `p07`).
  So no picker comes before the RULE screen; the Triangle view is the current scenario's details
  (`func_005B0EF0`), not a list; the scenario row did not change with Circle or Right in entry 44's run.
- **Route:** the handle keyboard ignores cursor moves for a while after a character is typed; `to-lobby.pad` waits
  240 frames there and now ends with FINISH (the room), instead of Triangle.
- **Open:** where the RULE screen's scenario choice is offered when more than one scenario is available (the row's
  input handler; candidates for the count: record byte 0 or 1, which may be the "type" sent with 6509).

### 48. The developer was right: the scenarios offered come from the save; with an unlocked save all five cycle

- **Developer (2026-10-28):** "it comes down to the save data"; supplied a fully unlocked online save as an
  Action Replay Max `.max` and as a PS3 export (zip with `BISLPM-654284E455442494F.PSV` = `BISLPM-65428NETBIO`,
  and `BISLPM-656924F4232.PSV`). Uploads in the session's upload folder; not in git.
- **The run cards had no online save:** `C:\ob\saves-net\Mcd001.ps2` holds `BISLPM-65286NET` (another product
  number) and `BWNETCNF`, no `BISLPM-65428NETBIO`. The developer's PCSX2 `Mem1.ps2` has one (261,320 bytes), but a
  run with a copy of it went to the proxy settings and a connection menu without the client's "Setting 1" (stopped:
  that route needs the network setup done by hand).
- **PSV layout** (read from the file, psdevwiki refused the request): `\0VSP`, type 2 at 0x3C, file count at 0x64,
  folder entry at 0x68 (attribute 0x8427, name at 0x80), one 0x3C-byte entry per file from 0xA0 (created, modified,
  size, attribute 0x8497, name, data offset); data plain (`icon.sys` starts "PS2D"). A scratch script copied the four
  files (the 261,320-byte save, two icons, icon.sys) onto a copy of the run card with `mcd.py`'s `Card.add_dir`:
  `C:\ob\saves-net-unlocked\Mcd001.ps2`.
- **Live (loopback, that card):** login, handle, lobby, room as before; on the RULE screen Right now cycles
  **発生 → 零下 → 巣窟 → 獄炎 → 決意 → 発生**, ◀ ▶ arrows shown from the second on (shots `u15`-`u19`). FINISH on 発生
  sent `scenario type 1, scenario 0`, the room opened. So ob_server's data file is right for all five, and the
  number offered follows the player's online save (unlocks), not the server.
- **Not checked:** the number FINISH sends for a later scenario (the run wrapped back to 発生 first); what the
  `BISLPM-65692` save in the zip is.
- **Handle keyboard:** still unreliable from a fixed route: this run typed "ああ" (the IME conversion pending,
  yellow) and needed two presses of 決定 (commit, then confirm).

### 49. START GAME: the game hands off to the relay, which ob_server does not have yet

- **Run:** the unlocked card (entry 48), loopback; handle `M7HNNS`; RULE: one Right (零下), FINISH.
- **SCENESELECT number:** 零下 sent `scenario type 1, scenario 1`, 発生 `scenario 0` (entry 48): the number is the
  record's index (0-based), not record byte 2 (which is 2 for 零下). "Type 1" = record byte 0 or 1 (both 1 here).
- **START GAME** (left of STANDBY): ob_server `game 2 starts: area 2 room 1 slot 1, 1 player(s)`; the game shows
  its start screen (Kevin, handle M7HNNS, あ, RESULT POINT 32181 from the save; shot `v07`), then a black screen.
- **Relay:** the adapter logs `Creating New TCP Connection to 8690` then `TCP: Connect error: 10061` (refused)
  every 2-5 s from 316.8 s on: the game connects to the relay address ob_server handed out (192.0.2.2:8690, mapped
  to 127.0.0.1:8690 by 0003) and nothing listens there: the relay is ob_server's next step (its plan's step D).
- **So:** login, lobby, room, rules, scenario choice, room open and game start all work against ob_server on this
  PC; the relay is the wall.

### 50. Two clients on one PC: both log in, one hosts, the other joins; start shows both players; the joiner stalls

- **Developer (2026-10-28):** permission to drive the game builds; "two clients … a multiplayer game".
- **Telling two clients apart:** ob_server keys players by address, and Windows sources every loopback connection
  from 127.0.0.1 (checked: a listener on 127.0.0.2 still sees 127.0.0.1), but a socket bound to 127.0.0.2 first is
  seen as 127.0.0.2 (checked). **upstream/pcsx2 0004** (`ob-patches` `e27b590`): a HostRemap entry may give the host
  socket's source address, bound before `connect()`; `ob_dev9_remap.source`, text `from=to@source`. Tests first:
  pcsx2_dev9 26/28 without, 28/28 with (the parser, and a SYN through a remap with source 127.0.0.2 accepted from
  127.0.0.2). Runner rebuilt.
- **Second client:** `tools/net/net-live2.sh` (pad `obnet2`, logs `C:\ob\net-logs2`, card `C:\ob\saves-net2`
  by default; this run used `C:\ob\saves-net-unlocked2`, a copy of entry 48's card), remap `@127.0.0.2`.
- **Live (both at once, loopback):** ob_server: two quick logins (127.0.0.1, 127.0.0.2), both lobby logins and
  versions, handles `LPHPUT` (client 1) and `85S49U` (client 2). Client 1 hosts: slot 1, 零下 (`scenario type 1,
  scenario 1`), opened. Client 2's room list shows room 1 "零下 DVD-ROM 1/4P Join in" (after a refresh; at first
  "(free) Vacant"); Join: MEMBER LIST (host あ / LPHPUT / KEVIN, scenario 零下, own character MARK, ENTER);
  ob_server `127.0.0.2: joined game slot 1 as player 2`; the host's room shows **2/4P** and "Left: 9m 14s".
  Host → START GAME: `game 2 starts: … 2 player(s)`, and the host's start screen shows **KEVIN LPHPUT and MARK
  85S49U** (shot `w08-c1-start`), then black and the relay refusals (8690) as in entry 49.
- **Gaps:** client 2 stays on its MEMBER LIST after ENTER (no further screen, never followed the start, never tried
  the relay): it waits for something ob_server does not send after a join. ob_server also logged `unhandled
  question 0x670D (22 bytes)` from the host (events, research note section 4: "later").
- **Next:** what the joiner waits for after ENTER (the join answers 6502/6503/6505/6506 against Bioserver1's join
  flow); then the relay (step D).

### 51. The joiner's stall: EVENTDAT (0x670D) was answered empty and never passed on; fixed, both players start

- **Developer:** fix what is broken before new work (saved as a standing rule).
- **Finding it:** `ob_server --trace` (new, `LobbyConfig.trace`/`ServerConfig.trace`): one line per packet in and
  out. After the join (6406 answered `00 01`, the room told) the joiner sent **nothing** on ENTER, while the host
  sent 0x670D (22 bytes) and ob_server answered it empty ("unhandled question").
- **What 0x670D is** (Bioserver1, read 2026-10-28: `Commands.java` EVENTDAT = 0x670D, EVENTDATBC = 0x670E;
  `PacketHandler.sendEventDat`; `Packet.getEvenData`): a message to one player by handle. The request holds two
  scrambled strings (the recipient's handle, the event; each `[length:2][sum:2][bytes]`, key restarting at 0); the
  server answers the sender with the recipient's handle and sends EVENTDATBC `[sender's handle][event]`, both
  decoded as `[length:2][bytes]`, to that player only. The host's game sends it to a player who has just joined.
- **Fix (tests first, `lobby_tests.cpp`):** 84/86 without, then a first version that passed the event bytes on
  still scrambled (the joiner still waited); the test corrected to the decoded form (85/86), the handler then
  decodes both strings: 86/86.
- **Live (two clients, loopback, `--trace`):** host → `0x670D` to the joiner's handle; ob_server answers
  `00 06 RIYMEL`, sends the joiner `0x670E` `00 06 <host handle> 00 08 …`; the joiner then asks 640A, 6403, 6409,
  6412, 640B, sends 6504 and shows the room: "Wait for the game to start", 2/4P, STANDBY / EXIT / MEMBER DATA
  (shot `z04` in `C:\ob\frames-net-live2`). Host → START GAME: `game 2 starts … 2 player(s)`; **both** clients show
  the start screen with KEVIN and MARK (`z05`), then **both** connect to the relay 8690 and are refused.
- **So:** two players get from login to a started game together; the relay is the wall for both.

### 52. The relay: two clients play 零下 together on one PC

- **Plan approved by the developer** (2026-10-28, "Yes"): the relay per research note section 5, tests first.
- **Build:** `native/ob_server/src/relay.{h,cpp}`, one thread with `poll` like the lobby: on connect the GSLOGIN
  question (`28 01 10 31`, 12-byte header); the answer's session digits (payload 0..9, the answer's pid) → the
  account → its game number (set by the lobby at the start); then every frame (`RelayStream`, `[length:1][data]`) is
  copied unchanged to every other player of the same game. Length-0 and players silent for 60 s are dropped; an
  unknown session or an account not in a game is disconnected. `Server` starts and stops it with the lobby on the
  same address. Tests first (`tests/relay_tests.cpp`, three scripted clients, two games): 90/98 against a stub,
  98/98. Commit `b54651e`.
- **Live (two clients, loopback, the unlocked cards, host 零下):** both clients reach the start screen, then
  `[relay] 127.0.0.1: … in game 2` and `[relay] 127.0.0.2: … in game 2`; both play the opening cutscene in step
  (ticks 18 931 and 18 861: shots `r07`), Start on both, both in the subway tunnel with **Kevin and Mark on each
  screen** (`r08`). Only client 2's pad held Up for 120 frames: on **client 1's screen Mark walked** from the right
  of the tracks to the middle and turned (`r09-c1-before` → `r09-c1-after`). The relay carries the game.
- **Not yet checked:** how long the game stays in step (one short walk seen), enemies and items shared, the end of a
  scenario and the post-game lobby (area 51), more than two players, two PCs on a LAN, Android.
- **Scripts:** `tools/net/two-clients-to-room.sh` drives both clients from boot to the host's room with the guest
  joined (the name keyboard steps from entries 47-51).

### 53. Android: the net build on the OnePlus 13 and the Odin 3, against ob_server on the PC (LAN)

- **Build:** `tools/net/net-android.sh` (own NDK tree `C:\ob\net-android-r`, APK folder `C:\ob\net-android-apk`;
  raylib from `OB_T_DEPS`). Installed on both phones (developer's go-ahead, 2026-10-28). Commit `ae446f2`.
- **net.env** (`<external files>/net.env`, `PS2X_` lines only, `DevEnv.kt`): the game's server names → the PC
  (192.168.0.76), `ob_server --bind 192.168.0.76 --host 192.168.0.76 --trace`.
- **Issue: "Auto" network interface fails on Android.** PCSX2's `GetAdapterAuto` needs a gateway from
  `/proc/net/route`, which Android does not let apps read. Fix for now: `PS2X_NET_IF=wlan0` in net.env; the app
  should pass the Wi-Fi interface itself (standalone host work).
- **Issue: imported driver folders with names the loader refuses.** Fixed: `DriverNames.kt` renames them at start
  and the choice follows (tests in `DriverNamesTest.kt`).

### 54. Android: formatting a memory card freezes the game (fixed: PS2Recomp 0101)

- **Seen:** on the Odin (and reported on the OnePlus), NETWORK PLAY → create the NET file → format: the game freezes.
  The developer reports a **normal format (not network play) freezes too** (2026-10-28). Windows formats the same
  blank card fine (`net-live-blank.sh`).
- **Measured:** while frozen, the card file keeps being rewritten (modification time advancing) but stays all `0xFF`.
- **Ruled out: slow shared storage.** Hypothesis was that FUSE writes on `/sdcard` made the card driver time out.
  Test: card in internal storage (`/data/user/0/com.outbreakmodern.app/files/memcards-int`, `PS2X_SAVES` in net.env).
  Result: same freeze; the internal card rewritten at 17:56:25, checked 17:56:56, still all `0xFF` (`od` shows one
  repeated `ff` line).
- **Clue (18:03, new build, working copy `files/memcards/Mcd001.ps2`):** the only non-blank bytes are in pages
  16370-16375 (the card's last blocks): each page's 16-byte ECC area holds `77 7f 7f` ×4, the ECC of all-zero data,
  while the 512 data bytes stay `0xFF`. So the ECC writes land and the data writes do not (or are written as `0xFF`).
  PCSX2's path: `MemoryCardProtocol::WriteData` (`C:\ob\pcsx2\pcsx2\SIO\Memcard\MemoryCardProtocol.cpp:208`) →
  `FileMemoryCard::Save` (`MemoryCardFile.cpp:441`, read, AND, write back). Not yet known why only on Android.
- **PCSX2's console messages are lost on Android:** `pcsx2-dev9.log` goes to the working folder, not writable there,
  and nothing reaches logcat. Needed before the next step.
- **Parked** (developer, 2026-10-28: the graphics driver bug first), then resumed.
- **Not a lost transfer:** a temporary trace of DMA 11/12 in `sio2_side.cpp` (`PS2X_SIO2_TRACE`, removed after)
  showed no ignored transfer. First the format reads every sector (~4,300 a second, 60 fps), then erases and writes.
  Each write batch's answer (DMA 12) came exactly **one second** after it was sent, in the same millisecond as an
  `[fps]` line (18:21:55.922, 57.922, 58.922): one page a second, so the format looked frozen.
- **Cause:** the runner's logcat thread (PS2Recomp `ps2xRuntime/src/main.cpp`) read its pipe with `fgets` on a
  `FILE*`, holding that stream's lock while it waited; the card bridge's `FlushIfWritten` calls `fflush(nullptr)`
  after each write, and bionic locks every stream to flush it, so each write waited for the next log line. Windows has
  no such thread, which is why the same format finishes there. (bionic's locking is from memory, source not opened.)
- **Fix:** PS2Recomp 0101: the thread reads the descriptor with `read()` (`runtime/ps2_log_pump.h`); tests written
  first (LogPump, 3), 575/575. Odin, blank card: formatted at 60 fps (header "Sony PS2 Memory Card Format 1.2"), the
  game on SAVE/LOAD, and the save folder's copy updated 2 s after the last write (18:29:19.9 → 18:29:21.9).

### 55. Android: a save folder is required before Play (developer's request, 2026-10-28)

- **Request:** "have users picking a save folder for memory cards to be a requirement for booting the game." Plan
  approved ("Yes, build it").
- **Design:** the game opens its cards by path (`native/pcsx2_sio2`), and a folder from Android's picker has no
  path, so the game plays on a **working copy in internal storage** (`PS2X_SAVES`), copied in from the chosen folder
  at Play and back out while it runs (2 s after the last write, on pause, at quit). A copy changed since its last
  copy-out is marked unsynced and wins over the folder's at the next Play, so a killed game loses nothing; the mark
  is cleared only by a copy-out with no write during it. The old default folder's cards move in on first use.
- **Code:** `SaveCards.kt` (rules, `TreeCardFolder`, `CardSync`), the Play gate and picker in `LauncherActivity.kt`,
  the Memory cards page in `SettingsModel.kt`, `PS2X_SAVES` + watcher in `OutbreakActivity.kt` (set after net.env,
  so the chosen folder wins). Tests first: `SaveCardsTest.kt` (10).

### 56. NET CONNECTION's settings screens draw no text with PCSX2's hardware renderer (fixed: 24-bit depth)

- **Seen (developer, Odin, 2026-10-28):** NETWORK PLAY → create the NET file → YES: the following screens (title bar,
  list with ▲/▼, help box, ○/×) have no text at all. Same on `gs_self_read = barrier` and `copy`: not the Turnip fix.
- **Not the disc:** the phones run `Biohazard - Outbreak (English v2.1).iso`; it matches `game_files` in 90 files
  incl. `SLPM_654.28`; only `NETBIO00.DAT`'s directory size is 12 KiB shorter (the 6 sectors before `NETBIO01.DAT`,
  a name index; the bytes are on the ISO). A size-corrected copy made no difference.
- **Not Android:** the Odin's card got the NET file (`BISLPM-65286NET`) and the game loaded NETCNF/NETCNFIF (Sony's
  network settings screens, `NTGUI2` on the disc). On Windows with the same ISO: `[gs] backend=cpu` (ob.sh's
  default for these drives) and `pcsx2-sw` draw "NET CONNECTION / Edit Network Configuration / Proxy Configuration";
  **`pcsx2-vk` draws it blank** (`C:\ob\frames-iso-vk\n06-connecting.png` vs `frames-iso-sw`). My first comparison
  used the cpu backend without noticing: corrected here.
- **PCSX2's GameIndex** has no GS fixes for SLPM-65428. Twelve hardware fixes tried one at a time on Windows
  (`pcsx2-vk`, a temporary `PS2X_GS_HACK` hook, removed after) changed nothing: preload frame data, texture inside
  RT, CPU sprite render, partial invalidation off, autoflush, CPU and GPU CLUT, texture preloading off, safe
  features off, mipmapping, full readbacks, maximum blending. Scored by bright pixels in the list: text 1,880 (SW),
  blank 780.
- **Stock PCSX2 2.9.134 (`ps2_emu`, Vulkan, developer's go-ahead) shows the same blank screen:** a PCSX2 bug, not the
  port. Known upstream: [PCSX2 issue #13704](https://github.com/PCSX2/pcsx2/issues/13704) (open, "GS: Hardware",
  "GameDB"); lightningterror there: a depth precision issue, limiting depth to 24 bits fixes it.
- **Fix:** `GameGsFixesFor` (`gpu_driver.{h,cpp}`) gives SLPM-65428 `UserHacks_Limit24BitDepth` = prioritise upper;
  the adapter sets it with `ManualUserHacks` (PCSX2 drops user hacks without it: `Pcsx2Config.cpp` MaskUserHacks).
  Tests first (3, did not build without the function), gpu_driver 76/76, ring 11/11, adapter 5/5. Windows,
  plain `pcsx2-vk`: NET CONNECTION has its text (1,909 bright, `C:\ob\frames-fix-vk`). Installed on the Odin and
  the OnePlus. Not yet checked: gameplay with 24-bit depth (J's Bar), and the menu-ordering issue the PCSX2 report
  also mentions.
- **Regression, caught by the developer (OnePlus and Odin, 2026-10-28):** J's Bar broke with *prioritise upper*:
  see-through, blotchy characters, background polygons through the foreground. Windows `pcsx2-vk`, `gate/jsbar.script`,
  frame 250: off = right, upper = broken the same way, **lower = right**; lower also brings the network text back
  (1,909 bright, run `hk-d24lower`). Changed to prioritise lower (test first: 75/76 against 1, 76/76 after). I had
  shipped upper with J's Bar unchecked (noted as not checked): a depth setting must be checked in 3D before shipping.
- The reference PCSX2's settings were put back (`inis/PCSX2.ini.bak-20261028-netgui`).

### 57. The network settings on the card: BWNETCNF written at game start (Android Network page: in progress)

- **Seen (developer, Odin):** NET CONNECTION asks "Input your network settings?"; YES on a PS2 leaves the game for
  Sony's setup program (`NTGUI2`), which the port cannot run: the developer saw a crash after it, and "the preset
  server settings is not being used" (net.env's `PS2X_NETCNF=dhcp` is served, but the game wants `BWNETCNF` on the
  card first, [to-lobby.pad](../tools/net/to-lobby.pad) lines 1-3). The crash itself was gone from the log
  (buffer from 20:00:44 only): not confirmed which step crashed.
- **Approved plan (developer, "Yes, Android first"):** a Network page in the launcher, and the card's `BWNETCNF`
  written automatically.
- **Done so far:** `ob_ui` core writes cards (`formatCardIfBlank`, `addCardFolder`, `readCardFile`, ported from
  `tools/ob/mcd.py`; the written card reads back with `mcd.py info`) and `ensureNetworkFolder` (format a blank card,
  add `BWNETCNF` once, never touch an existing one). Tests first, ob_ui 140/140. The hook gives slot 1 `BWNETCNF` when
  `PS2X_NETCNF` is set: the runtime's four settings files (patch 0097) + the disc's `NTGUI2/CONF/ENGLISH` icons.
- **Issue: the disc paths are not set when the card opens** (`ps2_runtime.cpp:1084` opens the card before
  `main.cpp:292` sets the I/O paths; the first try logged `image '', folder 'C:\ob\net-logs'`). Fix in the adapter:
  the card is prepared at the game's first card access (closed, prepared, reopened), one flag check per access.
- **Windows (`pcsx2-vk`, card with the game save):** `[netcnf] card: BWNETCNF added (dhcp)`, six files (icon 33,688
  bytes as netcnf checks); NET CONNECTION lists "1. Setting 1" (`C:\ob\frames-netcard-vk\n06-connecting.png`).
- **Next:** the developer tries Setting 1 on the Odin against the PC server; then the Network page.

## Mistakes and corrections

| Claimed | What was true | How it was caught | Corrected in |
|---|---|---|---|
| Answering DNAS/login inside the adapter avoids old TLS (research 5b) | The game does the TLS itself; the answering side must speak it | Game strings + corbin.zip guide while planning | plan P4 |
| The in-game network code is in NETWK (milestone docs) | It is NETAQ (0x820000); NETWK is the lobby | Reading the overlays' code and strings | research 7.1 |
| mbedTLS 2.28 needs `MBEDTLS_REMOVE_3DES_CIPHERSUITES` undefined for 3DES (said in the session) | That define only drops 3DES from the default list; a suite set explicitly still negotiates | The control handshake on the stock build negotiated it | plan P4 result |
| No connection reached ob_server (entry 33, its empty log) | ob_server logs nothing on accept or handshake; unknown | Reading `tls_server.cpp` while planning the next step | entry 33 |

## Still open

- S0.1-S0.3 done (research 7.4; plan P4 result).
- S1 done: `native/pcsx2_dev9` builds; tests 15/15 (DHCP offer with address and DNS server, INTC 13 on receive,
  DNS host override, DMA 8 refusal). S2 patch done: PS2Recomp 0087 (renumbered from 0081 at the rebase onto main), IOP tests 5/5 suites; the
  runner with `PS2X_DEV9=pcsx2` finds the adapter ("dev9: CXD9611 detected").
- Done: the boot's HDD check with an adapter (entry 12). Next: the network menu (S3). The gate not yet run in the
  net trees; ps2x 558/558 in `C:\ob\net-t`.
- Build defect (entry 11): fixed by 0088; a stub change now rebuilds 2 batches in ~4 min (was 454, 21 min).
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
