# Online play, first release: this port's clients on one network (plan)

- **Date:** 2026-10-26
- **Status:** approved 2026-10-26 (developer: "proceed with the plans", with a Network settings section where
  players make all these changes and save their login). Implementation starts with S0.
- **Research:** `research/2026-10-26-network-crossplay.md` (sections 1-7). This plan cites its sections as R§n.

## 1. What it is

Windows and Android players of outbreak-modern play Outbreak's online mode together, in any mix, on the same
network. One player presses **Host game**; the others see the game in a list (or type the host's address) and
join. The game's own online code runs unchanged. A player can also point the game at any other server that
speaks the File #1 protocol (a server someone runs at home), by address.

Not in this release (approved scope, R§5b): PCSX2 and real PS2 players, the fan server Obsrv, internet hosting
built into the app.

Words used below: **DEV9** = the PS2's expansion bay, where the network adapter sits; **SMAP** = the adapter's
Ethernet chip; **IOP** = the PS2's I/O processor, which runs the disc's network drivers on our interpreter;
**overlay** = a block of game code loaded at run time; **DNAS** = Sony's online disc check; **relay** = the
server part that forwards in-game messages to the other players.

## 2. Decisions

### Approved (developer, 2026-10-26)

| # | Decision |
|---|---|
| N1 | First release: this port's Windows and Android clients only. PCSX2, PS2 and Obsrv in the next release. |
| N2 | Play on the same network: "Host game" on any Windows or Android device; others join from a list or by address. Players can also use a server they or others run. |
| N3 | Limitations documented in the README (local network only, AGPL host server, next-release limits). |
| N4 | Proposals P1-P15 below approved ("proceed with the plans"). |
| N5 | A **Network** page in the settings menu (launcher and in-game menu, Windows and Android) where players make every online change and save their login (section 3a). |
| N6 | Section 6's open questions, defaults taken since no answer was given: 4 players (the game's maximum); `ob_server.exe` ships; Docker for S3 asked with "proceed" at that point; tests on the Odin + one Windows PC. Any of these can still change. |

### Approved proposals

