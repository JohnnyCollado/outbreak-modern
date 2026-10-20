/* Addresses in Biohazard Outbreak File #1 (SLPM-65428, CRC 32088394; the English fan patch keeps
   them). Each one is from the GalaxyNostalgia/Outbreak-decomp (CC0) and was confirmed on the
   device by the PC tools (tools/f1_*.py); see docs/phase1-feasibility-report.md section 13.
   Only addresses and field offsets: no game code. */
#ifndef GAME_H
#define GAME_H

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;
typedef signed int s32;
typedef float f32;

#define MEM(type, addr) (*(volatile type *)(addr))

/* The camera (D_00306320): pos +0x18, target +0x24, fov +0x60 (f32). */
#define CAMERA          0x00306320u
/* The players (D_00476DD0, 0x10E0 each); which one is ours (D_0048BF7D). */
#define PLAYERS         0x00476DD0u
#define PLAYER_STRIDE   0x10E0u
#define PLAYER_COUNT    0x0023BE14u
#define OUR_PLAYER      0x0048BF7Du
#define P_POS           0x38        /* f32[3] */
#define P_HEADING       0x92        /* s16, 0x10000 per turn, atan2(dx, dz) */
/* The on-screen text lines (D_002F7440 + 0x10, 32 of 0x8C): used s16, x s16, y s16, timer s16,
   size u8, fade s8, alpha u8, prio u8, text[0x80]. Drawn while Options+0x16 != 0. */
#define TEXT_LINES      0x002F7450u
#define TEXT_LINE_SIZE  0x8C
#define TEXT_SLOT       31          /* the last one: the game fills from the start */
#define TEXT_PRIO       0xFF        /* the larger number gives way */

#endif
