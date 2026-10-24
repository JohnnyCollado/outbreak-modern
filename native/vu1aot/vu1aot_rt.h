#pragma once

// Runtime of the VU1 ahead-of-time code (generator: tools/vu1aot/emit.py; report reports/2026-10-24-vu1-aot-emitter.md).
//
// The generated functions (game-derived, under C:\ob\vu1aot\gen, never in git) include this header. It holds the
// state record they run on (plain types, no PS2Recomp header) and the helpers that give every result the same bits
// as PS2Recomp's VU1 interpreter (C:\ob\up\ps2xRuntime\src\lib\vu\ps2_vu1_*.cpp), which is the reference they are
// compared against (vucompare --aot):
//   - operands are normalised on every read (denormal -> signed zero, Inf/NaN -> signed FLT_MAX: normalizeOperand);
//   - FMAC results: round toward zero (the interpreter's fesetround, set here through MXCSR), no flush-to-zero, no
//     fused multiply-add; the result rules of normalizeFmacExactResult (ps2_vu1_core.cpp:397). For ADD/SUB/MUL those
//     rules reduce to "denormal -> signed zero". For the multiply-adds a vector test proves the double-precision
//     value lies in the normal range, where the float result is kept; a lane that fails it takes the interpreter's
//     scalar double computation (vu_fmac_slow).
//   - DIV/SQRT/RSQRT/EFU, CLIP, FTOI/ITOF, XGKICK as in the interpreter's lower/upper files.
// x86 SSE4.1 for now (MSVC); the arm64 build will need NEON versions of the vector helpers.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include <immintrin.h>

#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define VU_INLINE __forceinline
#define VU_NOINLINE __declspec(noinline)
#else
#define VU_INLINE inline __attribute__((always_inline))
#define VU_NOINLINE __attribute__((noinline))
#endif

// One PATH1 transfer (XGKICK). The interpreter copies one qword every 2 cycles from the kick on (qword k at kick
// cycle + 1 + 2k, startXgkick/progressXgkick ps2_vu1_core.cpp:860-968) and a store landing before a qword is copied
// changes the packet: the copy here runs lazily, caught up before every store while a transfer is active.
struct Vu1AotXg
{
    uint64_t kickCycle = 0, endCycle = 0;
    uint32_t src = 0, copied = 0, tagEnd = 0, total = 0;
    bool active = false, eop = false;
    uint8_t buf[0x10000];
};

struct Vu1AotCtx
{
    // VU1 state in and out (the interpreter's VU1State fields that a run reads or writes)
    float vf[32][4];
    int32_t vi[16];
    float acc[4];
    float q, p, i;
    uint32_t r, clip, mac, status;
    uint32_t top, itop;
    uint32_t pc;     // in: start pc (byte address), out: pc after the E-bit stop (MSCNT resumes there)
    uint8_t *data;   // VU1 data memory, 16 KB
    void (*path1)(void *user, const uint8_t *packet, uint32_t bytes);
    void *path1User;
    uint64_t cycles; // out: VU1 cycles of the run (the interpreter's count)
    int error;       // out: 0 ok, 1 unknown entry pc, 2 JR to an unknown target (c.pc), 3 bad PATH1 packet
    Vu1AotXg xg;
};

struct Vu1AotProgram
{
    const char *name;
    int pairs;
    const uint8_t *code; // the program's micro memory bytes [0, pairs * 8): the dispatch key
    int (*fn)(Vu1AotCtx &);
};

extern const Vu1AotProgram kVu1AotPrograms[];
extern const int kVu1AotProgramCount;

