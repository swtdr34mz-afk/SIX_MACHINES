/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES' sound engine, the FM machine: an FM drum (as the DrumBrute Impact's) that turns into FM metal (the
 * cymbals). Part of dsp/engine.c (included there, in order: the device builds the engine as one unit). */
/* ======================================================================= FM === */
/* The FM drum: two operators, a carrier at PITCH and a modulator at MOD times it (sweetspots on the ratios that ring:
 * 1:1, the metallic 1.41, 2:1, a bell's 3.5; between them it detunes into the inharmonic), AMOUNT the modulation's
 * index (its top feeds the modulator back on itself: noise), DECAY its length; a short DECAY comes with a pitch drop
 * (the Impact's COLOR): the blip and zap of minimal. The index falls faster than the sound: bright at the hit.
 * Past BELL, MOD fades into the METAL: three FM pairs at a cymbal's inharmonic ratios (RIDE, CRASH, INDUSTRY), a ring
 * modulation of two of them, noise and a resonant high-pass; there AMOUNT is their shimmer. */
static const bp_t F_PITCH_HZ[] = {{0, 30.0f}, {15, 45.0f}, {40, 110.0f}, {70, 330.0f}, {100, 1000.0f}, {127, 2500.0f}};
static const bp_t F_RATIO[] = {{0, 0.5f}, {10, 1.0f}, {30, 1.41f}, {50, 2.0f}, {68, 3.5f}, {127, 3.5f}};
static const bp_t F_METAL[] = {{0, 0.0f}, {68, 0.0f}, {88, 1.0f}, {127, 1.0f}};   /* BELL .. RIDE: the FM fades into the metal */
static const bp_t F_INDEX[] = {{0, 0.0f}, {35, 1.5f}, {70, 4.0f}, {105, 7.0f}, {127, 9.0f}};
static const bp_t F_FEEDBACK[] = {{0, 0.0f}, {70, 0.0f}, {105, 0.6f}, {127, 1.2f}};
static const bp_t F_DECAY_S[] = {{0, 0.02f}, {10, 0.03f}, {40, 0.10f}, {75, 0.35f}, {115, 1.2f}, {127, 2.0f}};
static const bp_t F_DROP_OCT[] = {{0, 2.5f}, {10, 2.0f}, {40, 1.0f}, {75, 0.25f}, {115, 0.0f}, {127, 0.0f}};
static const bp_t F_DROP_S[] = {{0, 0.010f}, {10, 0.012f}, {40, 0.025f}, {75, 0.040f}, {127, 0.050f}};
/* the metal's ratio sets: RIDE, CRASH, INDUSTRY */
static const bp_t X_C0[] = {{88, 1.0f}, {106, 1.0f}, {124, 1.0f}};
static const bp_t X_C1[] = {{88, 2.13f}, {106, 1.53f}, {124, 1.9f}};
static const bp_t X_C2[] = {{88, 3.7f}, {106, 2.89f}, {124, 3.3f}};
static const bp_t X_M0[] = {{88, 1.41f}, {106, 2.71f}, {124, 1.13f}};
static const bp_t X_M1[] = {{88, 3.17f}, {106, 4.37f}, {124, 7.3f}};
static const bp_t X_M2[] = {{88, 5.1f}, {106, 6.13f}, {124, 0.53f}};
static const bp_t X_IDX[] = {{88, 1.8f}, {106, 3.0f}, {124, 4.0f}};
static const bp_t X_HP[] = {{0, 600.0f}, {35, 1500.0f}, {70, 3000.0f}, {105, 6000.0f}, {127, 8000.0f}};
static const bp_t X_NOISE[] = {{0, 0.0f}, {35, 0.04f}, {70, 0.12f}, {105, 0.30f}, {127, 0.40f}};
static const bp_t X_RINGMOD[] = {{0, 0.0f}, {70, 0.0f}, {105, 0.4f}, {127, 0.6f}};
/* the levels that keep MOD and AMOUNT changes of character (measured: the first 150 ms; the metal ~4 dB under the
 * drum, as cymbals sit in a mix) */
static const bp_t F_MOD_G[] = {{0, 0.84f}, {9, 0.82f}, {18, 0.82f}, {27, 0.82f}, {36, 0.82f}, {45, 0.82f}, {54, 0.82f}, {63, 0.82f}, {72, 0.93f}, {81, 1.14f}, {90, 0.80f}, {99, 0.69f}, {108, 0.70f}, {117, 0.76f}, {126, 0.75f}};
static const bp_t F_MAKEUP[] = {{0, 0.92f}, {9, 0.93f}, {18, 0.97f}, {27, 1.02f}, {36, 1.07f}, {45, 1.10f}, {54, 1.06f}, {63, 1.00f}, {72, 0.98f}, {81, 0.98f}, {90, 1.00f}, {99, 1.00f}, {108, 0.99f}, {117, 0.99f}, {126, 0.99f}};

