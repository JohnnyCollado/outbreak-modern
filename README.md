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

Online play uses the game's own network code, so the port plays with any server that runs the File #1 protocol
and with any client on it: Windows, Android, PCSX2 and real PS2s. Research and design:
`research/2026-10-26-network-crossplay.md`.

- **Pick a server** in the settings: the fan server (Obsrv), a server you or someone else runs, or a game hosted
  on your network. The game has no LAN mode of its own; every match goes through a server.
- **Play on the same network** in two ways: run the fan server software (Bioserver1) on a PC at home and have
  everyone pick it, or (later) press "Host game" in the app.

Limitations:

- **An Android host serves only Windows and Android players of this port.** Android apps can't open ports below
  1024, and the game's login check (DNAS) uses port 443, which a PS2 or PCSX2 can't be told to change. A Windows
  host serves everyone.
- **Hosting is for your local network.** Playing over the internet needs port forwarding or a VPN such as
  Tailscale, set up by you; the app doesn't do it.
- **The built-in host server would be AGPL-3.0**, like the fan server it would be based on, and ship as its own
  program or library.
- **Live play on Obsrv waits for its admins' agreement.** Until then, use your own server.
- **Only the Japanese release is tested on the fan server.** Whether the English fan patch changes anything
  online is not known yet.
