/* The standalone Modern Camera mod for File #1, run by the game every frame (stub.S).
   Milestone 1: prove the pipeline -- our code runs inside the game every frame, keeps a status
   block (tools/f1_patchcheck.py reads it) and says so on screen for a few seconds. The camera,
   aiming and controls of tools/f1_camtest.py come next, one milestone at a time. */
#include "mod.h"

#define STATUS (*(volatile ModStatus *)MOD_STATUS)

static const char banner[] = "Modern Camera patch: running";

/* Our text line, if the game isn't using that slot. */
static void show_text(const char *text, s16 x, s16 y, s16 frames, u8 size) {
    u8 *line = (u8 *)(TEXT_LINES + TEXT_SLOT * TEXT_LINE_SIZE);
    u32 i;

    if (MEM(s16, line) != 0 && line[0x0B] != TEXT_PRIO) {
        return;
    }
    MEM(s16, line + 2) = x;
    MEM(s16, line + 4) = y;
    MEM(s16, line + 6) = frames;
    line[8] = size;
    line[9] = 0;
    line[0x0A] = 0x80;
    line[0x0B] = TEXT_PRIO;
    for (i = 0; i < 0x7F && text[i]; i++) {
        line[0x0C + i] = text[i];
    }
    line[0x0C + i] = 0;
    MEM(s16, line) = 1;                 /* used: last, so the game never draws half a line */
}

void mod_frame(u32 caller) {
    volatile ModStatus *st = &STATUS;
    u32 n = MEM(u32, PLAYER_COUNT);
    u32 idx = MEM(u8, OUR_PLAYER);

    if (st->magic != MOD_MAGIC || st->version != MOD_VERSION) {
        u32 starts = st->magic == MOD_MAGIC ? st->starts : 0;
        st->magic = MOD_MAGIC;
        st->version = MOD_VERSION;
        st->frames = 0;
        st->starts = starts + 1;
    }
    st->frames++;
    st->caller = caller;
    if (n > 0 && n <= 4 && idx < n) {
        u32 p = PLAYERS + idx * PLAYER_STRIDE;
        st->player[0] = MEM(f32, p + P_POS);
        st->player[1] = MEM(f32, p + P_POS + 4);
        st->player[2] = MEM(f32, p + P_POS + 8);
    }
    if (st->frames == 30) {
        show_text(banner, 16, 16, 300, 16);   /* ~5 s, once the room is up */
    }
}
