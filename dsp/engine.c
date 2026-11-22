/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES' sound engine (engine.h): the machines, the tracks' insert and modulation, the sequencer, the master,
 * the performance effects. No libc here: the device builds this unit on its own. */
#include "engine.h"
#include "fastmath.h"
#include "plat.h"

#define RGB565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

#include "tables.c"

#include "helpers.h"

#include "kick.c"

#include "snare.c"

#include "toms.c"

#include "metal.c"

#include "hats.c"

#include "clap.c"

#include "machines.c"

/* =============================================================== the state === */
static const track_t *TRK;
static const master_t *MST;
static track_t track0[NMACHINES];        /* (until the app binds its own) */
static master_t master0 = {{0, 0, 64, 55}, {75, 55, 60, 80}, {64, 0, 64, 20}, {{0}}, 0, 16, 124, 0};

typedef struct {
    mcoef_t cur, tgt;                    /* the render's coefficients, and where they glide to */
    mvoice_t v[2];                       /* two voices: a new hit takes the other one, the old one fades */
    uint8_t next, gliding, locked;       /* locked: the last trig's p-locks, held until its next trig */
    uint8_t lock[4];
    float eff[4];                        /* the macros the coefficients were made from */
    float gain;                          /* the makeup, smoothed per sample */
    float env, env_att, env_k, env_rise; /* ENV: its value, its attack step, its decay, rising */
    float lfo_sh, lfo_last;              /* LFO: the sample-and-hold value, the phase it was taken at */
    float mod[4];                        /* the modulation now, in macro units */
    int sat_type;                        /* the insert: saturation, its tone, its mix */
    float sat_g, sat_mix, sat_makeup, sat_lp_a, sat_hp_a, sat_lp, sat_hp, crush_n, crush_s, sat_q;
    float gl, gr, rev, dly;              /* level x pan, the sends */
    uint8_t rt_left;                     /* retrigs still to come in the step (the sequencer's) */
    float rt_period, rt_count, rt_vel;
    uint8_t seq_locked, seq_lock[4];     /* the last trig's locks, for its retrigs (the sequencer's) */
    uint32_t noise;                      /* its own noise generator */
    float peak;                          /* the block's peak (the screen's) */
} track_state_t;
static track_state_t T[NMACHINES];
static volatile uint8_t muted, changed;

/* ------------------------------------------------------------- the effects --- */
#define DL_N 22050u                      /* the ping-pong delay, per side: half a second */
#define TP_N 12000u                      /* tape stop: how far behind it may fall (0.27 s) */
#define PRE_N 2700u                      /* the reverb's pre-delay: up to 60 ms */
static int16_t dl_buf[2][DL_N] PLAT_POOL;
static int16_t tp_buf[2][TP_N] PLAT_POOL;
static float rv_comb0[1116] PLAT_POOL, rv_comb1[1188] PLAT_POOL, rv_comb2[1277] PLAT_POOL, rv_comb3[1356] PLAT_POOL;
static float rv_ap0[556] PLAT_POOL, rv_ap1[441] PLAT_POOL, rv_ap2[579] PLAT_POOL, rv_ap3[464] PLAT_POOL;
static float rv_pre[PRE_N] PLAT_POOL;

static struct {
    uint32_t ci[4], ai[4], pi;           /* reverb */
    float cf[4], fb, damp, pre;
    uint32_t di;                         /* delay */
    float dtime, dtime_t, dfb, dlp_a, dhp_a, dlp[2], dhp[2], dret;
    float c_thr, c_ratio, c_att, c_rel, c_sc, c_makeup, c_on, c_env, c_kick;   /* compressor */
    float w_g, w_norm, w_lp_a, w_lp[2], w_crush, w_n, w_s[2];                 /* warmth */
    float t_lp_a, t_lp[2], t_lo, t_hi;                                         /* tilt */
    svf_c dj;                            /* the DJ filter */
    svf_s djs[2];
    float dj_mode, dj_on;
    float i_lo[2], i_mid[2], g_lo, g_mid, g_hi, t_glo, t_gmid, t_ghi;   /* the kills */
    float b_hp_a, b_hp[2], b_gain;       /* build-up */
    svf_c sw;                            /* SWEEP */
    svf_s sws[2];
    float lf_n, lf_s[2], lf_lp[2];       /* LO-FI */
    uint32_t ti;                         /* tape stop */
    float t_speed, t_lag, t_in, t_in_lp[2];
} fx;

