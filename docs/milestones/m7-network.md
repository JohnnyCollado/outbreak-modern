# Milestone 7: network and cross-play

## Goal

From the design: "Obsrv lobby, then a room; cross-play test PS2/PCSX2 + Windows + Android
against a local server first (the decomp's Phase 6 plan), live only with the Obsrv admins'
agreement."

## Done when

(From the decomp's Phase 6 check, widened to our platforms.)

- On a local BioServer, a native client logs in, joins the lobby, chats and enters a room.
- A mixed room (PCSX2 + Windows + Android) plays a scenario to the end with no desync.
- For the same inputs, the native client's lobby and game packets match PCSX2's byte for byte
  (the decomp's NETWORK.md testing plan, step 3).
- Live on Obsrv only after the admins agree.

## Depends on M6, and on M2's maths layer being as exact as this milestone turns out to need

## Work

1. **Transport.** The game talks to Sony's socket library on the IOP (INET over DEV9). Replace it
   with host sockets (Winsock / POSIX), leaving the game's own protocol code unchanged (decomp
   NETWORK.md: "Do not rewrite the netcode").
2. **DNAS and the KDDI login pages.** HTTPS with `EDH-RSA-DES-CBC3-SHA` / `EDH-DSS-DES-CBC3-SHA`,
   HTTP/1.0, a fixed User-Agent, EUC-JP pages with CSV in comments (NETWORK.md). Modern TLS
   libraries disable these ciphers. Which mbedTLS (design Decision 3) or OpenSSL build still
   allows them is **unknown**: test.
   - Or keep the game's own TLS client (NETHTTP.BIN, 1,549 functions, recompiled) and give it raw
     sockets. Then no host TLS is needed. Which way the game works (does NETHTTP do its own TLS,
     or call an IOP TLS service?) is **unknown**.
3. **NETDNAS.BIN**: Sony's DNAS code, "few, very large, obfuscated functions" (LAYOUT.md).
   Whether the recompiler handles them is unknown until tried.
4. **The v1.01 code patch.** BioServer 1 sends File 1 clients a 0x7AA0-byte code patch when they
   report an old patch level (NETWORK.md). A static recompiler can't translate code that arrives
   at run time. Options:
   - capture the patch from your own session (the decomp's suggestion), recompile it ahead of
     time, and have the client report v1.01 so the server doesn't send it. Whether a captured
     patch can be shared, or each user must capture their own, is **unknown** (it's Capcom code);
   - an interpreter just for patched code: a small emulator inside the port.
5. **Configurable hosts** (DNS for `gate1.jp.dnas.playstation.org`, `www01.kddi-mmbb.jp`), with
   Obsrv's current addresses as defaults (NETWORK.md gives them, noting they have changed before).
6. **Determinism test.** Two clients on the same inputs: compare game state every tick (positions,
   RNG state, enemy state) between PCSX2 and native. This is the test that answers the maths
   question below.

## What's needed

- A local BioServer (Docker images `yabab/bioserver-docker`, `TAB64-K/ps2-outbreak-server`;
  NETWORK.md). Whether it runs in this container (Docker, network policy) is **unknown**.
- Two PCSX2 instances in DEV9 sockets mode, and Wireshark captures (NETWORK.md).
- A real PS2 with a network adapter for the PS2 part of the test: whether you have one is
  **unknown**.
- The Obsrv admins' agreement before anything live (decomp LEGAL.md rule 5).

## Limitations

- **Obsrv may differ from the published server source** (NETWORK.md says so). A local pass
  doesn't guarantee Obsrv works.
- **Presentation options must be off, or provably visual only**, online (NETWORK.md). Some of
  Decision 6 (interpolation, frame rate) need care: the 30 Hz tick and what is sent must not
  change.
- **The Modern Camera online** may count as a gameplay change. To ask the admins.

## Unknowns (the most important first)

1. **Does online play need bit-exact PS2 maths?** If peers run each other's simulation from
   inputs (lockstep), any float difference desyncs. If each peer sends its own state and the
   others just show it, small differences matter less. The in-game packets are "undocumented
   and peer-interpreted" (NETWORK.md), so **nobody here knows which**. Answer: read the game's
   message code in NETWK.BIN/GAME.BIN (still assembly), or measure with the determinism test.
   This decides how expensive M2's maths layer has to be.
2. **Which RNG the game uses and whether peers share its state.** Unknown. Same answer.
3. **Whether DNAS can be stubbed locally** (NETWORK.md: "only if the later login steps don't
   depend on its result; verify first").
4. **Whether the English fan patch changes anything online** (NETWORK.md: it re-enables DNAS
   checks). Only relevant if you play with it.
5. **Whether the Obsrv admins will accept native clients at all.** Only they can answer.
