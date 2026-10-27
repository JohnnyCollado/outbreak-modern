# outbreak-modern

A native, statically recompiled build of Resident Evil Outbreak File #1 (SLPM-65428, JP v1.04 + English
fan patch v2.1) with modern controls: Windows first, then Linux and Android.

The game's code is recompiled from your own disc at build time on PS2Recomp (with our numbered patches in
`upstream/ps2recomp/`), and drawn by PCSX2's GS built in as a library (`native/pcsx2_gs/`). No game data
is in this repository.

- What each part of the repo is for: `docs/ARCHITECTURE.md`
- Goals, stack and early decisions: `PROJECT_CONTEXT.md`; roadmap: `docs/milestones/`
- Build, run and test: `docs/ops/build.md`
- The earlier Modern Camera patch for ARMSX2 that this grew out of: `legacy/README.md`

## Online and same-network play (planned, not built yet)

Online play uses the game's own network code. Research and design: `research/2026-10-26-network-crossplay.md`.

**First release: this port's clients only.** Windows and Android players of outbreak-modern play together, in any
mix. Playing with PCSX2 or real PS2s, and on the fan server (Obsrv), comes in the next release.

- **Pick a server** in the settings: a server you or someone else runs, or a game hosted on your network. The
  game has no LAN mode of its own; every match goes through a server.
- **Play on the same network:** press "Host game" on one Windows or Android device, and the others join it from a
  list or by its IP address.

Limitations:

- **This port's clients only** in the first release (above).
- **Hosting is for your local network.** Playing over the internet needs port forwarding or a VPN such as
  Tailscale, set up by you; the app doesn't do it.
- **The host server would be AGPL-3.0**, like the fan server it is based on, and ship as its own program or
  library.

Next release, when PCSX2 and PS2 players can join:

- **An Android host will serve only this port's players.** Android apps can't open ports below 1024, and the
  game's login check (DNAS) uses port 443, which a PS2 or PCSX2 can't be told to change. A Windows host will
  serve everyone.
- **Live play on Obsrv waits for its admins' agreement.**
- **Only the Japanese release is tested on the fan server.** Whether the English fan patch changes anything
  online is not known yet.
