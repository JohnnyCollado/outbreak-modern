/* The standalone Modern Camera mod for File #1, run by the game every frame (stub.S), right
   before the camera goes to the renderer: what tools/f1_camtest.py `run` does from the PC, here
   inside the game. Milestone 2: the over-the-shoulder camera on the right stick, modern
   walking, callouts on L1 + right stick, SELECT + R3 to switch it all off and on.
   Collision, aiming and the button layout come in the next milestones. */
#include "mod.h"
#include "mathf.h"

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
    u32 facing;
    s16 callout_dz;     /* the game's own right-stick dead zone, once found */
    u16 dz_known;
    u16 last_buttons;
} State;

static State st;        /* .bss: never written by the patch; set up below */

static const char banner_on[] = "Modern Camera: on   (SELECT + R3: off)";
static const char banner_off[] = "Modern Camera: off  (SELECT + R3: on)";

static u32 cycle_count(void) {
    u32 c;
    __asm__ volatile("mfc0 %0, $9" : "=r"(c));
    return c;
}

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

void mod_frame(u32 caller) {
    volatile ModStatus *ms = &STATUS;
    u32 n = MEM(u32, PLAYER_COUNT), idx = MEM(u8, OUR_PLAYER), p, now;
    u16 buttons;
    u8 act;
    float dt, sx, sy, yaw, ce, se, fx, fz, rx, rz, pivot[3], d[3], eye[3], look[3];

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
            if (!ms->enabled) callouts_to_game();
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

    /* Callouts only with L1 + right stick; the right stick alone is the camera. */
    if (st.dz_known) {
        MEM(s16, RIGHT_STICK_DZ) = (buttons & BTN_L1) ? st.callout_dz : 0x80;
    }
    sx = stick(MEM(u8, PAD + 4));
    sy = stick(MEM(u8, PAD + 5));
    if (buttons & BTN_L1) sx = sy = 0.0f;
    /* Stick right turns the view right (that is -yaw here; +yaw felt "southpaw" on the device). */
    st.yaw = wrap_pi(st.yaw - sx * ORBIT_SPEED * dt);
    st.pitch = clampf(st.pitch + sy * ORBIT_SPEED * 0.6f * dt, PITCH_MIN, PITCH_MAX);

    /* The orbit rig (f1_camtest.ots_camera): around a pivot at her shoulder, looking along
       (yaw, elevation -pitch); eye `distance` behind the pivot, look-at `lookahead` ahead. */
    yaw = st.yaw;
    fx = sinf_(yaw);
    fz = cosf_(yaw);
    rx = fz;
    rz = -fx;
    ce = cosf_(-st.pitch);
    se = sinf_(-st.pitch);
    pivot[0] = ms->player[0] + rx * SHOULDER;
    pivot[1] = ms->player[1] + HEIGHT;
    pivot[2] = ms->player[2] + rz * SHOULDER;
    d[0] = fx * ce;
    d[1] = se;
    d[2] = fz * ce;
    eye[0] = pivot[0] - d[0] * DISTANCE;
    eye[1] = pivot[1] - d[1] * DISTANCE;
    eye[2] = pivot[2] - d[2] * DISTANCE;
    look[0] = pivot[0] + d[0] * LOOKAHEAD;
    look[1] = pivot[1] + d[1] * LOOKAHEAD;
    look[2] = pivot[2] + d[2] * LOOKAHEAD;
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
    act = MEM(u8, p + P_ACT);
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
}
