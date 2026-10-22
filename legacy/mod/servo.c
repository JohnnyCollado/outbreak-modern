/* tools/f1_freeaim.py's find_servo / Servo, inside the game. The scan is spread over frames
   (SLICE bytes each) so finding it never stalls a frame. */
#include "servo.h"

#define SLICE   0x10000u
#define JR_RA   0x03E00008u
#define NOP     0u

#define W(a) (*(volatile const u32 *)(a))      /* the word at address a */

static int is_prologue(u32 w) { return (w >> 16) == 0x27BD && (w & 0x8000); }   /* addiu sp, sp, -N */
static int is_store_elev(u32 w) { return (w >> 26) == 0x29 && (w & 0xFFFF) == P_ELEV; }   /* sh x, 0xBC8(y) */

/* The function containing the RATE_BACK at word address i, if it is the servo; else 0. */
static u32 servo_at(u32 i) {
    u32 j, k, steps = 0, end = 0, stores = 0;
    for (k = i - 24 * 4; k < i; k += 4) {
        if (W(k) == SERVO_RATE_STEP) steps++;
    }
    if (steps != 2) return 0;
    /* back to the prologue (or our own patch there), right after the previous function's jr ra */
    for (j = i; j > i - 128 * 4 && j > GAME_BIN_LO + 16; j -= 4) {
        int patched = W(j) == JR_RA && W(j + 4) == NOP;
        if (is_prologue(W(j)) || patched) {
            int prev = 0;
            for (k = j - 16; k < j; k += 4) prev |= W(k) == JR_RA;
            if (!prev) return 0;
            for (k = i; k < i + 64 * 4 && k < GAME_BIN_HI; k += 4) {
                if (W(k) == JR_RA) { end = k; break; }
            }
            if (!end) return 0;
            for (k = j; k < end; k += 4) stores += is_store_elev(W(k));
            return stores >= 5 ? j : 0;
        }
    }
    return 0;
}

static void flush_caches(void) {
    /* FlushCache(0): write back the data cache; FlushCache(2): drop the instruction cache. The EE
       kernel's syscall 0x64. Real hardware needs it after writing code; emulators don't mind. */
    __asm__ volatile("move $4, $0\n\taddiu $3, $0, 0x64\n\tsyscall\n\t"
                     "addiu $4, $0, 2\n\taddiu $3, $0, 0x64\n\tsyscall"
                     ::: "$2", "$3", "$4", "$5", "$6", "$7", "memory");
}

static void patch(Servo *s) {
    volatile u32 *f = (volatile u32 *)s->addr;
    f[1] = NOP;
    f[0] = JR_RA;
    flush_caches();
    s->off = 1;
}

int servo_keep_off(Servo *s) {
    if (s->addr) {
        u32 a = W(s->addr), b = W(s->addr + 4);
        if (a == JR_RA && b == NOP) {
            s->off = 1;
            return 1;
        }
        if (a == s->orig[0] && b == s->orig[1]) {   /* GAME.BIN reloaded: the servo is back */
            patch(s);
            return 1;
        }
        s->addr = 0;                                /* something else is loaded there now */
        s->off = 0;
        s->scan = GAME_BIN_LO;
    }
    if (s->scan < GAME_BIN_LO || s->scan >= GAME_BIN_HI) {
        if (s->scan >= GAME_BIN_HI) s->passes++;
        s->scan = GAME_BIN_LO + 128 * 4;
    }
    {
        u32 end = s->scan + SLICE, k;
        if (end > GAME_BIN_HI - 64 * 4) end = GAME_BIN_HI - 64 * 4;
        for (k = s->scan; k < end; k += 4) {
            if (W(k) == SERVO_RATE_BACK) {
                u32 f = servo_at(k);
                if (f) {
                    s->addr = f;
                    if (W(f) == JR_RA && W(f + 4) == NOP) {
                        s->off = 1;          /* already off (the PC tool left it so): originals unknown */
                        s->orig[0] = s->orig[1] = 0;
                        return 1;
                    }
                    if (!is_prologue(W(f))) { s->addr = 0; continue; }
                    s->orig[0] = W(f);
                    s->orig[1] = W(f + 4);
                    patch(s);
                    return 1;
                }
            }
        }
        s->scan = end >= GAME_BIN_HI - 64 * 4 ? GAME_BIN_HI : end;
    }
    return 0;
}

void servo_restore(Servo *s) {
    volatile u32 *f = (volatile u32 *)s->addr;
    if (s->addr && s->off && s->orig[0] && f[0] == JR_RA) {
        f[0] = s->orig[0];
        f[1] = s->orig[1];
        flush_caches();
    }
    s->off = 0;
}