/* ------------------------------------------------------------ the sequencer --- */
static volatile int8_t play_req = -1;    /* -1 nothing asked, 0 stop, 1 start */
static volatile uint8_t hit_req;
static volatile float hit_vel[NMACHINES];
static volatile int32_t step_now = -1;
static volatile uint32_t hits;
static volatile float env_out[NMACHINES];
static volatile uint32_t pads;           /* held now (the main loop's) */
static uint32_t pads_was;
static float step_left;                  /* samples to the next step */
static int32_t step_pos;                 /* the next step to play */
static uint32_t loops;                   /* the pattern's turns since PLAY */
static float bar_pos;                    /* bars since the start (the LFOs' clock) */
static int32_t rep_start = -1, rep_len;  /* beat repeat: its window (steps) */
static uint8_t rep_rt;                   /* 1/32: each repeated hit twice */
static float build_t, build_len, build_on, build_roll;   /* build-up: elapsed, length (samples) */
static float scene_x;                    /* the scene morph now 0..1 (smoothed) */
static uint8_t pads_drop;                /* the drop's crash, due */
static int32_t half_start = -1;          /* HALF TIME: where it began (-1 off) */
static float pitch_x, sweep_x, lofi_x, gate_g = 1.0f;   /* PITCH DOWN, SWEEP (0..1 of the way), LO-FI, the gate */
static float gain_now;

void eng_bind(const track_t *tracks, const master_t *master)
{
    uint32_t f = plat_lock();
    TRK = tracks;
    MST = master;
    changed = 0x3Fu;
    plat_unlock(f);
}
void eng_changed(int machine)
{
    if (machine >= 0 && machine < NMACHINES)
        changed |= (uint8_t)(1u << machine);
}
void eng_mute(int machine, int on)
{
    uint32_t f = plat_lock();
    if (on)
        muted |= (uint8_t)(1u << machine);
    else
        muted &= (uint8_t)~(1u << machine);
    plat_unlock(f);
}
void eng_play(int on) { play_req = (int8_t)(on ? 1 : 0); }
void eng_hit(int machine, float vel)
{
    uint32_t f;
    if (machine < 0 || machine >= NMACHINES)
        return;
    f = plat_lock();
    hit_vel[machine] = vel;
    hit_req |= (uint8_t)(1u << machine);
    plat_unlock(f);
}
void eng_pads(uint32_t held) { pads = held; }
int eng_step(void) { return step_now; }
float eng_env(int machine) { return machine >= 0 && machine < NMACHINES ? env_out[machine] : 0.0f; }
uint32_t eng_hits(void) { return hits; }
float eng_build(void) { return build_on > 0.0f ? fm_clampf(build_t / fm_maxf(build_len, 1.0f), 0.0f, 1.0f) : 0.0f; }
void eng_mod(int machine, int8_t out[4])
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        out[k] = (int8_t)fm_clampf(T[machine].mod[k], -127.0f, 127.0f);
}

void eng_panic(void)
{
    uint32_t t;
    for (t = 0; t < NMACHINES; t++) {
        T[t].v[0].on = T[t].v[1].on = 0;
        T[t].rt_left = 0;
    }
    step_now = -1;
    play_req = -1;
}

/* ------------------------------------------------------- glide, modulation --- */
/* New parameters while a machine sounds (a knob turned, a preset, an LFO) must not jump: a level, a drive or a filter
 * that changes from one sample to the next is a click. While a voice rings, every coefficient glides to its new value
 * (every 8th sample, about 5 ms to most of the way); a silent machine, and a new trig, take them at once. */
static void glide(float *c, const float *t, uint32_t n)   /* every 8th sample: 1 - (1 - 0.005)^8 of the way */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        c[i] += 0.039f * (t[i] - c[i]);
}
static int arrived(const float *c, const float *t, uint32_t n)   /* within 0.1 % (then it snaps) */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (fm_fabsf(t[i] - c[i]) > 0.001f * fm_fabsf(t[i]) + 1e-9f)
            return 0;
    return 1;
}

static inline int sounding(int t) { return T[t].v[0].on || T[t].v[1].on; }

/* a track's macros now: its own or its trig's p-locks, morphed towards scene B, plus its modulation */
static void effective(int t, float *e)
{
    const track_t *tr = &TRK[t];
    track_state_t *s = &T[t];
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        float v = (s->locked && s->lock[k] != NO_LOCK) ? (float)s->lock[k] : (float)tr->m[k];
        if (MST->has_scene)
            v += scene_x * ((float)MST->scene[t][k] - v);
        if (k == 0)                      /* PITCH DOWN: every machine's TUNE down */
            v -= 40.0f * pitch_x;
        e[k] = fm_clampf(v + s->mod[k], 0.0f, 127.0f);
    }
}

/* the coefficients from the macros now: a new trig snaps to them, a sounding machine glides */
static void retarget(int t, int snap)
{
    track_state_t *s = &T[t];
    float e[4];
    uint32_t k;
    int moved = snap || (changed >> t & 1u);
    effective(t, e);
    for (k = 0; k < 4u; k++)
        if (fm_fabsf(e[k] - s->eff[k]) > 0.02f)
            moved = 1;
    if (!moved)
        return;
    for (k = 0; k < 4u; k++)
        s->eff[k] = e[k];
    coefs(t, e, &s->tgt);
    if (snap || !sounding(t)) {
        s->cur = s->tgt;
        s->gliding = 0;
    } else {
        s->gliding = 1;
    }
}

