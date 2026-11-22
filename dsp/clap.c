/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES' sound engine, the CLAP machine. Part of dsp/engine.c (included there, in order: the device builds the engine
 * as one unit). */
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