// ---- scalars ----
VU_INLINE float vu_bits(uint32_t u)
{
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
VU_INLINE uint32_t vu_ubits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
// normalizeOperand: denormal -> signed zero, Inf/NaN -> signed FLT_MAX
VU_INLINE float vu_norm1(float v)
{
    uint32_t b = vu_ubits(v);
    const uint32_t e = (b >> 23) & 0xFFu;
    if (e == 0u)
        b &= 0x80000000u;
    else if (e == 0xFFu)
        b = (b & 0x80000000u) | 0x7F7FFFFFu;
    return vu_bits(b);
}
// normalizeResult (value only)
VU_INLINE float vu_normres(float v) { return vu_norm1(v); }

// ---- vectors ----
#define K_VF0 _mm_setr_ps(0.0f, 0.0f, 0.0f, 1.0f)

VU_INLINE __m128 vu_norm(__m128 v)
{
    const __m128i b = _mm_castps_si128(v);
    const __m128i sign = _mm_and_si128(b, _mm_set1_epi32(int(0x80000000u)));
    __m128i mag = _mm_and_si128(b, _mm_set1_epi32(0x7FFFFFFF));
    mag = _mm_min_epi32(mag, _mm_set1_epi32(0x7F7FFFFF));                          // Inf/NaN -> FLT_MAX
    mag = _mm_and_si128(mag, _mm_cmpgt_epi32(mag, _mm_set1_epi32(0x007FFFFF))); // denormal -> 0
    return _mm_castsi128_ps(_mm_or_si128(mag, sign));
}
// denormal result -> signed zero (exponent 0 keeps only the sign)
VU_INLINE __m128 vu_flush(__m128 f)
{
    const __m128i b = _mm_castps_si128(f);
    const __m128i z = _mm_cmpeq_epi32(_mm_and_si128(b, _mm_set1_epi32(0x7F800000)), _mm_setzero_si128());
    return _mm_castsi128_ps(_mm_andnot_si128(_mm_and_si128(z, _mm_set1_epi32(0x7FFFFFFF)), b));
}
VU_INLINE __m128 vu_bc0(__m128 v) { return _mm_shuffle_ps(v, v, 0x00); }
VU_INLINE __m128 vu_bc1(__m128 v) { return _mm_shuffle_ps(v, v, 0x55); }
VU_INLINE __m128 vu_bc2(__m128 v) { return _mm_shuffle_ps(v, v, 0xAA); }
VU_INLINE __m128 vu_bc3(__m128 v) { return _mm_shuffle_ps(v, v, 0xFF); }
VU_INLINE __m128 vu_yzx(__m128 v) { return _mm_shuffle_ps(v, v, _MM_SHUFFLE(3, 0, 2, 1)); }
VU_INLINE __m128 vu_zxy(__m128 v) { return _mm_shuffle_ps(v, v, _MM_SHUFFLE(3, 1, 0, 2)); }
VU_INLINE __m128 vu_mr32(__m128 v) { return _mm_shuffle_ps(v, v, _MM_SHUFFLE(0, 3, 2, 1)); }
VU_INLINE float vu_lane(__m128 v, int c)
{
    alignas(16) float t[4];
    _mm_store_ps(t, v);
    return t[c];
}
VU_INLINE uint32_t vu_lanebits(__m128 v, int c) { return vu_ubits(vu_lane(v, c)); }
VU_INLINE __m128 vu_w0(__m128 v) { return _mm_blend_ps(v, _mm_setzero_ps(), 8); }

// ADD/SUB/MUL (and the A forms): for a single rounded operation the interpreter's exact-result rules give the float
// result with a denormal flushed to signed zero (overflow already saturates to FLT_MAX under round toward zero).
VU_INLINE __m128 vu_add(__m128 a, __m128 b) { return vu_flush(_mm_add_ps(a, b)); }
VU_INLINE __m128 vu_sub(__m128 a, __m128 b) { return vu_flush(_mm_sub_ps(a, b)); }
VU_INLINE __m128 vu_mul(__m128 a, __m128 b) { return vu_flush(_mm_mul_ps(a, b)); }
VU_INLINE __m128 vu_opmula(__m128 a, __m128 b) { return vu_w0(vu_mul(a, b)); }

// normalizeFmacExactResult (ps2_vu1_core.cpp:397) on one lane: the double value decides zero, overflow and
// underflow; inside the normal range the float result stands.
VU_INLINE float vu_classify(double e, float f)
{
    const bool neg = std::signbit(e);
    const double m = std::fabs(e);
    if (m == 0.0 || m < static_cast<double>(std::numeric_limits<float>::min()))
        return neg ? -0.0f : 0.0f;
    if (m > static_cast<double>(std::numeric_limits<float>::max()))
        return vu_bits((neg ? 0x80000000u : 0u) | 0x7F7FFFFFu);
    return f;
}

// Multiply-add lanes where the interpreter keeps a denormal float result (its double value is normal, the float
// sum cancelled below FLT_MIN): the register then holds a non-normalised value although the generator treats every
// FMAC result as normalised (and lets later reads skip normalizeOperand). Needs operands near 2^-103; counted so a
// run can prove it never happened (vucompare --aot prints it).
inline thread_local uint64_t vu1aot_denormal_kept = 0;

// acc +- a * b, the lanes in mask: the interpreter's code exactly (float result, double check)
VU_NOINLINE inline __m128 vu_fmac_slow(__m128 acc, __m128 a, __m128 b, __m128 f, int mask, bool sub)
{
    alignas(16) float ta[4], tb[4], tc[4], tf[4];
    _mm_store_ps(ta, a);
    _mm_store_ps(tb, b);
    _mm_store_ps(tc, acc);
    _mm_store_ps(tf, f);
    for (int k = 0; k < 4; ++k)
    {
        if (!(mask & (1 << k)))
            continue;
        const double e = sub ? static_cast<double>(tc[k]) - static_cast<double>(ta[k]) * static_cast<double>(tb[k])
                             : static_cast<double>(tc[k]) + static_cast<double>(ta[k]) * static_cast<double>(tb[k]);
        tf[k] = vu_classify(e, tf[k]); // tf: the float result, product and sum each rounded (as the interpreter)
        if ((vu_ubits(tf[k]) & 0x7F800000u) == 0u && (vu_ubits(tf[k]) & 0x007FFFFFu) != 0u)
            ++vu1aot_denormal_kept;
    }
    return _mm_load_ps(tf);
}

// Lanes (bit k = lane k) where the float multiply-add is provably the interpreter's value: with Ef, Ep the biased
// exponents of the float result f and the float product p, 2 <= Ef <= 0xFD, Ep <= 0xFD and Ep <= Ef + 21 bound the
// double value's distance from f (< ulp(p) + ulp(f)) well inside [FLT_MIN, FLT_MAX]. As float compares on
// magnitudes: max(|f|, |p|) < 2^127, and |f| >= max(2^-125, |p| * 2^-21) (exact scaling; a product so small that the
// scaling underflows leaves the 2^-125 bound, which already implies Ep <= Ef + 21).
VU_INLINE int vu_fmac_ok(__m128 f, __m128 p)
{
    const __m128 absm = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
    const __m128 af = _mm_and_ps(f, absm), ap = _mm_and_ps(p, absm);
    const __m128 hi = _mm_cmplt_ps(_mm_max_ps(af, ap), _mm_set1_ps(1.7014118346046923e38f)); // 2^127
    const __m128 lo = _mm_cmple_ps(_mm_max_ps(_mm_mul_ps(ap, _mm_set1_ps(4.76837158203125e-07f)), // 2^-21
                                              _mm_set1_ps(2.350988701644575e-38f)),          // 2^-125
                                   af);
    return _mm_movemask_ps(_mm_and_ps(hi, lo));
}

constexpr int vu_lanes(int dest) { return ((dest >> 3) & 1) | (((dest >> 2) & 1) << 1) | (((dest >> 1) & 1) << 2) | ((dest & 1) << 3); }

template <int Dest>
VU_INLINE __m128 vu_madd(__m128 acc, __m128 a, __m128 b)
{
    const __m128 p = _mm_mul_ps(a, b);
    const __m128 f = _mm_add_ps(acc, p);
    constexpr int m = vu_lanes(Dest);
    const int bad = ~vu_fmac_ok(f, p) & m;
    return bad ? vu_fmac_slow(acc, a, b, f, bad, false) : f;
}
template <int Dest>
VU_INLINE __m128 vu_msub(__m128 acc, __m128 a, __m128 b)
{
    const __m128 p = _mm_mul_ps(a, b);
    const __m128 f = _mm_sub_ps(acc, p);
    constexpr int m = vu_lanes(Dest);
    const int bad = ~vu_fmac_ok(f, p) & m;
    return bad ? vu_fmac_slow(acc, a, b, f, bad, true) : f;
}
// OPMSUB: x,y,z = acc - a * b with a = fs.yzx, b = ft.zxy; w = +0 (the interpreter's exact value 0.0)
template <int Dest>
VU_INLINE __m128 vu_opmsub(__m128 acc, __m128 a, __m128 b)
{
    return vu_w0(vu_msub<(Dest & 0xE)>(acc, a, b));
}

// MAC flags of one FMAC (updateFmacFlags :486): Z/S/U/O per dest lane, from the double value
VU_INLINE uint32_t vu_macbits(double e, int c)
{
    const uint32_t lane = 8u >> c;
    const double m = std::fabs(e);
    uint32_t r = std::signbit(e) ? lane << 4 : 0u;
    if (m == 0.0)
        r |= lane;
    else if (m > static_cast<double>(std::numeric_limits<float>::max()))
        r |= lane << 12;
    else if (m < static_cast<double>(std::numeric_limits<float>::min()))
        r |= lane | (lane << 8);
    return r;
}
template <int Kind> // 0 add, 1 sub, 2 mul, 3 madd, 4 msub, 5 opmsub, 6 opmula
inline uint32_t vu_mac_gen(int dest, __m128 acc, __m128 a, __m128 b)
{
    alignas(16) float ta[4], tb[4], tc[4];
    _mm_store_ps(ta, a);
    _mm_store_ps(tb, b);
    _mm_store_ps(tc, acc);
    uint32_t mac = 0;
    for (int k = 0; k < 4; ++k)
    {
        if (!(dest & (8 >> k)))
            continue;
        const double x = ta[k], y = tb[k], z = tc[k];
        double e = 0.0;
        if ((Kind == 5 || Kind == 6) && k == 3)
            e = 0.0;
        else if (Kind == 0)
            e = x + y;
        else if (Kind == 1)
            e = x - y;
        else if (Kind == 2 || Kind == 6)
            e = x * y;
        else if (Kind == 3)
            e = z + x * y;
        else
            e = z - x * y;
        mac |= vu_macbits(e, k);
    }
    return mac;
}
template <int Dest> inline uint32_t vu_mac_add(__m128 a, __m128 b) { return vu_mac_gen<0>(Dest, a, a, b); }
template <int Dest> inline uint32_t vu_mac_sub(__m128 a, __m128 b) { return vu_mac_gen<1>(Dest, a, a, b); }
template <int Dest> inline uint32_t vu_mac_mul(__m128 a, __m128 b) { return vu_mac_gen<2>(Dest, a, a, b); }
template <int Dest> inline uint32_t vu_mac_opmula(__m128 a, __m128 b) { return vu_mac_gen<6>(Dest, a, a, b); }
template <int Dest> inline uint32_t vu_mac_madd(__m128 acc, __m128 a, __m128 b) { return vu_mac_gen<3>(Dest, acc, a, b); }
template <int Dest> inline uint32_t vu_mac_msub(__m128 acc, __m128 a, __m128 b) { return vu_mac_gen<4>(Dest, acc, a, b); }
template <int Dest> inline uint32_t vu_mac_opmsub(__m128 acc, __m128 a, __m128 b) { return vu_mac_gen<5>(Dest, acc, a, b); }

VU_INLINE __m128 vu_abs(__m128 v) { return _mm_and_ps(v, _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF))); }
// ITOF: int -> float under round toward zero, then the exact power-of-two scale
VU_INLINE __m128 vu_itof0(__m128 v) { return _mm_cvtepi32_ps(_mm_castps_si128(v)); }
VU_INLINE __m128 vu_itof4(__m128 v) { return _mm_mul_ps(vu_itof0(v), _mm_set1_ps(1.0f / 16.0f)); }
VU_INLINE __m128 vu_itof12(__m128 v) { return _mm_mul_ps(vu_itof0(v), _mm_set1_ps(1.0f / 4096.0f)); }
VU_INLINE __m128 vu_itof15(__m128 v) { return _mm_mul_ps(vu_itof0(v), _mm_set1_ps(1.0f / 32768.0f)); }
// FTOI (vuFloatToInt, ps2_vu1_upper.cpp:10): truncate, saturate; cvttps2dq gives INT_MIN for both overflows
VU_INLINE __m128 vu_ftoi_s(__m128 v, float scale)
{
    const __m128 x = _mm_mul_ps(v, _mm_set1_ps(scale));
    const __m128i r = _mm_cvttps_epi32(x);
    const __m128i pos = _mm_castps_si128(_mm_cmpge_ps(x, _mm_set1_ps(2147483648.0f)));
    return _mm_castsi128_ps(_mm_xor_si128(r, pos));
}
VU_INLINE __m128 vu_ftoi0(__m128 v) { return vu_ftoi_s(v, 1.0f); }
VU_INLINE __m128 vu_ftoi4(__m128 v) { return vu_ftoi_s(v, 16.0f); }
VU_INLINE __m128 vu_ftoi12(__m128 v) { return vu_ftoi_s(v, 4096.0f); }
VU_INLINE __m128 vu_ftoi15(__m128 v) { return vu_ftoi_s(v, 32768.0f); }

