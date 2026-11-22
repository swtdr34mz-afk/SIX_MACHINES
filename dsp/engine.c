/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES' sound engine (engine.h): the KICK machine, the step sequencer, the master. */
#include "engine.h"
#include "fastmath.h"
#include "plat.h"

/* ------------------------------------------------------------- the macros --- */
/* The zones' centres are where the curves below have their breakpoints: at a zone every parameter a macro moves is
 * exactly the value chosen for it there; between two zones they move from one to the other. */
const macro_t KICK_MACROS[4] = {
    {"TUNE", 4, {20, 50, 75, 105}, {"SUB", "DEEP", "808", "TIGHT"}},
    {"PUNCH", 4, {12, 50, 80, 115}, {"SOFT", "SYNTAKT", "TECHNO", "ZAP"}},
    {"BOOM", 4, {20, 50, 85, 115}, {"TIGHT", "SYNTAKT", "808", "DRONE"}},
    {"GRIT", 5, {0, 35, 70, 100, 127}, {"CLEAN", "WARM", "RUMBLE", "FOLD", "DESTROY"}},
};

const kick_preset_t KICK_PRESETS[NKICK_PRESETS] = {
    {"SYNTAKT DEEP", {50, 50, 50, 35}},
    {"808 CLASSIC", {75, 12, 85, 0}},
    {"WAREHOUSE", {105, 80, 20, 70}},
    {"PSY ZAP", {60, 115, 20, 35}},
};

/* a curve: breakpoints (x 0..127, y), x rising, straight lines between them */
typedef struct {
    uint8_t x;
    float y;
} bp_t;
#define NBP(t) (sizeof(t) / sizeof((t)[0]))

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

/* TUNE: the tone, in semitones from A1 (55 Hz): SUB D1, DEEP G1, 808 A1, TIGHT C2; 30 .. 80 Hz at the ends */
static const bp_t TUNE_ST[] = {{0, -10.48f}, {20, -7.0f}, {50, -2.0f}, {75, 0.0f}, {105, 3.0f}, {127, 6.49f}};
/* PUNCH: not a straight line: soft, then harder and shorter, then from about 90 the sweep long again (the zap) */
static const bp_t PUNCH_OCT[] = {{0, 0.4f}, {12, 0.6f}, {50, 2.5f}, {80, 3.5f}, {115, 4.0f}, {127, 4.2f}};
static const bp_t PUNCH_S[] = {{0, 0.030f}, {12, 0.028f}, {50, 0.018f}, {80, 0.008f}, {95, 0.012f}, {115, 0.045f},
                               {127, 0.070f}};
static const bp_t PUNCH_CLICK[] = {{0, 0.0f}, {12, 0.03f}, {50, 0.30f}, {80, 0.70f}, {115, 0.40f}, {127, 0.35f}};
/* BOOM: the body's length (time constants: heard about three times as long: TIGHT ~150 ms, SYNTAKT ~350, 808 ~900,
 * DRONE ~2.5 s) and how much of it is the long deep tail; the 808's glide down from about 70 */
static const bp_t BOOM_S[] = {{0, 0.030f}, {20, 0.050f}, {50, 0.110f}, {85, 0.300f}, {115, 0.80f}, {127, 1.10f}};
static const bp_t BOOM_SUB[] = {{0, 0.05f}, {20, 0.10f}, {50, 0.30f}, {85, 0.60f}, {115, 0.80f}, {127, 0.80f}};
static const bp_t BOOM_DROOP[] = {{0, 0.0f}, {70, 0.0f}, {85, -0.10f}, {115, -0.15f}, {127, -0.167f}};
/* GRIT: drive, the folder, the tone after them, the crusher, and the level that keeps it as loud as clean */
static const bp_t GRIT_DRIVE[] = {{0, 1.0f}, {35, 2.5f}, {70, 8.0f}, {100, 10.0f}, {127, 14.0f}};
static const bp_t GRIT_FOLD[] = {{0, 0.0f}, {85, 0.0f}, {100, 0.55f}, {127, 1.0f}};
static const bp_t GRIT_TONE[] = {{0, 16000.0f}, {35, 11000.0f}, {70, 5500.0f}, {100, 8000.0f}, {127, 11000.0f}};
static const bp_t GRIT_CRUSH[] = {{0, 0.0f}, {110, 0.0f}, {127, 1.0f}};
static const bp_t GRIT_MAKEUP[] = {{0, 1.00f}, {10, 0.86f}, {20, 0.765f}, {35, 0.674f}, {50, 0.51f}, {70, 0.456f},
                                   {85, 0.456f}, {100, 0.736f}, {110, 0.716f}, {127, 0.60f}};   /* measured: within ~1 dB */

