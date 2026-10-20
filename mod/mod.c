/* The standalone Modern Camera mod for File #1, run by the game every frame (stub.S), right
   before the camera goes to the renderer: what tools/f1_camtest.py `run` does from the PC, here
   inside the game. Milestone 2: the over-the-shoulder camera on the right stick, modern
   walking, callouts on L1 + right stick, SELECT + R3 to switch it all off and on.
   Milestone 3: the camera stays inside the room (walls) and under the stairs (headroom).
   Milestone 4: aiming -- a closer aim camera, the game's aim servo switched off so the gun
   follows the camera up and down, RE5-style (she turns to where the camera looks, the camera
   holds still), shots converging on the crosshair, and a "+" crosshair.
   The button layout comes next. */
#include "mod.h"
#include "mathf.h"
#include "collide.h"
#include "servo.h"

#define STATUS (*(volatile ModStatus *)MOD_STATUS)

/* Settings: the PC tool's defaults, as tuned on the device. */
#define DISTANCE        220.0f
#define HEIGHT          150.0f
#define SHOULDER        35.0f
#define LOOKAHEAD       200.0f
#define PITCH_START     DEG(8.0f)
#define PITCH_MIN       DEG(-30.0f)
#define PITCH_MAX       DEG(60.0f)
#define ORBIT_SPEED     DEG(100.0f)     /* rad/s at full right stick (device: 150 felt fast) */
#define TURN_SPEED      DEG(900.0f)     /* modern walking: how fast she turns to the stick */
#define STICK_DEAD      0.15f
#define TARGET_HEIGHT   140.0f          /* her middle, where the wall trace starts */
#define WALL_MARGIN     20.0f           /* units kept between the camera and a wall */
#define HEADROOM        190.0f          /* max camera height above the floor under it (no ceilings in the data) */
#define PULL_EASE       0.3f            /* seconds to ease back out after a wall */
/* the aim rig, blended in over AIM_BLEND (f1_camtest --aim-*) */
#define AIM_DISTANCE    120.0f
#define AIM_HEIGHT      160.0f
#define AIM_TARGET_HEIGHT 150.0f
#define AIM_SHOULDER    50.0f
#define AIM_LOOKAHEAD   400.0f
#define AIM_BLEND       0.12f
#define AIM_FOLLOW      0.3f            /* no servo: seconds to swing round to a lock-on */
#define AIM_MAX         DEG(45.0f)      /* free aim up/down limit (the game's own) */
#define AIM_TURN        DEG(720.0f)     /* she turns into the aim at this rate (device: one-frame snap felt bad) */
#define CONVERGE        1500.0f         /* aim at what the crosshair is on, else this far ahead */
#define CROSS_SLOT      30              /* the crosshair's text line (the banner uses TEXT_SLOT) */
#define CROSS_SIZE      24
#define COUNT_HZ        294912000.0f    /* the EE's cycle counter (COP0 Count) */
#define COMBO_HOLD      (COUNT_HZ * 1.0f)

typedef struct {
    u32 ready;
    u32 last_count;
    u32 combo_since;    /* Count when SELECT + R3 went down, 0 = not held */
    u32 combo_done;
    u32 player;         /* which player the camera was set up behind */
    f32 yaw, pitch;
    f32 face;           /* modern walking: her facing, radians */
    f32 pull;           /* fraction of the shoulder -> eye distance in use (walls) */
    u32 facing;
    s16 callout_dz;     /* the game's own right-stick dead zone, once found */
    u16 dz_known;
    u16 last_buttons;
    u32 aiming;         /* last frame */
    f32 aim;            /* 0 = hip rig, 1 = aim rig */
    f32 aim_face;       /* free aim: her facing, turned toward the aim */
    u32 aim_face_set;
    u32 cross_shown;
    Servo servo;
} State;

static State st;        /* .bss: never written by the patch; set up below */

static const char banner_on[] = "Modern Camera: on   (SELECT + R3: off)";
static const char banner_off[] = "Modern Camera: off  (SELECT + R3: on)";
static const char cross_glyph[] = "\x81\x7b";     /* the full-width plus (Shift-JIS): square on screen */

