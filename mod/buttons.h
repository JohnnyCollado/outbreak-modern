/* The modern button layout (tools/f1_buttons.py), inside the game: L2 aims, R2 fires (circle
   still works), R1 runs, cross kicks while aiming and backs out of menus but doesn't run, lock-on
   aim and L2's old action off. Confirmed on the device with the PC tool, 2026-10-20. */
#ifndef BUTTONS_H
#define BUTTONS_H
#include "game.h"

/* Write the layout for this frame's context (cross: kick / menu back / no run). Returns 1 if
   applied, 0 if the game's preset doesn't look like a button map (then nothing is touched). */
int buttons_apply(int aiming, int moving);
/* Put the game's own entries back. */
void buttons_restore(void);

#endif
