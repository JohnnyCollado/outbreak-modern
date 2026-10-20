# What a native Android build of Outbreak would have to implement

A map of every place the game reaches the PS2's hardware or its system libraries: rendering,
sound, files, input, saves, movies, timing, threads, network. Each section lists where it is in
the decomp, what the game expects, and what would stand in for it on Android.

Source: [GalaxyNostalgia/Outbreak-decomp](https://github.com/GalaxyNostalgia/Outbreak-decomp) at
`eb6af38` (2026-10-19), File 1 (SLPM-65428). Function names are the decomp's (`func_<address>`;
some have names in comments, e.g. `SdrSeReq`). Built from the decomp's headers, its file comments
and a keyword scan of the C. The decomp's native PC port
([Outbreak-PC](https://github.com/GalaxyNostalgia/Outbreak-PC)) has the actual platform layer,
but it couldn't be read from this session, so nothing here is copied from it.

## First, the two things that decide the approach

1. **Most of the game is not C yet.** About 1,660 File 1 functions have C, out of ~14,500 (the
   main ELF has 5,357; GAME.BIN, where the gameplay is, has 4,412). The decomp's PC port runs the
   decompiled `main()` natively; everything still in assembly needs either the decomp to get
   there, or another way to run MIPS code on ARM (an interpreter/JIT, which is an emulator again,
   or static recompilation of the remaining functions to C).
2. **The PC port is a 32-bit build; the Odin 3 is 64-bit only.** The decomp keeps the game's
   struct layouts with 4-byte pointers ("the PC build is 32-bit, so pointers stay 4 bytes",
   docs/PCPORT.md). As far as I know, the Snapdragon 8 Elite (Odin 3) runs only 64-bit Android
   code, so a straight 32-bit build won't run there. An Android build needs
   a **guest-memory model**: the game's 32 MiB of RAM as one block, every pointer the game stores
   a 32-bit offset into it, translated on use. The decomp already has the start of this
   (`ps2mem.h`: `PS2_RAM()` maps fixed addresses into `pc_ram`); it would have to cover every
   pointer the game keeps in its structs, not only the fixed buffers.

## How the decomp already marks the boundary

| Macro / type | PS2 | PC build (`PLATFORM_PC`) | Android equivalent |
|---|---|---|---|
| `PS2_RAM(addr)` (ps2mem.h) | the address | offset into `pc_ram` (32 MiB) | same, and for all guest pointers (above) |
| `PS2_SPR(addr)` | scratchpad 0x70000000, 16 KiB | `pc_spr` block | same |
| `HW_REG32/64(addr)` | EE device / GS registers | `pc_hw_reg()`: models the timers, ignores the rest | same idea |
| `u128` (common.h) | 128-bit EE register type | `{u64 lo, hi}` struct | same, or NEON `uint64x2_t` |
| `SYNC_EI()` (system.h) | `sync; ei` | nothing | nothing |
| `malloc`/`free` (sce.h) | Sony libc | `pc_game_malloc`/`pc_game_free` | allocator inside guest RAM |

## Rendering

The game never calls a "draw" function. Everything becomes **DMA packets** in two double-buffered
lists, sent on the VIF1 channel: 2D work as raw GS packets (VIF1 DIRECT), 3D meshes to **VU1
microprograms** that transform, light and emit GS primitives. A renderer port replaces the
receiving end: the GS and VU1.

| Piece | Where | What it does | Android |
|---|---|---|---|
| Boot graphics | `func_001A85C0` (boot.c) | resets VU0/VU1/VIF/DMA (`sceDev*Reset`, `sceDmaReset`), `sceGsResetGraph`, 640x448 double buffer, boot image | create the Vulkan device/swapchain |
| Display setup, flip | `func_0019EE00`, `func_0019F930`, v-blank handler (display.c, ~line 328) | frame buffers + Z in GS memory, `GS_PMODE/DISPFB/DISPLAY` registers, flip on v-blank | swapchain present; FIFO present mode for the v-blank |
| Clearing GS memory | `func_0019ED40` (display.c) | 16 strips of 1024x64 uploaded | clear the targets |
| Frame loop | frame.c (`func_001A84A0` and the steps around it) | finish the frame, wait for the flip (`sceGsSyncV`), send, start the next (full-screen clear sprite) | the frame loop around `vkQueuePresentKHR` |
| DMA queue | dmaq.c (`send_entry`, `func_0018C830`, `func_0018CC10`, `func_0018C8E0`, `func_0018D0E0`) | queues and chains packets, sends them on DMA-end interrupts, reads the image back on GS FINISH | no DMA: hand the finished lists to the renderer |
| Packet lists | drawlist.c (`func_001B4C50` …) | closes the main list and GAME.BIN's second list, sends them | same, to the renderer |
| Render state | gfxstate.c `func_00195970(cmd, arg)`, 112 commands | blend, Z, alpha test, fog, matrices… (matrices in the scratchpad, 0x70003700-0x70003FFF) | state in the renderer: pipeline state + uniforms |
| GS state packets | gsstate.c (`state_packet`) | A+D register writes per context | translated into pipeline state |
| Projection | projection.c (`func_0018F0E0`, `func_0018F2B0`) | perspective matrix and derived matrices in scratchpad slots | uniforms (keep the PS2's conventions, e.g. its 640x448 and Z range) |
| 3D meshes | memory.c: `func_001983E0` picks one of **27 VU1 programs** (`D_00212760`…`D_00212CB0`) by drawing flags and mesh format (`0x21035`/`0x121035` plain, `0x221135`/`0x321135` with bone weights); `func_00189A40`, `func_00189D60`, `func_0018BA20`, `func_0018AEA0` batch vertices per program | the VU1 microcode itself is data in the main ELF (0x1d9b00-0x23ba00, not split out yet) | **port each microprogram** to a vertex shader (or CPU code feeding the GPU). The decomp's roadmap: never emulate VU1 |
| Textures | fltex.c (Capcom's flPS2 texture manager: `func_0018C0B0`…`func_0019CD70`), tim2.c | TIM2 pictures to GS memory, VRAM allocation, upload packets, CLUTs | Vulkan images; decode TIM2 (4/8-bit palettised, 16/24/32-bit) on upload |
| 2D | sprite2d.c (`func_001AAE10`, `func_001AAB10`, `func_001AB4F0`, `func_001937B0`), fade.c | rectangles, fades, the screen clear | a 2D pass |
| Text | font.c (`func_001B55A0`, `func_001B56E0`, `func_001B58E0`) | Shift-JIS glyphs uploaded as used, one sprite per character | same, from the font file |
| In-game screen | submain/ingame.c (`func_00345740`, `func_00345870`, `func_003479E0`) | camera to the renderer, view matrix, effects | uniforms |
| Title | demo/title.c (`func_00548EB0`) | title-screen packets | 2D pass |

The decomp's suggested path (ROADMAP Phase 5, OpenGOAL's approach): first a slow "packet → GS"
reference renderer that interprets GS packets (useful because every draw already goes through
packets), then native renderers per draw type. On Android: Vulkan (the Odin 3's Adreno has it;
GLES 3.2 as a fallback).

**VU0 math** (vu0.h, 45 functions: matrices, vectors, rotations, splines, e.g. `func_00190C50`
multiply, `func_001911F0` look-at, `func_001913A0` vector x matrix; mathf.c wraps them). Hand
written VU0 macro-mode assembly in the original; plain C (NEON if needed) on Android.

## Sound

The EE doesn't mix audio. It packs commands into a byte stream each frame and sends them over SIF
RPC to **Tamsoft's sound driver (TSNDDRV.IRX)** on the IOP, which plays sound-bank waves on the
SPU2. Music and voice are **CRI ADX** streams.

| Piece | Where | Android |
|---|---|---|
| Driver link | sdr.c: `SdrInit` `func_001A66A0`, `SdrBind` `func_001A6630` (two RPC channels: TSNDDRV_DEV commands, TSNDGET_DEV state) | none: call the replacement directly |
| Sound effects | `SdrSeReq` `func_001A67B0`, `SdrSeCancel` `func_001A68D0`, the 32-slot queue, packing (`func_001A7580`, `func_001A7660`, `func_001A7730`, `func_001A78D0`, `func_001A7980`, `func_001A7A20`), `SdrSendReq` `func_001A7280` | a native **reimplementation of TSNDDRV's commands** (the opcodes are in sdr.c) on a software mixer |
| Volumes, mute, mono | `SdrSeMasterVol` `func_001A6B30`, `SdrMasterVol` `func_001A6BD0`, `SdrAllMute` `func_001A7050`, `func_001A70F0` | mixer gains |
| Reverb | `SdrSetRev` `func_001A7190` (SPU2 core reverb) | a reverb effect in the mixer |
| Sound banks | `SdrTransBank` `func_001A6DA0`, `func_001A6C70`; sebank.c (banks queued, one per frame), enemysnd.c | parse the banks; decode SPU2 ADPCM (VAG) waves |
| Driver state / waits | `func_001A7440`, `func_001A7540`, `func_001A73E0`, `func_001A74E0`, `SdrDelayThread` `func_001A6580` | direct calls; no RPC waits |
| Music / voice streams | options.c: `ADXT_Create/StartAfs/Pause/Stop/SetOutVol…` on three streams (`func_001ACEA0`, `func_001ACCC0`, `func_001ADA60`, `func_001AD350`, `func_001AD470`, `func_001AD740` …); fsinit.c `ADXT_Init` | an ADX decoder (vgmstream-style) feeding the mixer |
| Output | SPU2 | **Oboe / AAudio** |

The IOP side of TSNDDRV is not in the decomp (ROADMAP Phase 4: Capcom's/Tamsoft's own IOP modules
get decompiled). Its command semantics come from sdr.c until then.

## Files

| Piece | Where | Android |
|---|---|---|
| Disc paths | cdfile.c: `"cdrom0:\NAME;1"`, sizes (the PC port routes them to its `sce_io.c`, reading the user's disc image) | read the user's ISO (Storage Access Framework or app storage) |
| CRI file system + AFS | fsinit.c: `ADXPS2_SetupDvdFs/HstFs/Thrd`, `ADXPS2_LoadFileListDvd/Hst`, `ADXF_LoadPartitionNw`, `ADXF_LoadPtFromAfsNw` (`func_001A7DF0`, `func_001A80C0`…`func_001A8E10`); the data lives in three AFS archives | an AFS reader over the ISO |
| Loading | loader.c (`func_001A9270`, `func_001A9380`, `func_001A9510`: `ADXF_OpenAfs/ReadNw/GetStat`), scenario.c, roomdata.c, sebank.c, overlay.c `func_001A88F0` | synchronous or threaded reads; keep the "start, poll status" shape |
| Sony I/O | `sceOpen/sceRead/sceLseek/sceClose` (sce.h), `sceFsReset` | file I/O on the ISO |
| Code overlays | overlay.c (`func_001A8870`, `func_001A8990`), task.c, patch.c `func_001D9810` (patches applied after load) | no loading: all overlays compiled in. Overlays share address ranges (0x540000: DEMO/GAME/NETWK; 0x6C0000; 0x820000: SINGLE/NETAQ), so their data must keep separate guest addresses or be swapped in as on PS2 |

## Movies

submain/movie.c: CRI **Sofdec** (`mwPlyCreateSofdec`, `mwPlyStartAfs/Seamless`, `mwPlyGetCurFrm`,
`mwPlyRelCurFrm` …; set up in gamestart.c `func_001CF5C0`), frames uploaded to GS memory and drawn
as sprites (`func_00349530`, `func_003493B0`, `func_00348EC0`, `func_003490F0`); ipu.c decodes
pictures on the **IPU** (MPEG-2). Android: **FFmpeg** (Sofdec is MPEG-1/2 video with ADX audio;
Android's MediaCodec generally doesn't cover MPEG-2), frames into a texture.

## Input

| Piece | Where | Android |
|---|---|---|
| Pad library | input.c `func_001A5B20` (`scePad2Init`, `sceDbcInit`), `func_001A5B60` (sockets) | Android game controllers (`AInputEvent` / GameActivity + Paddleboat), touch overlay if wanted |
| Reading | padread.c `func_001A5C20` (`scePad2Read`, state, button profile) fills PadData 0x292D40; everything above (button map, dead zones, direction bits, ctrl.c) is plain C | fill PadData the same way |
| Vibration | padread.c `func_001A6450` (libdbc actuators `func_00189308`, `func_00189388`), vibe.c patterns (task slot 14) | `Vibrator` / controller rumble |
| USB keyboard | usbkb.c (libusbkb, for chat), USBD.IRX/USBKB.IRX | hardware keyboard or the soft keyboard |

## Saves

memcard.c `func_001BD8F0` (`sceMc2Init`, sockets), hddwork.c (`sceMc2CheckAsync`, `GetInfoAsync`,
`SearchFileAsync`, `ReadFileAsync`: `func_001BDB80`…`func_001BDEA0`), mcjob.h (17 async calls:
create, read, write, delete, mkdir, format …). Android: save files in app storage behind the
same async "start, then poll" interface.

## Time, threads, interrupts

| Piece | Where | Android |
|---|---|---|
| Frame timer | sys.c `func_0019E9A0`: Timer 1 (`HW_REG32(0x10000810/0x10000800)`, bus clock) | `clock_gettime(CLOCK_MONOTONIC)` |
| Frame pacing | display.c v-blank handler, `sceGsSyncV`/`sceGsSyncVCallback`; the logic runs at 30 Hz (ROADMAP) | vsync from the swapchain / Choreographer; keep the 30 Hz tick |
| Real-time clock | `sceCdReadClock` (gamestart.c `func_001B9720`, memcard.c) | system time (as BCD) |
| Kernel | kernel.h: `CreateThread/StartThread/SleepThread/WakeupThread`, `CreateSema/WaitSema/SignalSema/PollSema`, `SetAlarm` (sdr.c), `AddIntcHandler/AddDmacHandler/Enable*` (dmaq.c `func_0018C8E0`), `FlushCache` (~10 places) | pthreads/semaphores; interrupt handlers become direct calls; `FlushCache` nothing |

## The IOP and its modules

sys.c (`func_001AA830`, `func_001AA7D0`) reboots the IOP (`DNAS280.IMG`), sets up its heap and
RPC (`sceSifInitRpc`, `sceSifInitIopHeap`), and starts modules packed in `DRIVE.DAT` plus
`USBD.IRX`/`USBKB.IRX`; iop.c (`func_0018D1F0`, `func_001BB420`, `func_001BB500`) loads more with
retries. On disc: Sony's CDVD, pad, memory card, network (INET, PPP/PPPoE, SMAP, DEV9, modems),
HDD/PFS, USB keyboard, DNAS, NTGUI2, and Tamsoft's TSNDDRV. On Android none are loaded: each is
replaced by the native service in the sections above.

HDD: hddcheck.c (the game can run from an HDD install) → report "no HDD".

## Network (online play on Obsrv)

docs/NETWORK.md: lobby TCP 8300, game relay TCP 8690 (File 1). Keep the game's own protocol code
unchanged (PS2 peers read the packets directly); replace only the transport: Sony's socket library
on the IOP network stack → Android sockets; DNAS and the KDDI login pages over HTTPS (legacy TLS,
HTTP/1.0) to the hosts Obsrv answers on; File 1's v1.01 code patch from the server has to be
handled from source. netstate.c holds the connection state. NETWK.BIN, NETHTTP.BIN and
NETDNAS.BIN are still assembly.

## What needs nothing

Everything that is game logic over game memory: character movement (charmove.c), collision
(collision.c, collgrid.c), the camera and view matrix (ingame.c), animation and skeletons (anim.c,
skel.c, keyframe.c, motion.c), ad-libs (talk.c), items, records, scripts, AI (partner.c), the
button mapping and controls (ctrl.c), the heaps and pools (memory.c's allocators, blockmem.c,
pool.c). That is also everything the Modern Camera patch touches, which is why the mod is only
memory reads and writes.

## Order that would make sense

1. Guest-memory model and the ARM64 build of the decomp's C (the 64-bit problem first).
2. Files: ISO + AFS, so the game boots to its first screens.
3. Rendering: the GS-packet reference renderer (2D first: boot image, logos, title, text).
4. Input (PadData) and timing (30 Hz, v-blank).
5. The VU1 microprograms, one by one (the 3D world).
6. Sound: TSNDDRV commands on a mixer, ADX streams, Oboe.
7. Saves, movies, vibration.
8. Network last (the decomp's own Phase 6).

All of it gated on step 0: the remaining ~88% of the code getting to C (or another way to run it).