// CLIP (ps2_vu1_upper.cpp:385): raw bits of fs.xyz against |ft.w| as ordered integers
VU_INLINE uint32_t vu_clip(__m128 fs, __m128 ft)
{
    const uint32_t w = vu_lanebits(ft, 3);
    const int32_t limit = (w & 0x7F800000u) != 0u ? static_cast<int32_t>(w & 0x7FFFFFFFu) : 0x007FFFFF;
    alignas(16) uint32_t b[4];
    _mm_store_si128(reinterpret_cast<__m128i *>(b), _mm_castps_si128(fs));
    uint32_t flags = 0u;
    for (int k = 0; k < 3; ++k)
    {
        if (static_cast<int32_t>(b[k]) > limit)
            flags |= 1u << (2 * k);
        if (static_cast<int32_t>(b[k] ^ 0x80000000u) > limit)
            flags |= 2u << (2 * k);
    }
    return flags;
}

// ---- data memory ----
VU_INLINE __m128 vu_ld(const uint8_t *d, uint32_t qw) { return _mm_loadu_ps(reinterpret_cast<const float *>(d + qw * 16u)); }
template <int Dest>
VU_INLINE void vu_st(uint8_t *d, uint32_t qw, __m128 v)
{
    float *p = reinterpret_cast<float *>(d + qw * 16u);
    if constexpr (Dest == 0xF)
        _mm_storeu_ps(p, v);
    else
        _mm_storeu_ps(p, _mm_blend_ps(_mm_loadu_ps(p), v, vu_lanes(Dest)));
}
VU_INLINE int32_t vu_ldw(const uint8_t *d, uint32_t qw, int comp)
{
    uint32_t v;
    std::memcpy(&v, d + qw * 16u + comp * 4u, 4);
    return static_cast<int32_t>(v);
}
template <int Dest>
VU_INLINE void vu_stw(uint8_t *d, uint32_t qw, int32_t vi)
{
    const uint32_t v = static_cast<uint16_t>(vi & 0xFFFF);
    for (int k = 0; k < 4; ++k)
        if (Dest & (8 >> k))
            std::memcpy(d + qw * 16u + k * 4u, &v, 4);
}

