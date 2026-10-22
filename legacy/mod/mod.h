/* The standalone mod's memory: code at MOD_BASE (written by the patch), state after it (never
   written by the patch), and a status block at a fixed address the PC tools can read over PINE
   (tools/f1_patchcheck.py). 0x84000-0x100000 stayed zero through gameplay on the device
   (f1_camtest.py freemem, 2026-10-19). */
#ifndef MOD_H
#define MOD_H
#include "game.h"

#define MOD_BASE    0x000A0000u
#define MOD_STATE   0x000A8000u
#define MOD_STATUS  0x000AFF00u
#define MOD_MAGIC   0x434D4F31u     /* "1OMC" */
/* The patch file's "Debug" group (off unless switched on in ARMSX2's Patch Manager) writes
   DEBUG_ON here every frame; the mod reads it. A specific value, so stray memory can't turn it on. */
#define MOD_CONFIG  0x000AFEF0u
#define DEBUG_ON    0x47424431u     /* "1DBG" */
#define MOD_VERSION 10

typedef struct {
    u32 magic;          /* MOD_MAGIC once our code has run since the region was last cleared */
    u32 version;
    u32 frames;         /* calls since then */
    u32 caller;         /* where the last call came from (the patched call site + 8) */
    f32 player[3];      /* our player's position, as we read it */
    u32 starts;         /* times the status was (re)initialised */
    /* 0x20: milestone 2 */
    u32 enabled;        /* 1 while the camera is ours (SELECT + R3 toggles) */
    f32 yaw, pitch;     /* the camera's look direction, radians */
    f32 dt;             /* seconds since the previous call */
    f32 eye[3], look[3];
    u32 facing;         /* 1 while modern walking turns her */
    u32 callout_dz;     /* the game's own right-stick dead zone, as found (0xFFFF: not found) */
    /* 0x50: milestone 3 */
    f32 pull;           /* fraction of the camera distance in use (1: no wall in the way) */
    /* 0x54: milestone 4 */
    u32 aiming;         /* her action is the aim */
    u32 free_aim;       /* 1 while the elevation is ours (servo off) */
    u32 servo;          /* the servo's address | 1 while it is switched off (0: not found yet) */
    f32 elev;           /* the gun's elevation we set, radians */
    /* 0x64: milestone 4, device fixes */
    f32 game_fov;       /* the camera's FOV (degrees; the shot's own, left alone) */
    f32 converge;       /* free aim: distance to what the crosshair is on (enemy, wall, or 1500) */
    f32 trim_up;        /* aim trim, degrees (R3 + D-pad while aiming) */
    f32 trim_right;
    /* 0x74: milestone 5 */
    u32 buttons_on;     /* 1 while the modern layout is in the game's button map */
    u32 quick_turn;     /* 1 stick / 2 D-pad while a quick turn runs */
    u32 debug;          /* 1 while the patch file's Debug group is on */
} ModStatus;

#endif
