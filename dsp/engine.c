/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES' sound engine (engine.h): the machines, the step sequencer, the master. */
#include "engine.h"
#include "fastmath.h"
#include "plat.h"
#include <string.h>

#define RGB565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

/* ------------------------------------------------------------- the machines --- */
/* The zones' centres are where the curves below have their breakpoints: at a zone every parameter a macro moves is
 * exactly the value chosen for it there; between two zones they move from one to the other. */
#define SOON(n) {n, 0, RGB565(110, 110, 118), {{"", 0, {0}, {0}}}, {{"", {0}}}}
const machine_t MACHINES[NMACHINES] = {
    {"KICK", 1, RGB565(255, 92, 40),
     {{"TUNE", 4, {20, 50, 75, 105}, {"SUB", "DEEP", "808", "TIGHT"}},
      {"PUNCH", 4, {12, 50, 80, 115}, {"SOFT", "SYNTAKT", "TECHNO", "ZAP"}},
      {"BOOM", 4, {20, 50, 85, 115}, {"TIGHT", "SYNTAKT", "808", "DRONE"}},
      {"GRIT", 5, {0, 35, 70, 100, 127}, {"CLEAN", "WARM", "RUMBLE", "FOLD", "DESTROY"}}},
     {{"SYNTAKT DEEP", {50, 50, 50, 35}}, {"808 CLASSIC", {75, 12, 85, 0}}, {"WAREHOUSE", {105, 80, 20, 70}},
      {"PSY ZAP", {60, 115, 20, 35}}}},
    {"SNARE", 1, RGB565(70, 200, 255),
     {{"TUNE", 4, {20, 50, 80, 110}, {"FAT", "SYNTAKT", "TIGHT", "PICCOLO"}},
      {"SNAP", 4, {15, 50, 85, 115}, {"BRUSH", "SYNTAKT", "CRACK", "RIMSHOT"}},
      {"BODY", 4, {15, 50, 85, 118}, {"NOISY", "SYNTAKT", "FAT", "TONE"}},
      {"RATTLE", 5, {10, 45, 75, 105, 127}, {"DRY", "SYNTAKT", "TRAP", "GATED", "DIRT"}}},
     {{"SYNTAKT SD", {50, 50, 50, 45}}, {"TRAP SNARE", {80, 85, 15, 75}}, {"GATED 80S", {35, 50, 85, 105}},
      {"RIMSHOT", {110, 115, 85, 10}}}},
    SOON("TOMS"),
    SOON("METAL"),
    {"HATS", 1, RGB565(255, 214, 60),
     {{"TUNE", 4, {20, 50, 85, 115}, {"LOW", "808", "HIGH", "TINY"}},
      {"DECAY", 5, {10, 35, 70, 105, 127}, {"TICK", "CLOSED", "HALF", "OPEN", "WASH"}},
      {"METAL", 4, {15, 50, 85, 118}, {"NOISE", "SYNTAKT", "808", "RING"}},
      {"CRISP", 4, {15, 50, 85, 118}, {"SOFT", "CRISP", "SIZZLE", "HARSH"}}},
     {{"SYNTAKT HH", {50, 35, 50, 50}}, {"808 HAT", {50, 35, 85, 50}}, {"OPEN WASH", {50, 105, 50, 85}},
      {"TICK", {85, 10, 15, 85}}}},
    SOON("CLAP"),
};

/* a curve: breakpoints (x 0..127, y), x rising, straight lines between them */
typedef struct {
    uint8_t x;
    float y;
} bp_t;
#define NBP(t) (sizeof(t) / sizeof((t)[0]))
#define CURVE(t, x) curve((t), NBP(t), (x))

static float curve(const bp_t *t, uint32_t n, float x)
{
    uint32_t i;
    if (x <= (float)t[0].x)
        return t[0].y;
    for (i = 1; i < n; i++)
        if (x <= (float)t[i].x) {
            float a = (x - (float)t[i - 1].x) / (float)(t[i].x - t[i - 1].x);
            return t[i - 1].y + a * (t[i].y - t[i - 1].y);
        }
    return t[n - 1].y;
}

static float coef(float tau_s) { return tau_s <= 0.0f ? 0.0f : fm_expf(-1.0f / (tau_s * ENG_SR)); }
static float onepole(float hz) { return 1.0f - fm_expf(-FM_TWO_PI * fm_minf(hz, 18000.0f) / ENG_SR); }

static uint32_t noise = 0x2545F491u;
static inline float white(void)
{
    noise ^= noise << 13;
    noise ^= noise >> 17;
    noise ^= noise << 5;
    return (float)(int32_t)noise * (1.0f / 2147483648.0f);
}

/* a soft clip that starts clean: drive 1 is untouched, the first 0.5 of drive blends tanh in */
static inline float drive_clip(float x, float drive)
{
    float y = x * drive, sat = fm_clampf((drive - 1.0f) * 2.0f, 0.0f, 1.0f);
    return sat > 0.0f ? y + sat * (fm_tanhf(y) - y) : y;
}

