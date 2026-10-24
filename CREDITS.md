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
- **ARMSX2 developers (ARMSX2 Dev Team, yaps2 Dev Team, isztld)** -- the ARM64 microVU VU1 recompiler and its vixl
  helpers (`pcsx2/arm64/microVU-arm64*`, `AsmHelpers`, `vuaot/`), GPL-3.0(+), built from
  [github.com/ARMSX2/ARMSX2](https://github.com/ARMSX2/ARMSX2) at commit `90126bd` into the Android app's
  `libob_a64vu.so` (`native/pcsx2_vu/a64`); nothing of it is copied into this repository.
- **ARM Limited and the VIXL authors** -- [VIXL](https://github.com/Linaro/vixl) (BSD-3-Clause, its `LICENCE` file), the
  AArch64 assembler PCSX2 vendors in `3rdparty/vixl`: behind the Android GS's software-renderer JIT and ARMSX2's
  ARM64 microVU.
