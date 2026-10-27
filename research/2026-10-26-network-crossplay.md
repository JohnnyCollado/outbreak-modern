# Online play with PC/Android cross-play: research (2026-10-26)

Read-only study on branch `netplay-research` (from `main` at f386a8a). No code changed in the repo or in
`C:\ob\up`. Sources: the repo's docs and reports, PS2Recomp in `C:\ob\up` (our patched tree), and web pages read
on 2026-10-26 (GitHub and well-known sites only, nothing downloaded). Each claim carries its source; **Unverified**
marks a guess or a page not opened.

Terms: **DEV9** is the PS2's expansion bay (the slot the network adapter or HDD plugs into); **SMAP** is the
Ethernet chip on the network adapter; **IOP** is the PS2's I/O processor, which runs the network drivers (IRX
modules); **lockstep** means every player's machine runs the whole game from everyone's button presses, so all
machines must compute bit-identical results; **state sync** means each player sends their own position/actions
and the others just display them; a **desync** is when machines drift apart.

## Verdict

**Feasible, and cross-play comes almost for free if we take the hardware route.** The fan server for File #1
(Bioserver1) relays in-game packets over TCP to everyone in the room without reading them, and the Japanese disc
(ours) is the tested version (section 3). So any client that speaks the game's own protocol plays with any other:
PC, Android, PCSX2, real PS2. The cheapest way to speak it exactly is to keep the game's whole network stack and
emulate the network adapter under it, borrowing PCSX2's DEV9 "Sockets" adapter the way memory cards borrowed
PCSX2's SIO2 (option A, section 5). Two real blockers remain: the network overlays are not recompiled yet, and the
server pushes a 0x7AA0-byte code patch to old clients. **Follow-up (section 7): the game is state sync, not
lockstep** (high confidence, from its code): each client sends its own character's state, and each room's
first player runs and broadcasts that room's enemies. So PC and Android don't need bit-identical maths. They
need the same message bytes and the same room-owner rules, which recompiling the same game code gives.

## 1. What the repo already knows