| # | Proposal | Why |
|---|---|---|
| P1 | **Route A:** emulate the network adapter (DEV9 + SMAP) with PCSX2's code and its "Sockets" mode; the game's network code and the disc's drivers run unchanged. | Same bytes on the wire as a PS2, so the next release adds PCSX2/PS2 without redoing the transport (R§5). The game is state sync, so Windows vs Android maths need not be identical (R§7.1). |
| P2 | PCSX2's DEV9 as `native/pcsx2_dev9`, built like `native/pcsx2_sio2` (adapter + stubs + tests), behind a new external-device hook in PS2Recomp (numbered patch, as 0071 did for SIO2). | Proven pattern; the dependencies are already in our build (R§7.2). |
| P3 | **The host server is our own C++ program, `ob_server`**, following Bioserver1's behaviour (lobby 8300, relay 8690, the login and DNAS pages). It runs inside the app for "Host game", and as a standalone `ob_server.exe` for a dedicated PC. Licence AGPL-3.0, kept in its own folder and library. | Bioserver1 is Java + MariaDB + Apache/PHP: it can't run inside an Android app (R§3). A C++ port can. |
| P4 | **Old encryption (DNAS and login are HTTPS with 3DES ciphers, done by the game itself):** first try a TLS library that still speaks them (spike S0.2). If none works on both platforms within the spike, skip the two steps for our clients by hooking the recompiled game (DNAS result = success; login pages fetched over plain HTTP from `ob_server`). | Both hosts are HTTPS in the game's strings (`https://www01.kddi-mmbb.jp/00000002/CRS-top.jsp`, NETWK/NETHTTP data); the fan setup needs OpenSSL 1.0.2 with weak ciphers (corbin.zip guide). The next release needs real old TLS on a Windows host anyway, so try it first. This corrects R§5b, which said no TLS would be needed. |
| P4 result | (S0.1/S0.2, 2026-10-26) **DNAS is skipped in our client, permanently; the login pages use real TLS.** NETDNAS has two runtime-scrambled code regions (0x6C4000-0x6CC000, 0x728000-0x744000: high byte entropy, 2,216 recompile errors, all "unhandled instruction"), so it can't be recompiled; our client reports DNAS as passed (a hook on the game's DNAS calls, found in S3). PS2 players next release are unaffected: their DNAS is answered by the server. NETHTTP (the game's own HTTPS client) recompiles with 0 errors. `ob_server` serves the login pages with **mbedTLS 2.28.10**: TLS 1.0 + `TLS-DHE-RSA-WITH-3DES-EDE-CBC-SHA` handshakes between its own server and client on Windows, and it builds for Android arm64 (on-device run untested). The cipher list is set explicitly in code; the PS2's real offer is checked in S3. | Measured; report entries 2-5. |
| P5 | Accounts: a hosted game accepts any login and creates the account on first use; the player's name comes from the game's own handle-name screen. No passwords stored by the app. | Same-network play among friends; Bioserver1's login is a database of users (`login.php`, `startsession.php`). |
| P6 | Finding games: the host announces itself on the local network every 2 s (UDP broadcast, one small JSON line: name, address, ports, version, players). Android listens with a Wi-Fi multicast lock while the server list is open. Typing an address always works too. | Simple, no dependencies. |
| P7 | Server choice goes through the adapter's own DNS: each server entry maps the game's fixed hostnames (`gate1.jp.dnas.playstation.org`, `www01.kddi-mmbb.jp`) to its address. Stored in `outbreak.ini [net]` and the Android settings. | How PCSX2 players do it (R§3), and it needs no game changes (R§7.2). |
| P8 | Android hosts: our clients reach the host's port 443 service on another port (18443) through a port remap in the adapter. | Android apps can't open ports below 1024 (R§5b). Where the remap lives (carried PCSX2 patch, connect wrapper, or server side) is decided in S1 (R§7.2). |
| P9 | The network device costs nothing in single player: the adapter's thread and polling start only when the game opens the network adapter. | Engine is performance-only; the gate and single player must not change. |
| P10 | Online, presentation options stay visual only: the 30 Hz tick and the messages sent must not change. Modern Camera, widescreen and 60 fps are allowed between our clients, with a check in S6 that the sent messages match with them on and off. | R§7.1: what peers see is the message bytes. |
| P11 | Diagnostics (per-tick remote player positions, message counts, relay traffic dumps) live in the optional `ob_diag` plugin, not the engine. | Project rule. |
| P12 | **Quick login** on `ob_server`: its `CRS-top.jsp` redirects straight to the "OK + session code" page the game reads, so no ID or password is typed. The account is found from a small **hello** our app sends just before the game goes online (device ID, a random ID made once per install, and the player name). Goal: from Online to the lobby with no typing. | The game's browser follows a meta refresh (Bioserver1's `CRS-top.jsp` is one); `startsession.php` returns `<!--<CSV>"OK","<session>","…enterareas.html","…login.php",</CSV>-->`, the line the game parses. The game encrypts its own browser traffic, so identity can't ride on it. |
| P13 | **Pre-launch check:** "Test connection" on the Network page, and automatically on Play when online is chosen: the server answers, runs the same outbreak-modern version, has room, and the name is free (our servers); for a Bioserver1-type server, the saved login is accepted. A plain message per failure. | Players learn about problems before the game boots, not inside its online menus. |
| P14 | **Saved logins** per server for servers with real accounts (anyone's Bioserver1 now; Obsrv next release): ID and password stored by the OS, never in `outbreak.ini`: Windows Credential Manager (`CredWrite`), Android Keystore (an AES key in the Keystore encrypting the password in app storage). A warning on the page: the PS2's encryption is from 2003, don't reuse a password from anywhere else. | The developer asked for saved logins. Checking them needs the old-cipher TLS client from S0.2 (the server speaks only the PS2's ciphers, likely; unverified). |
| P16 note | (2026-10-27, first real-game drive) **Required, not optional:** the game's editor is a separate program (`LoadExecPS2("cdrom0:\NTGUI2\YN_FILEJ.BIN;1")`), which the port can't run. The app must write `BWNETCNF` itself; there is no in-game fallback. | Report entry 17. |
| P16 | (from S0.3) **Network configuration made for the player:** the game needs `BWNETCNF` on the memory card and otherwise asks the player to fill in its own network editor. The app writes a DHCP + automatic DNS entry for the network adapter onto the card if the format allows (checked in S3); else the first online start shows the game's editor once, with a one-line hint ("Hardware: network adapter, IP: Auto, DNS: Auto"). `ob_server` also pre-registers the Player name as the account's first handle name, so the game's name screen only asks to pick it. | R§7.4. |
| P15 | **Auto-fill** a saved login into the game's own login form: a hook in the recompiled game's browser that fills the ID and password fields and submits. Its own step (S8), after the core works. | The form arrives encrypted by the game, so it can only be filled from inside the game code. |