// ---- FDIV (Q) ----
VU_INLINE float vu_div(float n, float d)
{
    const float num = vu_norm1(n), den = vu_norm1(d);
    float r;
    if (den == 0.0f)
        r = std::signbit(num) != std::signbit(den) ? -std::numeric_limits<float>::max() : std::numeric_limits<float>::max();
    else
        r = num / den;
    return vu_normres(vu_normres(r));
}
VU_INLINE float vu_sqrt(float t) { return vu_normres(std::sqrt(std::fabs(vu_norm1(t)))); }
VU_INLINE float vu_rsqrt(float s, float t)
{
    const float num = vu_norm1(s), rad = vu_norm1(t);
    const float den = std::sqrt(std::fabs(rad));
    float r;
    if (den != 0.0f)
        r = num / den;
    else
        r = std::signbit(num) ? -std::numeric_limits<float>::max() : std::numeric_limits<float>::max();
    return vu_normres(vu_normres(r));
}

// ---- EFU (P), ps2_vu1_lower.cpp:670-777; queueP normalises ----
inline float vu_eatan_poly(float value)
{
    constexpr float c[] = {0.999999344348907f, -0.333298563957214f, 0.199465364217758f, -0.13085337519646f,
                           0.096420042216778f, -0.055909886956215f, 0.021861229091883f, -0.004054057877511f};
    const float sq = value * value;
    float p = c[7];
    for (int k = 6; k >= 0; --k)
        p = c[k] + sq * p;
    return 0.785398185253143f + value * p;
}
inline float vu_esin_poly(float value)
{
    constexpr float c[] = {1.0f, -0.166666567325592f, 0.008333025500178f, -0.000198074136279f, 0.000002601886990f};
    const float sq = value * value;
    float p = c[4];
    for (int k = 3; k >= 0; --k)
        p = c[k] + sq * p;
    return value * p;
}
inline float vu_eexp_poly(float value)
{
    constexpr float c[] = {0.249998688697815f, 0.031257584691048f, 0.002591371303424f,
                           0.000171562001924f, 0.000005430199963f, 0.000000690600018f};
    float p = c[5];
    for (int k = 4; k >= 0; --k)
        p = c[k] + value * p;
    p = 1.0f + value * p;
    p *= p;
    p *= p;
    return p != 0.0f ? 1.0f / p : std::numeric_limits<float>::max();
}
VU_INLINE float vu_sumsq(__m128 v)
{
    const float x = vu_norm1(vu_lane(v, 0)), y = vu_norm1(vu_lane(v, 1)), z = vu_norm1(vu_lane(v, 2));
    return x * x + y * y + z * z;
}
VU_INLINE float vu_esadd(__m128 v) { return vu_normres(vu_sumsq(v)); }
VU_INLINE float vu_ersadd(__m128 v)
{
    const float s = vu_sumsq(v);
    return vu_normres(s != 0.0f ? 1.0f / s : s);
}
VU_INLINE float vu_eleng(__m128 v) { return vu_normres(std::sqrt(vu_sumsq(v))); }
VU_INLINE float vu_erleng(__m128 v)
{
    const float l = std::sqrt(vu_sumsq(v));
    return vu_normres(l != 0.0f ? 1.0f / l : l);
}
VU_INLINE float vu_eatanxy(__m128 v)
{
    const float x = vu_norm1(vu_lane(v, 0)), y = vu_norm1(vu_lane(v, 1));
    return vu_normres(x != 0.0f ? vu_eatan_poly(y / x) : 0.0f);
}
VU_INLINE float vu_eatanxz(__m128 v)
{
    const float x = vu_norm1(vu_lane(v, 0)), z = vu_norm1(vu_lane(v, 2));
    return vu_normres(x != 0.0f ? vu_eatan_poly(z / x) : 0.0f);
}
VU_INLINE float vu_esum(__m128 v)
{
    float s = 0.0f;
    for (int k = 0; k < 4; ++k)
        s += vu_norm1(vu_lane(v, k));
    return vu_normres(s);
}
VU_INLINE float vu_esqrt(float t)
{
    const float v = vu_norm1(t);
    return vu_normres(v >= 0.0f ? std::sqrt(v) : v);
}
VU_INLINE float vu_ersqrt(float t)
{
    float r = vu_norm1(t);
    if (r >= 0.0f)
    {
        r = std::sqrt(r);
        if (r != 0.0f)
            r = 1.0f / r;
    }
    return vu_normres(r);
}
VU_INLINE float vu_ercpr(float t)
{
    const float v = vu_norm1(t);
    return vu_normres(v != 0.0f ? 1.0f / v : v);
}
VU_INLINE float vu_esin(float t) { return vu_normres(vu_esin_poly(vu_norm1(t))); }
VU_INLINE float vu_eatan(float t) { return vu_normres(vu_eatan_poly(vu_norm1(t))); }
VU_INLINE float vu_eexp(float t) { return vu_normres(vu_eexp_poly(vu_norm1(t))); }

