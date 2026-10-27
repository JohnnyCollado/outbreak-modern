# Outbreak File #1 online protocol: what our built-in server must answer (2026-10-27)

Behavioural spec for `ob_server` (plan `docs/plans/2026-10-26-online-play.md`, P3/P12), written from reading the fan
server Bioserver1's source (TAB64-K/ps2-outbreak-server, AGPL-3.0; read only, nothing copied) by two read-only
helpers, plus checks against the game. Players never connect to a third-party server: `ob_server` runs inside our
app on the host's device. Tags: **[R]** read in the fan server's source (file/method), **[G]** confirmed in the game's
own code or strings, **[I]** inferred, **[?]** open.

## 1. The path a player takes

1. **DNAS** (gate1.jp.dnas.playstation.org:443): skipped in our client by a hook (plan P4 result). [R: real PS2s need
   a captured reply file + DNAS certificate: next release]
2. **Web login** (www01.kddi-mmbb.jp, HTTPS, the PS2's old TLS: mbedTLS 2.28, plan P4) in the game's own browser:
   `/00000002/CRS-top.jsp` → … → a page marked `CRS-GAME-START` carrying the session id → `enterareas.html` marked
   `CRS-LBS-INFO-GET` with the lobby address. [R] [G: `CRS-GAME-START`, `CRS-LBS-INFO-GET`, `CRS-TOP`, `<CSV>` and the
   CRS-top.jsp URL are in the game's strings]
3. **Lobby** TCP 8300: login with the session id, handle name, areas → rooms → game slots, create/join, start. [R]
4. **Relay** TCP 8690: login with the same session id; every in-game message is copied to the other players of the
   same game number. [R]

## 2. Web login (section 2 of the login/relay study)

| Page | Must contain | Notes |
|---|---|---|
| `CRS-top.jsp` | `<meta http-equiv="Refresh" content="1; url=…">` | the game's browser follows it [R] |
| game-start page (Bioserver1 `startsession.php?sessid=N`) | `<!--CRS-game-start-->`, `<META HTTP-EQUIV=Content-Type CONTENT=text/html;CHARSET=EUC-JP>`, `<!--<CSV>"OK","<sessid>","https://www01.kddi-mmbb.jp/00000002/enterareas.html","https://www01.kddi-mmbb.jp/00000002/login.php",</CSV>-->` (every field quoted and comma-ended) | sessid = **8 decimal digits** [R] |
| `enterareas.html` | `<!--CRS-lbs-info-get-->`, then a `<CSV>` block: `"OK",` then per lobby six quoted fields: `"www01.kddi-mmbb.jp:8300"`, `"0"`, `"999"`, info 1 (e.g. `"0ad601082008,WEST TOWN,2"`), info 2 and 3 (game markup `<BODY><SIZE=4>…<END>`) | the lobby address is a hostname: our adapter's DNS points it at the host [R]; meaning of `0ad601082008` [?] |

**Quick login (P12):** `ob_server` serves `CRS-top.jsp` as a meta refresh straight to the game-start page with a
session id it minted for the device (from the app's hello), skipping the form pages. Whether the game accepts a
zero-delay refresh is [?]; keep `content="1; …"` first.

## 3. Packet format (lobby and relay login)

12-byte header, big-endian [R `Packet.java`]:

| Off | Size | Field |
|---|---|---|
| 0 | 1 | who: `0x18` server, `0x81` client, `0x28` relay server, `0x82` relay client |
| 1 | 1 | `0x01` query, `0x02` answer (tell), `0x10` broadcast |
| 2 | 2 | command |
| 4 | 2 | payload length |
| 6 | 2 | packet id: an answer repeats the question's id; the server's own packets count up from 1 |
| 8 | 1 | `0x00`, or `0xFF` for an error answer (payload = a markup message) |
| 9 | 3 | `FF FF FF` |

- **Strings the server sends:** 2-byte length + bytes; nicknames Shift-JIS, the rest ASCII with the game's markup
  tags. [R]
- **Strings the client sends** are scrambled: `[len:2][sum:2][data]`, each data byte XOR `calc_shift(i, pid & 0xFF)`
  with fixed tables (`Packet.calc_shift`). [R] The tables are not stored in the game's files (searched), so the game
  computes them in code; first decode of our own client's traffic confirms them [?].
- **Session id over TCP:** the client sends 10 ASCII digits = (first 4 digits + pid, 5 digits) and (last 4 + pid);
  the server subtracts the pid (`check_session`). Lobby: digits at payload 2..11; relay: at payload 0..9 [R; relay
  offset unverified against a capture].

## 4. Lobby (TCP 8300): the minimal path

Each client question gets an answer with the same packet id. Full command table: the lobby study (session record,
2026-10-27); the parts the first release needs:

| Step | Commands | Server does |
|---|---|---|
| Connect | → LOGIN 6101 query `28 37` | check the session from the answer; then CHECKVERSION 6103 query |
| Version | ← 6103 answer | **always continue; never send 6121-6125 (the code patch)** [R]; log the decoded version once [?] |
| Handles | → IDHNPAIRS 6131 broadcast (count + up to 3 × `00 06`+handle+`00 nn`+nickname+`00 00`); ← HNSELECT 6132 | `******` = new: make a 6-char [0-9A-Z] handle; answer `00 06`+handle; then 6104 broadcast (end of login) |
| Login extras | 61A0, 600E, 61A1, 614C (message of the day), 6190 (character: 0xD0 bytes, keep), 6881/6882 (see 7), 6145 (rankings: dummy) | fixed answers [R] |
| Areas | 6203 count (2), 6204 name, 620A description, 6206 status (3 = open), 6205 player counts, 6207 select, 6209 exit | "East Town" / "West Town" [R] |
| Rooms | 6301 count (10), 6302 name, 6304 status, 6303 counts, 6308, 6305 enter, 6408 exit | |
| Slots | 6401 count (20), 6402 title, 6404 status (0 off, 1 free, 2 creating, 3 open, 4 full), 6403, 640B, 6405, 650A, 640A player stats | |
| Rules | 6603, 6607, 6604, 6606, 6605, 6608, 660E, 6601/6602; 660B set | players 2-4, wait limit, difficulty, friendly fire [R] |
| Create | 6407 create, 6509 scenario (type `0x11` DVD / `0x12` HDD [R comment; ?]), 6609 name, 660A password, 660C, 6412, 6504 (host: slot open), 6409 timer | |
| Join / leave | 6406 join (errors: "game is full", "not possible", "wrong password"), 6501 cancel; server → 6502, 6503, 6505, 6506 | player numbers: host 1, joiners 2-4 |
| Start | ← 6508 (broadcast) or the timer reaching 0 | assign a game number (first = 2), → GETREADY 6910 to the slot |
| Hand-off | 6911 count, 6912 my number, 6913 player record, 6914 difficulty, **6915 game number** (15 ASCII digits), **6916 relay address** (`00 04` + IPv4 + `00 02 21 F2` + `00 00 1E 00`), 6917 score, 6002 | 6916's IP = the host device's LAN address |
| Keep alive | → 6202 every 30 s; → 6001 query every 60 s | drop a client silent for 60-120 s |
| After the game | 6210-6215 (post-game lobby, area 51) | needed if the game returns there [I] |

Chat (6701/6702), private messages (6704/6705), buddies (6703/6707), events (670D/670E) and info pages (6801): later.

## 5. Relay (TCP 8690)

- On connect: send `28 01 10 31 00 00 <pid> 00 FF FF FF` (GSLOGIN query). The answer (`82 02 10 31 …`) carries the
  session digits; look up the player's game number. No reply. [R]
- Then every frame is `[length:1 incl. itself][data]`; copy it unchanged to every other player with the same game
  number. Reject length 0 (the reference loops forever). The server never reads game data. [R]
- Drop a player silent for 30-60 s; tell nobody (the game notices) [R].

## 6. State (all in memory; persistent: accounts and handles)

| Persist (small file next to the app's settings) | Per run |
|---|---|
| device id → account; up to 3 handles (6 chars) with Shift-JIS nicknames | sessions (8-digit ids), areas/rooms/slots, game numbers, who is where |

## 7. Open points, in order of risk

1. **The 6881/6882 data file** (4,701 bytes the server hands out at login; starts with "outbreak", holds
   `SLPM-65428/65429/65430` and Shift-JIS text). Meaning unknown. Options: answer "no file" and see whether the game
   accepts it; work out its format from the game's code; or, since `ob_server` is AGPL anyway (plan N3/P3), include it
   with credit. Test the first.
2. The scrambling tables and the relay's login offset: confirmed by decoding our own client's traffic.
3. Scenario numbers for File #1 (the fan server's names are File #2's), and DVD/HDD type values `0x11`/`0x12`.
4. Exact bytes of SLOTATTRIB2 (two readings disagreed by one position); constants 61A0/61A1/6308/heartbeat/GSINFO
   tail: replay the fan server's values.
5. Whether the game always goes through the post-game lobby (area 51) after a scenario.

## Sources

- Fan server source (read 2026-10-27): https://github.com/TAB64-K/ps2-outbreak-server, `bioserver1/Bioserver1-master/
  bioserver/` (Packet, PacketHandler, Commands, ServerStreamBuffer, ServerThread, HeartBeatThread, HNPair(s), Area(s),
  Room(s), Slot(s), RuleSet, Client, ClientList, Packet6881, MessageOfTheDay, Information, Database,
  GameServerPacketHandler, GameServerThread, Patch, ServerMain, config.properties), `www/` (CRS-top.jsp, login.php,
  login_form.php, startsession.php, enterareas.html), `database/bioserver.sql`, `patch/update.txt` (a packet capture).
- Game strings: `C:\ob\splat\SLPM_654.28` (CRS-* markers, `<CSV>`, the CRS-top.jsp URL); main program 0x13C000 loads
  LOGIN 0x6101; command numbers are built from group bases 0x6000/0x6100/0x6200/… (not matched one by one).
