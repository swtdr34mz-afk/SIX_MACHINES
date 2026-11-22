/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES' sound engine, the sequencer: conditions, steps, the performance pads. Part of dsp/engine.c (included there, in order: the device builds the engine
 * as one unit). */
/* ------------------------------------------------------------- the sequencer --- */
static int cond_ok(int c)
{
    switch (c) {
    case C_1_2: return (loops & 1u) == 0;
    case C_2_2: return (loops & 1u) == 1;
    case C_1_4: return (loops & 3u) == 0;
    case C_2_4: return (loops & 3u) == 1;
    case C_3_4: return (loops & 3u) == 2;
    case C_4_4: return (loops & 3u) == 3;
    case C_P50: return rnd01() < 0.5f;
    case C_P25: return rnd01() < 0.25f;
    case C_P75: return rnd01() < 0.75f;
    case C_FILL: return (pads >> P_FILL & 1u) != 0;
    case C_NOFILL: return !(pads >> P_FILL & 1u);
    case C_FIRST: return loops == 0;
    default: return 1;
    }
}

static void play_step(int32_t real, uint32_t at)
{
    int32_t len = MST->length == 32u ? 32 : 16, src = real;
    uint32_t t, bit;
    step_now = real;
    if (rep_start >= 0) {                /* beat repeat: the window's steps, round */
        src = rep_start + (real - rep_start + len) % len % rep_len;
    } else if (half_start >= 0) {        /* half time: every step twice as long (the odd ones rest) */
        int32_t d = (real - half_start + len) % len;
        if (d & 1)
            return;
        src = half_start + d / 2;
    }
    src %= len;
    bit = 1u << src;
    for (t = 0; t < NMACHINES; t++) {
        const track_t *tr = &TRK[t];
        track_state_t *s = &T[t];
        uint32_t n;
        if (!(tr->steps & bit) || (muted >> t & 1u) || (t == M_KICK && (pads >> P_BREAK & 1u)))
            continue;
        if (t == M_SNARE && build_on > 0.0f)   /* the build-up's roll has the snare */
            continue;
        if (!cond_ok(tr->cond[src]))
            continue;
        s->rt_vel = (tr->acc & bit) ? 1.0f : 0.72f;
        push_ev((int)t, at, s->rt_vel, tr->lock[src]);
        for (n = 0; n < 4u; n++)
            s->seq_lock[n] = tr->lock[src][n];
        s->seq_locked = 1;
        n = tr->retrig[src] < 2u ? 1u : tr->retrig[src];
        if (rep_rt && n < 2u)
            n = 2;
        s->rt_left = (uint8_t)(n - 1u);
        s->rt_period = step_len(real) / (float)n;
        s->rt_count = s->rt_period;
    }
}

/* the pads: what changed since the last block */
static void pads_block(void)
{
    uint32_t p = pads, on = p & ~pads_was, off = pads_was & ~p;
    int32_t len = MST->length == 32u ? 32 : 16;
    static const uint8_t REP[4] = {4, 2, 1, 1};
    int r;
    /* beat repeat: the shortest held wins; its window starts on the grid where it was pressed */
    for (r = 3; r >= 0; r--)
        if (p >> (P_REP4 + r) & 1u)
            break;
    if (r >= 0 && step_now >= 0) {
        int32_t l = REP[r];
        if (rep_start < 0 || l != rep_len || (on >> (P_REP4 + r) & 1u)) {
            int32_t cur = step_now;
            rep_len = l;
            rep_start = (cur - cur % l + len) % len;
        }
        rep_rt = r == 3;
    } else {
        rep_start = -1;
        rep_rt = 0;
    }
    /* build-up: held, it runs; let go (or DROP): the drop */
    if (on >> P_BUILD & 1u) {
        static const float BARS[3] = {1.0f, 2.0f, 4.0f};
        build_on = 1.0f;
        build_t = 0.0f;
        build_roll = 0.0f;
        build_len = BARS[nearest(&PAGE_KNOBS[PG_GLO][2], MST->glo[2])] * 16.0f * step_base();
    }
    if (((off >> P_BUILD & 1u) && build_on > 0.0f) || (on >> P_DROP & 1u)) {
        build_on = 0.0f;
        if (step_now >= 0) {             /* from the top, now, with a crash */
            step_pos = 0;
            step_left = 0.0f;
            loops = 0;
            pads_drop = 1;
        }
    }
    if (on >> P_TAPE & 1u)               /* tape stop: held, it winds down */
        fx.t_speed = 1.0f;
    if (!(p >> P_HALF & 1u))
        half_start = -1;
    else if (on >> P_HALF & 1u)
        half_start = step_now >= 0 ? step_now : 0;
    pitch_x += ((float)(p >> P_PITCH & 1u) - pitch_x) * 0.15f;
    pads_was = p;
}