// ---- XGKICK (PATH1) ----
inline void vu_xg_start(Vu1AotCtx &c, Vu1AotXg &xg, uint32_t qwAddr, uint64_t cycle)
{
    xg.active = true;
    xg.src = (qwAddr * 16u) % 0x4000u;
    xg.copied = xg.tagEnd = xg.total = 0;
    xg.eop = false;
    xg.kickCycle = cycle;
    (void)c;
}
// Copy the qwords the interpreter has copied by cycle `now` (qword k at kickCycle + 1 + 2k); submit at the end.
VU_NOINLINE inline void vu_xg_progress(Vu1AotCtx &c, Vu1AotXg &xg, uint64_t now)
{
    while (xg.active && xg.kickCycle + 1u + 2u * static_cast<uint64_t>(xg.copied / 16u) <= now)
    {
        const uint64_t at = xg.kickCycle + 1u + 2u * static_cast<uint64_t>(xg.copied / 16u);
        if (xg.copied > 0x10000u - 16u)
        {
            c.error = 3;
            xg.active = false;
            return;
        }
        const uint32_t off = xg.copied;
        const uint32_t start = (xg.src + off) % 0x4000u;
        if (start + 16u <= 0x4000u)
            std::memcpy(xg.buf + off, c.data + start, 16);
        else
            for (uint32_t k = 0; k < 16u; ++k)
                xg.buf[off + k] = c.data[(start + k) % 0x4000u];
        xg.copied += 16u;
        if (xg.tagEnd == 0u)
        {
            uint64_t tag;
            std::memcpy(&tag, xg.buf + off, 8);
            const uint32_t nloop = static_cast<uint32_t>(tag & 0x7FFFu);
            const uint32_t fmt = static_cast<uint32_t>((tag >> 58) & 3u);
            uint32_t nreg = static_cast<uint32_t>((tag >> 60) & 0xFu);
            if (nreg == 0u)
                nreg = 16u;
            uint64_t bytes = 16u;
            if (fmt == 0u)
                bytes += static_cast<uint64_t>(nloop) * nreg * 16u;
            else if (fmt == 1u)
                bytes += ((static_cast<uint64_t>(nloop) * nreg + 1u) & ~1ull) * 8u;
            else if (fmt == 2u)
                bytes += static_cast<uint64_t>(nloop) * 16u;
            else
            {
                c.error = 3;
                xg.active = false;
                return;
            }
            if (bytes > 0x10000u - off)
            {
                c.error = 3;
                xg.active = false;
                return;
            }
            xg.tagEnd = off + static_cast<uint32_t>(bytes);
            xg.eop = ((tag >> 15) & 1u) != 0u;
            if (xg.eop)
                xg.total = xg.tagEnd;
        }
        if (xg.copied >= xg.tagEnd)
        {
            if (xg.eop)
            {
                if (c.path1)
                    c.path1(c.path1User, xg.buf, xg.total);
                xg.active = false;
                xg.endCycle = at;
            }
            else
            {
                xg.tagEnd = 0u;
                xg.eop = false;
            }
        }
    }
}
// Finish the transfer (nothing else runs until it ends); the cycle it ended (0 if there was none).
inline uint64_t vu_xg_drain(Vu1AotCtx &c, Vu1AotXg &xg)
{
    if (xg.active)
        vu_xg_progress(c, xg, ~0ull);
    return xg.endCycle;
}