static u32 cycle_count(void) {
    u32 c;
    __asm__ volatile("mfc0 %0, $9" : "=r"(c));
    return c;
}

/* Our text line in `slot`, if the game isn't using it. */
static int show_line(int slot, const char *text, s16 x, s16 y, s16 frames, u8 size) {
    u8 *line = (u8 *)(TEXT_LINES + slot * TEXT_LINE_SIZE);
    u32 i;

    if (MEM(s16, line) != 0 && line[0x0B] != TEXT_PRIO) {
        return 0;
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
    return 1;
}

static void show_text(const char *text, s16 x, s16 y, s16 frames, u8 size) {
    show_line(TEXT_SLOT, text, x, y, frames, size);
}

static void hide_line(int slot) {
    u8 *line = (u8 *)(TEXT_LINES + slot * TEXT_LINE_SIZE);
    if (MEM(s16, line) != 0 && line[0x0B] == TEXT_PRIO) MEM(s16, line) = 0;
}

/* A stick byte (0-255, 128 centre) to -1..1 with a dead zone, as the PC tool's stick(). */
static float stick(u8 v) {
    float x = ((float)v - 127.5f) * (1.0f / 127.5f);
    if (absf(x) < STICK_DEAD) return 0.0f;
    x = (x - (x < 0.0f ? -STICK_DEAD : STICK_DEAD)) * (1.0f / (1.0f - STICK_DEAD));
    return clampf(x, -1.0f, 1.0f);
}

static float heading_of(u32 p) {
    return (float)MEM(u16, p + P_HEADING) * (TWO_PI_F / 65536.0f);
}

static void callouts_to_game(void) {
    if (st.dz_known) MEM(s16, RIGHT_STICK_DZ) = st.callout_dz;
}

static float lerp(float a, float b, float t) { return a + (b - a) * t; }

void mod_frame(u32 caller) {
    volatile ModStatus *ms = &STATUS;
    u32 n = MEM(u32, PLAYER_COUNT), idx = MEM(u8, OUR_PLAYER), p, now;
    u16 buttons;
    u8 act;
    float dt, sx, sy, yaw, ce, se, fx, fz, rx, rz, pivot[3], d[3], eye[3], look[3];
    float r_dist, r_height, r_th, r_shoulder, r_look;
    int aiming, free_aim;

    if (ms->magic != MOD_MAGIC || ms->version != MOD_VERSION) {
        u32 starts = ms->magic == MOD_MAGIC ? ms->starts : 0;
        ms->magic = MOD_MAGIC;
        ms->version = MOD_VERSION;
        ms->frames = 0;
        ms->starts = starts + 1;
        ms->enabled = 1;
        st.ready = 0;
    }
    ms->frames++;
    ms->caller = caller;
    if (!(n > 0 && n <= 4 && idx < n)) {
        return;
    }
    p = PLAYERS + idx * PLAYER_STRIDE;
    ms->player[0] = MEM(f32, p + P_POS);
    ms->player[1] = MEM(f32, p + P_POS + 4);
    ms->player[2] = MEM(f32, p + P_POS + 8);

    now = cycle_count();
    if (!st.ready || st.player != idx) {
        s16 dz = MEM(s16, RIGHT_STICK_DZ);
        if (!st.dz_known && dz >= 0 && dz < 0x7F) {     /* (0x80 would be ours, from before) */
            st.callout_dz = dz;
            st.dz_known = 1;
        }
        st.ready = 1;
        st.player = idx;
        st.yaw = heading_of(p);          /* start right behind her */
        st.pitch = PITCH_START;
        st.last_count = now;
        st.combo_since = 0;
        st.combo_done = 0;
        st.facing = 0;
        st.pull = 1.0f;
        show_text(ms->enabled ? banner_on : banner_off, 16, 16, 240, 16);
    }
    dt = (float)(now - st.last_count) * (1.0f / COUNT_HZ);
    st.last_count = now;
    if (!(dt > 0.0f && dt < 0.1f)) dt = 1.0f / 60.0f;   /* first call, a pause, a load */
    ms->dt = dt;
    ms->callout_dz = st.dz_known ? (u32)st.callout_dz : 0xFFFF;

    buttons = MEM(u16, PAD + 2);
    if ((buttons & (BTN_UP | BTN_DOWN)) == (BTN_UP | BTN_DOWN) || (buttons & (BTN_LEFT | BTN_RIGHT)) == (BTN_LEFT | BTN_RIGHT)) {
        buttons = st.last_buttons;      /* not a real pad state (seen from the PC mid-update) */
    }
    st.last_buttons = buttons;

    /* SELECT + R3 held for a second: switch the mod off / on. */
    if ((buttons & (BTN_SELECT | BTN_R3)) == (BTN_SELECT | BTN_R3)) {
        if (!st.combo_since) st.combo_since = now | 1;
        if (!st.combo_done && (float)(now - st.combo_since) > COMBO_HOLD) {
            st.combo_done = 1;
            ms->enabled = !ms->enabled;
            if (!ms->enabled) {
                callouts_to_game();
                servo_restore(&st.servo);
                hide_line(CROSS_SLOT);
                st.cross_shown = 0;
            }
            else st.yaw = heading_of(p), st.pitch = PITCH_START;
            show_text(ms->enabled ? banner_on : banner_off, 16, 16, 180, 16);
        }
    } else {
        st.combo_since = 0;
        st.combo_done = 0;
    }
    if (!ms->enabled) {
        ms->facing = 0;
        return;
    }

    /* Aiming. Free aim needs the servo off; until it's found (a second's scan) or if it never
       is, aiming works the game's way and the camera swings round behind her. */
    act = MEM(u8, p + P_ACT);
    aiming = act == ACT_AIM;
    free_aim = servo_keep_off(&st.servo) && aiming;
    if (aiming && !st.aiming) {
        st.aim_face_set = 0;
        if (!free_aim) st.yaw = heading_of(p);      /* RE5 style keeps the view; else swing behind */
    }
    st.aiming = aiming;
    st.aim += ((aiming ? 1.0f : 0.0f) - st.aim) * (dt / (AIM_BLEND + dt));
    r_dist = lerp(DISTANCE, AIM_DISTANCE, st.aim);
    r_height = lerp(HEIGHT, AIM_HEIGHT, st.aim);
    r_th = lerp(TARGET_HEIGHT, AIM_TARGET_HEIGHT, st.aim);
    r_shoulder = lerp(SHOULDER, AIM_SHOULDER, st.aim);
    r_look = lerp(LOOKAHEAD, AIM_LOOKAHEAD, st.aim);

    /* Callouts only with L1 + right stick; the right stick alone is the camera. */
    if (st.dz_known) {
        MEM(s16, RIGHT_STICK_DZ) = (buttons & BTN_L1) ? st.callout_dz : 0x80;
    }
    sx = stick(MEM(u8, PAD + 4));
    sy = stick(MEM(u8, PAD + 5));
    if (buttons & BTN_L1) sx = sy = 0.0f;
    /* Stick right turns the view right (that is -yaw here; +yaw felt "southpaw" on the device). */
    st.yaw = wrap_pi(st.yaw - sx * ORBIT_SPEED * dt);
    /* free aim may look further up (as far as the gun goes) */
    st.pitch = clampf(st.pitch + sy * ORBIT_SPEED * 0.6f * dt, free_aim ? -AIM_MAX : PITCH_MIN, PITCH_MAX);
    if (aiming && !free_aim && MEM(u32, p + P_TARGET)) {
        /* no free aim, locked on: the game turns her to the target; follow her round, slowly */
        st.yaw = wrap_pi(st.yaw + wrap_pi(heading_of(p) - st.yaw) * (dt / (AIM_FOLLOW + dt)));
    }

    /* The orbit rig (f1_camtest.ots_camera): around a pivot at her shoulder, looking along
       (yaw, elevation -pitch); eye `distance` behind the pivot, look-at `lookahead` ahead. */
    yaw = st.yaw;
    fx = sinf_(yaw);
    fz = cosf_(yaw);
    rx = fz;
    rz = -fx;
    ce = cosf_(-st.pitch);
    se = sinf_(-st.pitch);
    pivot[0] = ms->player[0] + rx * r_shoulder;
    pivot[1] = ms->player[1] + r_height;
    pivot[2] = ms->player[2] + rz * r_shoulder;
    d[0] = fx * ce;
    d[1] = se;
    d[2] = fz * ce;
    eye[0] = pivot[0] - d[0] * r_dist;
    eye[1] = pivot[1] - d[1] * r_dist;
    eye[2] = pivot[2] - d[2] * r_dist;
    look[0] = pivot[0] + d[0] * r_look;
    look[1] = pivot[1] + d[1] * r_look;
    look[2] = pivot[2] + d[2] * r_look;
    {
        /* No room data (a load, a room change): no wall in the way, so the pull eases back. */
        float floor, top, centre[3], side[3], ts, span, want = 1.0f, t, keep;
        int i, ready = coll_ready();
        /* Head-room: no ceilings in the data, but floors are, and anywhere people walk has room
           above it: keep the eye within HEADROOM of the floor under it (stairs). */
        top = eye[1] > ms->player[1] + r_th ? eye[1] : ms->player[1] + r_th;
        floor = ready ? coll_floor_below(eye[0], top + 50.0f, eye[2], 1500.0f, -1e30f) : -1e30f;
        if (floor > -1e29f) {
            eye[1] = clampf(eye[1], floor + 30.0f, floor + HEADROOM);
        }
        /* Walls (f1_camtest.collide_pull): trace from her middle out to her shoulder point, then
           from there to the eye; stop WALL_MARGIN short of the first triangle. The pivot is the
           shoulder, so a squeeze ends over her shoulder rather than inside her back. */
        centre[0] = ms->player[0];
        centre[1] = ms->player[1] + r_th;
        centre[2] = ms->player[2];
        side[0] = centre[0] + rx * r_shoulder;
        side[1] = centre[1];
        side[2] = centre[2] + rz * r_shoulder;
        ts = ready ? coll_first_hit(centre, side) : -1.0f;
        if (ts >= 0.0f) {
            keep = ts - WALL_MARGIN / r_shoulder;
            if (keep < 0.0f) keep = 0.0f;
            for (i = 0; i < 3; i++) side[i] = centre[i] + (side[i] - centre[i]) * keep;
        }
        span = sqrtf_((eye[0] - side[0]) * (eye[0] - side[0]) + (eye[1] - side[1]) * (eye[1] - side[1])
                      + (eye[2] - side[2]) * (eye[2] - side[2]));
        t = ready ? coll_first_hit(side, eye) : -1.0f;
        if (t >= 0.0f) {
            want = t - WALL_MARGIN / (span > 1e-3f ? span : 1e-3f);
            if (want < 0.0f) want = 0.0f;
        }
        /* snap in, ease back out */
        st.pull = want < st.pull ? want : st.pull + (want - st.pull) * (dt / (PULL_EASE + dt));
        for (i = 0; i < 3; i++) eye[i] = side[i] + (eye[i] - side[i]) * st.pull;
    }
    ms->pull = st.pull;
    MEM(f32, CAM_POS) = eye[0];
    MEM(f32, CAM_POS + 4) = eye[1];
    MEM(f32, CAM_POS + 8) = eye[2];
    MEM(f32, CAM_TARGET) = look[0];
    MEM(f32, CAM_TARGET + 4) = look[1];
    MEM(f32, CAM_TARGET + 8) = look[2];
    ms->yaw = st.yaw;
    ms->pitch = st.pitch;
    ms->eye[0] = eye[0]; ms->eye[1] = eye[1]; ms->eye[2] = eye[2];
    ms->look[0] = look[0]; ms->look[1] = look[1]; ms->look[2] = look[2];

    /* Modern walking: the game already walks relative to this camera (func_006307E0, confirmed
       on the device). Face her straight down the stick (camera yaw + the stick's angle, as the
       game computes it), turning at most TURN_SPEED, so she walks instead of arcing. */
    if (act != ACT_AIM && act <= 1 && MEM(s16, p + P_STICK_MAG) >= 0x10) {
        float cam_units = atan2f_(look[0] - eye[0], look[2] - eye[2]) * (65536.0f / TWO_PI_F);
        float want = (cam_units + (float)(MEM(u16, p + P_STICK_ANG) & 0xFFF0)) * (TWO_PI_F / 65536.0f);
        float step = TURN_SPEED * dt;
        if (!st.facing) st.face = heading_of(p);
        st.face = wrap_pi(st.face + clampf(wrap_pi(want - st.face), -step, step));
        MEM(s16, p + P_HEADING) = (s16)(int)(st.face * (65536.0f / TWO_PI_F));
        st.facing = 1;
    } else {
        st.facing = 0;
    }
    ms->facing = st.facing;

    if (free_aim) {
        /* She faces where the camera looks and the gun takes its elevation, so the shot lines up
           with the view. Converge: the camera sits a shoulder to the side, so its centre ray (the
           crosshair) runs beside her gun line (device: "slightly off to the right"); aim from her
           chest at what the crosshair is on -- the first wall down that ray, else CONVERGE ahead.
           Clear the game's auto-aim lock every frame, as its own "aim at nothing" does. */
        float dd[3], far[3], ap[3], chest[3], n, t, face, elev, step;
        int i;
        for (i = 0; i < 3; i++) dd[i] = look[i] - eye[i];
        n = sqrtf_(dd[0] * dd[0] + dd[1] * dd[1] + dd[2] * dd[2]);
        if (n < 1e-3f) n = 1e-3f;
        for (i = 0; i < 3; i++) {
            dd[i] /= n;
            far[i] = eye[i] + dd[i] * CONVERGE;
        }
        t = coll_ready() ? coll_first_hit(eye, far) : -1.0f;
        if (t < 0.0f) t = 1.0f;
        for (i = 0; i < 3; i++) ap[i] = eye[i] + dd[i] * CONVERGE * t;
        chest[0] = ms->player[0];
        chest[1] = ms->player[1] + r_th;
        chest[2] = ms->player[2];
        face = atan2f_(ap[0] - chest[0], ap[2] - chest[2]);
        elev = atan2f_(ap[1] - chest[1], sqrtf_((ap[0] - chest[0]) * (ap[0] - chest[0]) + (ap[2] - chest[2]) * (ap[2] - chest[2])));
        if (!st.aim_face_set) {
            st.aim_face = heading_of(p);
            st.aim_face_set = 1;
        }
        step = AIM_TURN * dt;
        st.aim_face = wrap_pi(st.aim_face + clampf(wrap_pi(face - st.aim_face), -step, step));
        elev = clampf(elev, -AIM_MAX, AIM_MAX);
        MEM(u32, p + P_TARGET) = 0;
        MEM(u8, p + P_TARGET_IDX) = 0xFF;
        MEM(s16, p + P_HEADING) = (s16)(int)(st.aim_face * (65536.0f / TWO_PI_F));
        MEM(s16, p + P_ELEV) = (s16)(int)(elev * (65536.0f / TWO_PI_F));
        ms->elev = elev;
    }
    ms->aiming = aiming;
    ms->free_aim = free_aim;
    ms->servo = st.servo.addr | (st.servo.off ? 1 : 0);

    /* The crosshair: the game's own text line (data only), centred, while aiming. Kept topped
       up every frame with a short timer, so it goes by itself if we ever stop. */
    if (aiming) {
        st.cross_shown = show_line(CROSS_SLOT, cross_glyph, SCREEN_W / 2 - CROSS_SIZE / 2,
                                   SCREEN_H / 2 - CROSS_SIZE / 2, 30, CROSS_SIZE);
    } else if (st.cross_shown) {
        hide_line(CROSS_SLOT);
        st.cross_shown = 0;
    }
}
