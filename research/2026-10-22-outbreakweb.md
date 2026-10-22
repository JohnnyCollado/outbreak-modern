# Outbreak·Web (outbreakweb.com/file1/), read-only survey (2026-10-22)

Link given by the developer. Surveyed read-only by a web retriever: only that site's own pages,
no downloads (no .wasm, .js bundles, ISOs or BIOS), no other domains. Nothing looked like a scam:
no login, payment or download push.

- A thin browser front end, branded "Outbreak·Web". It names no author, engine, disc
  requirement, license or server. No GitHub, Discord, FAQ (`/file1/faq/` is 404) or credits.
- UI: chat, lobby, settings and guides tabs; PS2-style on-screen pad.
- Resolutions 640x512, 1280x1024, 1920x1536 (1x-3x of a 640x512 PS2 frame).
- URL parameters: `?gateway=ws://host:port/eth` (a WebSocket gateway, so networking is likely
  tunnelled as raw Ethernet frames to a relay), `?nonet`, `?lang=es|en|pt|fr|de|it`,
  `?PS2_SCALE=1-3`, `?debug`.
- Whether it is an in-browser emulator or a recompile is not stated (inference only).
- Search snippet (not opened, conflicts with the reo README fetched earlier today): sp00nznet/reo
  described as having a private server for JP (SLPM) online play. The README fetched directly
  said US SLUS-20765 and online "out of scope", so treat the snippet as stale or wrong until
  checked.

Takeaway for us: the `/eth` gateway suggests the game's own network stack is kept and its packets
are carried over a WebSocket to a relay that reaches the private servers. A native build could do
the same with a plain socket. No code taken.
