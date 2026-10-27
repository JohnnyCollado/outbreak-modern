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

## Mistakes and corrections

| Claimed | What was true | How it was caught | Corrected in |
|---|---|---|---|
| Answering DNAS/login inside the adapter avoids old TLS (research 5b) | The game does the TLS itself; the answering side must speak it | Game strings + corbin.zip guide while planning | plan P4 |
| The in-game network code is in NETWK (milestone docs) | It is NETAQ (0x820000); NETWK is the lobby | Reading the overlays' code and strings | research 7.1 |

## Still open

- S0.3 done (research section 7.4).
- S0.1 (recompile NETWK, NETAQ, NETHTTP, NETDNAS) and S0.2 (TLS spike) need work outside the repo (`C:\ob`) and a
  library download: waiting for the developer's "proceed".

## Useful for other developers

- In-game network messages: `research/2026-10-26-network-crossplay.md` section 7.1 (types, sizes, room owner).