## 3. How it works

```
 game (recompiled): NETWK lobby, NETAQ in-game messages, NETHTTP, NETDNAS overlays
        │  socket calls
 IOP interpreter: the disc's dev9 / smap / ACCESS AVETCP stack (unchanged)
        │  hardware registers, DMA channel 8, interrupt 13
 PS2Recomp: DEV9 external device  ──►  native/pcsx2_dev9 (PCSX2 DEV9 + SMAP + Sockets adapter, DHCP + DNS)
        │  host TCP sockets
 ob_server (on the host device or a PC): DNAS + login pages (443/18443), lobby 8300, relay 8690
```

- **Game side:** the overlays NETWK (lobby), NETAQ (in-game messages), NETHTTP and NETDNAS are recompiled like
  GAME and SUBMAIN (guest modules, `tools/mwo3_to_elf.py`). Today they are not built (R§1).
- **Adapter:** PCSX2's DEV9 compiled almost unchanged (~16.6k lines; pcap and TAP stubbed). Its DHCP server hands
  the game an address; its DNS answers the game's hostnames from the chosen server entry (R§7.2).
- **PS2Recomp patch:** the SPEED/SMAP register page at 0x10000000 at real access widths, the revision register
  0x1F80146E, DMA channel 8, interrupt line 13 raised at any time, and waking the idle IOP when a packet
  arrives (R§7.2). Possibly a dmacman extension for channel 8.
- **ob_server:** one process, three listeners. The lobby keeps areas, rooms and slots; the relay forwards every
  in-game message to the other players of the same game (Bioserver1 behaviour, R§7.3). It never reads the
  game's messages; the room-owner rules live in the game (R§7.1).
- **Settings:** the Network page (section 3a).

## 3a. The Network settings page (N5)

One page in both menus (Windows ImGui, Android Compose), same rows, controller-navigable like the rest
(settings plan A11, A15). In the in-game menu, rows that can't change while online are shown read-only.

```
 Network
 ─────────────────────────────────────────────────────────────────
 Player name          [ Johnny            ]   used by quick login (P12)
 Play online via      [ Games on this network ▾ ]
                        • Games on this network  (found automatically, P6)
                        • Saved servers
                        • Host a game on this device
 ── Games on this network ──────────────── Refresh
   Johnny's game   192.168.1.20   2/4   ready          [Join]
 ── Saved servers ──────────────────────── Add server…
   Home PC         192.168.1.50   quick login          [Edit] [Remove]
   Friend's server  bio.example    login: kevin ✓       [Edit] [Remove]
 ── Host a game ───────────────────────────────────────
   Game name          [ Johnny's game ]
   Players            [ 4 ▾ ]                 2-4
   This device         192.168.1.20 (Wi-Fi)
 ── Connection ────────────────────────────────────────
   Network interface  [ Auto ▾ ]             Android: Wi-Fi / Ethernet list
   [ Test connection ]                        result line under it (P13)
```

- **Add / Edit server:** name, address (IP or hostname), optional ports, "This server needs a login" → ID +
  password (saved by the OS, P14) + [Check login]. Servers run by this app use quick login and show no fields.