/* ================================================================== KICK === */
/* TUNE: the tone, in semitones from A1 (55 Hz): SUB D1, DEEP G1, 808 A1, TIGHT C2; 30 .. 80 Hz at the ends */
static const bp_t K_TUNE_ST[] = {{0, -10.48f}, {20, -7.0f}, {50, -2.0f}, {75, 0.0f}, {105, 3.0f}, {127, 6.49f}};
/* PUNCH: not a straight line: soft, then harder and shorter, then from about 90 the sweep long again (the zap) */
static const bp_t K_PUNCH_OCT[] = {{0, 0.4f}, {12, 0.6f}, {50, 2.5f}, {80, 3.5f}, {115, 4.0f}, {127, 4.2f}};
static const bp_t K_PUNCH_S[] = {{0, 0.030f}, {12, 0.028f}, {50, 0.018f}, {80, 0.008f}, {95, 0.012f}, {115, 0.045f},
                                 {127, 0.070f}};
static const bp_t K_PUNCH_CLICK[] = {{0, 0.0f}, {12, 0.03f}, {50, 0.30f}, {80, 0.70f}, {115, 0.40f}, {127, 0.35f}};
/* BOOM: the body's length (time constants: heard about three times as long: TIGHT ~150 ms, SYNTAKT ~350, 808 ~900,
 * DRONE ~2.5 s) and how much of it is the long deep tail; the 808's glide down from about 70 */
static const bp_t K_BOOM_S[] = {{0, 0.030f}, {20, 0.050f}, {50, 0.110f}, {85, 0.300f}, {115, 0.80f}, {127, 1.10f}};
static const bp_t K_BOOM_SUB[] = {{0, 0.05f}, {20, 0.10f}, {50, 0.30f}, {85, 0.60f}, {115, 0.80f}, {127, 0.80f}};
static const bp_t K_BOOM_DROOP[] = {{0, 0.0f}, {70, 0.0f}, {85, -0.10f}, {115, -0.15f}, {127, -0.167f}};
/* GRIT: drive, the folder, the tone after them, the crusher, and the level that keeps it as loud as clean */
static const bp_t K_GRIT_DRIVE[] = {{0, 1.0f}, {35, 2.5f}, {70, 8.0f}, {100, 10.0f}, {127, 14.0f}};
static const bp_t K_GRIT_FOLD[] = {{0, 0.0f}, {85, 0.0f}, {100, 0.55f}, {127, 1.0f}};
static const bp_t K_GRIT_TONE[] = {{0, 16000.0f}, {35, 11000.0f}, {70, 5500.0f}, {100, 8000.0f}, {127, 11000.0f}};
static const bp_t K_GRIT_CRUSH[] = {{0, 0.0f}, {110, 0.0f}, {127, 1.0f}};
static const bp_t K_GRIT_MAKEUP[] = {{0, 1.00f}, {10, 0.86f}, {20, 0.765f}, {35, 0.674f}, {50, 0.51f}, {70, 0.456f},
                                     {85, 0.456f}, {100, 0.736f}, {110, 0.716f}, {127, 0.60f}};   /* measured: ~1 dB */

typedef struct {
    float hz, sweep_oct, droop_oct, k_sweep, k_body, k_tail, sub, k_click, click;
    float drive, fold, tone_a, crush_hold, crush_q, makeup;
} kick_coef_t;

typedef struct {
    uint8_t on;
    float phase, e_sweep, e_body, e_tail, e_click, vel, fade, k_fade;
    float lp, hp_prev, crush_s, crush_n;
} kick_voice_t;

static void kick_coefs(const uint8_t m[4], kick_coef_t *c)
{
    float tune = (float)m[0], punch = (float)m[1], boom = (float)m[2], grit = (float)m[3];
    float tn = tune / 127.0f, bn = boom / 127.0f, gn = grit / 127.0f, decay, crush;
    c->hz = 55.0f * fm_exp2f(CURVE(K_TUNE_ST, tune) / 12.0f);
    c->sweep_oct = CURVE(K_PUNCH_OCT, punch);
    c->k_sweep = coef(CURVE(K_PUNCH_S, punch) * (1.0f - 0.25f * tn));   /* a high kick sweeps a little shorter */
    c->click = CURVE(K_PUNCH_CLICK, punch);
    c->k_click = coef(0.0012f);
    decay = CURVE(K_BOOM_S, boom);
    c->k_body = coef(decay);
    c->k_tail = coef(decay * 1.6f);
    c->sub = fm_clampf(CURVE(K_BOOM_SUB, boom) + 0.20f * (1.0f - tn) - 0.10f, 0.0f, 0.9f);   /* low: more tail */
    c->droop_oct = CURVE(K_BOOM_DROOP, boom);
    c->drive = CURVE(K_GRIT_DRIVE, grit);
    c->fold = CURVE(K_GRIT_FOLD, grit);
    c->tone_a = onepole(CURVE(K_GRIT_TONE, grit));
    crush = CURVE(K_GRIT_CRUSH, grit);
    c->crush_hold = 1.0f + 7.0f * crush;       /* hold each sample this many (a lower rate) */
    c->crush_q = 4096.0f - 4080.0f * crush;    /* the levels: 12 bits .. 4 */
    c->makeup = CURVE(K_GRIT_MAKEUP, grit) * (1.0f - 0.25f * bn * gn);   /* long and driven: a little less */
}