void eng_kick_params(const uint8_t m[4], kick_params_t *p)
{
    float tune = (float)m[0], punch = (float)m[1], boom = (float)m[2], grit = (float)m[3];
    float tn = tune / 127.0f, bn = boom / 127.0f, gn = grit / 127.0f;
    p->hz = 55.0f * fm_exp2f(curve(TUNE_ST, NBP(TUNE_ST), tune) / 12.0f);
    p->sweep_oct = curve(PUNCH_OCT, NBP(PUNCH_OCT), punch);
    /* coupled: a high kick sweeps a little shorter (no laser where a thump was meant) */
    p->sweep_s = curve(PUNCH_S, NBP(PUNCH_S), punch) * (1.0f - 0.25f * tn);
    p->click = curve(PUNCH_CLICK, NBP(PUNCH_CLICK), punch);
    p->decay_s = curve(BOOM_S, NBP(BOOM_S), boom);
    /* coupled: a low kick gets more of the deep tail */
    p->sub = fm_clampf(curve(BOOM_SUB, NBP(BOOM_SUB), boom) + 0.20f * (1.0f - tn) - 0.10f, 0.0f, 0.9f);
    p->droop_oct = curve(BOOM_DROOP, NBP(BOOM_DROOP), boom);
    p->drive = curve(GRIT_DRIVE, NBP(GRIT_DRIVE), grit);
    p->fold = curve(GRIT_FOLD, NBP(GRIT_FOLD), grit);
    p->tone_hz = curve(GRIT_TONE, NBP(GRIT_TONE), grit);
    p->crush = curve(GRIT_CRUSH, NBP(GRIT_CRUSH), grit);
    /* coupled: a long body driven hard fills the mix: a little less of it */
    p->makeup = curve(GRIT_MAKEUP, NBP(GRIT_MAKEUP), grit) * (1.0f - 0.25f * bn * gn);
}

/* ----------------------------------------------------------------- the kick --- */
/* what the render plays with: the parameters as per-sample coefficients */
typedef struct {
    float hz, sweep_oct, droop_oct, k_sweep, k_body, k_tail, sub, k_click, click;
    float drive, fold, tone_a, crush_hold, crush_q, makeup;
} kick_coef_t;

typedef struct {
    uint8_t on;
    float phase, e_sweep, e_body, e_tail, e_click, vel, fade, k_fade;
    float lp, hp_prev, crush_s, crush_n;
} kick_voice_t;

static kick_coef_t kc_next, kc;          /* kc_next: from the main loop (under the lock); kc: the render's copy */
static volatile uint8_t kc_new;
static kick_voice_t kv[2];               /* two: a new hit takes the other one, the old one fades out in 3 ms */
static uint32_t kv_next;
static uint32_t noise = 0x2545F491u;
static float kick_level = 0.85f;

static float coef(float tau_s) { return tau_s <= 0.0f ? 0.0f : fm_expf(-1.0f / (tau_s * ENG_SR)); }