- **Saved where:** `outbreak.ini [net]` on Windows (player name, servers without passwords, host settings,
  device ID); Android's settings store for the same keys; passwords only in the OS store (P14).
- **Launcher Play:** when online is chosen, Play runs the P13 check first; on failure it stays on this page with
  the reason (like settings plan P9 for the disc).
- **Mockup:** text only, here; a visual mockup comes with S5 before that code is written (settings plan practice).

## 4. Build order

Estimates are rough; each step ends with a done-when check. Every PS2Recomp change is a numbered patch with a
test that fails without it (next free numbers at the time; the touch-controls branch may take some). Issues go
into `reports/<date>-online-play.md` as they happen.

| Step | Work | Done when | Estimate |
|---|---|---|---|
| S0.1 | Recompile NETWK, NETAQ, NETHTTP, NETDNAS as guest modules. NETDNAS is "few, very large, obfuscated functions" (`m7-network.md` item 3). | All four build; the game reaches its network menu without missing-function errors. | 2-4 days (NETDNAS unknown). **Recompile done 2026-10-26** in `C:\ob\net-ovl`: NETWK 2,381 functions, NETAQ 68, NETHTTP 1,549, 0 errors; NETDNAS 30 → 1,835 with `tools/fill_functions.py`, scrambled regions not recompilable (P4 result). Booting them needs a separate runner tree (`C:\ob\src` is shared with the touch-controls work): with S2. |
| S0.2 | (**done 2026-10-26**: mbedTLS 2.28.10 works, see P4 result) TLS spike: a throw-away server with a TLS library that still offers `DHE-RSA-DES-CBC3-SHA` / `EDH-DSS-DES-CBC3-SHA` and the PS2's old protocol version, built for Windows and Android. Candidates: mbedTLS 2.28 LTS, OpenSSL 1.0.2 (unsupported; last resort). | A decision for P4, with the handshake shown against PCSX2's DNAS request or our own client in S3. | 2 days |
| S0.3 | Read the open game-code questions: the enemy-message handler (`func_00609590`), port 8653, and whether the game needs a network settings file on the memory card (the disc ships `netcnf`; if it does, we generate one with `tools/ob/mcd.py`). | Each answered in the research note. | 1-2 days |
| S1 | `native/pcsx2_dev9`: build on Windows and Android, stubs, tests that send DHCP and DNS through the SMAP registers; DNS host entries; decide the port remap (P8). | Tests green on both platforms (`ob.sh test`). | 2-3 days |
| S2 | PS2Recomp patch: DEV9 external device (+ dmacman channel 8 if needed), with a failing-first test. | `ps2x` suite and gate green; the disc's dev9 stops printing "unknown dev9 hardware". | 2-3 days |
| S3 | Bring-up on Windows against **Bioserver1 in Docker on your PC** (development reference only; needs Docker installed: your "proceed"). DHCP → DNS → DNAS → login → lobby → room. Capture the traffic (in `ob_diag`). | One Windows client creates a room on Bioserver1. | 3-6 days |
| S4 | `ob_server` in C++: lobby, relay, login and DNAS pages; standalone exe + library; tested by replaying the S3 captures (same replies, byte for byte where the protocol requires). | Two Windows clients play a scenario together through `ob_server` on one PC. | 1-2 weeks |
| S5 | Network page on Windows (section 3a; visual mockup first), Host game, discovery (P6), saved servers, quick login + hello (P12), pre-launch check (P13), saved logins in Credential Manager (P14). | A second Windows client joins from the list with no typing; a bad address or full game is reported before the game boots. | 1 week |
| S6 | Android: interface pick from Kotlin, port remap, multicast lock, Network page in Compose, Keystore logins, Host game on Android. Tests on the Odin need your "proceed". | Windows + Android in one room, each as host once, a scenario to the end, no desync; messages equal with presentation options on/off (P10). | 1 week |
| S7 | Docs: `docs/ops/online.md` (how to host, join, run `ob_server.exe`), README update, report. | Written. | 1 day |
| S8 | Auto-fill saved logins into the game's login form (P15), tested against a local Bioserver1. | A saved login reaches the lobby with no typing on a Bioserver1-type server. | 3-5 days |

