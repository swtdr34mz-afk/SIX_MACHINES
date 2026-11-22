/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES' sound engine, the master: compressor, warmth, tilt, reverb, delay, DJ filter, kills, build-up. Part of dsp/engine.c (included there, in order: the device builds the engine
 * as one unit). */
/* ---------------------------------------------------------------- the master --- */
static const bp_t COMP_THR[] = {{0, 0.0f}, {35, -12.0f}, {70, -18.0f}, {110, -20.0f}, {127, -24.0f}};
static const bp_t COMP_RATIO[] = {{0, 1.0f}, {35, 2.0f}, {70, 4.0f}, {110, 6.0f}, {127, 8.0f}};
static const bp_t COMP_ATT[] = {{0, 0.010f}, {35, 0.010f}, {70, 0.015f}, {110, 0.001f}, {127, 0.001f}};
static const bp_t COMP_REL[] = {{0, 0.15f}, {35, 0.15f}, {70, 0.08f}, {110, 0.20f}, {127, 0.25f}};
static const bp_t COMP_SC[] = {{0, 0.0f}, {35, 0.0f}, {70, 0.2f}, {110, 0.8f}, {127, 0.9f}};
static const bp_t WARM_G[] = {{0, 1.0f}, {40, 1.8f}, {80, 3.5f}, {120, 7.0f}, {127, 8.0f}};
static const bp_t WARM_LP[] = {{0, 19000.0f}, {40, 14000.0f}, {80, 10000.0f}, {120, 7000.0f}, {127, 6500.0f}};
static const bp_t WARM_CRUSH[] = {{0, 0.0f}, {100, 0.0f}, {120, 0.7f}, {127, 1.0f}};
static const bp_t SPACE_FB[] = {{0, 0.68f}, {20, 0.72f}, {55, 0.82f}, {90, 0.88f}, {120, 0.93f}, {127, 0.94f}};
static const bp_t SPACE_DAMP[] = {{0, 0.5f}, {20, 0.45f}, {55, 0.25f}, {90, 0.35f}, {120, 0.5f}, {127, 0.5f}};
static const bp_t SPACE_PRE[] = {{0, 0.003f}, {20, 0.005f}, {55, 0.015f}, {90, 0.030f}, {120, 0.055f}, {127, 0.06f}};
static const bp_t DLY_FB[] = {{0, 0.1f}, {20, 0.2f}, {55, 0.45f}, {90, 0.65f}, {120, 0.85f}, {127, 0.9f}};
static const bp_t DLY_LP[] = {{0, 1500.0f}, {20, 2000.0f}, {60, 5000.0f}, {105, 12000.0f}, {127, 16000.0f}};
static const bp_t RET_G[] = {{0, 0.0f}, {40, 0.5f}, {80, 1.0f}, {115, 1.5f}, {127, 1.7f}};
static const bp_t DJ_Q[] = {{0, 0.7f}, {20, 0.7f}, {64, 2.0f}, {110, 5.0f}, {127, 6.0f}};

/* a step's length in samples: a sixteenth, the pairs swung (the first longer, the second shorter, up to 2 : 1) */
static float step_base(void) { return ENG_SR * 15.0f / (float)(MST->bpm < 40u ? 40u : MST->bpm); }
static float step_len(int32_t s)
{
    float sw = (float)MST->swing / 100.0f * 0.33f;
    return (s & 1) ? step_base() * (1.0f - sw) : step_base() * (1.0f + sw);
}