static void kick_start(kick_voice_t *v, float vel)
{
    v->on = 1;
    v->phase = 0.0f;                           /* from zero every time: the same punch on every hit */
    v->e_sweep = v->e_body = v->e_tail = v->e_click = 1.0f;
    v->vel = vel;
    v->fade = 1.0f;
    v->k_fade = 1.0f;
    v->lp = v->hp_prev = v->crush_s = v->crush_n = 0.0f;
}

static float kick_sample(kick_voice_t *v, const kick_coef_t *c)
{
    float f, s, amp, x, n, y;
    f = c->hz * fm_exp2f(c->sweep_oct * v->e_sweep + c->droop_oct * (1.0f - v->e_body));
    v->phase += f * (1.0f / ENG_SR);
    if (v->phase >= 1.0f)
        v->phase -= 1.0f;
    s = fm_sin_turns(v->phase);
    amp = (1.0f - c->sub) * v->e_body + c->sub * v->e_tail;
    x = s * amp;
    if (c->click > 0.0f && v->e_click > 0.001f) {   /* the click: a high-passed noise burst of about a millisecond */
        n = white();
        x += (n - v->hp_prev) * 0.5f * c->click * v->e_click;
        v->hp_prev = n;
    }
    if (c->fold > 0.0f) {
        float t = fm_tanhf(x * c->drive), fo = fm_sinf(x * c->drive * (1.0f + 2.5f * c->fold) * 1.5707963f);
        y = t + c->fold * (fo - t);
    } else {
        y = drive_clip(x, c->drive);
    }
    v->lp += c->tone_a * (y - v->lp);
    y = v->lp;
    if (c->crush_hold > 1.01f) {               /* DESTROY: fewer samples, fewer levels */
        v->crush_n += 1.0f;
        if (v->crush_n >= c->crush_hold) {
            v->crush_n -= c->crush_hold;
            v->crush_s = fm_floorf(y * c->crush_q + 0.5f) / c->crush_q;
        }
        y = v->crush_s;
    }
    y *= v->vel * v->fade;                    /* (the makeup: the render's smoothed gain) */
    v->e_sweep *= c->k_sweep;
    v->e_body *= c->k_body;
    v->e_tail *= c->k_tail;
    v->e_click *= c->k_click;
    v->fade *= v->k_fade;
    if ((v->e_tail < 0.0002f && v->e_body < 0.0002f) || v->fade < 0.0005f)
        v->on = 0;
    return y;
}

/* ================================================================= SNARE === */
/* Syntakt-style: a tuned body (two sines, the second at the drum's 1.59 mode, a short pitch drop) and a noise band
 * with its own decay (a hold before it: GATED), a snap of a click, drive at the end (DIRT). */
static const bp_t S_TUNE_HZ[] = {{0, 120.0f}, {20, 150.0f}, {50, 190.0f}, {80, 240.0f}, {110, 320.0f}, {127, 380.0f}};
static const bp_t S_SNAP_OCT[] = {{0, 0.0f}, {15, 0.10f}, {50, 0.60f}, {85, 1.00f}, {115, 1.60f}, {127, 1.80f}};
static const bp_t S_SNAP_S[] = {{0, 0.020f}, {50, 0.012f}, {85, 0.008f}, {115, 0.004f}, {127, 0.004f}};
static const bp_t S_SNAP_CLICK[] = {{0, 0.0f}, {15, 0.0f}, {50, 0.35f}, {85, 0.80f}, {115, 1.00f}, {127, 1.00f}};
static const bp_t S_SNAP_NOISE[] = {{0, 1.0f}, {85, 1.0f}, {115, 0.35f}, {127, 0.30f}};   /* a rimshot: little noise */
static const bp_t S_SNAP_BRUSH[] = {{0, 0.55f}, {15, 0.6f}, {50, 1.0f}, {127, 1.0f}};    /* brush: a darker noise */
static const bp_t S_BODY_LVL[] = {{0, 0.10f}, {15, 0.15f}, {50, 0.50f}, {85, 0.80f}, {118, 1.00f}, {127, 1.00f}};
static const bp_t S_BODY_S[] = {{0, 0.030f}, {15, 0.040f}, {50, 0.070f}, {85, 0.120f}, {118, 0.200f}, {127, 0.250f}};
static const bp_t S_BODY_NOISE[] = {{0, 1.0f}, {15, 1.0f}, {50, 0.75f}, {85, 0.55f}, {118, 0.20f}, {127, 0.15f}};
static const bp_t S_RAT_S[] = {{0, 0.030f}, {10, 0.040f}, {45, 0.090f}, {75, 0.200f}, {105, 0.020f}, {127, 0.120f}};
static const bp_t S_RAT_HOLD[] = {{0, 0.0f}, {75, 0.0f}, {105, 0.160f}, {127, 0.0f}};
static const bp_t S_RAT_HP[] = {{0, 1500.0f}, {10, 1200.0f}, {45, 900.0f}, {75, 1800.0f}, {105, 700.0f}, {127, 600.0f}};
static const bp_t S_RAT_LP[] = {{0, 6000.0f}, {10, 7000.0f}, {45, 9000.0f}, {75, 14000.0f}, {105, 8000.0f},
                                {127, 12000.0f}};