Total, roughly **6-8 weeks**; S0.1 and S3 carry the most uncertainty. S5 and S6 wait for the touch-controls
branch to merge (same menu files).

## 4a. Next release (not in this plan's scope)

PCSX2 and real PS2 players and Obsrv: the old-cipher TLS on a Windows host for real DNAS and login (S0.2's
library), the port-443 limit for Android hosts (README), the 0x7AA0-byte code patch read and handled, the
Obsrv admins' agreement, and the English fan patch's effect online. Saved logins and auto-fill (P14, P15) already
work for Obsrv accounts once it is allowed.

## 5. Risks

| Risk | Effect | Mitigation |
|---|---|---|
| The disc's drivers on our IOP interpreter (interrupt timing, DMA channel 8, driver timeouts) | Bring-up stalls in S3 | PCSX2 as a reference (same disc, same drivers); trace in `ob_diag` |
| NETDNAS's obfuscated functions don't recompile | No DNAS step | P4's fallback hook skips DNAS for our clients |
| No usable old-TLS library on Android | Android can't host with real DNAS/login | P4's fallback: hook both steps for our clients |
| Speed of the network stack on the Odin's interpreter | Lag or dropped connections | Measure in S6; the IOP loop speed-up (R: `native-iop-hle.md` stage 1) |
| Bioserver1's lobby protocol has parts no one documented | `ob_server` gaps | S3 captures from the real server are the reference |
| Overlap with the touch-controls branch in `native/ob_ui` and the Android settings | Merge conflicts | Start S5/S6 after touch controls merge |
| The server's 0x7AA0-byte code patch | Not sent by our server | `ob_server` reports every client as up to date; what the patch changes is read before the next release |

## 6. Open questions

Defaults taken (N6); answers welcome any time:

1. Player limit: default 4 (the game's maximum), settable 2-4 per hosted game.
2. `ob_server.exe` ships in the first release.
3. Docker on your PC for S3: asked with "proceed" when S3 starts.
4. Test devices: the Odin + one Windows PC; Android ↔ Android untested unless a second device appears.
5. Whether the game's handle-name screen can be pre-filled from the Player name (S0.3 / S3).

## 7. Sources

- `research/2026-10-26-network-crossplay.md` (all sections; web sources dated there).
- Game strings: `C:\ob\splat\SLPM_654.28` network overlays: `https://www01.kddi-mmbb.jp/00000002/CRS-top.jsp`,
  `https://%s:%d`, `%d.%d.%d.%d:8653`, `PsIIdnas2no 280mDnAsjpfermat20030708135338` (read 2026-10-26).
- https://www.corbin.zip/outbreak-server/ (read 2026-10-26): DNAS over HTTPS 443, OpenSSL 1.0.2q with weak
  ciphers, `DHE-RSA-DES-CBC3-SHA` / `EDH-DSS-DES-CBC3-SHA`, HTTP/1.0 forced.
- https://github.com/TAB64-K/ps2-outbreak-server, `bioserver1/Bioserver1-master/www/README.txt` (read
  2026-10-26): the login pages are served at `https://www01.kddi-mmbb.jp/00000002/`, self-signed certificate OK.
- `native/pcsx2_sio2/` and PS2Recomp commit 237c92c (the pattern for P2).
- Bioserver1 `www/` pages (read 2026-10-26, https://github.com/TAB64-K/ps2-outbreak-server):
  `CRS-top.jsp` (meta refresh to `login.php`), `login.php` (two forms, `login=manual` / `login=newaccount`, ID +
  password, posted to `login_form.php`), `startsession.php` (the `<CSV>"OK","<sessid>",…</CSV>` line).
- Unverified: whether mbedTLS 2.28 still offers the 3DES suites and the PS2's protocol version (S0.2 answers it).
