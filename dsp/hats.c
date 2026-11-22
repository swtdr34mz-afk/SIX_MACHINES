/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES' sound engine, the HATS machine. Part of dsp/engine.c (included there, in order: the device builds the engine
 * as one unit). */
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
