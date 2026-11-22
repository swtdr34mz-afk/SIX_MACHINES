/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES' sound engine (engine.h): the machines, the tracks' insert and modulation, the sequencer, the master,
 * the performance effects. No libc here: the device builds this unit on its own. */
#include "engine.h"
#include "fastmath.h"
#include "plat.h"

#define RGB565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

/* ================================================================ the tables === */
/* The zones' centres are where the curves below have their breakpoints: at a zone every parameter a macro moves is
 * exactly the value chosen for it there; between two zones they move from one to the other. */
const machine_t MACHINES[NMACHINES] = {
    {"KICK", RGB565(255, 92, 40),
     {{"TUNE", 4, {20, 50, 75, 105}, {"SUB", "DEEP", "808", "TIGHT"}},
      {"PUNCH", 4, {12, 50, 80, 115}, {"SOFT", "SYNTAKT", "TECHNO", "ZAP"}},
      {"BOOM", 4, {20, 50, 85, 115}, {"TIGHT", "SYNTAKT", "808", "DRONE"}},
      {"GRIT", 5, {0, 35, 70, 100, 127}, {"CLEAN", "WARM", "RUMBLE", "FOLD", "DESTROY"}}},
     {{"SYNTAKT DEEP", {50, 50, 50, 35}}, {"808 CLASSIC", {75, 12, 85, 0}}, {"WAREHOUSE", {105, 80, 20, 70}},
      {"PSY ZAP", {60, 115, 20, 35}}}},
    {"SNARE", RGB565(70, 200, 255),
     {{"TUNE", 4, {20, 50, 80, 110}, {"FAT", "SYNTAKT", "TIGHT", "PICCOLO"}},
      {"SNAP", 4, {15, 50, 85, 115}, {"BRUSH", "SYNTAKT", "CRACK", "RIMSHOT"}},
      {"BODY", 4, {15, 50, 85, 118}, {"NOISY", "SYNTAKT", "FAT", "TONE"}},
      {"RATTLE", 5, {10, 45, 75, 105, 127}, {"DRY", "SYNTAKT", "TRAP", "GATED", "DIRT"}}},
     {{"SYNTAKT SD", {50, 50, 50, 45}}, {"TRAP SNARE", {80, 85, 15, 75}}, {"GATED 80S", {35, 50, 85, 105}},
      {"RIMSHOT", {110, 115, 85, 10}}}},
    {"TOMS", RGB565(120, 230, 110),
     {{"TUNE", 4, {20, 50, 80, 110}, {"FLOOR", "LOW", "HIGH", "TIMBAL"}},
      {"BEND", 4, {10, 45, 80, 115}, {"FLAT", "SYNTAKT", "DROP", "SIMMONS"}},
      {"DECAY", 4, {15, 50, 85, 118}, {"SHORT", "SYNTAKT", "RING", "BOOM"}},
      {"SKIN", 4, {10, 45, 80, 115}, {"SOFT", "SYNTAKT", "WOOD", "METAL"}}},
     {{"SYNTAKT TOM", {50, 45, 50, 45}}, {"FLOOR BOOM", {20, 45, 118, 10}}, {"SIMMONS", {80, 115, 85, 45}},
      {"WOOD BLOCK", {110, 10, 15, 80}}}},
    {"METAL", RGB565(200, 140, 255),
     {{"TUNE", 4, {20, 50, 85, 115}, {"LOW", "MID", "HIGH", "SPLASH"}},
      {"RING", 5, {10, 40, 70, 95, 122}, {"RIDE", "CRASH", "GONG", "BELL", "INDUSTRY"}},
      {"DECAY", 5, {10, 40, 70, 100, 127}, {"CHOKE", "SHORT", "RIDE", "CRASH", "WASH"}},
      {"SHIMMER", 4, {15, 50, 85, 118}, {"DARK", "SILK", "SHINE", "SIZZLE"}}},
     {{"FM RIDE", {50, 10, 70, 50}}, {"FM CRASH", {50, 40, 100, 85}}, {"GONG", {20, 70, 127, 15}},
      {"INDUSTRIAL", {85, 122, 40, 118}}}},
    {"HATS", RGB565(255, 214, 60),
     {{"TUNE", 4, {20, 50, 85, 115}, {"LOW", "808", "HIGH", "TINY"}},
      {"DECAY", 5, {10, 35, 70, 105, 127}, {"TICK", "CLOSED", "HALF", "OPEN", "WASH"}},
      {"METAL", 4, {15, 50, 85, 118}, {"NOISE", "SYNTAKT", "808", "RING"}},
      {"CRISP", 4, {15, 50, 85, 118}, {"SOFT", "CRISP", "SIZZLE", "HARSH"}}},
     {{"SYNTAKT HH", {50, 35, 50, 50}}, {"808 HAT", {50, 35, 85, 50}}, {"OPEN WASH", {50, 105, 50, 85}},
      {"TICK", {85, 10, 15, 85}}}},
    {"CLAP", RGB565(255, 90, 170),
     {{"TUNE", 4, {20, 50, 80, 110}, {"LOW", "909", "808", "HIGH"}},
      {"SPREAD", 4, {10, 40, 70, 110}, {"SINGLE", "TIGHT", "909", "CROWD"}},
      {"ROOM", 4, {10, 45, 80, 115}, {"DRY", "SHORT", "ROOM", "HALL"}},
      {"MORPH", 3, {15, 64, 115}, {"CLAP", "RIM", "COWBELL"}}},
     {{"909 CLAP", {50, 70, 45, 15}}, {"808 CLAP", {80, 40, 80, 15}}, {"RIM CLICK", {80, 10, 10, 64}},
      {"COWBELL", {50, 10, 45, 115}}}},
};

#define TGT {0, 0, 0, 0}
#define BIPOLAR(n) {n, 5, {4, 34, 64, 94, 124}, {"-MAX", "-HALF", "OFF", "+HALF", "+MAX"}}
const macro_t PAGE_KNOBS[NPAGES][4] = {
    {{0}},
    {{"TYPE", 4, {15, 50, 85, 118}, {"TAPE", "TUBE", "FOLD", "CRUSH"}},
     {"AMOUNT", 4, {0, 35, 70, 110}, {"OFF", "LIGHT", "HOT", "MAX"}},
     {"TONE", 3, {20, 64, 105}, {"DARK", "OPEN", "BRIGHT"}},
     {"MIX", 3, {0, 64, 127}, {"DRY", "HALF", "WET"}}},
    {BIPOLAR("DEPTH"),
     {"ATTACK", 4, {5, 45, 90, 122}, {"SNAP", "SOFT", "SWELL", "SLOW"}},
     {"DECAY", 4, {15, 50, 85, 120}, {"SHORT", "MID", "LONG", "HOLD"}},
     {"TARGET", 4, {15, 50, 85, 118}, TGT}},
    {{"RATE", 5, {10, 35, 60, 85, 110}, {"4 BARS", "1 BAR", "1/4", "1/8", "1/16"}},
     BIPOLAR("DEPTH"),
     {"WAVE", 5, {10, 40, 70, 95, 120}, {"SINE", "TRI", "SAW", "SQUARE", "S&H"}},
     {"TARGET", 4, {15, 50, 85, 118}, TGT}},
    {{"LEVEL", 4, {40, 80, 100, 120}, {"SOFT", "MIX", "FULL", "HOT"}},
     {"PAN", 5, {0, 32, 64, 96, 127}, {"LEFT", "L/2", "CENTER", "R/2", "RIGHT"}},
     {"REVERB", 5, {0, 30, 64, 100, 127}, {"DRY", "TOUCH", "ROOM", "WET", "DROWN"}},
     {"DELAY", 5, {0, 30, 64, 100, 127}, {"DRY", "TOUCH", "ECHO", "WET", "DUB"}}},
    {{"COMP", 4, {0, 35, 70, 110}, {"OFF", "GLUE", "PUNCH", "PUMP"}},
     {"WARMTH", 4, {0, 40, 80, 120}, {"CLEAN", "TAPE", "HOT", "CRUSHED"}},
     {"TILT", 3, {20, 64, 108}, {"DARK", "FLAT", "BRIGHT"}},
     {"SPACE", 4, {20, 55, 90, 120}, {"ROOM", "PLATE", "HALL", "CAVE"}}},
    {{"TIME", 4, {15, 45, 75, 105}, {"1/16", "1/8", "3/16", "1/4"}},
     {"FEEDBK", 4, {20, 55, 90, 120}, {"SHORT", "ECHO", "DUB", "LOOP"}},
     {"TONE", 3, {20, 60, 105}, {"DARK", "TAPE", "CLEAR"}},
     {"RETURN", 4, {0, 40, 80, 115}, {"OFF", "LOW", "MIX", "LOUD"}}},
    {{"FILTER", 5, {5, 35, 64, 93, 122}, {"LP MAX", "LP", "OPEN", "HP", "HP MAX"}},
     {"SCENE", 3, {0, 64, 127}, {"A", "HALF", "B"}},
     {"BUILD", 3, {20, 64, 108}, {"1 BAR", "2 BARS", "4 BARS"}},
     {"RES", 3, {20, 64, 110}, {"SOFT", "ACID", "SCREAM"}}},
};
const char *const PAGE_NAME[NPAGES] = {"SOUND", "SAT", "ENV", "LFO", "MIX", "MASTER", "DELAY", "PERFORM"};
const uint8_t PAGE_DEFAULT[NPAGES][4] = {
    {0}, {15, 0, 64, 127}, {64, 5, 50, 15}, {35, 64, 10, 15}, {80, 64, 0, 0}, {0, 0, 64, 55}, {75, 55, 60, 80},
    {64, 0, 64, 20}};
const char *const COND_NAME[NCONDS] = {"ALWAYS", "1:2", "2:2", "1:4", "2:4", "3:4", "4:4", "50%", "25%", "75%",
                                       "FILL", "NOT FILL", "FIRST"};
const char *const PAD_NAME[NPADS] = {"REPEAT 1/4", "REPEAT 1/8", "REPEAT 1/16", "REPEAT 1/32", "TAPE STOP",
                                     "BUILD-UP", "REVERB THROW", "DELAY THROW", "KILL LOW", "KILL MID", "KILL HIGH",
                                     "FILL", "SCENE B", "BREAK", "FREEZE", "DROP", "GATE", "HALF TIME", "LO-FI",
                                     "PITCH DOWN", "SWEEP"};