void eng_kick_macros(const uint8_t m[4])
{
    kick_params_t p;
    kick_coef_t c;
    uint32_t f;
    eng_kick_params(m, &p);
    c.hz = p.hz;
    c.sweep_oct = p.sweep_oct;
    c.droop_oct = p.droop_oct;
    c.k_sweep = coef(p.sweep_s);
    c.k_body = coef(p.decay_s);
    c.k_tail = coef(p.decay_s * 1.6f);
    c.sub = p.sub;
    c.k_click = coef(0.0012f);
    c.click = p.click;
    c.drive = p.drive;
    c.fold = p.fold;
    c.tone_a = 1.0f - fm_expf(-FM_TWO_PI * p.tone_hz / ENG_SR);
    c.crush_hold = 1.0f + 7.0f * p.crush;        /* hold each sample this many (a lower rate) */
    c.crush_q = 4096.0f - 4080.0f * p.crush;     /* the levels: 12 bits .. 4 */
    c.makeup = p.makeup;
    f = plat_lock();
    kc_next = c;
    kc_new = 1;
    plat_unlock(f);
}

void eng_kick_level(float level) { kick_level = fm_clampf(level, 0.0f, 1.0f) * 0.85f; }

static void kick_start(float vel)
{
    kick_voice_t *o = &kv[kv_next], *v;
    kv_next ^= 1u;
    v = &kv[kv_next];
    if (o->on)                                  /* the old one: out in 3 ms, no click */
        o->k_fade = coef(0.003f);
    v->on = 1;
    v->phase = 0.0f;                            /* from zero every time: the same punch on every hit */
    v->e_sweep = 1.0f;
    v->e_body = 1.0f;
    v->e_tail = 1.0f;
    v->e_click = 1.0f;
    v->vel = vel;
    v->fade = 1.0f;
    v->k_fade = 1.0f;
    v->lp = 0.0f;
    v->hp_prev = 0.0f;
    v->crush_n = 0.0f;
    v->crush_s = 0.0f;
}

static inline float white(void)
{
    noise ^= noise << 13;
    noise ^= noise >> 17;
    noise ^= noise << 5;
    return (float)(int32_t)noise * (1.0f / 2147483648.0f);
}

static float kick_sample(kick_voice_t *v, const kick_coef_t *c)
{
    float f, s, amp, x, n, y;
    /* the tone: up by the sweep, down by the droop as the body fades */
    f = c->hz * fm_exp2f(c->sweep_oct * v->e_sweep + c->droop_oct * (1.0f - v->e_body));
    v->phase += f * (1.0f / ENG_SR);
    if (v->phase >= 1.0f)
        v->phase -= 1.0f;
    s = fm_sin_turns(v->phase);
    amp = (1.0f - c->sub) * v->e_body + c->sub * v->e_tail;
    x = s * amp;
    /* the click: a high-passed noise burst of about a millisecond */
    if (c->click > 0.0f && v->e_click > 0.001f) {
        n = white();
        x += (n - v->hp_prev) * 0.5f * c->click * v->e_click;
        v->hp_prev = n;
    }
    /* GRIT: drive into tanh, the folder over it, the tone */
    y = x * c->drive;
    if (c->fold > 0.0f) {
        float t = fm_tanhf(y), fo = fm_sinf(y * (1.0f + 2.5f * c->fold) * 1.5707963f);
        y = t + c->fold * (fo - t);
    } else {                                    /* from clean into tanh without a step: the first 0.5 of drive blends it in */
        float sat = fm_clampf((c->drive - 1.0f) * 2.0f, 0.0f, 1.0f);
        if (sat > 0.0f)
            y += sat * (fm_tanhf(y) - y);
    }
    v->lp += c->tone_a * (y - v->lp);
    y = v->lp;
    if (c->crush_hold > 1.01f) {                /* DESTROY: fewer samples, fewer levels */
        v->crush_n += 1.0f;
        if (v->crush_n >= c->crush_hold) {
            v->crush_n -= c->crush_hold;
            v->crush_s = fm_floorf(y * c->crush_q + 0.5f) / c->crush_q;
        }
        y = v->crush_s;
    }
    y *= c->makeup * v->vel * v->fade;
    v->e_sweep *= c->k_sweep;
    v->e_body *= c->k_body;
    v->e_tail *= c->k_tail;
    v->e_click *= c->k_click;
    v->fade *= v->k_fade;
    if ((v->e_tail < 0.0002f && v->e_body < 0.0002f) || v->fade < 0.0005f)
        v->on = 0;
    return y;
}

