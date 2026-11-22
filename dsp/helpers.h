/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES' sound engine, curves, coefficients, noise, the soft clip, the state variable filter. Part of dsp/engine.c (included there, in order: the device builds the engine
 * as one unit). */
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
