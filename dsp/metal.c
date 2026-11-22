/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES' sound engine, the METAL machine (FM cymbals). Part of dsp/engine.c (included there, in order: the device builds the engine
 * as one unit). */
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