/* ============================================================== the helpers === */
typedef struct {                         /* a curve: breakpoints (x 0..127, y), x rising, straight lines between */
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

/* the zone nearest a value (for the knobs that choose: a type, a wave, a target) */
static int nearest(const macro_t *k, float v)
{
    uint32_t z;
    int best = 0;
    float d = 1e9f;
    for (z = 0; z < k->nzones; z++) {
        float e = fm_fabsf(v - (float)k->centre[z]);
        if (e < d) {
            d = e;
            best = (int)z;
        }
    }
    return best;
}

static float coef(float tau_s) { return tau_s <= 0.0f ? 0.0f : fm_expf(-1.0f / (tau_s * ENG_SR)); }
static float onepole(float hz) { return 1.0f - fm_expf(-FM_TWO_PI * fm_clampf(hz, 5.0f, 18000.0f) / ENG_SR); }

/* white noise: each machine has its own generator (the two cores render machines at once), the sequencer its own */
static inline float white(uint32_t *ns)
{
    uint32_t n = *ns;
    n ^= n << 13;
    n ^= n >> 17;
    n ^= n << 5;
    *ns = n;
    return (float)(int32_t)n * (1.0f / 2147483648.0f);
}
static uint32_t seq_noise = 0x2545F491u;
static inline float rnd01(void) { return 0.5f + 0.5f * white(&seq_noise); }

/* a soft clip that starts clean: drive 1 is untouched, the first 0.5 of drive blends tanh in */
static inline float drive_clip(float x, float drive)
{
    float y = x * drive, sat = fm_clampf((drive - 1.0f) * 2.0f, 0.0f, 1.0f);
    return sat > 0.0f ? y + sat * (fm_tanhf(y) - y) : y;
}

/* a TPT state variable filter (Zavalishin): stable when its frequency moves */
typedef struct {
    float a1, a2, a3, k;
} svf_c;
typedef struct {
    float ic1, ic2;
} svf_s;
static void svf_set(svf_c *c, float hz, float q)
{
    float g = fm_tanf(FM_PI * fm_clampf(hz, 10.0f, 19000.0f) / ENG_SR);
    c->k = 1.0f / q;
    c->a1 = 1.0f / (1.0f + g * (g + c->k));
    c->a2 = g * c->a1;
    c->a3 = g * c->a2;
}
/* one sample: the low-pass in *lp, the band-pass in *bp; returns the high-pass */
static inline float svf_run(const svf_c *c, svf_s *s, float x, float *lp, float *bp)
{
    float v3 = x - s->ic2, v1 = c->a1 * s->ic1 + c->a2 * v3, v2 = s->ic2 + c->a2 * s->ic1 + c->a3 * v3;
    s->ic1 = 2.0f * v1 - s->ic1;
    s->ic2 = 2.0f * v2 - s->ic2;
    *lp = v2;
    *bp = v1;
    return x - c->k * v1 - v2;
}

/* ===================================================================== KICK === */
static const bp_t K_TUNE_ST[] = {{0, -10.48f}, {20, -7.0f}, {50, -2.0f}, {75, 0.0f}, {105, 3.0f}, {127, 6.49f}};
static const bp_t K_PUNCH_OCT[] = {{0, 0.4f}, {12, 0.6f}, {50, 2.5f}, {80, 3.5f}, {115, 4.0f}, {127, 4.2f}};
static const bp_t K_PUNCH_S[] = {{0, 0.030f}, {12, 0.028f}, {50, 0.018f}, {80, 0.008f}, {95, 0.012f}, {115, 0.045f},
                                 {127, 0.070f}};
static const bp_t K_PUNCH_CLICK[] = {{0, 0.0f}, {12, 0.03f}, {50, 0.30f}, {80, 0.70f}, {115, 0.40f}, {127, 0.35f}};
static const bp_t K_BOOM_S[] = {{0, 0.030f}, {20, 0.050f}, {50, 0.110f}, {85, 0.300f}, {115, 0.80f}, {127, 1.10f}};
static const bp_t K_BOOM_SUB[] = {{0, 0.05f}, {20, 0.10f}, {50, 0.30f}, {85, 0.60f}, {115, 0.80f}, {127, 0.80f}};
static const bp_t K_BOOM_DROOP[] = {{0, 0.0f}, {70, 0.0f}, {85, -0.10f}, {115, -0.15f}, {127, -0.167f}};
static const bp_t K_GRIT_DRIVE[] = {{0, 1.0f}, {35, 2.5f}, {70, 8.0f}, {100, 10.0f}, {127, 14.0f}};
static const bp_t K_GRIT_FOLD[] = {{0, 0.0f}, {85, 0.0f}, {100, 0.55f}, {127, 1.0f}};
static const bp_t K_GRIT_TONE[] = {{0, 16000.0f}, {35, 11000.0f}, {70, 5500.0f}, {100, 8000.0f}, {127, 11000.0f}};
static const bp_t K_GRIT_CRUSH[] = {{0, 0.0f}, {110, 0.0f}, {127, 1.0f}};
static const bp_t K_GRIT_MAKEUP[] = {{0, 1.00f}, {10, 0.86f}, {20, 0.765f}, {35, 0.674f}, {50, 0.51f}, {70, 0.456f},
                                     {85, 0.456f}, {100, 0.736f}, {110, 0.716f}, {127, 0.60f}};   /* measured: ~1 dB */

typedef struct {                         /* (every machine's coefficients: the makeup first, then floats only) */
    float makeup, hz, sweep_oct, droop_oct, k_sweep, k_body, k_tail, sub, k_click, click;
    float drive, fold, tone_a, crush_hold, crush_q;
} kick_coef_t;
typedef struct {
    uint8_t on;
    float phase, e_sweep, e_body, e_tail, e_click, vel, fade, k_fade;
    float lp, hp_prev, crush_s, crush_n;
} kick_voice_t;

static void kick_coefs(const float *m, kick_coef_t *c)
{
    float tune = m[0], punch = m[1], boom = m[2], grit = m[3];
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
    c->crush_hold = 1.0f + 7.0f * crush;
    c->crush_q = 4096.0f - 4080.0f * crush;
    c->makeup = CURVE(K_GRIT_MAKEUP, grit) * (1.0f - 0.25f * bn * gn);   /* long and driven: a little less */
}

static void kick_start(kick_voice_t *v, float vel)
{
    v->on = 1;
    v->phase = 0.0f;                           /* from zero every time: the same punch on every hit */
    v->e_sweep = v->e_body = v->e_tail = v->e_click = 1.0f;
    v->vel = vel;
    v->fade = v->k_fade = 1.0f;
    v->lp = v->hp_prev = v->crush_s = v->crush_n = 0.0f;
}

static float kick_sample(kick_voice_t *v, const kick_coef_t *c, uint32_t *ns)
{
    float f, s, x, n, y;
    f = c->hz * fm_exp2f(c->sweep_oct * v->e_sweep + c->droop_oct * (1.0f - v->e_body));
    v->phase += f * (1.0f / ENG_SR);
    if (v->phase >= 1.0f)
        v->phase -= 1.0f;
    s = fm_sin_turns(v->phase);
    x = s * ((1.0f - c->sub) * v->e_body + c->sub * v->e_tail);
    if (c->click > 0.0f && v->e_click > 0.001f) {   /* the click: a high-passed noise burst of about a millisecond */
        n = white(ns);
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
    y *= v->vel * v->fade;
    v->e_sweep *= c->k_sweep;
    v->e_body *= c->k_body;
    v->e_tail *= c->k_tail;
    v->e_click *= c->k_click;
    v->fade *= v->k_fade;
    if ((v->e_tail < 0.0002f && v->e_body < 0.0002f) || v->fade < 0.0005f)
        v->on = 0;
    return y;
}

/* ==================================================================== SNARE === */
/* a tuned body (two sines, the second at the drum's 1.59 mode, a short pitch drop) and a noise band with its own decay
 * (a hold before it: GATED), a snap of a click, drive at the end (DIRT) */
static const bp_t S_TUNE_HZ[] = {{0, 120.0f}, {20, 150.0f}, {50, 190.0f}, {80, 240.0f}, {110, 320.0f}, {127, 380.0f}};
static const bp_t S_SNAP_OCT[] = {{0, 0.0f}, {15, 0.10f}, {50, 0.60f}, {85, 1.00f}, {115, 1.60f}, {127, 1.80f}};
static const bp_t S_SNAP_S[] = {{0, 0.020f}, {50, 0.012f}, {85, 0.008f}, {115, 0.004f}, {127, 0.004f}};
static const bp_t S_SNAP_CLICK[] = {{0, 0.0f}, {15, 0.0f}, {50, 0.35f}, {85, 0.80f}, {115, 1.00f}, {127, 1.00f}};
static const bp_t S_SNAP_NOISE[] = {{0, 1.0f}, {85, 1.0f}, {115, 0.35f}, {127, 0.30f}};
static const bp_t S_SNAP_BRUSH[] = {{0, 0.55f}, {15, 0.6f}, {50, 1.0f}, {127, 1.0f}};
static const bp_t S_BODY_LVL[] = {{0, 0.10f}, {15, 0.15f}, {50, 0.50f}, {85, 0.80f}, {118, 1.00f}, {127, 1.00f}};
static const bp_t S_BODY_S[] = {{0, 0.030f}, {15, 0.040f}, {50, 0.070f}, {85, 0.120f}, {118, 0.200f}, {127, 0.250f}};
static const bp_t S_BODY_NOISE[] = {{0, 1.0f}, {15, 1.0f}, {50, 0.75f}, {85, 0.55f}, {118, 0.20f}, {127, 0.15f}};
static const bp_t S_RAT_S[] = {{0, 0.030f}, {10, 0.040f}, {45, 0.090f}, {75, 0.200f}, {105, 0.020f}, {127, 0.120f}};
static const bp_t S_RAT_HOLD[] = {{0, 0.0f}, {75, 0.0f}, {105, 0.160f}, {127, 0.0f}};
static const bp_t S_RAT_HP[] = {{0, 1500.0f}, {10, 1200.0f}, {45, 900.0f}, {75, 1800.0f}, {105, 700.0f}, {127, 600.0f}};
static const bp_t S_RAT_LP[] = {{0, 6000.0f}, {10, 7000.0f}, {45, 9000.0f}, {75, 14000.0f}, {105, 8000.0f},
                                {127, 12000.0f}};
static const bp_t S_RAT_DRIVE[] = {{0, 1.0f}, {105, 1.0f}, {127, 6.0f}};
static const bp_t S_MAKEUP[] = {{0, 1.30f}, {10, 1.19f}, {45, 0.90f}, {75, 0.76f}, {105, 0.56f}, {127, 0.43f}};
static const bp_t S_SNAP_GAIN[] = {{0, 1.2f}, {36, 1.04f}, {54, 1.0f}, {90, 1.1f}, {108, 1.33f}, {117, 1.5f},
                                   {127, 1.5f}};

typedef struct {
    float makeup, hz, sweep_oct, k_sweep, body, k_body, noise, k_noise, hold, hp_a, lp_a, click, k_click, drive;
} snare_coef_t;
typedef struct {
    uint8_t on;
    float ph1, ph2, e_sweep, e_body, e_noise, hold, e_click, vel, fade, k_fade;
    float hp_lp, lp, cl_prev;
} snare_voice_t;

static void snare_coefs(const float *m, snare_coef_t *c)
{
    float tune = m[0], snap = m[1], body = m[2], rat = m[3], tn = tune / 127.0f;
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
    v->fade = v->k_fade = 1.0f;
    v->hp_lp = v->lp = v->cl_prev = 0.0f;
}

static float snare_sample(snare_voice_t *v, const snare_coef_t *c, uint32_t *ns)
{
    float f = c->hz * fm_exp2f(c->sweep_oct * v->e_sweep), n = white(ns), x;
    v->ph1 += f * (1.0f / ENG_SR);
    if (v->ph1 >= 1.0f)
        v->ph1 -= 1.0f;
    v->ph2 += f * 1.59f * (1.0f / ENG_SR);
    if (v->ph2 >= 1.0f)
        v->ph2 -= 1.0f;
    x = (fm_sin_turns(v->ph1) + 0.45f * fm_sin_turns(v->ph2)) * 0.7f * c->body * v->e_body;
    v->hp_lp += c->hp_a * (n - v->hp_lp);       /* the noise: high-passed, then low-passed (its colour) */
    v->lp += c->lp_a * ((n - v->hp_lp) - v->lp);
    x += v->lp * 1.6f * c->noise * v->e_noise;
    if (c->click > 0.0f && v->e_click > 0.001f) {
        x += (n - v->cl_prev) * 0.35f * c->click * v->e_click;
        v->cl_prev = n;
    }
    x = drive_clip(x, c->drive) * v->vel * v->fade;
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

/* ===================================================================== TOMS === */
/* a membrane: a sine bent down from above, its 1.59 mode beside it, a stick's noise, and the SKIN: a frequency-modulated
 * carrier (WOOD: a woody 1.41 ratio, METAL: a bell's 2.76) */
static const bp_t T_TUNE_HZ[] = {{0, 55.0f}, {20, 70.0f}, {50, 100.0f}, {80, 160.0f}, {110, 260.0f}, {127, 330.0f}};
static const bp_t T_BEND_OCT[] = {{0, 0.0f}, {10, 0.05f}, {45, 0.5f}, {80, 1.0f}, {115, 1.6f}, {127, 2.0f}};
static const bp_t T_BEND_S[] = {{0, 0.03f}, {10, 0.04f}, {45, 0.05f}, {80, 0.12f}, {115, 0.25f}, {127, 0.30f}};
static const bp_t T_DECAY_S[] = {{0, 0.04f}, {15, 0.05f}, {50, 0.12f}, {85, 0.30f}, {118, 0.70f}, {127, 0.90f}};
static const bp_t T_NOISE[] = {{0, 0.0f}, {10, 0.02f}, {45, 0.20f}, {80, 0.15f}, {115, 0.10f}, {127, 0.10f}};
static const bp_t T_MODE2[] = {{0, 0.10f}, {10, 0.15f}, {45, 0.30f}, {80, 0.20f}, {115, 0.10f}, {127, 0.10f}};
static const bp_t T_FMI[] = {{0, 0.0f}, {10, 0.0f}, {45, 0.3f}, {80, 1.5f}, {115, 4.0f}, {127, 5.0f}};
static const bp_t T_FMR[] = {{0, 1.41f}, {80, 1.41f}, {115, 2.76f}, {127, 3.1f}};
static const bp_t T_FMDEC[] = {{0, 0.3f}, {80, 0.3f}, {115, 0.8f}, {127, 1.0f}};
static const bp_t T_MAKEUP[] = {{0, 0.8f}, {127, 0.8f}};   /* measured: SKIN keeps the loudness */

typedef struct {
    float makeup, hz, bend_oct, k_bend, k_amp, k_amp2, noise, k_noise, mode2, fmi, fmr, k_fm;
} tom_coef_t;
typedef struct {
    uint8_t on;
    float pc, pm, p2, e_bend, e_amp, e_amp2, e_noise, e_fm, vel, fade, k_fade, hp_prev;
} tom_voice_t;

static void tom_coefs(const float *m, tom_coef_t *c)
{
    float tune = m[0], bend = m[1], dec = m[2], skin = m[3], d;
    c->hz = CURVE(T_TUNE_HZ, tune);
    c->bend_oct = CURVE(T_BEND_OCT, bend);
    c->k_bend = coef(CURVE(T_BEND_S, bend));
    d = CURVE(T_DECAY_S, dec) * (1.2f - 0.4f * tune / 127.0f);   /* a high tom rings shorter */
    c->k_amp = coef(d);
    c->k_amp2 = coef(d * 0.45f);
    c->noise = CURVE(T_NOISE, skin);
    c->k_noise = coef(0.006f);
    c->mode2 = CURVE(T_MODE2, skin);
    c->fmi = CURVE(T_FMI, skin);
    c->fmr = CURVE(T_FMR, skin);
    c->k_fm = coef(d * CURVE(T_FMDEC, skin));
    c->makeup = CURVE(T_MAKEUP, skin);
}

static void tom_start(tom_voice_t *v, float vel)
{
    v->on = 1;
    v->pc = v->pm = v->p2 = 0.0f;
    v->e_bend = v->e_amp = v->e_amp2 = v->e_noise = v->e_fm = 1.0f;
    v->vel = vel;
    v->fade = v->k_fade = 1.0f;
    v->hp_prev = 0.0f;
}

static float tom_sample(tom_voice_t *v, const tom_coef_t *c, uint32_t *ns)
{
    float f = c->hz * fm_exp2f(c->bend_oct * v->e_bend), x, n;
    v->pc += f * (1.0f / ENG_SR);
    if (v->pc >= 1.0f)
        v->pc -= 1.0f;
    v->pm += f * c->fmr * (1.0f / ENG_SR);
    if (v->pm >= 1.0f)
        v->pm -= 1.0f;
    v->p2 += f * 1.59f * (1.0f / ENG_SR);
    if (v->p2 >= 1.0f)
        v->p2 -= 1.0f;
    x = fm_sin_turns(v->pc + fm_sin_turns(v->pm) * c->fmi * v->e_fm * 0.159f) * v->e_amp;
    x += fm_sin_turns(v->p2) * c->mode2 * v->e_amp2;
    if (c->noise > 0.0f && v->e_noise > 0.001f) {
        n = white(ns);
        x += (n - v->hp_prev) * c->noise * v->e_noise;
        v->hp_prev = n;
    }
    x *= 0.8f * v->vel * v->fade;
    v->e_bend *= c->k_bend;
    v->e_amp *= c->k_amp;
    v->e_amp2 *= c->k_amp2;
    v->e_noise *= c->k_noise;
    v->e_fm *= c->k_fm;
    v->fade *= v->k_fade;
    if (v->e_amp < 0.0003f || v->fade < 0.0005f)
        v->on = 0;
    return x;
}

/* ==================================================================== METAL === */
/* three FM pairs (carrier : modulator) at a cymbal's inharmonic ratios, their index fading slower than the sound, a
 * ring modulation of two of them, noise, a resonant high-pass. RING walks through prepared ratio sets (ride, crash,
 * gong, bell, industrial) and between them morphs from one set to the next */
static const bp_t X_TUNE_HZ[] = {{0, 400.0f}, {20, 600.0f}, {50, 900.0f}, {85, 1400.0f}, {115, 2200.0f}, {127, 2800.0f}};
static const bp_t X_C0[] = {{10, 1.0f}, {40, 1.0f}, {70, 0.5f}, {95, 1.0f}, {122, 1.0f}};
static const bp_t X_C1[] = {{10, 2.13f}, {40, 1.53f}, {70, 1.0f}, {95, 2.76f}, {122, 1.9f}};
static const bp_t X_C2[] = {{10, 3.7f}, {40, 2.89f}, {70, 1.7f}, {95, 5.4f}, {122, 3.3f}};
static const bp_t X_M0[] = {{10, 1.41f}, {40, 2.71f}, {70, 0.71f}, {95, 3.5f}, {122, 1.13f}};
static const bp_t X_M1[] = {{10, 3.17f}, {40, 4.37f}, {70, 1.37f}, {95, 1.0f}, {122, 7.3f}};
static const bp_t X_M2[] = {{10, 5.1f}, {40, 6.13f}, {70, 2.3f}, {95, 1.0f}, {122, 0.53f}};
static const bp_t X_IDX[] = {{10, 1.8f}, {40, 3.0f}, {70, 1.5f}, {95, 1.0f}, {122, 4.0f}};
static const bp_t X_DECAY_S[] = {{0, 0.02f}, {10, 0.03f}, {40, 0.12f}, {70, 0.40f}, {100, 1.0f}, {127, 2.0f}};
static const bp_t X_HP[] = {{0, 300.0f}, {15, 500.0f}, {50, 2000.0f}, {85, 4500.0f}, {118, 7000.0f}, {127, 8000.0f}};
static const bp_t X_NOISE[] = {{0, 0.0f}, {15, 0.02f}, {50, 0.06f}, {85, 0.15f}, {118, 0.30f}, {127, 0.35f}};
static const bp_t X_RINGMOD[] = {{0, 0.0f}, {50, 0.0f}, {85, 0.3f}, {118, 0.6f}, {127, 0.7f}};
/* the levels that keep RING and SHIMMER changes of character (measured: the first 150 ms) */
static const bp_t X_RING_G[] = {{0, 1.08f}, {9, 1.08f}, {18, 1.00f}, {27, 0.91f}, {36, 0.84f}, {45, 0.87f}, {54, 0.97f}, {63, 1.19f}, {72, 1.43f}, {81, 1.14f}, {90, 0.99f}, {99, 0.94f}, {108, 0.86f}, {117, 0.89f}, {126, 0.94f}};
static const bp_t X_MAKEUP[] = {{0, 0.63f}, {9, 0.64f}, {18, 0.66f}, {27, 0.70f}, {36, 0.75f}, {45, 0.81f}, {54, 0.91f}, {63, 1.07f}, {72, 1.21f}, {81, 1.33f}, {90, 1.42f}, {99, 1.45f}, {108, 1.44f}, {117, 1.40f}, {126, 1.36f}};

typedef struct {
    float makeup, ic[3], im[3], idx, k_amp, k_idx, noise, ring;
    svf_c hp;
} metal_coef_t;
typedef struct {
    uint8_t on;
    float pc[3], pm[3], e_amp, e_idx, vel, fade, k_fade;
    svf_s hp;
} metal_voice_t;

static void metal_coefs(const float *m, metal_coef_t *c)
{
    float hz = CURVE(X_TUNE_HZ, m[0]), r = m[1], d = CURVE(X_DECAY_S, m[2]);
    c->ic[0] = hz * CURVE(X_C0, r) / ENG_SR;
    c->ic[1] = hz * CURVE(X_C1, r) / ENG_SR;
    c->ic[2] = hz * CURVE(X_C2, r) / ENG_SR;
    c->im[0] = hz * CURVE(X_M0, r) / ENG_SR;
    c->im[1] = hz * CURVE(X_M1, r) / ENG_SR;
    c->im[2] = hz * CURVE(X_M2, r) / ENG_SR;
    c->idx = CURVE(X_IDX, r);
    c->k_amp = coef(d);
    c->k_idx = coef(d * 1.8f);
    c->noise = CURVE(X_NOISE, m[3]);
    c->ring = CURVE(X_RINGMOD, m[3]);
    svf_set(&c->hp, CURVE(X_HP, m[3]) * fm_sqrtf(hz / 900.0f), 0.9f);
    c->makeup = CURVE(X_MAKEUP, m[3]) * CURVE(X_RING_G, r);
}

static void metal_start(metal_voice_t *v, float vel)
{
    v->on = 1;                                  /* the phases run on: the metal's grain */
    v->e_amp = v->e_idx = 1.0f;
    v->vel = vel;
    v->fade = v->k_fade = 1.0f;
}

static float metal_sample(metal_voice_t *v, const metal_coef_t *c, uint32_t *ns)
{
    float o[3], x, lp, bp;
    uint32_t k;
    for (k = 0; k < 3u; k++) {
        v->pm[k] += c->im[k];
        if (v->pm[k] >= 1.0f)
            v->pm[k] -= 1.0f;
        v->pc[k] += c->ic[k];
        if (v->pc[k] >= 1.0f)
            v->pc[k] -= 1.0f;
        o[k] = fm_sin_turns(v->pc[k] + fm_sin_turns(v->pm[k]) * c->idx * v->e_idx * 0.159f);
    }
    x = (o[0] + o[1] + o[2]) * (1.0f / 3.0f);
    x += c->ring * (o[0] * o[1] - x);
    x += c->noise * (white(ns) - x);
    x = svf_run(&c->hp, &v->hp, x, &lp, &bp);
    x *= v->e_amp * v->vel * v->fade;
    v->e_amp *= c->k_amp;
    v->e_idx *= c->k_idx;
    v->fade *= v->k_fade;
    if (v->e_amp < 0.0003f || v->fade < 0.0005f)
        v->on = 0;
    return x;
}

/* ===================================================================== HATS === */
/* six square oscillators at the 808's spacings (metal), white noise, blended; a resonant high-pass, a low-pass, a decay
 * from a tick to a wash, drive at the harsh end. RING: the squares in pairs multiplied: clangier */
static const float HAT_HZ[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
static const bp_t H_TUNE[] = {{0, 0.60f}, {20, 0.80f}, {50, 1.00f}, {85, 1.35f}, {115, 1.80f}, {127, 2.00f}};
static const bp_t H_DECAY_S[] = {{0, 0.006f}, {10, 0.010f}, {35, 0.025f}, {70, 0.080f}, {105, 0.250f}, {127, 0.600f}};
static const bp_t H_METAL[] = {{0, 0.0f}, {15, 0.05f}, {50, 0.45f}, {85, 1.0f}, {118, 0.9f}, {127, 0.9f}};
static const bp_t H_RING[] = {{0, 0.0f}, {85, 0.0f}, {118, 1.0f}, {127, 1.0f}};
static const bp_t H_HP[] = {{0, 3000.0f}, {15, 4500.0f}, {50, 7000.0f}, {85, 9000.0f}, {118, 8000.0f}, {127, 8500.0f}};
static const bp_t H_RES[] = {{0, 0.7f}, {50, 0.9f}, {85, 1.4f}, {118, 1.0f}, {127, 1.0f}};
static const bp_t H_LP[] = {{0, 9000.0f}, {15, 11000.0f}, {50, 15000.0f}, {85, 18000.0f}, {127, 18000.0f}};
static const bp_t H_DRIVE[] = {{0, 1.0f}, {85, 1.0f}, {118, 4.0f}, {127, 6.0f}};
static const bp_t H_MAKEUP[] = {{0, 1.0f}, {81, 1.0f}, {90, 0.62f}, {99, 0.46f}, {108, 0.38f}, {117, 0.335f},
                                {127, 0.285f}};
static const bp_t H_METAL_GAIN[] = {{0, 0.575f}, {18, 0.63f}, {36, 0.77f}, {45, 0.92f}, {54, 1.11f}, {63, 1.4f},
                                    {72, 1.7f}, {81, 1.8f}, {90, 2.04f}, {99, 2.5f}, {108, 2.43f}, {117, 2.0f},
                                    {127, 1.95f}};

typedef struct {
    float makeup, inc[6], metal, ring, k_amp, lp_a, drive;
    svf_c hp;
} hat_coef_t;
typedef struct {
    uint8_t on;
    float ph[6], e_amp, vel, fade, k_fade, lp;
    svf_s hp;
} hat_voice_t;

static void hat_coefs(const float *m, hat_coef_t *c)
{
    float sc = CURVE(H_TUNE, m[0]);
    uint32_t i;
    for (i = 0; i < 6u; i++)
        c->inc[i] = HAT_HZ[i] * sc * (1.0f / ENG_SR);
    c->metal = CURVE(H_METAL, m[2]);
    c->ring = CURVE(H_RING, m[2]);
    c->k_amp = coef(CURVE(H_DECAY_S, m[1]));
    svf_set(&c->hp, fm_clampf(CURVE(H_HP, m[3]) * fm_sqrtf(sc), 1000.0f, 15000.0f), CURVE(H_RES, m[3]));
    c->lp_a = onepole(CURVE(H_LP, m[3]));
    c->drive = CURVE(H_DRIVE, m[3]);
    c->makeup = CURVE(H_MAKEUP, m[3]) * CURVE(H_METAL_GAIN, m[2]);
}

static void hat_start(hat_voice_t *v, float vel)
{
    v->on = 1;                                  /* the oscillators run on: their phases are the hat's grain */
    v->e_amp = 1.0f;
    v->vel = vel;
    v->fade = v->k_fade = 1.0f;
}

static float hat_sample(hat_voice_t *v, const hat_coef_t *c, uint32_t *ns)
{
    float sq[6], sum = 0.0f, ring, metal, x, lp, bp;
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
    x = (1.0f - c->metal) * white(ns) + c->metal * metal;
    x = svf_run(&c->hp, &v->hp, x, &lp, &bp);
    v->lp += c->lp_a * (x - v->lp);
    x = drive_clip(v->lp * 0.8f, c->drive) * v->vel * v->fade * v->e_amp;
    v->e_amp *= c->k_amp;
    v->fade *= v->k_fade;
    if (v->e_amp < 0.0003f || v->fade < 0.0005f)
        v->on = 0;
    return x;
}

/* ===================================================================== CLAP === */
/* hands: bursts of band-passed noise a few ms apart, then the room's tail; MORPH turns it into a rimshot (a high
 * click-tone) and a cowbell (two square waves, band-limited) */
static const bp_t C_TUNE_HZ[] = {{0, 600.0f}, {20, 700.0f}, {50, 1100.0f}, {80, 1500.0f}, {110, 2200.0f},
                                 {127, 2600.0f}};
static const bp_t C_BURSTS[] = {{0, 1.0f}, {10, 1.0f}, {40, 3.0f}, {70, 4.0f}, {110, 6.0f}, {127, 7.0f}};
static const bp_t C_GAP_MS[] = {{0, 5.0f}, {10, 5.0f}, {40, 6.0f}, {70, 9.0f}, {110, 14.0f}, {127, 16.0f}};
static const bp_t C_JITTER[] = {{0, 0.0f}, {70, 0.0f}, {110, 0.3f}, {127, 0.4f}};
static const bp_t C_TAIL_S[] = {{0, 0.015f}, {10, 0.02f}, {45, 0.08f}, {80, 0.20f}, {115, 0.50f}, {127, 0.60f}};
static const bp_t C_TAIL_LVL[] = {{0, 0.2f}, {10, 0.3f}, {45, 0.6f}, {80, 0.7f}, {115, 0.8f}, {127, 0.8f}};
static const bp_t C_W_CLAP[] = {{0, 1.0f}, {15, 1.0f}, {64, 0.0f}, {127, 0.0f}};
static const bp_t C_W_RIM[] = {{0, 0.0f}, {15, 0.0f}, {64, 1.0f}, {115, 0.0f}, {127, 0.0f}};
static const bp_t C_W_BELL[] = {{0, 0.0f}, {64, 0.0f}, {115, 1.0f}, {127, 1.0f}};
/* the levels that keep TUNE and MORPH changes of character (measured: the first 150 ms) */
static const bp_t C_TUNE_G[] = {{0, 1.23f}, {27, 1.21f}, {54, 1.0f}, {81, 0.8f}, {108, 0.7f}, {127, 0.63f}};
static const bp_t C_MAKEUP[] = {{0, 0.98f}, {9, 1.01f}, {18, 1.08f}, {27, 1.22f}, {36, 1.5f}, {45, 1.58f}, {54, 1.43f},
                                {63, 1.23f}, {72, 1.33f}, {81, 1.27f}, {90, 1.06f}, {99, 0.85f}, {108, 0.68f},
                                {117, 0.59f}, {127, 0.59f}};

typedef struct {
    float makeup, bursts, gap, jitter, k_burst, k_tail, tail, wc, wr, wb, rim_inc1, rim_inc2, k_rim, bell_inc1,
        bell_inc2, k_bell, bell_lp;
    svf_c bp;
} clap_coef_t;
typedef struct {
    uint8_t on;
    float t, next, left, e_burst, e_tail, tail_on, r1, r2, e_rim, b1, b2, e_bell, blp, bhp, vel, fade, k_fade;
    svf_s bp;
} clap_voice_t;

static void clap_coefs(const float *m, clap_coef_t *c)
{
    float hz = CURVE(C_TUNE_HZ, m[0]), s = hz / 1100.0f, room = m[2];
    c->bursts = fm_floorf(CURVE(C_BURSTS, m[1]) + 0.5f);
    c->gap = CURVE(C_GAP_MS, m[1]) * 0.001f * ENG_SR;
    c->jitter = CURVE(C_JITTER, m[1]);
    c->k_burst = coef(0.0025f);
    c->k_tail = coef(CURVE(C_TAIL_S, room));
    c->tail = CURVE(C_TAIL_LVL, room);
    c->wc = CURVE(C_W_CLAP, m[3]);
    c->wr = CURVE(C_W_RIM, m[3]);
    c->wb = CURVE(C_W_BELL, m[3]);
    c->rim_inc1 = 1700.0f * s / ENG_SR;
    c->rim_inc2 = 455.0f * s / ENG_SR;
    c->k_rim = coef(0.010f + 0.010f * room / 127.0f);
    c->bell_inc1 = 540.0f * s / ENG_SR;
    c->bell_inc2 = 800.0f * s / ENG_SR;
    c->k_bell = coef(0.05f + 0.12f * room / 127.0f);
    c->bell_lp = onepole(3500.0f * s);
    svf_set(&c->bp, hz, 1.6f);
    c->makeup = 0.95f * CURVE(C_MAKEUP, m[3]) * CURVE(C_TUNE_G, m[0]);
}

static void clap_start(clap_voice_t *v, float vel)
{
    v->on = 1;
    v->t = v->next = 0.0f;
    v->left = -1.0f;                           /* (the bursts: set at the first sample, from the coefficients) */
    v->e_burst = v->e_tail = v->tail_on = 0.0f;
    v->e_rim = v->e_bell = 1.0f;
    v->r1 = v->r2 = v->b1 = v->b2 = v->blp = v->bhp = 0.0f;
    v->vel = vel;
    v->fade = v->k_fade = 1.0f;
}

static float clap_sample(clap_voice_t *v, const clap_coef_t *c, uint32_t *ns)
{
    float n = white(ns), x = 0.0f, lp, bp, sq;
    if (v->left < 0.0f)
        v->left = c->bursts;
    if (v->left > 0.0f && v->t >= v->next) {    /* the next hand */
        v->e_burst = 1.0f;
        v->left -= 1.0f;
        v->next += c->gap * (1.0f + c->jitter * white(ns));
        if (v->left <= 0.0f) {
            v->tail_on = 1.0f;
            v->e_tail = 1.0f;
        }
    }
    v->t += 1.0f;
    if (c->wc > 0.0f) {
        svf_run(&c->bp, &v->bp, n, &lp, &bp);
        x += bp * 2.2f * (v->e_burst + c->tail * v->e_tail * v->tail_on) * c->wc;
    }
    if (c->wr > 0.0f) {
        v->r1 += c->rim_inc1;
        if (v->r1 >= 1.0f)
            v->r1 -= 1.0f;
        v->r2 += c->rim_inc2;
        if (v->r2 >= 1.0f)
            v->r2 -= 1.0f;
        x += (fm_sin_turns(v->r1) * 0.7f + fm_sin_turns(v->r2) * 0.5f + n * 0.3f * v->e_rim) * v->e_rim * c->wr;
    }
    if (c->wb > 0.0f) {
        v->b1 += c->bell_inc1;
        if (v->b1 >= 1.0f)
            v->b1 -= 1.0f;
        v->b2 += c->bell_inc2;
        if (v->b2 >= 1.0f)
            v->b2 -= 1.0f;
        sq = (v->b1 < 0.5f ? 0.5f : -0.5f) + (v->b2 < 0.5f ? 0.5f : -0.5f);
        v->blp += c->bell_lp * (sq - v->blp);
        v->bhp += 0.04f * (v->blp - v->bhp);
        x += (v->blp - v->bhp) * (0.4f + 0.6f * v->e_bell) * v->e_bell * c->wb;
    }
    x *= v->vel * v->fade;
    v->e_burst *= c->k_burst;
    v->e_tail *= c->k_tail;
    v->e_rim *= c->k_rim;
    v->e_bell *= c->k_bell;
    v->fade *= v->k_fade;
    if ((v->left == 0.0f && v->e_tail < 0.0003f && v->e_rim < 0.0003f && v->e_bell < 0.0003f) || v->fade < 0.0005f)
        v->on = 0;
    return x;
}

/* ============================================================ the machines === */
#define COEF_MAX 40
typedef union {
    float f[COEF_MAX];                   /* f[0]: the makeup */
    kick_coef_t k;
    snare_coef_t s;
    tom_coef_t t;
    metal_coef_t m;
    hat_coef_t h;
    clap_coef_t c;
} mcoef_t;
typedef union {
    uint8_t on;                          /* (each voice's first member) */
    kick_voice_t k;
    snare_voice_t s;
    tom_voice_t t;
    metal_voice_t m;
    hat_voice_t h;
    clap_voice_t c;
} mvoice_t;
_Static_assert(sizeof(mcoef_t) == sizeof(float) * COEF_MAX, "a machine's coefficients outgrew COEF_MAX");
static const uint8_t COEF_N[NMACHINES] = {
    sizeof(kick_coef_t) / 4, sizeof(snare_coef_t) / 4, sizeof(tom_coef_t) / 4,
    sizeof(metal_coef_t) / 4, sizeof(hat_coef_t) / 4, sizeof(clap_coef_t) / 4};

static void coefs(int t, const float *m, mcoef_t *c)
{
    switch (t) {
    case M_KICK: kick_coefs(m, &c->k); break;
    case M_SNARE: snare_coefs(m, &c->s); break;
    case M_TOMS: tom_coefs(m, &c->t); break;
    case M_METAL: metal_coefs(m, &c->m); break;
    case M_HATS: hat_coefs(m, &c->h); break;
    default: clap_coefs(m, &c->c); break;
    }
}

static void voice_start(int t, mvoice_t *v, float vel)
{
    switch (t) {
    case M_KICK: kick_start(&v->k, vel); break;
    case M_SNARE: snare_start(&v->s, vel); break;
    case M_TOMS: tom_start(&v->t, vel); break;
    case M_METAL: metal_start(&v->m, vel); break;
    case M_HATS: hat_start(&v->h, vel); break;
    default: clap_start(&v->c, vel); break;
    }
}

static inline float voice_sample(int t, mvoice_t *v, const mcoef_t *c, uint32_t *ns)
{
    switch (t) {
    case M_KICK: return kick_sample(&v->k, &c->k, ns);
    case M_SNARE: return snare_sample(&v->s, &c->s, ns);
    case M_TOMS: return tom_sample(&v->t, &c->t, ns);
    case M_METAL: return metal_sample(&v->m, &c->m, ns);
    case M_HATS: return hat_sample(&v->h, &c->h, ns);
    default: return clap_sample(&v->c, &c->c, ns);
    }
}

static void voice_fade(int t, mvoice_t *v, float tau)
{
    float k = coef(tau);
    switch (t) {
    case M_KICK: v->k.k_fade = k; break;
    case M_SNARE: v->s.k_fade = k; break;
    case M_TOMS: v->t.k_fade = k; break;
    case M_METAL: v->m.k_fade = k; break;
    case M_HATS: v->h.k_fade = k; break;
    default: v->c.k_fade = k; break;
    }
}

/* =============================================================== the state === */
static const track_t *TRK;
static const master_t *MST;
static track_t track0[NMACHINES];        /* (until the app binds its own) */
static master_t master0 = {{0, 0, 64, 55}, {75, 55, 60, 80}, {64, 0, 64, 20}, {{0}}, 0, 16, 124, 0};

typedef struct {
    mcoef_t cur, tgt;                    /* the render's coefficients, and where they glide to */
    mvoice_t v[2];                       /* two voices: a new hit takes the other one, the old one fades */
    uint8_t next, gliding, locked;       /* locked: the last trig's p-locks, held until its next trig */
    uint8_t lock[4];
    float eff[4];                        /* the macros the coefficients were made from */
    float gain;                          /* the makeup, smoothed per sample */
    float env, env_att, env_k, env_rise; /* ENV: its value, its attack step, its decay, rising */
    float lfo_sh, lfo_last;              /* LFO: the sample-and-hold value, the phase it was taken at */
    float mod[4];                        /* the modulation now, in macro units */
    int sat_type;                        /* the insert: saturation, its tone, its mix */
    float sat_g, sat_mix, sat_makeup, sat_lp_a, sat_hp_a, sat_lp, sat_hp, crush_n, crush_s, sat_q;
    float gl, gr, rev, dly;              /* level x pan, the sends */
    uint8_t rt_left;                     /* retrigs still to come in the step (the sequencer's) */
    float rt_period, rt_count, rt_vel;
    uint8_t seq_locked, seq_lock[4];     /* the last trig's locks, for its retrigs (the sequencer's) */
    uint32_t noise;                      /* its own noise generator */
    float peak;                          /* the block's peak (the screen's) */
} track_state_t;
static track_state_t T[NMACHINES];
static volatile uint8_t muted, changed;

/* ------------------------------------------------------------- the effects --- */
#define DL_N 22050u                      /* the ping-pong delay, per side: half a second */
#define TP_N 12000u                      /* tape stop: how far behind it may fall (0.27 s) */
#define PRE_N 2700u                      /* the reverb's pre-delay: up to 60 ms */
static int16_t dl_buf[2][DL_N] PLAT_POOL;
static int16_t tp_buf[2][TP_N] PLAT_POOL;
static float rv_comb0[1116] PLAT_POOL, rv_comb1[1188] PLAT_POOL, rv_comb2[1277] PLAT_POOL, rv_comb3[1356] PLAT_POOL;
static float rv_ap0[556] PLAT_POOL, rv_ap1[441] PLAT_POOL, rv_ap2[579] PLAT_POOL, rv_ap3[464] PLAT_POOL;
static float rv_pre[PRE_N] PLAT_POOL;

static struct {
    uint32_t ci[4], ai[4], pi;           /* reverb */
    float cf[4], fb, damp, pre;
    uint32_t di;                         /* delay */
    float dtime, dtime_t, dfb, dlp_a, dhp_a, dlp[2], dhp[2], dret;
    float c_thr, c_ratio, c_att, c_rel, c_sc, c_makeup, c_on, c_env, c_kick;   /* compressor */
    float w_g, w_norm, w_lp_a, w_lp[2], w_crush, w_n, w_s[2];                 /* warmth */
    float t_lp_a, t_lp[2], t_lo, t_hi;                                         /* tilt */
    svf_c dj;                            /* the DJ filter */
    svf_s djs[2];
    float dj_mode, dj_on;
    float i_lo[2], i_mid[2], g_lo, g_mid, g_hi, t_glo, t_gmid, t_ghi;   /* the kills */
    float b_hp_a, b_hp[2], b_gain;       /* build-up */
    svf_c sw;                            /* SWEEP */
    svf_s sws[2];
    float lf_n, lf_s[2], lf_lp[2];       /* LO-FI */
    uint32_t ti;                         /* tape stop */
    float t_speed, t_lag, t_in, t_in_lp[2];
} fx;

/* ------------------------------------------------------------ the sequencer --- */
static volatile int8_t play_req = -1;    /* -1 nothing asked, 0 stop, 1 start */
static volatile uint8_t hit_req;
static volatile float hit_vel[NMACHINES];
static volatile int32_t step_now = -1;
static volatile uint32_t hits;
static volatile float env_out[NMACHINES];
static volatile uint32_t pads;           /* held now (the main loop's) */
static uint32_t pads_was;
static float step_left;                  /* samples to the next step */
static int32_t step_pos;                 /* the next step to play */
static uint32_t loops;                   /* the pattern's turns since PLAY */
static float bar_pos;                    /* bars since the start (the LFOs' clock) */
static int32_t rep_start = -1, rep_len;  /* beat repeat: its window (steps) */
static uint8_t rep_rt;                   /* 1/32: each repeated hit twice */
static float build_t, build_len, build_on, build_roll;   /* build-up: elapsed, length (samples) */
static float scene_x;                    /* the scene morph now 0..1 (smoothed) */
static uint8_t pads_drop;                /* the drop's crash, due */
static int32_t half_start = -1;          /* HALF TIME: where it began (-1 off) */
static float pitch_x, sweep_x, lofi_x, gate_g = 1.0f;   /* PITCH DOWN, SWEEP (0..1 of the way), LO-FI, the gate */
static float gain_now;

void eng_bind(const track_t *tracks, const master_t *master)
{
    uint32_t f = plat_lock();
    TRK = tracks;
    MST = master;
    changed = 0x3Fu;
    plat_unlock(f);
}
void eng_changed(int machine)
{
    if (machine >= 0 && machine < NMACHINES)
        changed |= (uint8_t)(1u << machine);
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
void eng_play(int on) { play_req = (int8_t)(on ? 1 : 0); }
void eng_hit(int machine, float vel)
{
    uint32_t f;
    if (machine < 0 || machine >= NMACHINES)
        return;
    f = plat_lock();
    hit_vel[machine] = vel;
    hit_req |= (uint8_t)(1u << machine);
    plat_unlock(f);
}
void eng_pads(uint32_t held) { pads = held; }
int eng_step(void) { return step_now; }
float eng_env(int machine) { return machine >= 0 && machine < NMACHINES ? env_out[machine] : 0.0f; }
uint32_t eng_hits(void) { return hits; }
float eng_build(void) { return build_on > 0.0f ? fm_clampf(build_t / fm_maxf(build_len, 1.0f), 0.0f, 1.0f) : 0.0f; }
void eng_mod(int machine, int8_t out[4])
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        out[k] = (int8_t)fm_clampf(T[machine].mod[k], -127.0f, 127.0f);
}

void eng_panic(void)
{
    uint32_t t;
    for (t = 0; t < NMACHINES; t++) {
        T[t].v[0].on = T[t].v[1].on = 0;
        T[t].rt_left = 0;
    }
    step_now = -1;
    play_req = -1;
}

/* ------------------------------------------------------- glide, modulation --- */
/* New parameters while a machine sounds (a knob turned, a preset, an LFO) must not jump: a level, a drive or a filter
 * that changes from one sample to the next is a click. While a voice rings, every coefficient glides to its new value
 * (every 8th sample, about 5 ms to most of the way); a silent machine, and a new trig, take them at once. */
static void glide(float *c, const float *t, uint32_t n)   /* every 8th sample: 1 - (1 - 0.005)^8 of the way */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        c[i] += 0.039f * (t[i] - c[i]);
}
static int arrived(const float *c, const float *t, uint32_t n)   /* within 0.1 % (then it snaps) */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (fm_fabsf(t[i] - c[i]) > 0.001f * fm_fabsf(t[i]) + 1e-9f)
            return 0;
    return 1;
}

static inline int sounding(int t) { return T[t].v[0].on || T[t].v[1].on; }

/* a track's macros now: its own or its trig's p-locks, morphed towards scene B, plus its modulation */
static void effective(int t, float *e)
{
    const track_t *tr = &TRK[t];
    track_state_t *s = &T[t];
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        float v = (s->locked && s->lock[k] != NO_LOCK) ? (float)s->lock[k] : (float)tr->m[k];
        if (MST->has_scene)
            v += scene_x * ((float)MST->scene[t][k] - v);
        if (k == 0)                      /* PITCH DOWN: every machine's TUNE down */
            v -= 40.0f * pitch_x;
        e[k] = fm_clampf(v + s->mod[k], 0.0f, 127.0f);
    }
}

/* the coefficients from the macros now: a new trig snaps to them, a sounding machine glides */
static void retarget(int t, int snap)
{
    track_state_t *s = &T[t];
    float e[4];
    uint32_t k;
    int moved = snap || (changed >> t & 1u);
    effective(t, e);
    for (k = 0; k < 4u; k++)
        if (fm_fabsf(e[k] - s->eff[k]) > 0.02f)
            moved = 1;
    if (!moved)
        return;
    for (k = 0; k < 4u; k++)
        s->eff[k] = e[k];
    coefs(t, e, &s->tgt);
    if (snap || !sounding(t)) {
        s->cur = s->tgt;
        s->gliding = 0;
    } else {
        s->gliding = 1;
    }
}

/* the track's insert, envelope and LFO from its pages: once a block */
static const bp_t SAT_AMT_G[] = {{0, 1.0f}, {35, 2.0f}, {70, 5.0f}, {110, 12.0f}, {127, 16.0f}};
static const bp_t ENV_ATT_S[] = {{0, 0.0005f}, {5, 0.001f}, {45, 0.02f}, {90, 0.15f}, {122, 0.6f}, {127, 0.8f}};
static const bp_t ENV_DEC_S[] = {{0, 0.02f}, {15, 0.03f}, {50, 0.15f}, {85, 0.5f}, {120, 2.0f}, {127, 3.0f}};
static const bp_t LFO_RATE_L2[] = {{0, -3.0f}, {10, -2.0f}, {35, 0.0f}, {60, 2.0f}, {85, 3.0f}, {110, 4.0f}, {127, 5.0f}};
static const bp_t LEVEL_G[] = {{0, 0.0f}, {40, 0.32f}, {80, 0.8f}, {100, 1.0f}, {120, 1.2f}, {127, 1.26f}};
static const bp_t SEND_G[] = {{0, 0.0f}, {30, 0.12f}, {64, 0.35f}, {100, 0.7f}, {127, 1.0f}};
static const float MIX_TRIM[NMACHINES] = {0.85f, 0.70f, 0.75f, 0.60f, 0.55f, 0.65f};   /* each machine at FULL (100) */

static void track_block(int t)
{
    const track_t *tr = &TRK[t];
    track_state_t *s = &T[t];
    float lv, pan, rate, ph, w = 0.0f, d_env, d_lfo, tone;
    int wave, tgt_e, tgt_l;
    uint32_t k;
    /* SAT */
    s->sat_type = nearest(&PAGE_KNOBS[PG_SAT][0], tr->sat[0]);
    s->sat_g = CURVE(SAT_AMT_G, tr->sat[1]);
    s->sat_makeup = 1.0f / (1.0f + 0.30f * fm_log2f(s->sat_g));
    s->sat_mix = tr->sat[1] ? (float)tr->sat[3] / 127.0f : 0.0f;
    tone = tr->sat[2];
    s->sat_lp_a = onepole(tone < 64.0f ? 1500.0f * fm_exp2f(tone / 64.0f * 3.6f) : 18000.0f);
    s->sat_hp_a = onepole(tone > 64.0f ? 20.0f * fm_exp2f((tone - 64.0f) / 63.0f * 5.0f) : 5.0f);
    s->sat_q = 4096.0f - 4000.0f * (float)tr->sat[1] / 127.0f;
    /* MIX: level and pan (equal power), the sends (a throw sends everything) */
    lv = CURVE(LEVEL_G, tr->mix[0]) * MIX_TRIM[t];
    pan = (float)tr->mix[1] / 127.0f;
    s->gl = lv * fm_cosf(pan * 1.5707963f) * 1.4142f;
    s->gr = lv * fm_sinf(pan * 1.5707963f) * 1.4142f;
    s->rev = (pads >> P_RTHROW & 1u) ? 1.0f : CURVE(SEND_G, tr->mix[2]);
    s->dly = (pads >> P_DTHROW & 1u) ? 0.9f : CURVE(SEND_G, tr->mix[3]);
    /* ENV and LFO: their targets and depths (+-64 macro units at the ends; the OFF zone none), the LFO now */
    tgt_e = nearest(&PAGE_KNOBS[PG_ENV][3], tr->env[3]);
    tgt_l = nearest(&PAGE_KNOBS[PG_LFO][3], tr->lfo[3]);
    d_env = (float)tr->env[0] - 64.0f;
    d_lfo = (float)tr->lfo[1] - 64.0f;
    if (fm_fabsf(d_env) < 3.5f)
        d_env = 0.0f;
    if (fm_fabsf(d_lfo) < 3.5f)
        d_lfo = 0.0f;
    s->env_att = 1.0f / (CURVE(ENV_ATT_S, tr->env[1]) * ENG_SR);
    s->env_k = coef(CURVE(ENV_DEC_S, tr->env[2]));
    rate = fm_exp2f(CURVE(LFO_RATE_L2, tr->lfo[0]));   /* cycles a bar */
    ph = bar_pos * rate;
    ph -= fm_floorf(ph);
    wave = nearest(&PAGE_KNOBS[PG_LFO][2], tr->lfo[2]);
    switch (wave) {
    case 0: w = fm_sin_turns(ph); break;
    case 1: w = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; break;
    case 2: w = 2.0f * ph - 1.0f; break;
    case 3: w = ph < 0.5f ? 1.0f : -1.0f; break;
    default:
        if (ph < s->lfo_last)
            s->lfo_sh = white(&seq_noise);
        w = s->lfo_sh;
        break;
    }
    s->lfo_last = ph;
    for (k = 0; k < 4u; k++)
        s->mod[k] = 0.0f;
    if (d_env != 0.0f)
        s->mod[tgt_e] += d_env * s->env;
    if (d_lfo != 0.0f)
        s->mod[tgt_l] += d_lfo * w;
    if (t == M_SNARE && build_on > 0.0f)  /* the build-up tunes the snare up as it rolls */
        s->mod[0] += 30.0f * eng_build();
}

/* the track's insert: saturation (tape, tube, fold, crush) with its tone, mixed with the dry */
static inline float saturate(track_state_t *s, float x)
{
    float y, g;
    if (s->sat_mix <= 0.0f)
        return x;
    g = s->sat_g;
    switch (s->sat_type) {
    case 0: y = fm_tanhf(x * g) * s->sat_makeup * 1.15f; break;
    case 1: y = (x > 0.0f ? fm_tanhf(x * g) : fm_tanhf(x * g * 0.6f) * 1.3f) * s->sat_makeup; break;
    case 2: y = fm_sinf(x * g * 1.5707963f) * fm_minf(1.0f, s->sat_makeup * 1.5f); break;
    default:
        s->crush_n += 1.0f;
        if (s->crush_n >= 1.0f + g * 0.4f) {
            s->crush_n = 0.0f;
            s->crush_s = fm_floorf(x * s->sat_q + 0.5f) / s->sat_q;
        }
        y = s->crush_s;
        break;
    }
    s->sat_lp += s->sat_lp_a * (y - s->sat_lp);
    s->sat_hp += s->sat_hp_a * (s->sat_lp - s->sat_hp);
    y = s->sat_lp - s->sat_hp;
    return x + s->sat_mix * (y - x);
}

/* a machine struck: its envelope restarts, its trig's p-locks (or none) hold till its next trig, the coefficients snap
 * to them, its other voice starts and the one that sounded fades; struck from silence, its gain is its own at once */
static void strike(int t, float vel, const uint8_t *lock)
{
    track_state_t *s = &T[t];
    uint32_t a = s->next, b = a ^ 1u, k;
    int was = sounding(t);
    s->next = (uint8_t)b;
    s->locked = 0;
    if (lock)
        for (k = 0; k < 4u; k++) {
            s->lock[k] = lock[k];
            if (lock[k] != NO_LOCK)
                s->locked = 1;
        }
    s->env_rise = 1.0f;
    retarget(t, 1);
    if (!was)
        s->gain = s->cur.f[0];
    if (s->v[a].on)
        voice_fade(t, &s->v[a], t == M_HATS ? 0.002f : 0.003f);
    voice_start(t, &s->v[b], vel);
}

/* the sequencer's side: a hit for the machine at sample AT of the block, with its trig's locks (0: none). The
 * machines are rendered after the block is sequenced, each on its own (on either core), its hits in order */
#define NEV 8
typedef struct {
    uint16_t at;
    uint8_t has_lock, lock[4];
    float vel;
} ev_t;
static ev_t ev[NMACHINES][NEV];
static uint8_t nev[NMACHINES];
static void push_ev(int t, uint32_t at, float vel, const uint8_t *lock)
{
    ev_t *e;
    uint32_t k;
    if (nev[t] >= NEV)
        return;
    e = &ev[t][nev[t]++];
    e->at = (uint16_t)at;
    e->vel = vel;
    e->has_lock = lock != 0;
    if (lock)
        for (k = 0; k < 4u; k++)
            e->lock[k] = lock[k];
    hits++;
}

/* ---------------------------------------------------------------- the master --- */
static const bp_t COMP_THR[] = {{0, 0.0f}, {35, -12.0f}, {70, -18.0f}, {110, -20.0f}, {127, -24.0f}};
static const bp_t COMP_RATIO[] = {{0, 1.0f}, {35, 2.0f}, {70, 4.0f}, {110, 6.0f}, {127, 8.0f}};
static const bp_t COMP_ATT[] = {{0, 0.010f}, {35, 0.010f}, {70, 0.015f}, {110, 0.001f}, {127, 0.001f}};
static const bp_t COMP_REL[] = {{0, 0.15f}, {35, 0.15f}, {70, 0.08f}, {110, 0.20f}, {127, 0.25f}};
static const bp_t COMP_SC[] = {{0, 0.0f}, {35, 0.0f}, {70, 0.2f}, {110, 0.8f}, {127, 0.9f}};
static const bp_t WARM_G[] = {{0, 1.0f}, {40, 1.8f}, {80, 3.5f}, {120, 7.0f}, {127, 8.0f}};
static const bp_t WARM_LP[] = {{0, 19000.0f}, {40, 14000.0f}, {80, 10000.0f}, {120, 7000.0f}, {127, 6500.0f}};
static const bp_t WARM_CRUSH[] = {{0, 0.0f}, {100, 0.0f}, {120, 0.7f}, {127, 1.0f}};
static const bp_t SPACE_FB[] = {{0, 0.68f}, {20, 0.72f}, {55, 0.82f}, {90, 0.88f}, {120, 0.93f}, {127, 0.94f}};
static const bp_t SPACE_DAMP[] = {{0, 0.5f}, {20, 0.45f}, {55, 0.25f}, {90, 0.35f}, {120, 0.5f}, {127, 0.5f}};
static const bp_t SPACE_PRE[] = {{0, 0.003f}, {20, 0.005f}, {55, 0.015f}, {90, 0.030f}, {120, 0.055f}, {127, 0.06f}};
static const bp_t DLY_FB[] = {{0, 0.1f}, {20, 0.2f}, {55, 0.45f}, {90, 0.65f}, {120, 0.85f}, {127, 0.9f}};
static const bp_t DLY_LP[] = {{0, 1500.0f}, {20, 2000.0f}, {60, 5000.0f}, {105, 12000.0f}, {127, 16000.0f}};
static const bp_t RET_G[] = {{0, 0.0f}, {40, 0.5f}, {80, 1.0f}, {115, 1.5f}, {127, 1.7f}};
static const bp_t DJ_Q[] = {{0, 0.7f}, {20, 0.7f}, {64, 2.0f}, {110, 5.0f}, {127, 6.0f}};

/* a step's length in samples: a sixteenth, the pairs swung (the first longer, the second shorter, up to 2 : 1) */
static float step_base(void) { return ENG_SR * 15.0f / (float)(MST->bpm < 40u ? 40u : MST->bpm); }
static float step_len(int32_t s)
{
    float sw = (float)MST->swing / 100.0f * 0.33f;
    return (s & 1) ? step_base() * (1.0f - sw) : step_base() * (1.0f + sw);
}

static void master_block(float sb)
{
    float comp = MST->fx[0], warm = MST->fx[1], tilt = MST->fx[2], space = MST->fx[3], f, steps;
    /* compressor */
    fx.c_on = comp > 2.0f ? 1.0f : 0.0f;
    fx.c_thr = CURVE(COMP_THR, comp);
    fx.c_ratio = CURVE(COMP_RATIO, comp);
    fx.c_att = coef(CURVE(COMP_ATT, comp));
    fx.c_rel = coef(CURVE(COMP_REL, comp));
    fx.c_sc = CURVE(COMP_SC, comp);
    fx.c_makeup = fm_db2lin(-fx.c_thr * (1.0f - 1.0f / fx.c_ratio) * 0.5f);
    /* warmth */
    fx.w_g = CURVE(WARM_G, warm);
    fx.w_norm = 1.0f / (1.0f + 0.35f * fm_log2f(fx.w_g));
    fx.w_lp_a = onepole(CURVE(WARM_LP, warm));
    fx.w_crush = CURVE(WARM_CRUSH, warm);
    /* tilt: below the middle the lows up and the highs down, above it the other way */
    f = (tilt - 64.0f) / 64.0f;
    fx.t_lo = 1.0f - 0.5f * f;
    fx.t_hi = 1.0f + 0.6f * f;
    fx.t_lp_a = onepole(700.0f);
    /* reverb */
    fx.fb = CURVE(SPACE_FB, space);
    fx.damp = CURVE(SPACE_DAMP, space);
    fx.pre = CURVE(SPACE_PRE, space) * ENG_SR;
    /* delay: its time in steps (1, 2, 3, 4), halved till the line holds it; it glides there (tape) */
    steps = (float)(nearest(&PAGE_KNOBS[PG_FX2][0], MST->fx2[0]) + 1);
    while (steps * sb > (float)(DL_N - 4u) && steps > 0.5f)
        steps *= 0.5f;
    fx.dtime_t = steps * sb;
    fx.dfb = CURVE(DLY_FB, MST->fx2[1]);
    if (pads >> P_DTHROW & 1u)
        fx.dfb = fm_minf(fx.dfb + 0.15f, 0.92f);
    fx.dlp_a = onepole(CURVE(DLY_LP, MST->fx2[2]));
    fx.dhp_a = onepole(150.0f);
    fx.dret = CURVE(RET_G, MST->fx2[3]);
    /* the DJ filter: below the middle a low-pass, above it a high-pass, the middle open */
    {
        float v = MST->glo[0], q = CURVE(DJ_Q, MST->glo[3]);
        fx.dj_on = 1.0f;
        if (v < 60.0f) {
            fx.dj_mode = 0.0f;
            svf_set(&fx.dj, 20000.0f * fm_exp2f(-(60.0f - v) / 60.0f * 7.0f), q);
        } else if (v > 68.0f) {
            fx.dj_mode = 1.0f;
            svf_set(&fx.dj, 20.0f * fm_exp2f((v - 68.0f) / 59.0f * 8.0f), q);
        } else {
            fx.dj_on = 0.0f;
        }
    }
    /* the kills' targets (they glide per sample) */
    fx.t_glo = (pads >> P_KLOW & 1u) ? 0.0f : 1.0f;
    fx.t_gmid = (pads >> P_KMID & 1u) ? 0.0f : 1.0f;
    fx.t_ghi = (pads >> P_KHIGH & 1u) ? 0.0f : 1.0f;
    /* build-up: a high-pass rising, a little louder */
    if (build_on > 0.0f) {
        float p = eng_build();
        fx.b_hp_a = onepole(20.0f * fm_exp2f(p * 7.2f));
        fx.b_gain = 1.0f + 0.4f * p;
    } else {
        fx.b_hp_a = 0.0f;
        fx.b_gain = 1.0f;
    }
}

static inline float comb(float *buf, uint32_t n, uint32_t *i, float *f, float x)
{
    float y = buf[*i];
    *f = y + fx.damp * (*f - y);
    buf[*i] = x + *f * fx.fb;
    if (++*i >= n)
        *i = 0;
    return y;
}
static inline float allpass(float *buf, uint32_t n, uint32_t *i, float x)
{
    float b = buf[*i], y = b - x;
    buf[*i] = x + b * 0.5f;
    if (++*i >= n)
        *i = 0;
    return y;
}

/* ------------------------------------------------------------- the sequencer --- */
static int cond_ok(int c)
{
    switch (c) {
    case C_1_2: return (loops & 1u) == 0;
    case C_2_2: return (loops & 1u) == 1;
    case C_1_4: return (loops & 3u) == 0;
    case C_2_4: return (loops & 3u) == 1;
    case C_3_4: return (loops & 3u) == 2;
    case C_4_4: return (loops & 3u) == 3;
    case C_P50: return rnd01() < 0.5f;
    case C_P25: return rnd01() < 0.25f;
    case C_P75: return rnd01() < 0.75f;
    case C_FILL: return (pads >> P_FILL & 1u) != 0;
    case C_NOFILL: return !(pads >> P_FILL & 1u);
    case C_FIRST: return loops == 0;
    default: return 1;
    }
}

static void play_step(int32_t real, uint32_t at)
{
    int32_t len = MST->length == 32u ? 32 : 16, src = real;
    uint32_t t, bit;
    step_now = real;
    if (rep_start >= 0) {                /* beat repeat: the window's steps, round */
        src = rep_start + (real - rep_start + len) % len % rep_len;
    } else if (half_start >= 0) {        /* half time: every step twice as long (the odd ones rest) */
        int32_t d = (real - half_start + len) % len;
        if (d & 1)
            return;
        src = half_start + d / 2;
    }
    src %= len;
    bit = 1u << src;
    for (t = 0; t < NMACHINES; t++) {
        const track_t *tr = &TRK[t];
        track_state_t *s = &T[t];
        uint32_t n;
        if (!(tr->steps & bit) || (muted >> t & 1u) || (t == M_KICK && (pads >> P_BREAK & 1u)))
            continue;
        if (t == M_SNARE && build_on > 0.0f)   /* the build-up's roll has the snare */
            continue;
        if (!cond_ok(tr->cond[src]))
            continue;
        s->rt_vel = (tr->acc & bit) ? 1.0f : 0.72f;
        push_ev((int)t, at, s->rt_vel, tr->lock[src]);
        for (n = 0; n < 4u; n++)
            s->seq_lock[n] = tr->lock[src][n];
        s->seq_locked = 1;
        n = tr->retrig[src] < 2u ? 1u : tr->retrig[src];
        if (rep_rt && n < 2u)
            n = 2;
        s->rt_left = (uint8_t)(n - 1u);
        s->rt_period = step_len(real) / (float)n;
        s->rt_count = s->rt_period;
    }
}

/* the pads: what changed since the last block */
static void pads_block(void)
{
    uint32_t p = pads, on = p & ~pads_was, off = pads_was & ~p;
    int32_t len = MST->length == 32u ? 32 : 16;
    static const uint8_t REP[4] = {4, 2, 1, 1};
    int r;
    /* beat repeat: the shortest held wins; its window starts on the grid where it was pressed */
    for (r = 3; r >= 0; r--)
        if (p >> (P_REP4 + r) & 1u)
            break;
    if (r >= 0 && step_now >= 0) {
        int32_t l = REP[r];
        if (rep_start < 0 || l != rep_len || (on >> (P_REP4 + r) & 1u)) {
            int32_t cur = step_now;
            rep_len = l;
            rep_start = (cur - cur % l + len) % len;
        }
        rep_rt = r == 3;
    } else {
        rep_start = -1;
        rep_rt = 0;
    }
    /* build-up: held, it runs; let go (or DROP): the drop */
    if (on >> P_BUILD & 1u) {
        static const float BARS[3] = {1.0f, 2.0f, 4.0f};
        build_on = 1.0f;
        build_t = 0.0f;
        build_roll = 0.0f;
        build_len = BARS[nearest(&PAGE_KNOBS[PG_GLO][2], MST->glo[2])] * 16.0f * step_base();
    }
    if (((off >> P_BUILD & 1u) && build_on > 0.0f) || (on >> P_DROP & 1u)) {
        build_on = 0.0f;
        if (step_now >= 0) {             /* from the top, now, with a crash */
            step_pos = 0;
            step_left = 0.0f;
            loops = 0;
            pads_drop = 1;
        }
    }
    if (on >> P_TAPE & 1u)               /* tape stop: held, it winds down */
        fx.t_speed = 1.0f;
    if (!(p >> P_HALF & 1u))
        half_start = -1;
    else if (on >> P_HALF & 1u)
        half_start = step_now >= 0 ? step_now : 0;
    pitch_x += ((float)(p >> P_PITCH & 1u) - pitch_x) * 0.15f;
    pads_was = p;
}

/* =============================================================== the render === */
void eng_init(void)
{
    uint32_t t, k;
    for (t = 0; t < NMACHINES; t++) {
        for (k = 0; k < 4u; k++) {
            track0[t].m[k] = MACHINES[t].preset[0].m[k];
            track0[t].sat[k] = PAGE_DEFAULT[PG_SAT][k];
            track0[t].env[k] = PAGE_DEFAULT[PG_ENV][k];
            track0[t].lfo[k] = PAGE_DEFAULT[PG_LFO][k];
            track0[t].mix[k] = PAGE_DEFAULT[PG_MIX][k];
        }
        for (k = 0; k < NSTEPS; k++)
            track0[t].lock[k][0] = track0[t].lock[k][1] = track0[t].lock[k][2] = track0[t].lock[k][3] = NO_LOCK;
    }
    TRK = track0;
    MST = &master0;
    for (t = 0; t < NMACHINES; t++) {
        retarget((int)t, 1);
        T[t].gain = T[t].cur.f[0];
        T[t].noise = 0x2545F491u + 0x9E3779B9u * (t + 1u);
    }
    changed = 0x3Fu;
    fx.g_lo = fx.g_mid = fx.g_hi = fx.t_glo = fx.t_gmid = fx.t_ghi = 1.0f;
    fx.t_speed = fx.t_in = 1.0f;
    fx.dtime = 0.0f;
}

/* a machine's block: its hits at their samples, its voices, its envelope, its insert, into its own buffer */
#define BLK_MAX 256u
static float mbuf[NMACHINES][BLK_MAX];
static uint32_t blk_n;

static void render_machine(int t)
{
    track_state_t *s = &T[t];
    float *out = mbuf[t], pk = 0.0f;
    uint32_t i, e = 0, n = blk_n;
    for (i = 0; i < n; i++) {
        float y = 0.0f, a;
        while (e < nev[t] && ev[t][e].at <= i) {
            strike(t, ev[t][e].vel, ev[t][e].has_lock ? ev[t][e].lock : 0);
            e++;
        }
        if (s->env_rise > 0.0f) {        /* the envelope: up over its attack, then down */
            s->env += s->env_att;
            if (s->env >= 1.0f) {
                s->env = 1.0f;
                s->env_rise = 0.0f;
            }
        } else {
            s->env *= s->env_k;
        }
        if (!s->v[0].on && !s->v[1].on) {
            out[i] = 0.0f;
            continue;
        }
        if (s->gliding && !(i & 7u))
            glide(s->cur.f, s->tgt.f, COEF_N[t]);
        if (s->v[0].on)
            y += voice_sample(t, &s->v[0], &s->cur, &s->noise);
        if (s->v[1].on)
            y += voice_sample(t, &s->v[1], &s->cur, &s->noise);
        s->gain += (s->cur.f[0] - s->gain) * 0.005f;
        y = saturate(s, y * s->gain);
        a = fm_fabsf(y);
        if (a > pk)
            pk = a;
        out[i] = y;
    }
    if (s->gliding && (!sounding(t) || arrived(s->cur.f, s->tgt.f, COEF_N[t]))) {
        s->cur = s->tgt;
        s->gliding = 0;
    }
    s->peak = pk;
}

/* the second core's share: TOMS, METAL, HATS, CLAP (the first renders KICK, SNARE and the master: about even) */
static void job_machines(uint32_t arg)
{
    (void)arg;
    render_machine(M_TOMS);
    render_machine(M_METAL);
    render_machine(M_HATS);
    render_machine(M_CLAP);
}

/* the sequencer over the block: where each machine is struck */
static void sequence(uint32_t n, float sb)
{
    uint32_t i, t;
    for (t = 0; t < NMACHINES; t++)
        nev[t] = 0;
    if (pads_drop) {                     /* DROP (or a build-up let go): the crash, at the block's start */
        pads_drop = 0;
        push_ev(M_METAL, 0, 1.0f, 0);
    }
    if (hit_req) {
        for (t = 0; t < NMACHINES; t++)
            if (hit_req >> t & 1u)
                push_ev((int)t, 0, hit_vel[t], 0);
        hit_req = 0;
    }
    for (i = 0; i < n; i++) {
        bar_pos += 1.0f / (16.0f * sb);
        if (bar_pos > 4096.0f)
            bar_pos -= 4096.0f;
        if (step_now < 0)
            continue;
        step_left -= 1.0f;
        if (step_left <= 0.0f) {
            int32_t len = MST->length == 32u ? 32 : 16;
            if (step_pos >= len)
                step_pos = 0;
            play_step(step_pos, i);
            step_left += step_len(step_pos);
            if (++step_pos >= len) {
                step_pos = 0;
                loops++;
            }
        }
        for (t = 0; t < NMACHINES; t++)  /* retrigs: the rest of the step's hits, a little softer each */
            if (T[t].rt_left) {
                T[t].rt_count -= 1.0f;
                if (T[t].rt_count <= 0.0f) {
                    T[t].rt_count += T[t].rt_period;
                    T[t].rt_left--;
                    T[t].rt_vel *= 0.85f;
                    push_ev((int)t, i, T[t].rt_vel, T[t].seq_locked ? T[t].seq_lock : 0);
                }
            }
        if (build_on > 0.0f) {           /* the build-up's snare roll: eighths, sixteenths, thirty-seconds */
            float p = eng_build(), every = p < 0.5f ? 2.0f : p < 0.75f ? 1.0f : 0.5f;
            build_t += 1.0f;
            build_roll -= 1.0f;
            if (build_roll <= 0.0f) {
                build_roll += every * sb;
                push_ev(M_SNARE, i, 0.5f + 0.5f * p, 0);
            }
        }
    }
}

void eng_render(int32_t *lr, uint32_t n, float gain)
{
    uint32_t i, t;
    float sb = step_base(), x, bar0, binc;
    int on2, gate = (pads >> P_GATE & 1u) != 0, lofi = (pads >> P_LOFI & 1u) != 0;
    if (n > BLK_MAX)
        n = BLK_MAX;
    blk_n = n;
    if (play_req >= 0) {
        if (play_req) {
            step_pos = 0;
            step_left = 0.0f;
            step_now = 0;
            loops = 0;
            bar_pos = 0.0f;
        } else {
            step_now = -1;
            rep_start = -1;
            build_on = 0.0f;
            for (t = 0; t < NMACHINES; t++)
                T[t].rt_left = 0;
        }
        play_req = -1;
    }
    pads_block();
    /* SWEEP: held, the low-pass closes over a bar; let go, it opens in a quarter */
    if (pads >> P_SWEEP & 1u)
        sweep_x = fm_minf(sweep_x + (float)n / (16.0f * sb), 1.0f);
    else
        sweep_x = fm_maxf(sweep_x - (float)n / (4.0f * sb), 0.0f);
    if (sweep_x > 0.0f)
        svf_set(&fx.sw, 18000.0f * fm_exp2f(-sweep_x * 6.0f), 1.6f);
    /* the scene morph: the knob, or B while its pad is held (smoothed) */
    scene_x += (((pads >> P_SCENE & 1u) ? 1.0f : (float)MST->glo[1] / 127.0f) - scene_x) * 0.3f;
    master_block(sb);
    for (t = 0; t < NMACHINES; t++) {
        track_block((int)t);
        retarget((int)t, 0);
    }
    changed = 0;
    bar0 = bar_pos;
    binc = 1.0f / (16.0f * sb);
    sequence(n, sb);
    /* the machines: half on the second core, half here; if it cannot, all here */
    on2 = plat_cpu2_on() && plat_cpu2_run(job_machines, 0);
    render_machine(M_KICK);
    render_machine(M_SNARE);
    if (!on2 || plat_cpu2_wait() != 0)
        job_machines(0);
    for (t = 0; t < NMACHINES; t++)
        env_out[t] = T[t].peak > env_out[t] ? T[t].peak : env_out[t] * 0.85f;
    /* the mix and the master */
    for (i = 0; i < n; i++) {
        float L = 0.0f, R = 0.0f, rv = 0.0f, dl = 0.0f, kick = mbuf[M_KICK][i];
        for (t = 0; t < NMACHINES; t++) {
            float y = mbuf[t][i];
            if (y == 0.0f)
                continue;
            L += y * T[t].gl;
            R += y * T[t].gr;
            rv += y * T[t].rev;
            dl += y * T[t].dly;
        }
        /* the reverb: pre-delay, four combs, two all-passes a side */
        {
            float in, c0, c1, c2, c3;
            uint32_t pr = (fx.pi + PRE_N - (uint32_t)fx.pre) % PRE_N;
            rv_pre[fx.pi] = rv * 0.25f;
            in = rv_pre[pr];
            if (++fx.pi >= PRE_N)
                fx.pi = 0;
            c0 = comb(rv_comb0, 1116u, &fx.ci[0], &fx.cf[0], in);
            c1 = comb(rv_comb1, 1188u, &fx.ci[1], &fx.cf[1], in);
            c2 = comb(rv_comb2, 1277u, &fx.ci[2], &fx.cf[2], in);
            c3 = comb(rv_comb3, 1356u, &fx.ci[3], &fx.cf[3], in);
            L += allpass(rv_ap1, 441u, &fx.ai[1], allpass(rv_ap0, 556u, &fx.ai[0], c0 + c2 - 0.3f * c1)) * fx.dret;
            R += allpass(rv_ap3, 464u, &fx.ai[3], allpass(rv_ap2, 579u, &fx.ai[2], c1 + c3 - 0.3f * c2)) * fx.dret;
        }
        /* the ping-pong delay (FREEZE: no input, the loop holds) */
        {
            float pos, fr, ol, orr, fb, xin;
            uint32_t r0, r1;
            int fz = (pads >> P_FREEZE & 1u) != 0;
            fx.dtime += (fx.dtime_t - fx.dtime) * 0.0004f;
            pos = (float)fx.di - fx.dtime;
            if (pos < 0.0f)
                pos += (float)DL_N;
            r0 = (uint32_t)pos;
            fr = pos - (float)r0;
            r1 = r0 + 1u >= DL_N ? 0u : r0 + 1u;
            ol = ((float)dl_buf[0][r0] + fr * (float)(dl_buf[0][r1] - dl_buf[0][r0])) * (1.0f / 32768.0f);
            orr = ((float)dl_buf[1][r0] + fr * (float)(dl_buf[1][r1] - dl_buf[1][r0])) * (1.0f / 32768.0f);
            fb = fz ? 1.0f : fx.dfb;
            xin = fz ? 0.0f : dl;
            fx.dlp[0] += fx.dlp_a * (orr - fx.dlp[0]);   /* the tone in the loop */
            fx.dhp[0] += fx.dhp_a * (fx.dlp[0] - fx.dhp[0]);
            fx.dlp[1] += fx.dlp_a * (ol - fx.dlp[1]);
            fx.dhp[1] += fx.dhp_a * (fx.dlp[1] - fx.dhp[1]);
            dl_buf[0][fx.di] = (int16_t)(fm_tanhf(xin + fb * (fx.dlp[0] - fx.dhp[0])) * 32767.0f);
            dl_buf[1][fx.di] = (int16_t)(fm_tanhf(fb * (fx.dlp[1] - fx.dhp[1])) * 32767.0f);
            if (++fx.di >= DL_N)
                fx.di = 0;
            L += ol * fx.dret * 0.8f;
            R += orr * fx.dret * 0.8f;
        }
        /* tilt */
        if (MST->fx[2] != 64u) {
            fx.t_lp[0] += fx.t_lp_a * (L - fx.t_lp[0]);
            fx.t_lp[1] += fx.t_lp_a * (R - fx.t_lp[1]);
            L = fx.t_lp[0] * fx.t_lo + (L - fx.t_lp[0]) * fx.t_hi;
            R = fx.t_lp[1] * fx.t_lo + (R - fx.t_lp[1]) * fx.t_hi;
        } else {
            fx.t_lp[0] = L;
            fx.t_lp[1] = R;
        }
        /* the compressor: PUMP ducks all but the kick under the kick, then a glue on the sum */
        if (fx.c_on > 0.0f) {
            float d, lvl, over, gr, kl = kick * T[M_KICK].gl, kr = kick * T[M_KICK].gr;
            fx.c_kick += (fm_fabsf(kick) - fx.c_kick) * (fm_fabsf(kick) > fx.c_kick ? 0.05f : 0.0004f);
            d = 1.0f - fx.c_sc * fm_clampf(fx.c_kick * 2.0f, 0.0f, 1.0f);
            L = kl + (L - kl) * d;
            R = kr + (R - kr) * d;
            lvl = fm_maxf(fm_fabsf(L), fm_fabsf(R));
            fx.c_env = lvl > fx.c_env ? lvl + fx.c_att * (fx.c_env - lvl) : lvl + fx.c_rel * (fx.c_env - lvl);
            over = 6.0206f * fm_log2f(fm_maxf(fx.c_env, 1e-5f)) - fx.c_thr;
            gr = over > 0.0f ? fm_db2lin(-over * (1.0f - 1.0f / fx.c_ratio)) : 1.0f;
            L *= gr * fx.c_makeup;
            R *= gr * fx.c_makeup;
        }
        /* warmth */
        if (fx.w_g > 1.01f) {
            fx.w_lp[0] += fx.w_lp_a * (fm_tanhf(L * fx.w_g) * fx.w_norm - fx.w_lp[0]);
            fx.w_lp[1] += fx.w_lp_a * (fm_tanhf(R * fx.w_g) * fx.w_norm - fx.w_lp[1]);
            L = fx.w_lp[0];
            R = fx.w_lp[1];
            if (fx.w_crush > 0.0f) {
                fx.w_n += 1.0f;
                if (fx.w_n >= 1.0f + 5.0f * fx.w_crush) {
                    float q = 1024.0f - 1000.0f * fx.w_crush;
                    fx.w_n = 0.0f;
                    fx.w_s[0] = fm_floorf(L * q + 0.5f) / q;
                    fx.w_s[1] = fm_floorf(R * q + 0.5f) / q;
                }
                L = fx.w_s[0];
                R = fx.w_s[1];
            }
        }
        /* the DJ filter */
        if (fx.dj_on > 0.0f) {
            float lp, bp, hp;
            hp = svf_run(&fx.dj, &fx.djs[0], L, &lp, &bp);
            L = fx.dj_mode > 0.5f ? hp : lp;
            hp = svf_run(&fx.dj, &fx.djs[1], R, &lp, &bp);
            R = fx.dj_mode > 0.5f ? hp : lp;
        }
        /* the kills: the lows under ~250 Hz, the highs over ~2.5 kHz, the mids between */
        fx.g_lo += (fx.t_glo - fx.g_lo) * 0.003f;
        fx.g_mid += (fx.t_gmid - fx.g_mid) * 0.003f;
        fx.g_hi += (fx.t_ghi - fx.g_hi) * 0.003f;
        fx.i_lo[0] += 0.035f * (L - fx.i_lo[0]);
        fx.i_mid[0] += 0.30f * (L - fx.i_mid[0]);
        fx.i_lo[1] += 0.035f * (R - fx.i_lo[1]);
        fx.i_mid[1] += 0.30f * (R - fx.i_mid[1]);
        if (fx.g_lo < 0.9999f || fx.g_mid < 0.9999f || fx.g_hi < 0.9999f) {
            L = fx.i_lo[0] * fx.g_lo + (fx.i_mid[0] - fx.i_lo[0]) * fx.g_mid + (L - fx.i_mid[0]) * fx.g_hi;
            R = fx.i_lo[1] * fx.g_lo + (fx.i_mid[1] - fx.i_lo[1]) * fx.g_mid + (R - fx.i_mid[1]) * fx.g_hi;
        }
        /* SWEEP */
        if (sweep_x > 0.0f) {
            float lp, bp;
            svf_run(&fx.sw, &fx.sws[0], L, &lp, &bp);
            L = lp;
            svf_run(&fx.sw, &fx.sws[1], R, &lp, &bp);
            R = lp;
        }
        /* LO-FI: a quarter of the rate, 6 bits, a low-pass; in and out over a few ms */
        lofi_x += ((float)lofi - lofi_x) * 0.004f;
        if (lofi_x > 0.001f) {
            float a = 0.5f, l, r;
            fx.lf_n += 1.0f;
            if (fx.lf_n >= 4.0f) {
                fx.lf_n = 0.0f;
                fx.lf_s[0] = fm_floorf(L * 32.0f + 0.5f) * (1.0f / 32.0f);
                fx.lf_s[1] = fm_floorf(R * 32.0f + 0.5f) * (1.0f / 32.0f);
            }
            fx.lf_lp[0] += a * (fx.lf_s[0] - fx.lf_lp[0]);
            fx.lf_lp[1] += a * (fx.lf_s[1] - fx.lf_lp[1]);
            l = fx.lf_lp[0];
            r = fx.lf_lp[1];
            L += lofi_x * (l - L);
            R += lofi_x * (r - R);
        }
        /* GATE: open the first half of each thirty-second, on the bar's grid */
        {
            float ph = (bar0 + (float)i * binc) * 32.0f, tgt;
            ph -= fm_floorf(ph);
            tgt = (!gate || ph < 0.5f) ? 1.0f : 0.0f;
            gate_g += (tgt - gate_g) * 0.08f;
            L *= gate_g;
            R *= gate_g;
        }
        /* build-up: the high-pass */
        if (fx.b_hp_a > 0.0f) {
            fx.b_hp[0] += fx.b_hp_a * (L - fx.b_hp[0]);
            fx.b_hp[1] += fx.b_hp_a * (R - fx.b_hp[1]);
            L = (L - fx.b_hp[0]) * fx.b_gain;
            R = (R - fx.b_hp[1]) * fx.b_gain;
        } else {
            fx.b_hp[0] = fx.b_hp[1] = 0.0f;
        }
        /* tape stop: the sound written to a line and read back slower and slower; let go, it comes back in */
        tp_buf[0][fx.ti] = (int16_t)(fm_clampf(L, -1.0f, 1.0f) * 32767.0f);
        tp_buf[1][fx.ti] = (int16_t)(fm_clampf(R, -1.0f, 1.0f) * 32767.0f);
        if (pads >> P_TAPE & 1u) {
            float pos, fr, g;
            uint32_t r0, r1;
            fx.t_speed = fm_maxf(fx.t_speed - 1.0f / (0.6f * ENG_SR), 0.0f);
            fx.t_lag = fm_minf(fx.t_lag + 1.0f - fx.t_speed, (float)(TP_N - 4u));
            pos = (float)fx.ti - fx.t_lag;
            if (pos < 0.0f)
                pos += (float)TP_N;
            r0 = (uint32_t)pos;
            fr = pos - (float)r0;
            r1 = r0 + 1u >= TP_N ? 0u : r0 + 1u;
            g = fm_minf(1.0f, fx.t_speed * 4.0f) * (1.0f / 32768.0f);
            L = ((float)tp_buf[0][r0] + fr * (float)(tp_buf[0][r1] - tp_buf[0][r0])) * g;
            R = ((float)tp_buf[1][r0] + fr * (float)(tp_buf[1][r1] - tp_buf[1][r0])) * g;
            fx.t_in = 0.0f;
            fx.t_in_lp[0] = fx.t_in_lp[1] = 0.0f;
        } else if (fx.t_in < 1.0f) {     /* back: from silence, the sound opens up in 150 ms */
            float a;
            fx.t_lag = 0.0f;
            fx.t_in = fm_minf(fx.t_in + 1.0f / (0.15f * ENG_SR), 1.0f);
            a = onepole(200.0f * fm_exp2f(fx.t_in * 6.6f));
            fx.t_in_lp[0] += a * (L - fx.t_in_lp[0]);
            fx.t_in_lp[1] += a * (R - fx.t_in_lp[1]);
            L = fx.t_in_lp[0] * fx.t_in;
            R = fx.t_in_lp[1] * fx.t_in;
        }
        if (++fx.ti >= TP_N)
            fx.ti = 0;
        /* the limiter: clean below 0.6, rounded off above, never over full scale */
        if (fm_fabsf(L) > 0.6f) {
            x = 0.6f + 0.4f * fm_tanhf((fm_fabsf(L) - 0.6f) * 2.5f);
            L = L > 0.0f ? x : -x;
        }
        if (fm_fabsf(R) > 0.6f) {
            x = 0.6f + 0.4f * fm_tanhf((fm_fabsf(R) - 0.6f) * 2.5f);
            R = R > 0.0f ? x : -x;
        }
        gain_now += (gain - gain_now) * 0.004f;
        lr[2 * i] = (int32_t)(L * gain_now);
        lr[2 * i + 1] = (int32_t)(R * gain_now);
    }
}

