/* The game's aim servo (game.h): find it, switch it off, keep it off, put it back. */
#ifndef SERVO_H
#define SERVO_H
#include "game.h"

typedef struct {
    u32 addr;           /* the servo function, 0 = not found (yet) */
    u32 orig[2];        /* its first two words, to put back */
    u32 scan;           /* next address to scan */
    u32 off;            /* 1 while we have it switched off */
    u32 passes;         /* full scans that found nothing */
} Servo;

/* Call every frame while free aim is wanted: scans a slice, patches once found, re-patches when
   GAME.BIN is reloaded. Returns 1 while the servo is off (the elevation is ours). */
int servo_keep_off(Servo *s);
/* Put the original words back (mod switched off). */
void servo_restore(Servo *s);

#endif
