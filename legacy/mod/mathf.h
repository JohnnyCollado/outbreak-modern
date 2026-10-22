/* Single-precision math for the EE (no C library here). The EE FPU has sqrt.s but no NaN or
   infinity, so callers keep arguments in range. Errors: sin/cos < 2e-6, atan2 < 1e-5 rad. */
#ifndef MATHF_H
#define MATHF_H

#define PI_F     3.14159265f
#define TWO_PI_F 6.28318531f
#define DEG(d)   ((d) * (PI_F / 180.0f))

static inline float sqrtf_(float x) { return x > 0.0f ? __builtin_sqrtf(x) : 0.0f; }
static inline float absf(float x) { return x < 0.0f ? -x : x; }
static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

/* x wrapped into [-pi, pi). */
static inline float wrap_pi(float x) {
    float n = (float)(int)(x * (1.0f / TWO_PI_F));       /* truncates (cvt.w.s on the EE) */
    x -= n * TWO_PI_F;
    if (x >= PI_F) x -= TWO_PI_F;
    if (x < -PI_F) x += TWO_PI_F;
    return x;
}

/* sin on [-pi, pi]: fold to [-pi/2, pi/2], then an odd polynomial (Taylor to x^11). */
static inline float sinf_(float x) {
    float x2;
    x = wrap_pi(x);
    if (x > PI_F * 0.5f) x = PI_F - x;
    else if (x < -PI_F * 0.5f) x = -PI_F - x;
    x2 = x * x;
    return x * (1.0f + x2 * (-1.0f / 6 + x2 * (1.0f / 120 + x2 * (-1.0f / 5040 + x2 * (1.0f / 362880
              + x2 * (-1.0f / 39916800))))));
}

static inline float cosf_(float x) { return sinf_(x + PI_F * 0.5f); }

/* atan on [-1, 1] (a minimax odd polynomial), then atan2 by octant. */
static inline float atan_unit(float t) {
    float t2 = t * t;
    return t * (0.99997726f + t2 * (-0.33262347f + t2 * (0.19354346f + t2 * (-0.11643287f
              + t2 * (0.05265332f + t2 * -0.01172120f)))));
}

static inline float atan2f_(float y, float x) {
    float ax = absf(x), ay = absf(y), r;
    if (ax == 0.0f && ay == 0.0f) return 0.0f;
    r = ay > ax ? PI_F * 0.5f - atan_unit(ax / ay) : atan_unit(ay / ax);
    if (x < 0.0f) r = PI_F - r;
    return y < 0.0f ? -r : r;
}

#endif