/* the track's insert, envelope and LFO from its pages: once a block */
static const bp_t SAT_AMT_G[] = {{0, 1.0f}, {35, 2.0f}, {70, 5.0f}, {110, 12.0f}, {127, 16.0f}};
static const bp_t ENV_ATT_S[] = {{0, 0.0005f}, {5, 0.001f}, {45, 0.02f}, {90, 0.15f}, {122, 0.6f}, {127, 0.8f}};
static const bp_t ENV_DEC_S[] = {{0, 0.02f}, {15, 0.03f}, {50, 0.15f}, {85, 0.5f}, {120, 2.0f}, {127, 3.0f}};
static const bp_t LFO_RATE_L2[] = {{0, -3.0f}, {10, -2.0f}, {35, 0.0f}, {60, 2.0f}, {85, 3.0f}, {110, 4.0f}, {127, 5.0f}};
static const bp_t LEVEL_G[] = {{0, 0.0f}, {40, 0.32f}, {80, 0.8f}, {100, 1.0f}, {120, 1.2f}, {127, 1.26f}};
static const bp_t SEND_G[] = {{0, 0.0f}, {30, 0.12f}, {64, 0.35f}, {100, 0.7f}, {127, 1.0f}};
static const float MIX_TRIM[NMACHINES] = {0.85f, 0.70f, 0.75f, 0.60f, 0.55f, 0.65f};   /* each machine at FULL (100) */

static void track_block(int t)
{
    const track_t *tr = &TRK[t];
    track_state_t *s = &T[t];
    float lv, pan, rate, ph, w = 0.0f, d_env, d_lfo, tone;
    int wave, tgt_e, tgt_l;
    uint32_t k;
    /* SAT */
    s->sat_type = nearest(&PAGE_KNOBS[PG_SAT][0], tr->sat[0]);
    s->sat_g = CURVE(SAT_AMT_G, tr->sat[1]);
    s->sat_makeup = 1.0f / (1.0f + 0.30f * fm_log2f(s->sat_g));
    s->sat_mix = tr->sat[1] ? (float)tr->sat[3] / 127.0f : 0.0f;
    tone = tr->sat[2];
    s->sat_lp_a = onepole(tone < 64.0f ? 1500.0f * fm_exp2f(tone / 64.0f * 3.6f) : 18000.0f);
    s->sat_hp_a = onepole(tone > 64.0f ? 20.0f * fm_exp2f((tone - 64.0f) / 63.0f * 5.0f) : 5.0f);
    s->sat_q = 4096.0f - 4000.0f * (float)tr->sat[1] / 127.0f;
    /* MIX: level and pan (equal power), the sends (a throw sends everything) */
    lv = CURVE(LEVEL_G, tr->mix[0]) * MIX_TRIM[t];
    pan = (float)tr->mix[1] / 127.0f;
    s->gl = lv * fm_cosf(pan * 1.5707963f) * 1.4142f;
    s->gr = lv * fm_sinf(pan * 1.5707963f) * 1.4142f;
    s->rev = (pads >> P_RTHROW & 1u) ? 1.0f : CURVE(SEND_G, tr->mix[2]);
    s->dly = (pads >> P_DTHROW & 1u) ? 0.9f : CURVE(SEND_G, tr->mix[3]);
    /* ENV and LFO: their targets and depths (+-64 macro units at the ends; the OFF zone none), the LFO now */
    tgt_e = nearest(&PAGE_KNOBS[PG_ENV][3], tr->env[3]);
    tgt_l = nearest(&PAGE_KNOBS[PG_LFO][3], tr->lfo[3]);
    d_env = (float)tr->env[0] - 64.0f;
    d_lfo = (float)tr->lfo[1] - 64.0f;
    if (fm_fabsf(d_env) < 3.5f)
        d_env = 0.0f;
    if (fm_fabsf(d_lfo) < 3.5f)
        d_lfo = 0.0f;
    s->env_att = 1.0f / (CURVE(ENV_ATT_S, tr->env[1]) * ENG_SR);
    s->env_k = coef(CURVE(ENV_DEC_S, tr->env[2]));
    rate = fm_exp2f(CURVE(LFO_RATE_L2, tr->lfo[0]));   /* cycles a bar */
    ph = bar_pos * rate;
    ph -= fm_floorf(ph);
    wave = nearest(&PAGE_KNOBS[PG_LFO][2], tr->lfo[2]);
    switch (wave) {
    case 0: w = fm_sin_turns(ph); break;
    case 1: w = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; break;
    case 2: w = 2.0f * ph - 1.0f; break;
    case 3: w = ph < 0.5f ? 1.0f : -1.0f; break;
    default:
        if (ph < s->lfo_last)
            s->lfo_sh = white(&seq_noise);
        w = s->lfo_sh;
        break;
    }
    s->lfo_last = ph;
    for (k = 0; k < 4u; k++)
        s->mod[k] = 0.0f;
    if (d_env != 0.0f)
        s->mod[tgt_e] += d_env * s->env;
    if (d_lfo != 0.0f)
        s->mod[tgt_l] += d_lfo * w;
    if (t == M_SNARE && build_on > 0.0f)  /* the build-up tunes the snare up as it rolls */
        s->mod[0] += 30.0f * eng_build();
}

