/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES' sound engine, the SNARE machine. Part of dsp/engine.c (included there, in order: the device builds the engine
 * as one unit). */
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