static const bp_t S_RAT_DRIVE[] = {{0, 1.0f}, {105, 1.0f}, {127, 6.0f}};
/* the levels that keep a turn of the knob a change of character more than of loudness (measured: the first 120 ms) */
static const bp_t S_MAKEUP[] = {{0, 1.30f}, {10, 1.19f}, {45, 0.90f}, {75, 0.76f}, {105, 0.56f}, {127, 0.43f}};
static const bp_t S_SNAP_GAIN[] = {{0, 1.2f}, {36, 1.04f}, {54, 1.0f}, {90, 1.1f}, {108, 1.33f}, {117, 1.5f},
                                   {127, 1.5f}};

typedef struct {
    float hz, sweep_oct, k_sweep, body, k_body, noise, k_noise, hold, hp_a, lp_a, click, k_click, drive, makeup;
} snare_coef_t;

typedef struct {
    uint8_t on;
    float ph1, ph2, e_sweep, e_body, e_noise, hold, e_click, vel, fade, k_fade;
    float hp_lp, lp, cl_prev;
} snare_voice_t;

static void snare_coefs(const uint8_t m[4], snare_coef_t *c)
{
    float tune = (float)m[0], snap = (float)m[1], body = (float)m[2], rat = (float)m[3];
    float tn = tune / 127.0f;
    c->hz = CURVE(S_TUNE_HZ, tune);
    c->sweep_oct = CURVE(S_SNAP_OCT, snap);
    c->k_sweep = coef(CURVE(S_SNAP_S, snap));
    c->click = CURVE(S_SNAP_CLICK, snap);
    c->k_click = coef(0.0015f);
    c->body = CURVE(S_BODY_LVL, body);
    c->k_body = coef(CURVE(S_BODY_S, body) * (1.15f - 0.3f * tn));   /* a high snare rings a little shorter */
    c->noise = CURVE(S_BODY_NOISE, body) * CURVE(S_SNAP_NOISE, snap);
    c->k_noise = coef(CURVE(S_RAT_S, rat));
    c->hold = CURVE(S_RAT_HOLD, rat) * ENG_SR;
    c->hp_a = onepole(CURVE(S_RAT_HP, rat) * (0.8f + 0.5f * tn));
    c->lp_a = onepole(CURVE(S_RAT_LP, rat) * CURVE(S_SNAP_BRUSH, snap));
    c->drive = CURVE(S_RAT_DRIVE, rat);
    c->makeup = 0.75f * CURVE(S_MAKEUP, rat) * CURVE(S_SNAP_GAIN, snap);
}

static void snare_start(snare_voice_t *v, float vel)
{
    v->on = 1;
    v->ph1 = v->ph2 = 0.0f;
    v->e_sweep = v->e_body = v->e_noise = v->e_click = 1.0f;
    v->hold = 0.0f;
    v->vel = vel;
    v->fade = 1.0f;
    v->k_fade = 1.0f;
    v->hp_lp = v->lp = v->cl_prev = 0.0f;
}

static float snare_sample(snare_voice_t *v, const snare_coef_t *c)
{
    float f = c->hz * fm_exp2f(c->sweep_oct * v->e_sweep), n = white(), x, nb;
    v->ph1 += f * (1.0f / ENG_SR);
    if (v->ph1 >= 1.0f)
        v->ph1 -= 1.0f;
    v->ph2 += f * 1.59f * (1.0f / ENG_SR);
    if (v->ph2 >= 1.0f)
        v->ph2 -= 1.0f;
    x = (fm_sin_turns(v->ph1) + 0.45f * fm_sin_turns(v->ph2)) * 0.7f * c->body * v->e_body;
    v->hp_lp += c->hp_a * (n - v->hp_lp);       /* the noise: high-passed, then low-passed (its colour) */
    nb = n - v->hp_lp;
    v->lp += c->lp_a * (nb - v->lp);
    x += v->lp * 1.6f * c->noise * v->e_noise;
    if (c->click > 0.0f && v->e_click > 0.001f) {
        x += (n - v->cl_prev) * 0.35f * c->click * v->e_click;
        v->cl_prev = n;
    }
    x = drive_clip(x, c->drive);
    x *= v->vel * v->fade;
    v->e_sweep *= c->k_sweep;
    v->e_body *= c->k_body;
    if (v->hold < c->hold)
        v->hold += 1.0f;
    else
        v->e_noise *= c->k_noise;
    v->e_click *= c->k_click;
    v->fade *= v->k_fade;
    if ((v->e_body < 0.0003f && v->e_noise < 0.0003f) || v->fade < 0.0005f)
        v->on = 0;
    return x;
}

