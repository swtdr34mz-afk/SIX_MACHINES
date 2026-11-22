/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES' sound engine, the six machines behind one interface. Part of dsp/engine.c (included there, in order: the device builds the engine
 * as one unit). */
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