- M7 (`docs/milestones/m7-network.md`) is the network milestone: lobby, room, a mixed PS2/PCSX2 + Windows +
  Android room on a local BioServer first, live on Obsrv only with the admins' agreement. It was written for the
  decomp-based design, **before** PS2Recomp became the base (`docs/milestones/m0-decision.md`), so its
  "Transport" step (replace Sony's socket library with host sockets) predates the fact below.
- **The runner runs the disc's real IOP drivers on an interpreter.** Only some services are replaced natively
  (CDVD, pad, memory card, file I/O; `research/2026-10-24-native-iop-hle.md` section 1.4). `dev9.irx` is already
  loaded at boot and prints "unknown dev9 hardware" because no DEV9 hardware is emulated (same note, table 1.1,
  id 12). The network drivers (`smap`, `netcnf`, the `NETMOD\*.IRX` stack) are on the disc, not loaded in single
  player (same note, end of 1.1).
- **There is a template for borrowing PCSX2 hardware.** Memory cards came in as PCSX2's SIO2 code behind an
  "external device" hook in PS2Recomp: register range, DMA channels 11/12 and the SIO2 interrupt routed to a
  separate library (`C:\ob\up` commit 237c92c = patch 0071; `ps2xIOP/include/ps2x/iop/iop_sio2.h`,
  `native/pcsx2_sio2/adapter/sio2_side.cpp`, 213 lines of adapter). DEV9 has the same shape: registers, one DMA
  channel, one interrupt (channel and line numbers from memory of PCSX2's source: **Unverified**).
- **Network overlays are not recompiled yet.** NETWK, NETHTTP and NETAQ are not built; NETDNAS only its first 30
  functions (`PROJECT_CONTEXT.md:126-127`; `C:\ob\ovl` has demo, game, single, submain, netdnas only).
  NETDNAS is "few, very large, obfuscated functions" (`m7-network.md`, work item 3).
- **Run-time code is a hard problem for a static recompiler.** BioServer 1 sends File 1 clients a 0x7AA0-byte
  code patch when they report an old patch level (`docs/milestones/README.md`, corrections table, from the
  decomp's NETWORK.md). A grep of `ps2xRuntime/src` finds no EE interpreter to fall back on (**Unverified** beyond
  the grep).
- **Lockstep or state sync is still unknown** (`m7-network.md`, unknown 1). It decides how exact the maths must
  be between a PC and an Android client.

## 2. Cross-play maths: what the code shows today

- Every EE float add/sub/mul goes through `ps2_fadd`/`ps2_fsub`/`ps2_fmul`
  (`C:\ob\up\ps2xRuntime\include\ps2_runtime_macros.h:278-294`): the host result when it's a normal number,
  a PS2-exact slow path otherwise. Same code on x86 and arm64.
- `MADD.S`/`MSUB.S` are emitted as `FPU_ADD_S(acc, FPU_MUL_S(a, b))` (`ps2xRecomp/src/lib/fpu_translator.cpp:89-96`):
  two separately rounded steps, as on the PS2. The multiply lands in its own `const` local before the add, so
  Android's clang (`-ffp-contract=on`, which fuses only within one source expression) should not fuse them into
  one rounding. **Unverified on device**: the determinism test in section 5 is what proves it.
- `/fp:fast` exists in `ps2xRuntime/cmake/ReleaseMode.cmake:18` but is only applied to the analyzer
  (`ps2xAnalyzer/CMakeLists.txt:56-57`), not to the runtime (grep).
- Known open EE-FPU gaps from M0: host round-to-nearest (the PS2 rounds toward zero), MAX/MIN and compares on
  exponent-255 values, MAC/status flags (`PROJECT_CONTEXT.md:119-123`). These matter for PC vs **PS2/PCSX2** in
  lockstep; between our own PC and Android builds they are the same code, so they would differ only if the two
  hosts round differently (both use IEEE round-to-nearest by default).
- VU1 is different code per platform (x86 microVU vs ARMSX2's arm64 microVU, `docs/ARCHITECTURE.md:70`), but VU1
  draws; it doesn't run game logic. Its differences show on screen, not in what is sent. VU0 macro calls in game
  logic are HLE C (patches 0062-0070), the same on both.
- The J's Bar scene is **not deterministic run to run** on one machine
  (`reports/2026-10-24-jsbar-gate.md` entry 2; `reports/2026-10-23-gameplay.md` entry 28). If the game is
  lockstep, that is a desync source to understand first (likely host-time effects such as audio/IOP timing).

## 3. The fan servers (web, 2026-10-26)

[C] = page read; [S] = search snippet only.

- [C] The server stack is gh0stl1ne's Bioserver1 (File #1), Bioserver2 (File #2) and DNASrep, all AGPL-3.0, Java;
  protocol reconstruction credited to obsrv.org. A Docker bundle says "Only the Japanese region has been tested"
  (2 players tested of 4). (TAB64-K/ps2-outbreak-server README; gitlab.com/gh0stl1ne/Bioserver1.)
- [C] Ports for File #1: DNAS over HTTPS 443, lobby 8300, game session 8690 (same README).
- [C] **In-game traffic is a dumb TCP relay.** `GameServerPacketHandler.java` handles only packets starting 0x82
  (login); every other packet is copied as raw bytes to every other client with the same game number. No
  peer-to-peer, no UDP, no host role in that file. So the server can't tell us lockstep vs state sync.
- [C] The code patch: `Patch.java` ("patch length is 0x7aa0") sends `patch.raw` (the repo ships
  `patch/patch_101.raw`) in 0x100-byte chunks, only when the client's CHECKVERSION is too old (`patch/update.txt`:
  messages 6121-6125). What it changes is undocumented; the binary was not opened.
- [C] PCSX2 setup: Network & HDD → Ethernet on, device type **Sockets**, internal DNS hosts
  `gate1.jp.dnas.playstation.org`, `gate1.us.dnas.playstation.org`, `www01.kddi-mmbb.jp` → server IP. DNAS is
  answered by the server (OpenSSL 1.0.2 with weak ciphers; corbin.zip/outbreak-server guide, JP-specific).
- [S] Whether the public Obsrv server is up today and its current address: not confirmed (obsrv.org returned 403).

## 4. PCSX2's network adapter and Android

- [C] PCSX2's code is in `pcsx2/DEV9/` (DEV9.cpp, smap.cpp, net.cpp, sockets.cpp, pcap_io.cpp, Sessions/,
  PacketReader/, InternalServers/ (DNS, DHCP), Win32/), GPL-3.0+. The Sockets adapter is a user-mode NAT: it
  rebuilds Ethernet/IP/TCP/UDP frames from host sockets, so no admin rights and no packet-capture driver.
- How much of it is OS-specific: only the header was read; AdapterUtils and Win32/ look Windows-specific
  (**guess**). Our project is already GPL-3.0 (`research/2026-10-22-ps2recomp-and-ecosystem.md`), so licensing fits.
- [S] NetherSX2 2.2n and ARMSX2 1.0.6 (2025-11) added online play on Android with DEV9 Sockets, so the code
  runs on Android in some form. No report of Outbreak played online on Android was found.
- [C] sp00nznet/reo (US disc) has no online play ("out of scope"). Outbreak·Web's networking: nothing public.
- Mixed PCSX2 + real PS2 rooms: no reports either way.

## 5. Options

| | A. Emulate the network adapter (recommended) | B. Replace the socket library (M7 as written) | C. Our own netcode |
|---|---|---|---|
| What | PCSX2's DEV9 + SMAP + Sockets adapter as `native/pcsx2_dev9`, behind an external-device hook in PS2Recomp (like patch 0071); the disc's dev9/smap/INET/DNAS drivers and the game's code run unchanged | Native code answers the network module's RPC calls with Winsock/POSIX sockets | Sync our own clients by inputs or state, our own server |
| Plays with PCSX2/PS2 on Obsrv | Yes, same bytes on the wire | Yes if every call is matched exactly | No |
| PC ↔ Android | Yes, same server, same protocol | Yes | Yes |
| Risk | IOP interpreter cost of a TCP/IP stack on Android (unmeasured); DEV9 port effort | Undocumented RPC protocol; DNAS weak-cipher TLS on the host (`m7-network.md` item 2) | Needs bit-exact determinism, splits the community, most work |
| Shared by all | recompile NETWK/NETHTTP/NETDNAS/NETAQ; the v1.01 code patch | same | n/a |

**The code patch** (needed by A and B): it is sent only to clients reporting an old version. Options: recompile
`patch_101` ahead of time and apply it as an overlay variant, then report v1.01 so the server never sends it; or
a small EE interpreter for code that arrives at run time. Which one depends on what the patch touches: read it
first (it is in the server's public repo; whether it may be redistributed is a question, it is Capcom code).

**Suggested order (A):**
1. Reference run, no code: local Bioserver1 (Docker) + two PCSX2 instances with your disc in one room. Then the
   **lockstep test**: set one instance's EE cycle rate / speed differently and play; if nothing drifts, the game
   is very likely state sync and PC ↔ Android maths are low risk. Capture packets for later byte comparison.
2. Recompile the network overlays (NETWK, NETHTTP, NETDNAS, NETAQ) and read what `patch_101` touches.
3. DEV9 external device in PS2Recomp (numbered patch with a failing-first test) + `native/pcsx2_dev9`.
4. Windows client in the local room with PCSX2; then Android; then a PC + Android + PCSX2 room to the end of a
   scenario (M7's "done when").
5. Live Obsrv only with the admins' agreement (M7).

## 5b. Server choice and same-network play (proposal, not approved)

Asked for on 2026-10-26: play with others on the same network, and plug in any server (your own, or one like
Obsrv).

- **The game has no LAN mode.** Every match goes through a lobby (8300) and the relay (8690), plus DNAS (443) and
  the KDDI login pages before that (section 3). So same-network play means someone on the network runs a server.
- **Server choice = name resolution.** The game looks up fixed hostnames (`gate1.jp.dnas.playstation.org`,
  `www01.kddi-mmbb.jp`); the lobby then hands out the game server's address (`gs_ip`, TAB64-K README). PCSX2's
  Sockets adapter has an internal DNS with host overrides (`pcsx2/DEV9/InternalServers`, not opened), so a server
  setting only decides what those names resolve to. Settings (`outbreak.ini [net]`, same on PC and Android):
  Server = preset (Obsrv) / saved custom servers / "game hosted on this network"; Custom address = IP or hostname,
  optional ports.
- **Level 1, bring your own server:** document running Bioserver1 (Docker bundle exists) on a PC; players pick
  Custom. No new code beyond the client.
- **Level 2, "Host game" built in:** a small C++ server in the app (lobby, relay, DNAS and login replies),
  announced on the local network so others see it in a list (UDP broadcast or mDNS; protocol not chosen). Others
  can also type the host's IP.
  - Who can join: our Windows and Android clients always. PCSX2 and real PS2s too when the host can serve the
    standard ports.
  - Android hosts: Linux-based systems refuse ports below 1024 to normal apps (general rule, untested on the
    Odin), and DNAS uses 443. Our own clients can be sent to another port by the emulated adapter; a PS2 can't.
    So an Android host serves our clients only; a Windows host serves everyone (Windows doesn't restrict low
    ports; a firewall prompt is expected).
  - DNAS needs the old ciphers (`m7-network.md` item 2) in whatever TLS library the server uses. Alternative for
    our clients only: answer DNAS and the login pages inside the emulated adapter, no TLS on the wire.
  - Licence: a port of Bioserver1's logic would likely be AGPL-3.0 (combinable with our GPL-3.0; not legal
    advice). Keep the server as its own library or program.
  - Default to the local network only; internet hosting (port forwarding, Tailscale) documented, not built in.
- **Order:** client with the server setting first (needed for everything), Level 1 alongside, Level 2 after the
  sync test (section 5, step 1), as its own milestone.

**Scope decision (developer, 2026-10-26): the first release is this port's clients only** (Windows + Android).
PCSX2, real PS2s and Obsrv come in the next release. Still route A, so the next release adds compatibility
without redoing the transport. What the narrower scope allows:

- DNAS and the KDDI login pages can be answered inside the emulated adapter (no old-cipher TLS on the wire).
- Any device can host: the adapter can send our own clients to ports above 1024, so Android hosts work.
- Our server decides whether to send the v1.01 code patch, so it can skip it until we know what it changes.
- The determinism question narrows to Windows vs Android of the same build (section 2). It can be measured
  between our two clients once they connect; the PCSX2 test (section 5, step 1) stays the cheaper early answer.

## 7. Follow-up research (2026-10-26, after the scope decision)

### 7.1 State sync, not lockstep (from the game's code)

Read in the splat disassembly (`C:\ob\splat\SLPM_654.28`); "seen" = read in code, "inferred" = reasoning.

- **The in-game network layer is the NETAQ overlay** (0x820000, shares the address with SINGLE), not NETWK
  (NETWK is the lobby; it shares 0x540000 with GAME, so it can't be resident in play). Seen: NETAQ's strings
  (`netaq/data/data.data.s`): USERMSG, RUN_SESSION, DISCONNECT, PING, DROPOUT, "AQ buff full",
  "!!PL DATA full!!", "MY AQ EXEC", "OTHER AQ EXEC", and ACCESS's own TCP/IP drivers (AVETCP/AVEPPP/AVEWRAPM IRX),
  not Sony's INET. Spot-checked: 5 of these strings found.
- **Single player goes through the same message API.** Main-ELF wrappers check a mode byte (`func_001AF500`,
  D_003065C2: 0 = single, 1 = network) and call SINGLE's functions or NETAQ's: send `func_001AF000`, per-frame
  poll `func_001AEEA0` (seen; that SINGLE is a loopback is inferred).
- **What is sent** (GAME, `en/game/text.s`): message = subtype, size, sender slot, payload.
  - Types 1-4 = "PL DATA", one per player slot. `func_0064CF50` packs only the local player: position as 3 floats
    (chr+0x38/0x3C/0x40), angle, motion, a flags word, room, status bytes; size 0x2C (seen; spot-checked the 0x2C
    size and the `lwc1 0x38` load).
  - Type 8 = object/enemy events; type 9 = item placement (float x/y/z + id); types 6-7 = session control.
  - Receive: NETAQ `func_00820080` switches on type and drops its own messages; types 1-4 go to GAME
    `func_0064D140`, which applies a remote player's state (seen).
- **Who runs what:**
  - `func_001AF540(chr)` = "is this my character"; GAME calls it 136 times (spot-checked: 136). Most player logic
    runs only for the local character (inferred).
  - **Room owner:** NETAQ `func_00821590(room)` returns the first player who entered the room, from a per-room
    table at D_00825810; GAME calls `func_001AF440` 27 times (spot-checked). Enemy broadcasts (`func_006094A0`,
    type 8) happen only when I own the enemy's room (seen; spot-checked the call).
- **No lockstep stall:** NETAQ drains its receive queue each frame and moves on; no loop waiting for peers was
  found (read by scanning branches, not every line).
- **Not settled:** a shared random seed (not searched for specifically); whether non-owners skip enemy AI entirely
  (read `func_00609590`, the type-8 handler, to confirm); a second port 8653 in a NETAQ string (role unknown).

**What it means:** desync risk between our Windows and Android clients is low; float differences change only how
a remote player is drawn until their next update. The 30 Hz tick and the message bytes must not change (presentation
options stay visual only, as M7 says). Section 2's maths gaps matter for single-player fidelity, not for online.

### 7.2 Lifting PCSX2's network adapter (from `C:\ob\pcsx2\pcsx2\DEV9`)

- **Size:** ~16.6k lines compiled (core 5.2k, InternalServers 1.5k, PacketReader 3.9k, Sessions 3.2k, ATA
  compiled but never opened, because `DEV9.h` holds an `ATA*`). Skip pcap and `Win32/` (TAP) with small stubs.
- **Dependencies are already in our build** from `native/pcsx2_gs` (common/, fmt, `EmuConfig`;
  `config_stubs.cpp:34` already stubs `DEV9Options()`). The adapter defines `dev9Irq` and `psxDMA8Interrupt`.
- **Android:** POSIX paths exist (`getifaddrs`, BSD sockets). Needs only the INTERNET permission. Pass the network
  interface name from Kotlin, because the automatic pick reads `/proc/net/route` (`AdapterUtils.cpp:406`), which
  newer Android likely blocks (unverified). Use the internal DNS, not `/etc/resolv.conf`.
- **Server choice:** host overrides come from `EmuConfig.DEV9.EthHosts` (`DNS_Server.cpp:103-110`); entries must be
  lowercase (:183). **Port remap** for Android hosts: one line at `TCP_Session_Out.cpp:210`. That edits PCSX2
  source, which our precedent avoids, so the choice is a carried patch, a connect wrapper, or remapping on the
  server.
- **PS2Recomp side (new patch, like 0071):** the 64 KB SPEED/SMAP page at 0x10000000 with reads and writes at
  their real width (a 32-bit read of the receive FIFO pops data, so no read-modify-write); revision register
  0x1F80146E = 0x32; DMA channel 8 (0x1F801510-18); interrupt line 13 raised at any time, not only inside a
  register write; the idle IOP must wake when a packet arrives (follow the SPU2 device's event pattern,
  `iop_emulator.cpp:218-231`). Unverified: whether the disc's dev9.irx uses dmacman for channel 8 (then dmacman
  needs the same extension as for 11/12).
- **Effort (estimate):** build + link both platforms 1-2 days; PS2Recomp patch + tests 2 days; getting the disc's
  dev9 → smap → DHCP → DNS → TCP chain working 3-6 days. About 1.5-2 weeks.
- **Riskiest part:** the disc's network drivers on our IOP interpreter and HLE (interrupt and DMA gaps, timeouts),
  then speed on the Odin (unmeasured).

### 7.3 Server side (Bioserver1 source, read 2026-10-26)

- `Slot.java`: a slot (game) stores status, scenario, load type, a ruleset, a wait timer ("game will be started"
  on timeout), and `host` = "room master's userid". No seed or difficulty in this class (maybe in `RuleSet`).
- `GameServerPacketHandler.java`: login (0x82 0x02 + GSLOGIN) maps a session code to a user and that user's game
  number; every other packet goes to everyone with the same game number except the sender. A one-minute
  keep-alive drops frozen players. No host logic. So the room-owner rule lives entirely in the game (7.1).
- What our own first-release server needs: the lobby (8300, with its user and session database), the relay
  (8690), and DNAS/login answers (in the client's emulated adapter, scope decision in 5b).

### 7.4 Plan step S0.3: the open game-code questions (2026-10-26)

Read in `C:\ob\splat\SLPM_654.28` (the splat tree is the English-patched build, so addresses are that build's).

- **Enemies on non-owners: a hybrid** (seen). The type-8 handler `func_00609590` (90 sub-commands, table
  `jtbl_0070B400`) writes enemy position, rotation and state bytes. Enemy update functions still run on every
  client; only the decisions are gated on owning the room (`func_00577BC0`, `func_005B6C20`, `func_0059B5A0`).
  Inferred: the owner decides, non-owners animate the current state until the next message.
- **The random seed is shared** (seen). RNG `func_001ADEF0` (x = x·176 mod 0xFF53 on two seeds), called 571 times
  from GAME (spot-checked). `func_005CE890` sends seed D_23BBA0 in a type-6 message when `func_1AF0A0()` is true
  (inferred: "I am the session host"); NETAQ writes it on receive. A drift in RNG use between clients would show
  up only in non-owner cosmetic effects, because owners decide.
- **Port 8653 is a developer leftover** (seen: only reached with an argument every caller sets to 0; hardcoded LAN
  addresses 172.16.90.81, 192.168.0.253). Not needed.
- **A network configuration on the memory card is needed** (seen): NETWK loads `NETCNF.IRX` / `NETCNFIF.IRX` and
  reads `mc%d:/BWNETCNF/BWNETDCNF`. Without one the game says "In order to play online you need to configure the
  network settings" and offers its own editor (Hardware, IP Auto/Manual, DNS, Test Connection). DHCP is
  supported ("IP Address Setup Auto"). Whether we can write that file ourselves depends on its format (Sony's
  netcnf files may be scrambled; **unverified**).
- **Network drivers loaded in network mode** (seen): for Ethernet DEVGLUE, AVEDHCP, AVEPPP, then SMAP
  ("thpri=10", "device 5"), AVEWRAPE, AVETCP, a `dev9` presence check; DEV9 with ATAD/HDD/PFS. So the disc's own
  SMAP driver sits under ACCESS's stack: route A holds.
- **Handle name** (seen strings in NETWK): typed in game ("Please enter the Handle Name (HN)"), checked by the
  server ("Name is already registered."), up to 3 ID/HN per account. Inferred: the list comes from the server, so
  `ob_server` can pre-register the Player name as the account's first HN, and the player only picks it.

### 7.5 The hard disk install (2026-10-27)

Read in `C:\ob\splat\SLPM_654.28\en\main\text.s` and the overlays; [seen] = read in code, [inferred] = reasoning.

- **Optional, not required.** With a network adapter the boot (0x1BB420) loads DEV9/ATAD/HDD/PFS, asks
  `sceDevctl("hdd0:", 0x4807)` (drive status) and tries `sceMount("pfs0:", "hdd0:PP.SLPM-65428..NETBIO,…")`. Only a
  status of **3 (no drive)** continues from DVD with no message; a negative error means "drive present, not
  installed" [seen: 0x1BBA60 table]. PS2Recomp 0089 answers 3.
- **What an install copies:** one file, the disc's `NETBIO00.DAT` (904,636,416 bytes) to `pfs0:netbio00.dat`, plus a
  PS2 browser icon and jacket images (`pfs0:/res/…`). NETBIO01/02 stay on the disc [seen].
- **How it is used:** if mounted and the HDD copy's size equals the disc's (the only check found), the loader's
  room-archive path becomes `HST:pfs0:` for that one archive [seen: 0x1BC600, `fsinit.c`]. No other behaviour or
  timing difference found [unverified: not searched exhaustively]. Its benefit on a PS2 was load speed; our port
  already reads the disc image from local storage.
- **Online:** join/create messages carry 0x10000 (DVD) or 0x20000 (HDD); rooms can be DVD-only or HDD-only, and a DVD
  player is refused an HDD-only room with an error code [seen: netwk 128766-128782, 130255-130299]. Matches
  Bioserver1's room load type (`LOAD_DVDROM` 1, `LOAD_HARDSK` 2).
- **Faking an install without emulating a drive** would need: status 0, a mount that succeeds (repeatably), unmount,
  and `pfs0:netbio00.dat` opened, sized and read from the disc file. No drive image, no IOP disk drivers.
  Worth it only for HDD-only rooms; between our own clients the server could treat both types alike.

## 6. Open questions

1. ~~Lockstep or state sync~~: state sync (section 7.1). Left: confirm non-owners only apply enemy state; the
   shared-seed question; what port 8653 is for.
2. What `patch_101` changes, and whether it may be shipped or each user must capture it.
3. How fast the disc's TCP/IP stack runs on the IOP interpreter on the Odin.
4. How much of PCSX2's `DEV9/` builds outside PCSX2 and on Android.
5. Whether the public Obsrv server is up, and whether its admins accept native clients.
6. Whether presentation options (widescreen, Modern Camera, 60 fps) are allowed online (`m7-network.md`).
7. Whether the game needs a network configuration file on the memory card before it goes online (PS2 online
   games usually do; not checked).

## Sources

- Repo: `docs/milestones/m7-network.md`, `docs/milestones/README.md`, `research/2026-10-24-native-iop-hle.md`,
  `PROJECT_CONTEXT.md:115-130`, `docs/ARCHITECTURE.md:70`, `reports/2026-10-24-jsbar-gate.md`.
- PS2Recomp (`C:\ob\up`): commit 237c92c (patch 0071), `ps2xRuntime/include/ps2_runtime_macros.h:201-294`,
  `ps2xRecomp/src/lib/fpu_translator.cpp:89-96`, `ps2xRuntime/cmake/ReleaseMode.cmake:18`.
- Web (read 2026-10-26): https://github.com/TAB64-K/ps2-outbreak-server (README, `bioserver1/.../GameServerPacketHandler.java`,
  `Patch.java`, `patch/update.txt` first 100 KB), https://gitlab.com/gh0stl1ne/Bioserver1,
  https://www.corbin.zip/outbreak-server/, https://github.com/PCSX2/pcsx2/tree/master/pcsx2/DEV9 (`sockets.h`),
  https://github.com/sp00nznet/reo.
- Snippets only: https://newreleases.io/project/github/Trixarian/NetherSX2-patch/release/2.2n,
  https://steamdeckhq.com/ps2-emulator-armsx2-gets-online-multiplayer/,
  https://forums.pcsx2.net/Thread-How-to-use-latest-merged-dev9-PCSX2-for-Outbreak-obsrv-org-servers,
  https://pushsquare.com/news/2014/01/mix_some_fresh_green_herbs_resident_evil_outbreak_is_back_online (2014).
- Not opened: obsrv.org (403), outbreakweb.com body (empty).