static void master_block(float sb)
{
    float comp = MST->fx[0], warm = MST->fx[1], tilt = MST->fx[2], space = MST->fx[3], f, steps;
    /* compressor */
    fx.c_on = comp > 2.0f ? 1.0f : 0.0f;
    fx.c_thr = CURVE(COMP_THR, comp);
    fx.c_ratio = CURVE(COMP_RATIO, comp);
    fx.c_att = coef(CURVE(COMP_ATT, comp));
    fx.c_rel = coef(CURVE(COMP_REL, comp));
    fx.c_sc = CURVE(COMP_SC, comp);
    fx.c_makeup = fm_db2lin(-fx.c_thr * (1.0f - 1.0f / fx.c_ratio) * 0.5f);
    /* warmth */
    fx.w_g = CURVE(WARM_G, warm);
    fx.w_norm = 1.0f / (1.0f + 0.35f * fm_log2f(fx.w_g));
    fx.w_lp_a = onepole(CURVE(WARM_LP, warm));
    fx.w_crush = CURVE(WARM_CRUSH, warm);
    /* tilt: below the middle the lows up and the highs down, above it the other way */
    f = (tilt - 64.0f) / 64.0f;
    fx.t_lo = 1.0f - 0.5f * f;
    fx.t_hi = 1.0f + 0.6f * f;
    fx.t_lp_a = onepole(700.0f);
    /* reverb */
    fx.fb = CURVE(SPACE_FB, space);
    fx.damp = CURVE(SPACE_DAMP, space);
    fx.pre = CURVE(SPACE_PRE, space) * ENG_SR;
    /* delay: its time in steps (1, 2, 3, 4), halved till the line holds it; it glides there (tape) */
    steps = (float)(nearest(&PAGE_KNOBS[PG_FX2][0], MST->fx2[0]) + 1);
    while (steps * sb > (float)(DL_N - 4u) && steps > 0.5f)
        steps *= 0.5f;
    fx.dtime_t = steps * sb;
    fx.dfb = CURVE(DLY_FB, MST->fx2[1]);
    if (pads >> P_DTHROW & 1u)
        fx.dfb = fm_minf(fx.dfb + 0.15f, 0.92f);
    fx.dlp_a = onepole(CURVE(DLY_LP, MST->fx2[2]));
    fx.dhp_a = onepole(150.0f);
    fx.dret = CURVE(RET_G, MST->fx2[3]);
    /* the DJ filter: below the middle a low-pass, above it a high-pass, the middle open */
    {
        float v = MST->glo[0], q = CURVE(DJ_Q, MST->glo[3]);
        fx.dj_on = 1.0f;
        if (v < 60.0f) {
            fx.dj_mode = 0.0f;
            svf_set(&fx.dj, 20000.0f * fm_exp2f(-(60.0f - v) / 60.0f * 7.0f), q);
        } else if (v > 68.0f) {
            fx.dj_mode = 1.0f;
            svf_set(&fx.dj, 20.0f * fm_exp2f((v - 68.0f) / 59.0f * 8.0f), q);
        } else {
            fx.dj_on = 0.0f;
        }
    }
    /* the kills' targets (they glide per sample) */
    fx.t_glo = (pads >> P_KLOW & 1u) ? 0.0f : 1.0f;
    fx.t_gmid = (pads >> P_KMID & 1u) ? 0.0f : 1.0f;
    fx.t_ghi = (pads >> P_KHIGH & 1u) ? 0.0f : 1.0f;
    /* build-up: a high-pass rising, a little louder */
    if (build_on > 0.0f) {
        float p = eng_build();
        fx.b_hp_a = onepole(20.0f * fm_exp2f(p * 7.2f));
        fx.b_gain = 1.0f + 0.4f * p;
    } else {
        fx.b_hp_a = 0.0f;
        fx.b_gain = 1.0f;
    }
}

static inline float comb(float *buf, uint32_t n, uint32_t *i, float *f, float x)
{
    float y = buf[*i];
    *f = y + fx.damp * (*f - y);
    buf[*i] = x + *f * fx.fb;
    if (++*i >= n)
        *i = 0;
    return y;
}
static inline float allpass(float *buf, uint32_t n, uint32_t *i, float x)
{
    float b = buf[*i], y = b - x;
    buf[*i] = x + b * 0.5f;
    if (++*i >= n)
        *i = 0;
    return y;
}