// ---- microVU semantics (emit.py --mode mvu): PCSX2 microVU's results, not the interpreter's ----
// PCSX2's default VU1 configuration (Pcsx2Config.cpp:36-40, 459-466): MXCSR round toward zero + DAZ + FTZ, overflow
// clamping on, extra/sign overflow off. So results are never clamped; operands are clamped (mVUclamp2 -> mVUclamp1,
// microVU_Clamp.inl:25-57) only where the opcode table asks for it (microVU_Upper.inl:582-663, emit.py).
// mVUclamp1: min with FLT_MAX, then max with -FLT_MAX (a NaN becomes +FLT_MAX).
VU_INLINE __m128 vm_clamp(__m128 v)
{
    return _mm_max_ps(_mm_min_ps(v, _mm_castsi128_ps(_mm_set1_epi32(0x7F7FFFFF))),
                      _mm_castsi128_ps(_mm_set1_epi32(int(0xFF7FFFFFu))));
}
VU_INLINE float vm_clamp1(float v) { return _mm_cvtss_f32(vm_clamp(_mm_set_ss(v))); }
// MAX/MINI: integer compare of the bits made two's-complement ordered (MIN_MAX_PS, microVU_Misc.inl:380-428); a tie
// takes the second operand
VU_INLINE __m128i vm_ord(__m128 v)
{
    const __m128i b = _mm_castps_si128(v);
    return _mm_xor_si128(b, _mm_srli_epi32(_mm_srai_epi32(b, 31), 1));
}
VU_INLINE __m128 vm_max(__m128 a, __m128 b)
{
    const __m128 m = _mm_castsi128_ps(_mm_cmpgt_epi32(vm_ord(a), vm_ord(b)));
    return _mm_or_ps(_mm_and_ps(a, m), _mm_andnot_ps(m, b));
}
VU_INLINE __m128 vm_min(__m128 a, __m128 b)
{
    const __m128 m = _mm_castsi128_ps(_mm_cmpgt_epi32(vm_ord(b), vm_ord(a)));
    return _mm_or_ps(_mm_and_ps(a, m), _mm_andnot_ps(m, b));
}
// FTOI (mVU_FTOIx, microVU_Upper.inl:477-505): scale, truncate; float bits > 0x4effffff (as signed ints) turn the
// cvttps2dq overflow value 0x80000000 into 0x7fffffff
VU_INLINE __m128 vm_ftoi_s(__m128 v, float scale, bool doScale)
{
    const __m128 x = doScale ? _mm_mul_ps(v, _mm_set1_ps(scale)) : v;
    const __m128i t = _mm_cmpgt_epi32(_mm_castps_si128(x), _mm_set1_epi32(0x4effffff));
    return _mm_castsi128_ps(_mm_xor_si128(_mm_cvttps_epi32(x), t));
}
VU_INLINE __m128 vm_ftoi0(__m128 v) { return vm_ftoi_s(v, 1.0f, false); }
VU_INLINE __m128 vm_ftoi4(__m128 v) { return vm_ftoi_s(v, 16.0f, true); }
VU_INLINE __m128 vm_ftoi12(__m128 v) { return vm_ftoi_s(v, 4096.0f, true); }
VU_INLINE __m128 vm_ftoi15(__m128 v) { return vm_ftoi_s(v, 32768.0f, true); }
// CLIP (mVU_CLIP, microVU_Upper.inl:533-576): fs lanes with a zero exponent count as +0; limit |ft.w| as raw bits
VU_INLINE uint32_t vm_clip(__m128 fs, __m128 ft)
{
    const __m128i b = _mm_castps_si128(fs);
    const __m128i den = _mm_cmpeq_epi32(_mm_and_si128(b, _mm_set1_epi32(0x7F800000)), _mm_setzero_si128());
    const __m128i t1 = _mm_andnot_si128(den, b);
    const __m128i w = _mm_and_si128(_mm_shuffle_epi32(_mm_castps_si128(ft), 0xFF), _mm_set1_epi32(0x7FFFFFFF));
    const __m128i pos = _mm_cmpgt_epi32(t1, w);
    const __m128i neg = _mm_cmpgt_epi32(_mm_xor_si128(t1, _mm_set1_epi32(int(0x80000000u))), w);
    const int p = _mm_movemask_ps(_mm_castsi128_ps(pos)), n = _mm_movemask_ps(_mm_castsi128_ps(neg));
    return static_cast<uint32_t>((p & 1) | ((n & 1) << 1) | ((p & 2) << 1) | ((n & 2) << 2) | ((p & 4) << 2) | ((n & 4) << 3));
}
// MAC flags as microVU computes them (mVUupdateFlags, microVU_Upper.inl:27-119; overflow hack off): Z = lane == 0,
// S = sign bit, per dest lane, from the result
template <int Dest>
VU_INLINE uint32_t vm_mac(__m128 r)
{
    constexpr int m = vu_lanes(Dest);
    const int s = _mm_movemask_ps(r) & m, z = _mm_movemask_ps(_mm_cmpeq_ps(r, _mm_setzero_ps())) & m;
    auto flip = [](int x) { return static_cast<uint32_t>(((x & 1) << 3) | ((x & 2) << 1) | ((x & 4) >> 1) | ((x & 8) >> 3)); };
    return flip(z) | (flip(s) << 4);
}
// DIV/SQRT/RSQRT (microVU_Lower.inl:34-155): operands raw (DAZ), zero tests by float compare
VU_INLINE float vm_div(float fs, float ft)
{
    if (ft == 0.0f)
        return vu_bits(((vu_ubits(fs) ^ vu_ubits(ft)) & 0x80000000u) | 0x7F7FFFFFu);
    return vm_clamp1(fs / ft);
}
VU_INLINE float vm_sqrt(float ft)
{
    const __m128 a = _mm_min_ss(_mm_set_ss(vu_bits(vu_ubits(ft) & 0x7FFFFFFFu)), _mm_set_ss(vu_bits(0x7F7FFFFFu)));
    return _mm_cvtss_f32(_mm_sqrt_ss(a));
}
VU_INLINE float vm_rsqrt(float fs, float ft)
{
    const float d = _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(vu_bits(vu_ubits(ft) & 0x7FFFFFFFu))));
    if (d == 0.0f)
        return vu_bits((vu_ubits(fs) & 0x80000000u) | 0x7F7FFFFFu);
    return vm_clamp1(fs / d);
}
// EFU (microVU_Lower.inl): only the two the 27 programs use
VU_INLINE float vm_ersqrt(float fs)
{
    return 1.0f / _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(vu_bits(vu_ubits(fs) & 0x7FFFFFFFu))));
}
VU_INLINE float vm_erleng(__m128 fs)
{
    return 1.0f / _mm_cvtss_f32(_mm_sqrt_ss(_mm_dp_ps(fs, fs, 0x71)));
}

