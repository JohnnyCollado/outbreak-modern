# Milestone 5: sound, then saves and movies

## Goal

From the design: "sound driver commands, ADX music; then saves and movies."

## Done when

- Sound effects play for the actions that trigger them (`SdrSeReq`), with volume, mute, mono and
  reverb (port map, Sound). Compared by ear and by waveform against PCSX2 for a fixed set of
  scenes.
- Music and voice ADX streams play, pause and stop (options.c).
- The game saves and loads through its memory-card code (`memcard.c`, `hddwork.c`; 17 async
  calls in `mcjob.h`) to files on disk, on both platforms.
- The intro movie and in-game movies play with sound (Sofdec; `submain/movie.c`).
- Audio output works on both platforms (Windows through SDL3/WASAPI; Android through
  SDL3/AAudio, or Oboe as the port map suggests). Which one is still to choose.

## Depends on M2 (sound needs only the main ELF); movies on M3 (frames are drawn as sprites);
in-game sound on M4

## Work

1. **Sound driver.** The EE sends command streams over SIF RPC to Tamsoft's TSNDDRV.IRX on the
   IOP (two channels, TSNDDRV_DEV and TSNDGET_DEV; `sdr.c`). Replace the IOP side with native
   code that carries out the same commands on a software mixer. The command opcodes are known
   only from how the EE packs them in `sdr.c`.
2. **Sound banks.** Parse the banks (`SdrTransBank`, `sebank.c`, `enemysnd.c`) and decode SPU2
   ADPCM (VAG).
3. **Reverb.** `SdrSetRev` sets SPU2 core reverb. Matching it means modelling SPU2 reverb, which
   PCSX2's SPU2 code does (GPL-3.0).
4. **ADX.** A decoder (vgmstream has one) feeding the mixer through the `ADXT_*` calls the game
   makes.
5. **Saves.** One file per memory-card save, behind the same "start, then poll" interface.
   Choose a format: raw save files, or a PCSX2-compatible memory card image (to move saves
   between ARMSX2 and the native port). Not decided.
6. **Movies.** Sofdec: MPEG-1/2 video plus ADX audio (port map, Movies). Decode with FFmpeg into
   a texture. The game also uses the IPU (`ipu.c`). Whether that's only for movies is
   **unknown**.
7. **Clock.** `sceCdReadClock` returns system time as BCD.

## What's needed

- PCSX2/ARMSX2 sound captures for comparison.
- FFmpeg built for Windows and Android arm64 (its licence is LGPL or GPL depending on the build
  options).
- A save from your ARMSX2 setup, to test compatibility if saves should move between the two.
  Fetching it from the Odin needs your "proceed".

## Limitations

- **TSNDDRV's IOP side is not decompiled** (decomp ROADMAP Phase 4: Capcom's/Tamsoft's IOP modules
  "get decompiled", not done). Until it is, its behaviour is reconstructed from the EE side and
  from listening. Some effects may sound different (envelopes, pitch, priorities when the 48
  SPU2 voices run out).
- **REO took the other path**: it runs the game's own IRX modules, the sound driver included, on
  PS2Recomp's IOP interpreter (its README). Its SPU2 output isn't implemented yet. If M0 picks REO
  or PS2Recomp, this choice comes up again.
- **Movie artefacts**: REO shows green blocks on motion-compensated movie frames with Play!'s
  decoder. Using FFmpeg doesn't share that bug, but it means decoding the Sofdec stream outside
  the game's own IPU path. Keeping the game's timing (`mwPlyGetCurFrm`) right needs care.

## Unknowns

- **TSNDDRV's full command set and semantics.** Only the commands the EE sends are visible.
- **The exact sound bank format.** Not documented in the decomp. The
  [Outbreak Research](https://outbreak-research.github.io/Documentation/) documentation, linked
  from REO, may cover it: **not read yet**.
- **Whether vgmstream's ADX decoder covers every ADX variant on the disc** (encryption key,
  looping). Unknown until each stream is tried.
- **Audio latency on the Odin** through SDL3/AAudio. Not measured.
- **Whether saves must stay compatible with real PS2 or PCSX2 cards.** It's your call. If yes,
  the save format is fixed by Sony's card layout.