/* ================================================================== HATS === */
/* Six square oscillators at the 808's spacings (metal), white noise, blended; a resonant high-pass (a TPT state
 * variable filter), a low-pass, a decay from a tick to a wash, drive at the harsh end. RING: the squares in pairs
 * multiplied (ring-modulated): clangier. */
static const float HAT_HZ[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
static const bp_t H_TUNE[] = {{0, 0.60f}, {20, 0.80f}, {50, 1.00f}, {85, 1.35f}, {115, 1.80f}, {127, 2.00f}};
static const bp_t H_DECAY_S[] = {{0, 0.006f}, {10, 0.010f}, {35, 0.025f}, {70, 0.080f}, {105, 0.250f}, {127, 0.600f}};
static const bp_t H_METAL[] = {{0, 0.0f}, {15, 0.05f}, {50, 0.45f}, {85, 1.0f}, {118, 0.9f}, {127, 0.9f}};
static const bp_t H_RING[] = {{0, 0.0f}, {85, 0.0f}, {118, 1.0f}, {127, 1.0f}};
static const bp_t H_HP[] = {{0, 3000.0f}, {15, 4500.0f}, {50, 7000.0f}, {85, 9000.0f}, {118, 8000.0f}, {127, 8500.0f}};
static const bp_t H_RES[] = {{0, 0.7f}, {50, 0.9f}, {85, 1.4f}, {118, 1.0f}, {127, 1.0f}};
static const bp_t H_LP[] = {{0, 9000.0f}, {15, 11000.0f}, {50, 15000.0f}, {85, 18000.0f}, {127, 18000.0f}};
static const bp_t H_DRIVE[] = {{0, 1.0f}, {85, 1.0f}, {118, 4.0f}, {127, 6.0f}};
/* the levels that keep METAL and CRISP changes of character (measured: the first 60 ms) */
static const bp_t H_MAKEUP[] = {{0, 1.0f}, {81, 1.0f}, {90, 0.62f}, {99, 0.46f}, {108, 0.38f}, {117, 0.335f},
                                {127, 0.285f}};
static const bp_t H_METAL_GAIN[] = {{0, 0.575f}, {18, 0.63f}, {36, 0.77f}, {45, 0.92f}, {54, 1.11f}, {63, 1.4f},
                                    {72, 1.7f}, {81, 1.8f}, {90, 2.04f}, {99, 2.5f}, {108, 2.43f}, {117, 2.0f},
                                    {127, 1.95f}};

typedef struct {
    float inc[6], metal, ring, k_amp, a1, a2, a3, k, lp_a, drive, makeup;
} hat_coef_t;

typedef struct {
    uint8_t on;
    float ph[6], e_amp, vel, fade, k_fade, ic1, ic2, lp;
} hat_voice_t;

static void hat_coefs(const uint8_t m[4], hat_coef_t *c)
{
    float tune = (float)m[0], dec = (float)m[1], met = (float)m[2], cr = (float)m[3];
    float sc = CURVE(H_TUNE, tune), fc, g, q;
    uint32_t i;
    for (i = 0; i < 6u; i++)
        c->inc[i] = HAT_HZ[i] * sc * (1.0f / ENG_SR);
    c->metal = CURVE(H_METAL, met);
    c->ring = CURVE(H_RING, met);
    c->k_amp = coef(CURVE(H_DECAY_S, dec));
    fc = fm_clampf(CURVE(H_HP, cr) * fm_sqrtf(sc), 1000.0f, 15000.0f);   /* the filter follows the tune, gently */
    q = CURVE(H_RES, cr);
    g = fm_tanf(FM_PI * fc / ENG_SR);
    c->k = 1.0f / q;
    c->a1 = 1.0f / (1.0f + g * (g + c->k));
    c->a2 = g * c->a1;
    c->a3 = g * c->a2;
    c->lp_a = onepole(CURVE(H_LP, cr));
    c->drive = CURVE(H_DRIVE, cr);
    c->makeup = CURVE(H_MAKEUP, cr) * CURVE(H_METAL_GAIN, met);
}

static void hat_start(hat_voice_t *v, float vel)
{
    v->on = 1;
    v->e_amp = 1.0f;                           /* the oscillators run on: their phases are the hat's grain */
    v->vel = vel;
    v->fade = 1.0f;
    v->k_fade = 1.0f;
}

static float hat_sample(hat_voice_t *v, const hat_coef_t *c)
{
    float sq[6], sum = 0.0f, ring, metal, x, v1, v2, v3, hp;
    uint32_t i;
    for (i = 0; i < 6u; i++) {
        v->ph[i] += c->inc[i];
        if (v->ph[i] >= 1.0f)
            v->ph[i] -= 1.0f;
        sq[i] = v->ph[i] < 0.5f ? 1.0f : -1.0f;
        sum += sq[i];
    }
    ring = (sq[0] * sq[3] + sq[1] * sq[4] + sq[2] * sq[5]) * (1.0f / 3.0f);
    metal = sum * (1.0f / 3.0f);
    metal += c->ring * (ring - metal);
    x = (1.0f - c->metal) * white() + c->metal * metal;
    v3 = x - v->ic2;                           /* the high-pass: a TPT state variable filter */
    v1 = c->a1 * v->ic1 + c->a2 * v3;
    v2 = v->ic2 + c->a2 * v->ic1 + c->a3 * v3;
    v->ic1 = 2.0f * v1 - v->ic1;
    v->ic2 = 2.0f * v2 - v->ic2;
    hp = x - c->k * v1 - v2;
    v->lp += c->lp_a * (hp - v->lp);
    x = drive_clip(v->lp * 0.8f, c->drive);
    x *= v->vel * v->fade * v->e_amp;
    v->e_amp *= c->k_amp;
    v->fade *= v->k_fade;
    if (v->e_amp < 0.0003f || v->fade < 0.0005f)
        v->on = 0;
    return x;
}

/* ======================================================= machines in common === */
/* each machine: its coefficients (kept by the main loop under the lock, copied by the render at a block's start) and
 * two voices: a new hit takes the other one and the old one fades out in a few ms (no click; a hat chokes itself) */
static kick_coef_t kick_next;
static snare_coef_t snare_next;
static hat_coef_t hat_next;
static volatile uint8_t coef_new;        /* bit per machine */
static kick_voice_t kv[2];
static snare_voice_t sv[2];
static hat_voice_t hv[2];
static uint8_t voice_next[NMACHINES];
static const float LEVEL_FULL[NMACHINES] = {0.85f, 0.70f, 0.70f, 0.70f, 0.55f, 0.70f};   /* the mix at level 10 */
static float level[NMACHINES] = {0.85f, 0.70f, 0.70f, 0.70f, 0.55f, 0.70f};
static volatile uint8_t muted;           /* bit per machine */
static float gain_m[NMACHINES];          /* each machine's makeup x level, smoothed per sample (the render's) */

void eng_macros(int machine, const uint8_t m[4])
{
    kick_coef_t k;
    snare_coef_t s;
    hat_coef_t h;
    uint32_t f;
    if (machine == M_KICK) {
        kick_coefs(m, &k);
        f = plat_lock();
        kick_next = k;
    } else if (machine == M_SNARE) {
        snare_coefs(m, &s);
        f = plat_lock();
        snare_next = s;
    } else if (machine == M_HATS) {
        hat_coefs(m, &h);
        f = plat_lock();
        hat_next = h;
    } else {
        return;
    }
    coef_new |= (uint8_t)(1u << machine);
    plat_unlock(f);
}

void eng_level(int machine, float l)
{
    if (machine >= 0 && machine < NMACHINES)
        level[machine] = fm_clampf(l, 0.0f, 1.0f) * LEVEL_FULL[machine];
}

void eng_mute(int machine, int on)
{
    uint32_t f = plat_lock();
    if (on)
        muted |= (uint8_t)(1u << machine);
    else
        muted &= (uint8_t)~(1u << machine);
    plat_unlock(f);
}

static kick_coef_t kick_c;
static snare_coef_t snare_c;
static hat_coef_t hat_c;

/* a machine struck: its other voice starts, the one that sounded fades. Struck from silence, its gain is its own at
 * once (no glide from the last sound's into the attack) */
static void strike(int machine, float vel)
{
    uint32_t a = voice_next[machine], b = a ^ 1u;
    voice_next[machine] = (uint8_t)b;
    if (machine == M_KICK && !kv[0].on && !kv[1].on)
        gain_m[M_KICK] = kick_c.makeup * level[M_KICK];
    else if (machine == M_SNARE && !sv[0].on && !sv[1].on)
        gain_m[M_SNARE] = snare_c.makeup * level[M_SNARE];
    else if (machine == M_HATS && !hv[0].on && !hv[1].on)
        gain_m[M_HATS] = hat_c.makeup * level[M_HATS];
    if (machine == M_KICK) {
        if (kv[a].on)
            kv[a].k_fade = coef(0.003f);
        kick_start(&kv[b], vel);
    } else if (machine == M_SNARE) {
        if (sv[a].on)
            sv[a].k_fade = coef(0.003f);
        snare_start(&sv[b], vel);
    } else if (machine == M_HATS) {
        if (hv[a].on)
            hv[a].k_fade = coef(0.002f);
        hat_start(&hv[b], vel);
    }
}

/* ------------------------------------------------------------ the sequencer --- */
static volatile uint16_t pat_steps[NMACHINES], pat_acc[NMACHINES];
static volatile float seq_bpm = 124.0f, seq_swing;
static volatile int8_t play_req = -1;    /* -1 nothing asked, 0 stop, 1 start */
static volatile uint8_t hit_req;         /* bit per machine */
static volatile float hit_vel[NMACHINES];
static volatile int32_t step_now = -1;
static volatile uint32_t hits;
static volatile float env_now[NMACHINES];
static float step_left;                  /* samples to the next step */
static int32_t step_pos;                 /* the next step to play */
static float gain_now;

void eng_pattern(int machine, uint16_t steps, uint16_t accents)
{
    if (machine < 0 || machine >= NMACHINES)
        return;
    pat_steps[machine] = steps;
    pat_acc[machine] = accents;
}
void eng_tempo(float bpm, float swing)
{
    seq_bpm = fm_clampf(bpm, 40.0f, 240.0f);
    seq_swing = fm_clampf(swing, 0.0f, 1.0f);
}
void eng_play(int on) { play_req = (int8_t)(on ? 1 : 0); }
void eng_hit(int machine, float vel)
{
    uint32_t f;
    if (machine < 0 || machine >= NMACHINES || !MACHINES[machine].ready)
        return;
    f = plat_lock();
    hit_vel[machine] = vel;
    hit_req |= (uint8_t)(1u << machine);
    plat_unlock(f);
}
int eng_step(void) { return step_now; }
float eng_env(int machine) { return machine >= 0 && machine < NMACHINES ? env_now[machine] : 0.0f; }
uint32_t eng_hits(void) { return hits; }

void eng_panic(void)
{
    kv[0].on = kv[1].on = 0;
    sv[0].on = sv[1].on = 0;
    hv[0].on = hv[1].on = 0;
    step_now = -1;
    play_req = -1;
}

void eng_init(void)
{
    uint32_t i;
    for (i = 0; i < NMACHINES; i++)
        if (MACHINES[i].ready)
            eng_macros((int)i, MACHINES[i].preset[0].m);
    kick_c = kick_next;
    snare_c = snare_next;
    hat_c = hat_next;
    coef_new = 0;
    gain_m[M_KICK] = kick_c.makeup * level[M_KICK];
    gain_m[M_SNARE] = snare_c.makeup * level[M_SNARE];
    gain_m[M_HATS] = hat_c.makeup * level[M_HATS];
}

/* a step's length in samples: a sixteenth, the pairs swung (the first longer, the second shorter, up to 2 : 1) */
static float step_len(int32_t s)
{
    float base = ENG_SR * 15.0f / seq_bpm, sw = seq_swing * 0.33f;
    return (s & 1) ? base * (1.0f - sw) : base * (1.0f + sw);
}

/* New parameters while a machine sounds (a knob turned, a preset loaded) must not jump: a level, a drive or a filter
 * that changes from one sample to the next is a click, and so is one that changes in steps from block to block (a
 * zipper). While a voice rings, every parameter glides to its new value sample by sample (about 5 ms); the gains
 * (makeup x level) glide in the render the same way. A machine that is silent takes them at once: its next hit is
 * exactly the new sound. All the coefficients are floats. */
static void glide(float *c, const float *t, uint32_t n, float a)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        float d = t[i] - c[i];
        c[i] = fm_fabsf(d) > 1e-5f * fm_fabsf(t[i]) + 1e-12f ? c[i] + a * d : t[i];
    }
}
#define GLIDE(c, t, a) glide((float *)&(c), (const float *)&(t), sizeof(c) / sizeof(float), (a))
#define SAME(c, t) (!memcmp(&(c), &(t), sizeof(c)))
static uint8_t gliding;                  /* bit per machine: its parameters on their way */