/* the track's insert: saturation (tape, tube, fold, crush) with its tone, mixed with the dry */
static inline float saturate(track_state_t *s, float x)
{
    float y, g;
    if (s->sat_mix <= 0.0f)
        return x;
    g = s->sat_g;
    switch (s->sat_type) {
    case 0: y = fm_tanhf(x * g) * s->sat_makeup * 1.15f; break;
    case 1: y = (x > 0.0f ? fm_tanhf(x * g) : fm_tanhf(x * g * 0.6f) * 1.3f) * s->sat_makeup; break;
    case 2: y = fm_sinf(x * g * 1.5707963f) * fm_minf(1.0f, s->sat_makeup * 1.5f); break;
    default:
        s->crush_n += 1.0f;
        if (s->crush_n >= 1.0f + g * 0.4f) {
            s->crush_n = 0.0f;
            s->crush_s = fm_floorf(x * s->sat_q + 0.5f) / s->sat_q;
        }
        y = s->crush_s;
        break;
    }
    s->sat_lp += s->sat_lp_a * (y - s->sat_lp);
    s->sat_hp += s->sat_hp_a * (s->sat_lp - s->sat_hp);
    y = s->sat_lp - s->sat_hp;
    return x + s->sat_mix * (y - x);
}

/* a machine struck: its envelope restarts, its trig's p-locks (or none) hold till its next trig, the coefficients snap
 * to them, its other voice starts and the one that sounded fades; struck from silence, its gain is its own at once */
static void strike(int t, float vel, const uint8_t *lock)
{
    track_state_t *s = &T[t];
    uint32_t a = s->next, b = a ^ 1u, k;
    int was = sounding(t);
    s->next = (uint8_t)b;
    s->locked = 0;
    if (lock)
        for (k = 0; k < 4u; k++) {
            s->lock[k] = lock[k];
            if (lock[k] != NO_LOCK)
                s->locked = 1;
        }
    s->env_rise = 1.0f;
    retarget(t, 1);
    if (!was)
        s->gain = s->cur.f[0];
    if (s->v[a].on)
        voice_fade(t, &s->v[a], t == M_HATS ? 0.002f : 0.003f);
    voice_start(t, &s->v[b], vel);
}

/* the sequencer's side: a hit for the machine at sample AT of the block, with its trig's locks (0: none). The
 * machines are rendered after the block is sequenced, each on its own (on either core), its hits in order */
#define NEV 8
typedef struct {
    uint16_t at;
    uint8_t has_lock, lock[4];
    float vel;
} ev_t;
static ev_t ev[NMACHINES][NEV];
static uint8_t nev[NMACHINES];
static void push_ev(int t, uint32_t at, float vel, const uint8_t *lock)
{
    ev_t *e;
    uint32_t k;
    if (nev[t] >= NEV)
        return;
    e = &ev[t][nev[t]++];
    e->at = (uint16_t)at;
    e->vel = vel;
    e->has_lock = lock != 0;
    if (lock)
        for (k = 0; k < 4u; k++)
            e->lock[k] = lock[k];
    hits++;
}

#include "master.c"

#include "sequencer.c"

/* =============================================================== the render === */
void eng_init(void)
{
    uint32_t t, k;
    for (t = 0; t < NMACHINES; t++) {
        for (k = 0; k < 4u; k++) {
            track0[t].m[k] = MACHINES[t].preset[0].m[k];
            track0[t].sat[k] = PAGE_DEFAULT[PG_SAT][k];
            track0[t].env[k] = PAGE_DEFAULT[PG_ENV][k];
            track0[t].lfo[k] = PAGE_DEFAULT[PG_LFO][k];
            track0[t].mix[k] = PAGE_DEFAULT[PG_MIX][k];
        }
        for (k = 0; k < NSTEPS; k++)
            track0[t].lock[k][0] = track0[t].lock[k][1] = track0[t].lock[k][2] = track0[t].lock[k][3] = NO_LOCK;
    }
    TRK = track0;
    MST = &master0;
    for (t = 0; t < NMACHINES; t++) {
        retarget((int)t, 1);
        T[t].gain = T[t].cur.f[0];
        T[t].noise = 0x2545F491u + 0x9E3779B9u * (t + 1u);
    }
    changed = 0x3Fu;
    fx.g_lo = fx.g_mid = fx.g_hi = fx.t_glo = fx.t_gmid = fx.t_ghi = 1.0f;
    fx.t_speed = fx.t_in = 1.0f;
    fx.dtime = 0.0f;
}

