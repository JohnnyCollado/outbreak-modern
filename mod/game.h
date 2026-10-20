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
#define P_HEADING       0x92        /* s16, 0x10000 per turn, atan2(dx, dz); she faces along it */
#define P_ACT           0x08        /* act[0]: 0 standing, 1 moving steps, 2 aiming (charmove.c) */
#define ACT_AIM         2
#define P_STICK_MAG     (0xF78 + 0x20)  /* her Ctrl: the movement stick as the game reads it, s16 0-127 */
#define P_STICK_ANG     (0xF78 + 0x22)  /* u16, 0x10000 per turn, relative to the camera (func_006307E0) */
#define P_TARGET        0x24        /* u32: the aim's target (set by func_0062D6A0 on a lock-on aim) */
#define P_TARGET_IDX    0xCDC       /* u8: 0xFF = none, as the game's own "aim at nothing" sets it */
#define P_ELEV          0xBC8       /* s16: gun elevation, 0x10000 per turn (+-8191 = the game's 45 deg) */
/* The aim servo: a GAME.BIN function that steps P_ELEV each tick for our player (found on the
   device by its contents, 2026-10-20: 0x556800 in the English patch; tools/f1_freeaim.py). Found
   here the same way: `addiu a2, zero, 2730` with two `addiu a2, zero, 1170` in the 24 words
   before it, inside a function with at least five `sh` to +0xBC8. */
#define GAME_BIN_LO     0x00540000u
#define GAME_BIN_HI     0x00900000u
#define SERVO_RATE_STEP 0x24060492u
#define SERVO_RATE_BACK 0x24060AAAu
#define SCREEN_W        640
#define SCREEN_H        448
#define CAM_POS         (CAMERA + 0x18) /* f32[3] */
#define CAM_TARGET      (CAMERA + 0x24) /* f32[3]; the game's movement uses pos -> target as "forward" */
/* The pad this frame (D_00292D40): buttons u16 +2, active-high as the game leaves it, bytes
   swapped against the usual PS2 listing; sticks +4 right x, +5 right y, +6 left x, +7 left y
   (0-255; the decomp's pad.h comment says left first, the device showed otherwise). */
#define PAD             0x00292D40u
#define BTN_L2 0x0001
#define BTN_R2 0x0002
#define BTN_L1 0x0004
#define BTN_R1 0x0008
#define BTN_CROSS 0x0040
#define BTN_SELECT 0x0100
#define BTN_R3 0x0400
#define BTN_UP 0x1000
#define BTN_DOWN 0x4000
#define BTN_LEFT 0x8000
#define BTN_RIGHT 0x2000
/* The sticks' dead zones (D_0023BAE8 {x, y, z, w}); z is the right stick's. Above its 0x7F top,
   the game sees that stick centred, so it fires no callouts (padread.c func_001A5940). */
#define RIGHT_STICK_DZ  (0x0023BAE8u + 4)
/* The on-screen text lines (D_002F7440 + 0x10, 32 of 0x8C): used s16, x s16, y s16, timer s16,
   size u8, fade s8, alpha u8, prio u8, text[0x80]. Drawn while Options+0x16 != 0. */
#define TEXT_LINES      0x002F7450u
#define TEXT_LINE_SIZE  0x8C
#define TEXT_SLOT       31          /* the last one: the game fills from the start */
#define TEXT_PRIO       0xFF        /* the larger number gives way */

#endif
