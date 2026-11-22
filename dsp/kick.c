/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES' sound engine, the KICK machine. Part of dsp/engine.c (included there, in order: the device builds the engine
 * as one unit). */
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
