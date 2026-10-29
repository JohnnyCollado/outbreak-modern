# Local play: a built-in host on Android and Windows, nearby-game discovery, a Network page

Approved by the developer 2026-10-28 ("Just build with that limitations", "prioritize network security for this one").
Mock-up: `docs/plans/local-play-mockup.html`. Report: `reports/2026-10-26-online-play.md` (entries 53-61 got two phones
and a PC into one game through `ob_server` on a PC).

## What the player gets

- Settings > Network: Online play Off / Local play / Internet server; Connection Automatic (DHCP) / Manual IP.
- Local play opens on **Nearby games** (hosts on the same Wi-Fi, found automatically) with Join buttons; "Host a game on
  this device" is the second option. As DS Download Play (clients pick the host's game from a nearby list) and PSP
  ad hoc "Search for game" do, so a group gathers on one host instead of everyone hosting alone.
- Hosting screen: the address, the connected players, Play, Stop hosting.
- Android, next to Host: "Hosting on this device works for players using Outbreak Modern. To play with PCSX2 or PS2
  players on a fan server, join that server instead." (Android apps cannot listen on port 443:
  `ip_unprivileged_port_start` = 1024 on the Odin, Android 15, and the OnePlus, Android 16, read 2026-10-28; PPSSPP
  shifts its ports for the same reason, ppsspp.org/docs/multiplayer/how-to-play; NetherSX2 and ARMSX2 only join
  external servers.)

## How it works

- `ob_server` (`native/ob_server`, AGPL; combines with the GPL rest per its LICENSE.md) is linked into the runner
  (`libps2EntryRunner` on Android, `ps2EntryRunner.exe` on Windows). Android: started by a foreground service in the
  launcher's process (the game runs in `:game`), so it keeps running while the host plays. Windows: a thread of the game.
- Login site on 18443 on an Android host (443 on a Windows host); lobby 8300, relay 8690, data file 6881/6882 as now.
- Discovery: a host sends a small UDP broadcast once a second on port 18444; clients listen and list them.
- Joining host H: the game's server names point at H, and, when H's login site is not on 443, the adapter maps
  H:443 to H:18443 (`PS2X_NET_REMAP`, PCSX2 patch 0003). The host's own game joins the same way, through its Wi-Fi
  address.
- The card's `BWNETCNF` is written from the Connection setting (entry 57); `net.env` stays as a developer override.

## Network security (developer's priority)

The server speaks the PS2's 2003 protocols, so its crypto cannot be modern (SSL 3.0 / TLS 1.0, 3DES) and the lobby
protocol has no real authentication. It is therefore built to be reachable only on the local network, only while the
player chooses to host, and to treat every byte it receives as hostile.

1. **Off unless hosting.** No port is open and nothing is broadcast unless the player taps Host; Stop hosting, quitting
   the app or leaving the Network page's Local play closes everything. Android shows a persistent "Hosting" notification
   while the server runs.
2. **Local network only.** The server binds the Wi-Fi interface's own address, never "all interfaces" (no mobile-data
   or VPN exposure). Every accepted connection and every beacon is checked against the private ranges (10/8,
   172.16/12, 192.168/16, 169.254/16; loopback for the host's own game) and dropped otherwise. The client ignores
   beacons from non-private sources and from other subnets than its own.
3. **Limits everywhere.** At most 16 connections per service and 4 per address; idle connections closed (web 10 s,
   lobby/relay 60 s); request headers 16 KiB, bodies 64 KiB, lobby and relay packets capped at their protocol sizes;
   beacons at most 256 bytes, at most one counted per host per second.
4. **Strict parsing.** The beacon is a fixed text format with a magic word, a version and length-checked fields; names
   are cut to 24 characters of printable text. Every parser (web request, lobby packet, relay frame, beacon) gets a
   fuzz test (random and truncated input must neither crash nor hang).
5. **Host control.** The hosting screen lists every connected player with its address and a Remove button (closes its
   connections, refuses it for the rest of the session).
6. **Nothing secret, nothing kept.** No passwords (quick login), session numbers random per login (8 digits from a
   seeded generator, as now), nothing written to disk by the server. The beacon carries only the host's chosen name,
   the player count and the ports.
7. **Platform side.** Android: the service is not exported; INTERNET + foreground-service permissions only. Windows:
   the firewall prompt is answered by the player; the docs say to allow private networks only.

## Steps (each test-first, committed separately)

1. Security core in `ob_server`: private-address checks, connection limits and timeouts, beacon format + sender +
   listener, fuzz tests for the parsers.
2. `ob_server` linked into the runner; JNI: start/stop host, status (players), search nearby.
3. Android: host foreground service, port 18443; joining sets names + remap; the Network page with the fan-server note.
4. Windows: the same host and search behind the settings menu's Network page.
5. Three-device test with the Odin hosting.
