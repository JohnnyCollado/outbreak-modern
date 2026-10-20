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
#define MOD_VERSION 5

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
    f32 game_fov;       /* the current shot's own FOV (degrees); ours is held at mod.c FOV */
} ModStatus;

#endif