void eng_render(int32_t *lr, uint32_t n, float gain)
{
    uint32_t i, mc;
    float pk = 0.0f, ps = 0.0f, ph = 0.0f;
    if (coef_new) {                    /* new parameters: silent machines take them, sounding ones glide */
        if (!kv[0].on && !kv[1].on)
            kick_c = kick_next;
        else if (!SAME(kick_c, kick_next))
            gliding |= 1u << M_KICK;
        if (!sv[0].on && !sv[1].on)
            snare_c = snare_next;
        else if (!SAME(snare_c, snare_next))
            gliding |= 1u << M_SNARE;
        if (!hv[0].on && !hv[1].on)
            hat_c = hat_next;
        else if (!SAME(hat_c, hat_next))
            gliding |= 1u << M_HATS;
        coef_new = 0;
    }
    if (play_req >= 0) {
        if (play_req) {
            step_pos = 0;
            step_left = 0.0f;
            step_now = 0;
        } else {
            step_now = -1;
        }
        play_req = -1;
    }
    if (hit_req) {
        for (mc = 0; mc < NMACHINES; mc++)
            if (hit_req >> mc & 1u) {
                strike((int)mc, hit_vel[mc]);
                hits++;
            }
        hit_req = 0;
    }
    for (i = 0; i < n; i++) {
        float k = 0.0f, s = 0.0f, h = 0.0f, x, y;
        if (step_now >= 0) {
            step_left -= 1.0f;
            if (step_left <= 0.0f) {
                uint16_t bit = (uint16_t)(1u << step_pos);
                step_now = step_pos;
                for (mc = 0; mc < NMACHINES; mc++)
                    if ((pat_steps[mc] & bit) && !(muted >> mc & 1u) && MACHINES[mc].ready) {
                        strike((int)mc, (pat_acc[mc] & bit) ? 1.0f : 0.72f);
                        hits++;
                    }
                step_left += step_len(step_pos);
                step_pos = (step_pos + 1) % NSTEPS;
            }
        }
        if (gliding) {
            if (gliding & (1u << M_KICK))
                GLIDE(kick_c, kick_next, 0.005f);
            if (gliding & (1u << M_SNARE))
                GLIDE(snare_c, snare_next, 0.005f);
            if (gliding & (1u << M_HATS))
                GLIDE(hat_c, hat_next, 0.005f);
        }
        if (kv[0].on)
            k += kick_sample(&kv[0], &kick_c);
        if (kv[1].on)
            k += kick_sample(&kv[1], &kick_c);
        if (sv[0].on)
            s += snare_sample(&sv[0], &snare_c);
        if (sv[1].on)
            s += snare_sample(&sv[1], &snare_c);
        if (hv[0].on)
            h += hat_sample(&hv[0], &hat_c);
        if (hv[1].on)
            h += hat_sample(&hv[1], &hat_c);
        gain_m[M_KICK] += (kick_c.makeup * level[M_KICK] - gain_m[M_KICK]) * 0.005f;   /* ~5 ms, as the glide */
        gain_m[M_SNARE] += (snare_c.makeup * level[M_SNARE] - gain_m[M_SNARE]) * 0.005f;
        gain_m[M_HATS] += (hat_c.makeup * level[M_HATS] - gain_m[M_HATS]) * 0.005f;
        pk = fm_maxf(pk, fm_fabsf(k * kick_c.makeup));   /* the screen's: each machine as if at full level */
        ps = fm_maxf(ps, fm_fabsf(s * snare_c.makeup));
        ph = fm_maxf(ph, fm_fabsf(h * hat_c.makeup));
        x = k * gain_m[M_KICK] + s * gain_m[M_SNARE] + h * gain_m[M_HATS];
        /* the master: clean below 0.6, rounded off above, never over full scale */
        y = x;
        if (fm_fabsf(x) > 0.6f) {
            float o = 0.6f + 0.4f * fm_tanhf((fm_fabsf(x) - 0.6f) * 2.5f);
            y = x > 0.0f ? o : -o;
        }
        gain_now += (gain - gain_now) * 0.004f;
        lr[2 * i] = lr[2 * i + 1] = (int32_t)(y * gain_now);
    }
    if (gliding) {                     /* arrived (or silent: the rest at once) */
        if (SAME(kick_c, kick_next) || (!kv[0].on && !kv[1].on)) {
            kick_c = kick_next;
            gliding &= (uint8_t)~(1u << M_KICK);
        }
        if (SAME(snare_c, snare_next) || (!sv[0].on && !sv[1].on)) {
            snare_c = snare_next;
            gliding &= (uint8_t)~(1u << M_SNARE);
        }
        if (SAME(hat_c, hat_next) || (!hv[0].on && !hv[1].on)) {
            hat_c = hat_next;
            gliding &= (uint8_t)~(1u << M_HATS);
        }
    }
    env_now[M_KICK] = pk > env_now[M_KICK] ? pk : env_now[M_KICK] * 0.85f;
    env_now[M_SNARE] = ps > env_now[M_SNARE] ? ps : env_now[M_SNARE] * 0.85f;
    env_now[M_HATS] = ph > env_now[M_HATS] ? ph : env_now[M_HATS] * 0.85f;
}

#ifdef ENG_LAB
/* the lab (measurements off the device): one machine's hit, dry, into out (mono), n samples */
void eng_lab_hit(int machine, const uint8_t m[4], float *out, uint32_t n)
{
    uint32_t i;
    kick_voice_t k = {0};
    snare_voice_t s = {0};
    hat_voice_t h = {0};
    kick_coef_t kc;
    snare_coef_t sc;
    hat_coef_t hc;
    kick_coefs(m, &kc);
    snare_coefs(m, &sc);
    hat_coefs(m, &hc);
    kick_start(&k, 1.0f);
    snare_start(&s, 1.0f);
    hat_start(&h, 1.0f);
    for (i = 0; i < n; i++)
        out[i] = machine == M_KICK ? (k.on ? kick_sample(&k, &kc) * kc.makeup : 0.0f)
               : machine == M_SNARE ? (s.on ? snare_sample(&s, &sc) * sc.makeup : 0.0f)
               : (h.on ? hat_sample(&h, &hc) * hc.makeup : 0.0f);
}
#endif