// ---- function frame: VU state in locals (the compiler keeps them in registers) ----
#define VU1AOT_VF(n) __m128 v##n = _mm_loadu_ps(c.vf[n]);
#define VU1AOT_VI(n) int32_t i##n = c.vi[n];
#define VU1AOT_NV(k) __m128 n##k = _mm_setzero_ps(); (void)n##k;
// MXCSR: interpreter mode round toward zero only (fesetround); microVU mode PCSX2's VU1FPCR (+ DAZ + FTZ)
#if defined(VU1AOT_MVU)
#define VU1AOT_CSR 0xE040u
#else
#define VU1AOT_CSR 0x6000u
#endif
#define VU1AOT_PROLOGUE                                                                                            \
    const unsigned int vu_csr = _mm_getcsr();                                                                      \
    _mm_setcsr((vu_csr & ~0xE040u) | VU1AOT_CSR);                                                                  \
    VU1AOT_VF(1) VU1AOT_VF(2) VU1AOT_VF(3) VU1AOT_VF(4) VU1AOT_VF(5) VU1AOT_VF(6) VU1AOT_VF(7) VU1AOT_VF(8)         \
    VU1AOT_VF(9) VU1AOT_VF(10) VU1AOT_VF(11) VU1AOT_VF(12) VU1AOT_VF(13) VU1AOT_VF(14) VU1AOT_VF(15) VU1AOT_VF(16)  \
    VU1AOT_VF(17) VU1AOT_VF(18) VU1AOT_VF(19) VU1AOT_VF(20) VU1AOT_VF(21) VU1AOT_VF(22) VU1AOT_VF(23)              \
    VU1AOT_VF(24) VU1AOT_VF(25) VU1AOT_VF(26) VU1AOT_VF(27) VU1AOT_VF(28) VU1AOT_VF(29) VU1AOT_VF(30)              \
    VU1AOT_VF(31) __m128 acc = _mm_loadu_ps(c.acc);                                                                \
    /* shadows: nX = vu_norm(vX) where the generator found the value not known normal (emit.py NormState) */       \
    VU1AOT_NV(1) VU1AOT_NV(2) VU1AOT_NV(3) VU1AOT_NV(4) VU1AOT_NV(5) VU1AOT_NV(6) VU1AOT_NV(7) VU1AOT_NV(8)        \
    VU1AOT_NV(9) VU1AOT_NV(10) VU1AOT_NV(11) VU1AOT_NV(12) VU1AOT_NV(13) VU1AOT_NV(14) VU1AOT_NV(15) VU1AOT_NV(16) \
    VU1AOT_NV(17) VU1AOT_NV(18) VU1AOT_NV(19) VU1AOT_NV(20) VU1AOT_NV(21) VU1AOT_NV(22) VU1AOT_NV(23)              \
    VU1AOT_NV(24) VU1AOT_NV(25) VU1AOT_NV(26) VU1AOT_NV(27) VU1AOT_NV(28) VU1AOT_NV(29) VU1AOT_NV(30)              \
    VU1AOT_NV(31) __m128 nacc = _mm_setzero_ps(); (void)nacc;                                                      \
    VU1AOT_VI(1) VU1AOT_VI(2) VU1AOT_VI(3) VU1AOT_VI(4) VU1AOT_VI(5) VU1AOT_VI(6) VU1AOT_VI(7) VU1AOT_VI(8)         \
    VU1AOT_VI(9) VU1AOT_VI(10) VU1AOT_VI(11) VU1AOT_VI(12) VU1AOT_VI(13) VU1AOT_VI(14) VU1AOT_VI(15)               \
    float qh0 = c.q, qh1 = c.q, ph0 = c.p, ph1 = c.p, ph2 = c.p, I = c.i;                                         \
    uint32_t ch0 = c.clip, ch1 = c.clip, ch2 = c.clip, ch3 = c.clip, ch4 = c.clip;                                 \
    uint32_t mh0 = c.mac, mh1 = c.mac, mh2 = c.mac, mh3 = c.mac, mh4 = c.mac;                                      \
    uint64_t cyc = 0;                                                                                              \
    int32_t bkv = 0;                                                                                               \
    bool bt = false;                                                                                               \
    uint32_t jt = 0;                                                                                               \
    Vu1AotXg &xg = c.xg;                                                                                           \
    uint8_t *const vu_data = c.data; /* a local: a store need not reload it */                                    \
    xg.active = false;                                                                                             \
    xg.endCycle = 0;                                                                                               \
    c.error = 0;                                                                                                   \
    (void)qh1; (void)ph1; (void)ph2; (void)ch1; (void)ch2; (void)ch3; (void)ch4;                                   \
    (void)mh0; (void)mh1; (void)mh2; (void)mh3; (void)mh4; (void)bkv; (void)bt; (void)jt;