/* ------------------------------------------------------------ the sequencer --- */
static volatile uint16_t pat_steps = 0x1111u, pat_acc;
static volatile float seq_bpm = 124.0f, seq_swing;
static volatile int8_t play_req = -1;    /* -1 nothing asked, 0 stop, 1 start */
static volatile uint8_t hit_req;
static volatile float hit_vel = 1.0f;
static volatile int32_t step_now = -1;
static volatile uint32_t hits;
static volatile float env_now;
static float step_left;                  /* samples to the next step */
static int32_t step_pos;                 /* the next step to play */
static float gain_now;

void eng_pattern(uint16_t steps, uint16_t accents)
{
    pat_steps = steps;
    pat_acc = accents;
}
void eng_tempo(float bpm, float swing)
{
    seq_bpm = fm_clampf(bpm, 40.0f, 240.0f);
    seq_swing = fm_clampf(swing, 0.0f, 1.0f);
}
void eng_play(int on) { play_req = (int8_t)(on ? 1 : 0); }
void eng_hit(float vel)
{
    hit_vel = vel;
    hit_req = 1;
}
int eng_step(void) { return step_now; }
float eng_kick_env(void) { return env_now; }
uint32_t eng_hits(void) { return hits; }

void eng_panic(void)
{
    kv[0].on = kv[1].on = 0;
    step_now = -1;
    play_req = -1;
}

void eng_init(void)
{
    static const uint8_t m0[4] = {50, 50, 50, 35};
    eng_kick_macros(m0);
    kc = kc_next;
    kc_new = 0;
}

/* a step's length in samples: a sixteenth, the pairs swung (the first longer, the second shorter, up to 2 : 1) */
static float step_len(int32_t s)
{
    float base = ENG_SR * 15.0f / seq_bpm, sw = seq_swing * 0.33f;
    return (s & 1) ? base * (1.0f - sw) : base * (1.0f + sw);
}

void eng_render(int32_t *lr, uint32_t n, float gain)
{
    uint32_t i;
    float peak = 0.0f;
    if (kc_new) {
        kc = kc_next;
        kc_new = 0;
    }
    if (play_req >= 0) {
        if (play_req) {
            step_pos = 0;
            step_left = 0.0f;
            step_now = 0;
        } else {
            step_now = -1;
        }
        play_req = -1;
    }
    if (hit_req) {
        hit_req = 0;
        kick_start(hit_vel);
        hits++;
    }
    for (i = 0; i < n; i++) {
        float x = 0.0f, y;
        if (step_now >= 0) {
            step_left -= 1.0f;
            if (step_left <= 0.0f) {
                uint16_t bit = (uint16_t)(1u << step_pos);
                step_now = step_pos;
                if (pat_steps & bit) {
                    kick_start((pat_acc & bit) ? 1.0f : 0.72f);
                    hits++;
                }
                step_left += step_len(step_pos);
                step_pos = (step_pos + 1) % NSTEPS;
            }
        }
        if (kv[0].on)
            x += kick_sample(&kv[0], &kc);
        if (kv[1].on)
            x += kick_sample(&kv[1], &kc);
        x *= kick_level;
        /* the master: clean below about 0.6, rounded off above, never over full scale */
        y = x;
        if (fm_fabsf(x) > 0.6f) {
            float o = 0.6f + 0.4f * fm_tanhf((fm_fabsf(x) - 0.6f) * 2.5f);
            y = x > 0.0f ? o : -o;
        }
        if (fm_fabsf(y) > peak)
            peak = fm_fabsf(y);
        gain_now += (gain - gain_now) * 0.004f;
        lr[2 * i] = lr[2 * i + 1] = (int32_t)(y * gain_now);
    }
    env_now = peak > env_now ? peak : env_now * 0.85f;
}
