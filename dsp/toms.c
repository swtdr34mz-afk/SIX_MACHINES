/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES' sound engine, the TOMS machine. Part of dsp/engine.c (included there, in order: the device builds the engine
 * as one unit). */
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
