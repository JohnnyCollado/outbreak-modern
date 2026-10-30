# Third-party notices

Outbreak Modern is GPL-3.0 (`COPYING`); `native/ob_server` is AGPL-3.0-or-later (`native/ob_server/LICENSE.md`). It
contains no game data. The app bundles or builds from these, each under its own licence. Texts marked "in the app"
ship in the APK's `assets/licenses/`.

| Component | Used for | Licence | Text |
|---|---|---|---|
| PCSX2 (v2.9.134 + `upstream/pcsx2`) | GS, DEV9, SPU2, IPU, SIO2, microVU | GPL-3.0-or-later | in the app (`COPYING`) |
| ARMSX2 | Android ARM64 microVU | GPL-3.0-or-later | in the app (`COPYING`) |
| PS2Recomp (+ `upstream/ps2recomp`) | Recompiler and runtime | See its repository | project repository |
| mbedTLS 2.28.10 | Login site TLS (`ob_server`) | Apache-2.0 OR GPL-2.0-or-later | in the app (`LICENSE-mbedtls`) |
| libadrenotools, liblinkernsbypass | Android custom Vulkan drivers | BSD-2-Clause | in the app (`LICENSE-adrenotools`) |
| raylib | Window and input | zlib | in the app (`LICENSE-raylib`) |
| libzip | Texture packs (PCSX2 3rdparty) | BSD-3-Clause | in the app (`LICENSE-libzip`) |
| Dear ImGui | Menus | MIT | in the app (`LICENSE-imgui`) |
| zstd | PCSX2 dependency | BSD-3-Clause or GPL-2.0 | project repository |
| shaderc, glslang, SPIRV-Tools, SPIRV-Headers | Shader compilation | Apache-2.0 (glslang: mixed permissive) | project repositories |
| sse2neon | x86 intrinsics on ARM | MIT | project repository |
| vixl | ARM64 code generation (PCSX2 3rdparty) | BSD-3-Clause | project repository |
| zlib, libpng | Texture replacement | zlib, libpng licences | project repositories |
| AndroidX, Jetpack Compose | Android UI | Apache-2.0 | project repositories |
| Kotlin, kotlinx.coroutines | Android app | Apache-2.0 | project repositories |

Not yet complete for a public release: the AGPL text (`native/ob_server/COPYING.AGPL`) and the source link the licence
requires for players who connect to a host (`docs/plans/2026-10-30-security-queue-2.md`). Entries marked "project
repository" should be copied in verbatim before distribution; they were not on this machine in a form checked here.