/* a machine's block: its hits at their samples, its voices, its envelope, its insert, into its own buffer */
#define BLK_MAX 256u
static float mbuf[NMACHINES][BLK_MAX];
static uint32_t blk_n;

static void render_machine(int t)
{
    track_state_t *s = &T[t];
    float *out = mbuf[t], pk = 0.0f;
    uint32_t i, e = 0, n = blk_n;
    for (i = 0; i < n; i++) {
        float y = 0.0f, a;
        while (e < nev[t] && ev[t][e].at <= i) {
            strike(t, ev[t][e].vel, ev[t][e].has_lock ? ev[t][e].lock : 0);
            e++;
        }
        if (s->env_rise > 0.0f) {        /* the envelope: up over its attack, then down */
            s->env += s->env_att;
            if (s->env >= 1.0f) {
                s->env = 1.0f;
                s->env_rise = 0.0f;
            }
        } else {
            s->env *= s->env_k;
        }
        if (!s->v[0].on && !s->v[1].on) {
            out[i] = 0.0f;
            continue;
        }
        if (s->gliding && !(i & 7u))
            glide(s->cur.f, s->tgt.f, COEF_N[t]);
        if (s->v[0].on)
            y += voice_sample(t, &s->v[0], &s->cur, &s->noise);
        if (s->v[1].on)
            y += voice_sample(t, &s->v[1], &s->cur, &s->noise);
        s->gain += (s->cur.f[0] - s->gain) * 0.005f;
        y = saturate(s, y * s->gain);
        a = fm_fabsf(y);
        if (a > pk)
            pk = a;
        out[i] = y;
    }
    if (s->gliding && (!sounding(t) || arrived(s->cur.f, s->tgt.f, COEF_N[t]))) {
        s->cur = s->tgt;
        s->gliding = 0;
    }
    s->peak = pk;
}

/* the second core's share: TOMS, METAL, HATS, CLAP (the first renders KICK, SNARE and the master: about even) */
static void job_machines(uint32_t arg)
{
    (void)arg;
    render_machine(M_TOMS);
    render_machine(M_METAL);
    render_machine(M_HATS);
    render_machine(M_CLAP);
}

/* the sequencer over the block: where each machine is struck */
static void sequence(uint32_t n, float sb)
{
    uint32_t i, t;
    for (t = 0; t < NMACHINES; t++)
        nev[t] = 0;
    if (pads_drop) {                     /* DROP (or a build-up let go): the crash, at the block's start */
        pads_drop = 0;
        push_ev(M_METAL, 0, 1.0f, 0);
    }
    if (hit_req) {
        for (t = 0; t < NMACHINES; t++)
            if (hit_req >> t & 1u)
                push_ev((int)t, 0, hit_vel[t], 0);
        hit_req = 0;
    }
    for (i = 0; i < n; i++) {
        bar_pos += 1.0f / (16.0f * sb);
        if (bar_pos > 4096.0f)
            bar_pos -= 4096.0f;
        if (step_now < 0)
            continue;
        step_left -= 1.0f;
        if (step_left <= 0.0f) {
            int32_t len = MST->length == 32u ? 32 : 16;
            if (step_pos >= len)
                step_pos = 0;
            play_step(step_pos, i);
            step_left += step_len(step_pos);
            if (++step_pos >= len) {
                step_pos = 0;
                loops++;
            }
        }
        for (t = 0; t < NMACHINES; t++)  /* retrigs: the rest of the step's hits, a little softer each */
            if (T[t].rt_left) {
                T[t].rt_count -= 1.0f;
                if (T[t].rt_count <= 0.0f) {
                    T[t].rt_count += T[t].rt_period;
                    T[t].rt_left--;
                    T[t].rt_vel *= 0.85f;
                    push_ev((int)t, i, T[t].rt_vel, T[t].seq_locked ? T[t].seq_lock : 0);
                }
            }
        if (build_on > 0.0f) {           /* the build-up's snare roll: eighths, sixteenths, thirty-seconds */
            float p = eng_build(), every = p < 0.5f ? 2.0f : p < 0.75f ? 1.0f : 0.5f;
            build_t += 1.0f;
            build_roll -= 1.0f;
            if (build_roll <= 0.0f) {
                build_roll += every * sb;
                push_ev(M_SNARE, i, 0.5f + 0.5f * p, 0);
            }
        }
    }
}