// E-bit stop: the flush (the PATH1 transfer finishes; the run ends at the later of it and the pipeline flush)
#define VU1AOT_EPILOGUE                                                                                            \
    {                                                                                                              \
        const uint64_t ke = vu_xg_drain(c, xg);                                                                    \
        if (ke > cyc)                                                                                              \
            cyc = ke;                                                                                              \
    }

#define VU1AOT_SVF(n) _mm_storeu_ps(c.vf[n], v##n);
#define VU1AOT_STORE                                                                                               \
    VU1AOT_SVF(1) VU1AOT_SVF(2) VU1AOT_SVF(3) VU1AOT_SVF(4) VU1AOT_SVF(5) VU1AOT_SVF(6) VU1AOT_SVF(7)             \
    VU1AOT_SVF(8) VU1AOT_SVF(9) VU1AOT_SVF(10) VU1AOT_SVF(11) VU1AOT_SVF(12) VU1AOT_SVF(13) VU1AOT_SVF(14)        \
    VU1AOT_SVF(15) VU1AOT_SVF(16) VU1AOT_SVF(17) VU1AOT_SVF(18) VU1AOT_SVF(19) VU1AOT_SVF(20) VU1AOT_SVF(21)      \
    VU1AOT_SVF(22) VU1AOT_SVF(23) VU1AOT_SVF(24) VU1AOT_SVF(25) VU1AOT_SVF(26) VU1AOT_SVF(27) VU1AOT_SVF(28)      \
    VU1AOT_SVF(29) VU1AOT_SVF(30) VU1AOT_SVF(31) _mm_storeu_ps(c.acc, acc);                                        \
    c.vi[1] = i1; c.vi[2] = i2; c.vi[3] = i3; c.vi[4] = i4; c.vi[5] = i5; c.vi[6] = i6; c.vi[7] = i7;              \
    c.vi[8] = i8; c.vi[9] = i9; c.vi[10] = i10; c.vi[11] = i11; c.vi[12] = i12; c.vi[13] = i13; c.vi[14] = i14;    \
    c.vi[15] = i15;                                                                                                \
    c.q = qh0; c.p = ph0; c.i = I; c.clip = ch0;                                                                   \
    c.cycles = cyc;                                                                                                \
    _mm_setcsr(vu_csr);
