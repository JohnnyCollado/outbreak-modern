# Credits

- **HeySnippy** -- the original *Resident Evil Outbreak Modern Camera* mod for PCSX2 on Windows,
  which this project brings to ARMSX2 on Android (its settings for reference: `legacy/ro_settings.json`).
- **GalaxyNostalgia and contributors** --
  [Outbreak-decomp](https://github.com/GalaxyNostalgia/Outbreak-decomp) (CC0): the addresses,
  structures and functions every part of this mod is built on.
- **ARMSX2 / PCSX2 developers** -- the emulator, its PINE interface and its patch and texture
  replacement systems.
- **Silent Hell & REfan988** -- the English fan translation (v2.1) of the Japanese release.
- **REOB HD texture pack** -- by **NoBigDeal La**, used under its author's terms. Made for the US
  release; most textures match the Japanese one too (packs are keyed by texture content). Not
  stored in this repository (about 3 GB); get it from its author.
- **Sean Barrett** -- [stb](https://github.com/nothings/stb) `stb_image_write.h` / `stb_image.h` (public domain or
  MIT), used by the diagnostics plugin `native/ob_diag` for PNG frame dumps; taken from raylib's bundled copy.
- **Billy Laws (bylaws)** -- [libadrenotools](https://github.com/bylaws/libadrenotools) and
  [liblinkernsbypass](https://github.com/bylaws/liblinkernsbypass) (BSD-2-Clause), built from source into the Android
  app: they let a player load their own Vulkan driver (for example Mesa Turnip on Adreno) instead of the phone's.
  No driver is bundled; players supply their own.
