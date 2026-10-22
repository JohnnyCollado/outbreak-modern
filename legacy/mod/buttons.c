/* The game maps buttons through D_00292C80[port][16] {mask, group}: entry i sets game bit 1 << i
   when `raw & mask` (padread.c:377). It copies a preset there when a controller connects, chosen
   by the port's kind (D_00292D60[port] + 7: 0 digital -> 0x213620, 1/2 analog -> 0x2135A0). We
   rewrite five entries every frame while on (the game only copies the preset on a connect, so this
   also covers a reconnect) and put the preset's own entries back when switched off. */
#include "buttons.h"

#define PAD_MAP         0x00292C80u
#define PAD_KIND        (0x00292D60u + 7)
#define PRESET_DIGITAL  0x00213620u
#define PRESET_ANALOG   0x002135A0u
#define ENTRIES         16
#define B_L2 0x0001u
#define B_R2 0x0002u
#define B_R1 0x0008u
#define B_CIRCLE 0x0020u
#define B_CROSS 0x0040u

#define MAP(i)    (*(volatile u32 *)(PAD_MAP + 8 * (i)))
#define GROUP(i)  (*(volatile u32 *)(PAD_MAP + 8 * (i) + 4))

/* The game's preset for the connected pad, if it looks like one (single-bit masks, groups 0-3,
   at least 12 buttons), else 0: f1_buttons.ButtonMap._preset. */
static const u32 *preset(void) {
    u8 kind = MEM(u8, PAD_KIND);
    const u32 *t;
    int i, used = 0;
    if (kind > 2) return 0;
    t = (const u32 *)(kind == 0 ? PRESET_DIGITAL : PRESET_ANALOG);
    for (i = 0; i < ENTRIES; i++) {
        u32 m = t[2 * i], g = t[2 * i + 1];
        if (m >= 0x10000u || (m & (m - 1)) || g > 3) return 0;
        used += m != 0;
    }
    return used >= 12 ? t : 0;
}

static void set(int i, u32 mask, u32 group) {
    if (MAP(i) != mask) MAP(i) = mask;
    if (GROUP(i) != group) GROUP(i) = group;
}

int buttons_apply(int aiming, int moving) {
    int i, any = 0;
    if (!preset()) return 0;
    for (i = 0; i < ENTRIES; i++) any |= MAP(i) != 0;
    if (!any) return 0;                         /* no controller read yet */
    set(6, B_L2, 3);                            /* aim without a target: L2 */
    set(5, B_R2 | B_CIRCLE, 3);                 /* fire / interact: R2, circle still works */
    set(10, 0, 0);                              /* aim with lock-on (was R1): off, free aim aims */
    set(7, 0, 0);                               /* L2's old action (right-stick flicks): off */
    /* entry 4 (game bit 0x10): run, Kevin's kick while aiming (charmove.c func_0061E810), and
       "back" in the menus. cross shouldn't run, but must still kick and back out of menus. */
    if (aiming) set(4, B_CROSS, 2);
    else if (moving) set(4, B_R1, 3);
    else set(4, B_R1 | B_CROSS, 3);
    return 1;
}

void buttons_restore(void) {
    const u32 *t = preset();
    static const u8 ours[] = {4, 5, 6, 7, 10};
    unsigned k;
    if (!t) return;
    for (k = 0; k < sizeof(ours); k++) set(ours[k], t[2 * ours[k]], t[2 * ours[k] + 1]);
}