/* in the metal, AMOUNT (its shimmer) thins the sound: this keeps it as loud (measured, at RIDE) */
static const bp_t X_AMT_G[] = {{0, 0.96f}, {9, 0.98f}, {18, 1.0f}, {36, 1.0f}, {45, 1.05f}, {54, 1.17f}, {63, 1.33f},
                               {72, 1.5f}, {81, 1.76f}, {90, 1.91f}, {99, 1.89f}, {108, 1.91f}, {117, 1.8f}, {127, 1.72f}};

typedef struct {
    float makeup, hz, ratio, idx, fb, k_amp, k_idx, drop, k_drop, wm;
    float ic[3], im[3], midx, noise, ring;
    svf_c hp;
} fm_coef_t;
typedef struct {
    uint8_t on;
    float pc, pm, prev, e_amp, e_idx, e_drop, vel, fade, k_fade;
    float xc[3], xm[3];
    svf_s hp;
} fm_voice_t;

static void fm_coefs(const float *m, fm_coef_t *c)
{
    float d = CURVE(F_DECAY_S, m[3]), mod = m[1], amt = m[2], mhz;
    c->hz = CURVE(F_PITCH_HZ, m[0]);
    c->ratio = CURVE(F_RATIO, mod);
    c->wm = CURVE(F_METAL, mod);
    c->idx = CURVE(F_INDEX, amt);
    c->fb = CURVE(F_FEEDBACK, amt);
    c->k_amp = coef(d);
    c->k_idx = coef(d * 0.5f);
    c->drop = CURVE(F_DROP_OCT, m[3]) * (1.0f - c->wm);   /* the metal does not drop */
    c->k_drop = coef(CURVE(F_DROP_S, m[3]));
    mhz = c->hz * 2.7f;                                    /* the metal sits higher than the drum */
    c->ic[0] = mhz * CURVE(X_C0, mod) / ENG_SR;
    c->ic[1] = mhz * CURVE(X_C1, mod) / ENG_SR;
    c->ic[2] = mhz * CURVE(X_C2, mod) / ENG_SR;
    c->im[0] = mhz * CURVE(X_M0, mod) / ENG_SR;
    c->im[1] = mhz * CURVE(X_M1, mod) / ENG_SR;
    c->im[2] = mhz * CURVE(X_M2, mod) / ENG_SR;
    c->midx = CURVE(X_IDX, mod);
    c->noise = CURVE(X_NOISE, amt);
    c->ring = CURVE(X_RINGMOD, amt);
    svf_set(&c->hp, CURVE(X_HP, amt) * fm_sqrtf(mhz / 900.0f), 0.9f);
    c->makeup = CURVE(F_MAKEUP, amt) * CURVE(F_MOD_G, mod) * (1.0f + c->wm * (CURVE(X_AMT_G, amt) - 1.0f));
}

static void fm_start(fm_voice_t *v, float vel)
{
    v->on = 1;
    v->pc = v->pm = v->prev = 0.0f;            /* the drum from zero every time; the metal's phases run on (its grain) */
    v->e_amp = v->e_idx = v->e_drop = 1.0f;
    v->vel = vel;
    v->fade = v->k_fade = 1.0f;
}

static float fm_sample(fm_voice_t *v, const fm_coef_t *c, uint32_t *ns)
{
    float x = 0.0f;
    if (c->wm < 1.0f) {                        /* the drum */
        float f = c->hz * fm_exp2f(c->drop * v->e_drop), mod;
        v->pm += f * c->ratio * (1.0f / ENG_SR);
        v->pm -= fm_floorf(v->pm);
        v->pc += f * (1.0f / ENG_SR);
        if (v->pc >= 1.0f)
            v->pc -= 1.0f;
        mod = fm_sin_turns(v->pm + c->fb * v->prev * 0.159f);
        v->prev = mod;
        x = fm_sin_turns(v->pc + c->idx * v->e_idx * mod * 0.159f) * 0.8f * (1.0f - c->wm);
    }
    if (c->wm > 0.0f) {                        /* the metal */
        float o[3], y, lp, bp;
        uint32_t k;
        for (k = 0; k < 3u; k++) {
            v->xm[k] += c->im[k];
            if (v->xm[k] >= 1.0f)
                v->xm[k] -= 1.0f;
            v->xc[k] += c->ic[k];
            if (v->xc[k] >= 1.0f)
                v->xc[k] -= 1.0f;
            o[k] = fm_sin_turns(v->xc[k] + fm_sin_turns(v->xm[k]) * c->midx * (0.4f + 0.6f * v->e_idx) * 0.159f);
        }
        y = (o[0] + o[1] + o[2]) * (1.0f / 3.0f);
        y += c->ring * (o[0] * o[1] - y);
        y += c->noise * (white(ns) - y);
        y = svf_run(&c->hp, &v->hp, y, &lp, &bp);
        x += y * c->wm;
    }
    x *= v->e_amp * v->vel * v->fade;
    v->e_amp *= c->k_amp;
    v->e_idx *= c->k_idx;
    v->e_drop *= c->k_drop;
    v->fade *= v->k_fade;
    if (v->e_amp < 0.0003f || v->fade < 0.0005f)
        v->on = 0;
    return x;
}