void eng_render(int32_t *lr, uint32_t n, float gain)
{
    uint32_t i, t;
    float sb = step_base(), x, bar0, binc;
    int on2, gate = (pads >> P_GATE & 1u) != 0, lofi = (pads >> P_LOFI & 1u) != 0;
    if (n > BLK_MAX)
        n = BLK_MAX;
    blk_n = n;
    if (play_req >= 0) {
        if (play_req) {
            step_pos = 0;
            step_left = 0.0f;
            step_now = 0;
            loops = 0;
            bar_pos = 0.0f;
        } else {
            step_now = -1;
            rep_start = -1;
            build_on = 0.0f;
            for (t = 0; t < NMACHINES; t++)
                T[t].rt_left = 0;
        }
        play_req = -1;
    }
    pads_block();
    /* SWEEP: held, the low-pass closes over a bar; let go, it opens in a quarter */
    if (pads >> P_SWEEP & 1u)
        sweep_x = fm_minf(sweep_x + (float)n / (16.0f * sb), 1.0f);
    else
        sweep_x = fm_maxf(sweep_x - (float)n / (4.0f * sb), 0.0f);
    if (sweep_x > 0.0f)
        svf_set(&fx.sw, 18000.0f * fm_exp2f(-sweep_x * 6.0f), 1.6f);
    /* the scene morph: the knob, or B while its pad is held (smoothed) */
    scene_x += (((pads >> P_SCENE & 1u) ? 1.0f : (float)MST->glo[1] / 127.0f) - scene_x) * 0.3f;
    master_block(sb);
    for (t = 0; t < NMACHINES; t++) {
        track_block((int)t);
        retarget((int)t, 0);
    }
    changed = 0;
    bar0 = bar_pos;
    binc = 1.0f / (16.0f * sb);
    sequence(n, sb);
    /* the machines: half on the second core, half here; if it cannot, all here */
    on2 = plat_cpu2_on() && plat_cpu2_run(job_machines, 0);
    render_machine(M_KICK);
    render_machine(M_SNARE);
    if (!on2 || plat_cpu2_wait() != 0)
        job_machines(0);
    for (t = 0; t < NMACHINES; t++)
        env_out[t] = T[t].peak > env_out[t] ? T[t].peak : env_out[t] * 0.85f;
    /* the mix and the master */
    for (i = 0; i < n; i++) {
        float L = 0.0f, R = 0.0f, rv = 0.0f, dl = 0.0f, kick = mbuf[M_KICK][i];
        for (t = 0; t < NMACHINES; t++) {
            float y = mbuf[t][i];
            if (y == 0.0f)
                continue;
            L += y * T[t].gl;
            R += y * T[t].gr;
            rv += y * T[t].rev;
            dl += y * T[t].dly;
        }
        /* the reverb: pre-delay, four combs, two all-passes a side */
        {
            float in, c0, c1, c2, c3;
            uint32_t pr = (fx.pi + PRE_N - (uint32_t)fx.pre) % PRE_N;
            rv_pre[fx.pi] = rv * 0.25f;
            in = rv_pre[pr];
            if (++fx.pi >= PRE_N)
                fx.pi = 0;
            c0 = comb(rv_comb0, 1116u, &fx.ci[0], &fx.cf[0], in);
            c1 = comb(rv_comb1, 1188u, &fx.ci[1], &fx.cf[1], in);
            c2 = comb(rv_comb2, 1277u, &fx.ci[2], &fx.cf[2], in);
            c3 = comb(rv_comb3, 1356u, &fx.ci[3], &fx.cf[3], in);
            L += allpass(rv_ap1, 441u, &fx.ai[1], allpass(rv_ap0, 556u, &fx.ai[0], c0 + c2 - 0.3f * c1)) * fx.dret;
            R += allpass(rv_ap3, 464u, &fx.ai[3], allpass(rv_ap2, 579u, &fx.ai[2], c1 + c3 - 0.3f * c2)) * fx.dret;
        }
        /* the ping-pong delay (FREEZE: no input, the loop holds) */
        {
            float pos, fr, ol, orr, fb, xin;
            uint32_t r0, r1;
            int fz = (pads >> P_FREEZE & 1u) != 0;
            fx.dtime += (fx.dtime_t - fx.dtime) * 0.0004f;
            pos = (float)fx.di - fx.dtime;
            if (pos < 0.0f)
                pos += (float)DL_N;
            r0 = (uint32_t)pos;
            fr = pos - (float)r0;
            r1 = r0 + 1u >= DL_N ? 0u : r0 + 1u;
            ol = ((float)dl_buf[0][r0] + fr * (float)(dl_buf[0][r1] - dl_buf[0][r0])) * (1.0f / 32768.0f);
            orr = ((float)dl_buf[1][r0] + fr * (float)(dl_buf[1][r1] - dl_buf[1][r0])) * (1.0f / 32768.0f);
            fb = fz ? 1.0f : fx.dfb;
            xin = fz ? 0.0f : dl;
            fx.dlp[0] += fx.dlp_a * (orr - fx.dlp[0]);   /* the tone in the loop */
            fx.dhp[0] += fx.dhp_a * (fx.dlp[0] - fx.dhp[0]);
            fx.dlp[1] += fx.dlp_a * (ol - fx.dlp[1]);
            fx.dhp[1] += fx.dhp_a * (fx.dlp[1] - fx.dhp[1]);
            dl_buf[0][fx.di] = (int16_t)(fm_tanhf(xin + fb * (fx.dlp[0] - fx.dhp[0])) * 32767.0f);
            dl_buf[1][fx.di] = (int16_t)(fm_tanhf(fb * (fx.dlp[1] - fx.dhp[1])) * 32767.0f);
            if (++fx.di >= DL_N)
                fx.di = 0;
            L += ol * fx.dret * 0.8f;
            R += orr * fx.dret * 0.8f;
        }
        /* tilt */
        if (MST->fx[2] != 64u) {
            fx.t_lp[0] += fx.t_lp_a * (L - fx.t_lp[0]);
            fx.t_lp[1] += fx.t_lp_a * (R - fx.t_lp[1]);
            L = fx.t_lp[0] * fx.t_lo + (L - fx.t_lp[0]) * fx.t_hi;
            R = fx.t_lp[1] * fx.t_lo + (R - fx.t_lp[1]) * fx.t_hi;
        } else {
            fx.t_lp[0] = L;
            fx.t_lp[1] = R;
        }
        /* the compressor: PUMP ducks all but the kick under the kick, then a glue on the sum */
        if (fx.c_on > 0.0f) {
            float d, lvl, over, gr, kl = kick * T[M_KICK].gl, kr = kick * T[M_KICK].gr;
            fx.c_kick += (fm_fabsf(kick) - fx.c_kick) * (fm_fabsf(kick) > fx.c_kick ? 0.05f : 0.0004f);
            d = 1.0f - fx.c_sc * fm_clampf(fx.c_kick * 2.0f, 0.0f, 1.0f);
            L = kl + (L - kl) * d;
            R = kr + (R - kr) * d;
            lvl = fm_maxf(fm_fabsf(L), fm_fabsf(R));
            fx.c_env = lvl > fx.c_env ? lvl + fx.c_att * (fx.c_env - lvl) : lvl + fx.c_rel * (fx.c_env - lvl);
            over = 6.0206f * fm_log2f(fm_maxf(fx.c_env, 1e-5f)) - fx.c_thr;
            gr = over > 0.0f ? fm_db2lin(-over * (1.0f - 1.0f / fx.c_ratio)) : 1.0f;
            L *= gr * fx.c_makeup;
            R *= gr * fx.c_makeup;
        }
        /* warmth */
        if (fx.w_g > 1.01f) {
            fx.w_lp[0] += fx.w_lp_a * (fm_tanhf(L * fx.w_g) * fx.w_norm - fx.w_lp[0]);
            fx.w_lp[1] += fx.w_lp_a * (fm_tanhf(R * fx.w_g) * fx.w_norm - fx.w_lp[1]);
            L = fx.w_lp[0];
            R = fx.w_lp[1];
            if (fx.w_crush > 0.0f) {
                fx.w_n += 1.0f;
                if (fx.w_n >= 1.0f + 5.0f * fx.w_crush) {
                    float q = 1024.0f - 1000.0f * fx.w_crush;
                    fx.w_n = 0.0f;
                    fx.w_s[0] = fm_floorf(L * q + 0.5f) / q;
                    fx.w_s[1] = fm_floorf(R * q + 0.5f) / q;
                }
                L = fx.w_s[0];
                R = fx.w_s[1];
            }
        }
        /* the DJ filter */
        if (fx.dj_on > 0.0f) {
            float lp, bp, hp;
            hp = svf_run(&fx.dj, &fx.djs[0], L, &lp, &bp);
            L = fx.dj_mode > 0.5f ? hp : lp;
            hp = svf_run(&fx.dj, &fx.djs[1], R, &lp, &bp);
            R = fx.dj_mode > 0.5f ? hp : lp;
        }
        /* the kills: the lows under ~250 Hz, the highs over ~2.5 kHz, the mids between */
        fx.g_lo += (fx.t_glo - fx.g_lo) * 0.003f;
        fx.g_mid += (fx.t_gmid - fx.g_mid) * 0.003f;
        fx.g_hi += (fx.t_ghi - fx.g_hi) * 0.003f;
        fx.i_lo[0] += 0.035f * (L - fx.i_lo[0]);
        fx.i_mid[0] += 0.30f * (L - fx.i_mid[0]);
        fx.i_lo[1] += 0.035f * (R - fx.i_lo[1]);
        fx.i_mid[1] += 0.30f * (R - fx.i_mid[1]);
        if (fx.g_lo < 0.9999f || fx.g_mid < 0.9999f || fx.g_hi < 0.9999f) {
            L = fx.i_lo[0] * fx.g_lo + (fx.i_mid[0] - fx.i_lo[0]) * fx.g_mid + (L - fx.i_mid[0]) * fx.g_hi;
            R = fx.i_lo[1] * fx.g_lo + (fx.i_mid[1] - fx.i_lo[1]) * fx.g_mid + (R - fx.i_mid[1]) * fx.g_hi;
        }
        /* SWEEP */
        if (sweep_x > 0.0f) {
            float lp, bp;
            svf_run(&fx.sw, &fx.sws[0], L, &lp, &bp);
            L = lp;
            svf_run(&fx.sw, &fx.sws[1], R, &lp, &bp);
            R = lp;
        }
        /* LO-FI: a quarter of the rate, 6 bits, a low-pass; in and out over a few ms */
        lofi_x += ((float)lofi - lofi_x) * 0.004f;
        if (lofi_x > 0.001f) {
            float a = 0.5f, l, r;
            fx.lf_n += 1.0f;
            if (fx.lf_n >= 4.0f) {
                fx.lf_n = 0.0f;
                fx.lf_s[0] = fm_floorf(L * 32.0f + 0.5f) * (1.0f / 32.0f);
                fx.lf_s[1] = fm_floorf(R * 32.0f + 0.5f) * (1.0f / 32.0f);
            }
            fx.lf_lp[0] += a * (fx.lf_s[0] - fx.lf_lp[0]);
            fx.lf_lp[1] += a * (fx.lf_s[1] - fx.lf_lp[1]);
            l = fx.lf_lp[0];
            r = fx.lf_lp[1];
            L += lofi_x * (l - L);
            R += lofi_x * (r - R);
        }
        /* GATE: open the first half of each thirty-second, on the bar's grid */
        {
            float ph = (bar0 + (float)i * binc) * 32.0f, tgt;
            ph -= fm_floorf(ph);
            tgt = (!gate || ph < 0.5f) ? 1.0f : 0.0f;
            gate_g += (tgt - gate_g) * 0.08f;
            L *= gate_g;
            R *= gate_g;
        }
        /* build-up: the high-pass */
        if (fx.b_hp_a > 0.0f) {
            fx.b_hp[0] += fx.b_hp_a * (L - fx.b_hp[0]);
            fx.b_hp[1] += fx.b_hp_a * (R - fx.b_hp[1]);
            L = (L - fx.b_hp[0]) * fx.b_gain;
            R = (R - fx.b_hp[1]) * fx.b_gain;
        } else {
            fx.b_hp[0] = fx.b_hp[1] = 0.0f;
        }
        /* tape stop: the sound written to a line and read back slower and slower; let go, it comes back in */
        tp_buf[0][fx.ti] = (int16_t)(fm_clampf(L, -1.0f, 1.0f) * 32767.0f);
        tp_buf[1][fx.ti] = (int16_t)(fm_clampf(R, -1.0f, 1.0f) * 32767.0f);
        if (pads >> P_TAPE & 1u) {
            float pos, fr, g;
            uint32_t r0, r1;
            fx.t_speed = fm_maxf(fx.t_speed - 1.0f / (0.6f * ENG_SR), 0.0f);
            fx.t_lag = fm_minf(fx.t_lag + 1.0f - fx.t_speed, (float)(TP_N - 4u));
            pos = (float)fx.ti - fx.t_lag;
            if (pos < 0.0f)
                pos += (float)TP_N;
            r0 = (uint32_t)pos;
            fr = pos - (float)r0;
            r1 = r0 + 1u >= TP_N ? 0u : r0 + 1u;
            g = fm_minf(1.0f, fx.t_speed * 4.0f) * (1.0f / 32768.0f);
            L = ((float)tp_buf[0][r0] + fr * (float)(tp_buf[0][r1] - tp_buf[0][r0])) * g;
            R = ((float)tp_buf[1][r0] + fr * (float)(tp_buf[1][r1] - tp_buf[1][r0])) * g;
            fx.t_in = 0.0f;
            fx.t_in_lp[0] = fx.t_in_lp[1] = 0.0f;
        } else if (fx.t_in < 1.0f) {     /* back: from silence, the sound opens up in 150 ms */
            float a;
            fx.t_lag = 0.0f;
            fx.t_in = fm_minf(fx.t_in + 1.0f / (0.15f * ENG_SR), 1.0f);
            a = onepole(200.0f * fm_exp2f(fx.t_in * 6.6f));
            fx.t_in_lp[0] += a * (L - fx.t_in_lp[0]);
            fx.t_in_lp[1] += a * (R - fx.t_in_lp[1]);
            L = fx.t_in_lp[0] * fx.t_in;
            R = fx.t_in_lp[1] * fx.t_in;
        }
        if (++fx.ti >= TP_N)
            fx.ti = 0;
        /* the limiter: clean below 0.6, rounded off above, never over full scale */
        if (fm_fabsf(L) > 0.6f) {
            x = 0.6f + 0.4f * fm_tanhf((fm_fabsf(L) - 0.6f) * 2.5f);
            L = L > 0.0f ? x : -x;
        }
        if (fm_fabsf(R) > 0.6f) {
            x = 0.6f + 0.4f * fm_tanhf((fm_fabsf(R) - 0.6f) * 2.5f);
            R = R > 0.0f ? x : -x;
        }
        gain_now += (gain - gain_now) * 0.004f;
        lr[2 * i] = (int32_t)(L * gain_now);
        lr[2 * i + 1] = (int32_t)(R * gain_now);
    }
}

