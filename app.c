/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES: input, the knobs and their sweetspots, the pattern, the screen, settings. Part of the unity build
 * (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in scope. The sound is dsp/engine.c.
 *
 * Controls (v0.2: KICK, SNARE, HATS)
 *   black keys      the six machines, left to right (KICK SNARE TOMS METAL HATS CLAP): a key picks the machine (and
 *                   plays it while the sequencer is stopped); SEQ held + a key: the machine muted / back (TOMS, METAL, CLAP are coming)
 *   white keys      the picked machine's 16 steps: a tap turns a step on, again ACCENT (louder), again off
 *   K1..K4          the picked machine's four macros. Each moves several parameters at once; turned slowly it rests a
 *                   few detents on each sweetspot (its name lights up); turned fast it runs past them
 *   SELECT          tempo (BPM)      ALGORITHM  swing      PRESET  a start preset for the machine's four knobs
 *   PLAY            the sequencer: start from step 1 / stop
 *   REC held 1 s    the machine's pattern cleared
 *   OCT- / OCT+     the machine's level
 *   Settings save themselves a few seconds after the last change. */
#include "fastmath.h"
#include "engine.h"

#define SET_MAGIC 0x32584D36u            /* "6MX2": the settings record; another magic loads the defaults */
typedef struct {                         /* a machine's track */
    uint8_t m[4];                        /* its macros 0..127 */
    uint16_t steps, acc;                 /* its pattern: steps that play, and the accented ones */
    uint8_t preset;                      /* the preset PRESET loaded last */
    uint8_t level;                       /* 0 .. 10 */
    uint8_t rsv[2];
} track_t;
typedef struct {
    uint32_t magic;
    track_t tr[NMACHINES];
    uint8_t bpm;                         /* 60 .. 200 */
    uint8_t swing;                       /* 0 .. 100 % */
    uint8_t sel;                         /* the machine picked */
    uint8_t rsv[5];
} settings_t;
static settings_t set;
static uint8_t set_dirty;
static uint32_t dirty_at;

static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;

/* --------------------------------------------------------------- settings --- */
/* the first beat: four on the floor (the first accented), the snare on 2 and 4, the hats on the offbeats */
static const uint16_t DEF_STEPS[NMACHINES] = {0x1111u, 0x1010u, 0, 0, 0x4444u, 0};
static const uint16_t DEF_ACC[NMACHINES] = {0x0001u, 0, 0, 0, 0x4444u, 0};

static void settings_defaults(void)
{
    uint32_t i, k;
    memset(&set, 0, sizeof set);
    set.magic = SET_MAGIC;
    for (i = 0; i < NMACHINES; i++) {
        for (k = 0; k < 4u; k++)
            set.tr[i].m[k] = MACHINES[i].preset[0].m[k];
        set.tr[i].steps = DEF_STEPS[i];
        set.tr[i].acc = DEF_ACC[i];
        set.tr[i].level = 8;
    }
    set.bpm = 124;
    set.sel = M_KICK;
}

static int settings_ok(void)
{
    uint32_t i, k;
    for (i = 0; i < NMACHINES; i++) {
        for (k = 0; k < 4u; k++)
            if (set.tr[i].m[k] > 127u)
                return 0;
        if (set.tr[i].preset >= NPRESETS || set.tr[i].level > 10u)
            return 0;
    }
    return set.magic == SET_MAGIC && set.bpm >= 60u && set.bpm <= 200u && set.swing <= 100u && set.sel < NMACHINES
           && MACHINES[set.sel].ready;
}

static void settings_load(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || !settings_ok())
        settings_defaults();
}

static void dirty(void)
{
    set_dirty = 1;
    dirty_at = plat_ms();
}

static void settings_save(void)
{
    if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0)
        set_dirty = 0;
}

/* everything the engine plays from the settings */
static void apply(void)
{
    uint32_t i;
    for (i = 0; i < NMACHINES; i++) {
        eng_macros((int)i, set.tr[i].m);
        eng_pattern((int)i, set.tr[i].steps, set.tr[i].acc & set.tr[i].steps);
        eng_level((int)i, (float)set.tr[i].level / 10.0f);
    }
    eng_tempo((float)set.bpm, (float)set.swing / 100.0f);
}

#define TR (set.tr[set.sel])             /* the picked machine's track */
#define MC (MACHINES[set.sel])           /* and the machine */

/* ------------------------------------------------------- knobs: sweetspots --- */
/* A knob's position runs past each sweetspot zone REST detents more than its value: turned slowly, it rests on the
 * zone for those detents (a soft notch); turned fast, it runs past (the acceleration takes several at once). */
#define REST 5
static int32_t knob_pos[4];

static int32_t pos_max(const macro_t *mc) { return 127 + mc->nzones * REST; }

static int32_t val_of_pos(const macro_t *mc, int32_t p)
{
    uint32_t z;
    for (z = 0; z < mc->nzones; z++) {
        int32_t c = mc->centre[z];
        if (p <= c)
            return p;
        if (p <= c + REST)
            return c;
        p -= REST;
    }
    return p > 127 ? 127 : p;
}

static int32_t pos_of_val(const macro_t *mc, int32_t v)
{
    int32_t p = v;
    uint32_t z;
    for (z = 0; z < mc->nzones; z++) {
        if (mc->centre[z] < v)
            p += REST;
        else if (mc->centre[z] == v)
            p += REST / 2;               /* in the middle of its rest */
    }
    return p;
}

/* the zone the value is on (-1: between zones) */
static int zone_at(const macro_t *mc, int32_t v)
{
    uint32_t z;
    for (z = 0; z < mc->nzones; z++)
        if (mc->centre[z] == v)
            return (int)z;
    return -1;
}

/* the zone nearest the value (the one the knob is heading to or coming from) */
static int zone_near(const macro_t *mc, int32_t v)
{
    uint32_t z;
    int best = 0;
    int32_t d = 999;
    for (z = 0; z < mc->nzones; z++) {
        int32_t e = v - mc->centre[z];
        if (e < 0)
            e = -e;
        if (e < d) {
            d = e;
            best = (int)z;
        }
    }
    return best;
}

static void knobs_from_settings(void)    /* the knobs' positions: the picked machine's values */
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        knob_pos[k] = pos_of_val(&MC.macro[k], TR.m[k]);
}

/* ---------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][24];
    uint32_t msg_until;
    int8_t zone_k, zone_z;               /* a sweetspot just reached: knob, zone (shown big) */
    uint32_t zone_until;
    uint32_t knob_at[4];                 /* when each knob was last turned */
    uint32_t keys, btn;
    uint32_t rec_t;
    uint32_t enc_at[NE];
    uint8_t muted;                       /* bit per machine (live: not kept) */
    int32_t step_drawn;
    uint8_t hit_drawn;
    uint8_t playing;
    uint8_t redraw;
    uint32_t load_at, load_shown;        /* the audio load on the screen, looked at twice a second */
} ui;

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1200u;
    ui.redraw = 1;
    if (ann_on()) {
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

/* a quick turn moves further: x1 slow, more when the detents come close together */
static int32_t accel(int k, int32_t e, int32_t mid, int32_t fast)
{
    uint32_t now = plat_ms(), dt = now - ui.enc_at[k];
    ui.enc_at[k] = now;
    if (dt < 35u)
        return e * fast;
    if (dt < 80u)
        return e * mid;
    return e;
}

static void knob(int k, int32_t e)
{
    const macro_t *mc = &MC.macro[k];
    int32_t p = clamp(knob_pos[k] + accel(EN_K1 + k, e, 3, 6), 0, pos_max(mc)), v = val_of_pos(mc, p);
    int zb = zone_at(mc, TR.m[k]), za = zone_at(mc, v);
    knob_pos[k] = p;
    ui.knob_at[k] = plat_ms();
    ui.redraw = 1;
    if (v == TR.m[k])
        return;
    TR.m[k] = (uint8_t)v;
    eng_macros(set.sel, TR.m);
    dirty();
    if (za >= 0 && za != zb) {           /* a sweetspot reached: its name, big */
        ui.zone_k = (int8_t)k;
        ui.zone_z = (int8_t)za;
        ui.zone_until = plat_ms() + 700u;
        if (ann_on())
            ann_say(mc->zone[za], 0, ANN_NOW);
    }
}

static void preset_load(int32_t d)
{
    uint32_t k;
    TR.preset = (uint8_t)(((int32_t)TR.preset + d % NPRESETS + NPRESETS) % NPRESETS);
    for (k = 0; k < 4u; k++)
        TR.m[k] = MC.preset[TR.preset].m[k];
    knobs_from_settings();
    eng_macros(set.sel, TR.m);
    if (!ui.playing)
        eng_hit(set.sel, 1.0f);           /* stopped: hear it */
    dirty();
    app_say(MC.name, MC.preset[TR.preset].name);
}

static void pattern_sync(void) { eng_pattern(set.sel, TR.steps, TR.acc & TR.steps); }

static void step_tap(uint32_t s)         /* off -> on -> accent -> off */
{
    uint16_t bit = (uint16_t)(1u << s);
    if (!(TR.steps & bit)) {
        TR.steps |= bit;
        TR.acc &= (uint16_t)~bit;
    } else if (!(TR.acc & bit)) {
        TR.acc |= bit;
    } else {
        TR.steps &= (uint16_t)~bit;
        TR.acc &= (uint16_t)~bit;
    }
    pattern_sync();
    if (!ui.playing && (TR.steps & bit))
        eng_hit(set.sel, (TR.acc & bit) ? 1.0f : 0.72f);   /* stopped: hear what the step will play */
    dirty();
    ui.redraw = 1;
}

static void play_toggle(void)
{
    ui.playing = (uint8_t)!ui.playing;
    eng_play(ui.playing);
    ui.redraw = 1;
}

/* a black key: SEQ held, the machine muted or back; else the machine picked and played */
static void machine_key(uint32_t i, uint32_t btn)
{
    if (!MACHINES[i].ready) {
        app_say(MACHINES[i].name, "COMING SOON");
        return;
    }
    if (btn >> B_SEQ & 1u) {
        ui.muted ^= (uint8_t)(1u << i);
        eng_mute((int)i, ui.muted >> i & 1u);
        app_say(MACHINES[i].name, (ui.muted >> i & 1u) ? "MUTED" : "ON");
        return;
    }
    if (!ui.playing)                     /* stopped: hear it; while the beat runs a key only picks the machine */
        eng_hit((int)i, 1.0f);
    if (set.sel != i) {
        set.sel = (uint8_t)i;
        knobs_from_settings();
        ui.zone_until = 0;
        dirty();
    }
    ui.redraw = 1;
}

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), i;
    int32_t e;
    for (i = 0; i < 16u; i++) {          /* the steps */
        uint32_t bit = 1u << WK[i];
        if ((keys & bit) && !(ui.keys & bit))
            step_tap(i);
    }
    for (i = 0; i < NMACHINES; i++) {    /* the machines */
        uint32_t bit = 1u << BK[i];
        if ((keys & bit) && !(ui.keys & bit))
            machine_key(i, btn);
    }
    for (i = 0; i < NB; i++) {
        uint32_t bit = 1u << i;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        if (i == B_PLAY)
            play_toggle();
        else if (i == B_REC)
            ui.rec_t = plat_ms();
        else if (i == B_OCTDN || i == B_OCTUP) {
            int32_t l = clamp((int32_t)TR.level + (i == B_OCTUP ? 1 : -1), 0, 10);
            char t[8];
            TR.level = (uint8_t)l;
            eng_level(set.sel, (float)l / 10.0f);
            dirty();
            int_text(l * 10, t);
            text_cat(t, "%");
            app_say(MC.name, t);
        }
    }
    if (ui.rec_t) {                      /* REC held a second: the machine's pattern cleared */
        if (!(btn >> B_REC & 1u))
            ui.rec_t = 0;
        else if (plat_ms() - ui.rec_t >= 1000u) {
            ui.rec_t = 0;
            TR.steps = TR.acc = 0;
            pattern_sync();
            dirty();
            app_say(MC.name, "CLEARED");
        }
    }
    for (i = 0; i < 4u; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0)
            knob((int)i, e);
    if ((e = plat_enc(EN_SELECT)) != 0) {
        char t[8];
        set.bpm = (uint8_t)clamp((int32_t)set.bpm + accel(EN_SELECT, e, 2, 5), 60, 200);
        eng_tempo((float)set.bpm, (float)set.swing / 100.0f);
        dirty();
        int_text(set.bpm, t);
        app_say("BPM", t);
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        char t[8];
        set.swing = (uint8_t)clamp((int32_t)set.swing + accel(EN_ALGO, e, 2, 4), 0, 100);
        eng_tempo((float)set.bpm, (float)set.swing / 100.0f);
        dirty();
        int_text(set.swing, t);
        text_cat(t, "%");
        app_say("SWING", t);
    }
    if ((e = plat_enc(EN_PRESET)) != 0)
        preset_load(e > 0 ? 1 : -1);
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
}

static void lights(void)
{
    uint32_t lit = 0, mid = 0, dim = 0, i, btn = 0, dimb;
    int32_t s = eng_step();
    for (i = 0; i < 16u; i++) {          /* the picked machine's steps: accents lit, steps half */
        uint32_t bit = 1u << WK[i];
        if (TR.steps >> i & 1u) {
            if (TR.acc >> i & 1u)
                lit |= bit;
            else
                mid |= bit;
        }
    }
    if (s >= 0)                          /* the step under way: lit, or out if it plays (it flickers past) */
        lit ^= 1u << WK[s];
    for (i = 0; i < NMACHINES; i++) {    /* the machines: the picked one lit, a hit flashes, the others glow */
        uint32_t bit = 1u << BK[i];
        if (!MACHINES[i].ready || (ui.muted >> i & 1u))
            continue;
        if (i == set.sel || eng_env((int)i) > 0.3f)
            lit |= bit;
        else
            dim |= bit;
    }
    if (ui.playing)
        btn |= LED_PLAY_GREEN;
    dimb = (1u << B_PLAY) | (1u << B_REC) | (1u << B_SEQ) | (1u << B_OCTDN) | (1u << B_OCTUP);
    plat_lights(btn | (ui.btn & ~(1u << B_PLAY)), lit, dimb, dim, 1);
    plat_lights_mid(0, mid & ~lit);
}

void app_input_only(void) { input(); }

/* ------------------------------------------------------------------ drawing --- */
static uint16_t dim_of(uint16_t c)       /* a machine's colour at about a third */
{
    uint32_t r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
    return (uint16_t)(((r * 3u / 8u) << 11) | ((g * 3u / 8u) << 5) | (b * 3u / 8u));
}

static void ctext(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}

/* the rack: six modules; the picked one framed in its colour, each flashing with its hits; muted ones crossed out,
 * the coming ones grey */
static void draw_rack(int32_t y)
{
    uint32_t i;
    for (i = 0; i < NMACHINES; i++) {
        const machine_t *m = &MACHINES[i];
        int32_t x = 3 + (int32_t)i * 39;
        float env = eng_env((int)i);
        int sel = i == set.sel, mute = ui.muted >> i & 1u;
        if (!m->ready) {
            cv_rect(x, y, 37, 26, C_LINE);
            cv_rect(x + 1, y + 1, 35, 24, C_BLACK);
            ctext(x + 18, y + 8, &FONT_XS, m->name, C_DIM);
            continue;
        }
        cv_rect(x, y, 37, 26, sel ? m->rgb : env > 0.25f ? dim_of(m->rgb) : C_LINE);
        cv_rect(x + (sel ? 2 : 1), y + (sel ? 2 : 1), sel ? 33 : 35, sel ? 22 : 24, C_BLACK);
        if (!mute)
            cv_rect(x + 4, y + 18, (int32_t)(29.0f * fm_clampf(env * 1.4f, 0.0f, 1.0f)), 3, m->rgb);
        ctext(x + 18, y + 5, &FONT_XS, m->name, mute ? C_DIM : sel ? C_WHITE : C_GRAY);
        if (mute)
            cv_line(x + 4, y + 21, x + 32, y + 4, C_GRAY);
    }
}

/* one knob: its name, a bar 0..127 with the sweetspots as ticks, the value, and the zone it is on (lit) or near (dim) */
static void draw_knob(int k, int32_t x, int32_t y, uint32_t now)
{
    const macro_t *mc = &MC.macro[k];
    int32_t v = TR.m[k], w = 54, z = zone_at(mc, v), zn = zone_near(mc, v), fill = (v * (w - 2)) / 127;
    uint32_t i;
    int hot = ui.knob_at[k] && now - ui.knob_at[k] < 600u;
    char t[8];
    cv_text(x, y, &FONT_XS, mc->name, hot ? C_WHITE : C_GRAY);
    int_text(v, t);
    cv_text(x + w - text_w(&FONT_XS, t), y, &FONT_XS, t, hot ? C_HI : C_AMB);
    cv_rect(x, y + 14, w, 8, C_LINE);
    cv_rect(x + 1, y + 15, fill, 6, z >= 0 ? MC.rgb : C_HI);
    for (i = 0; i < mc->nzones; i++) {   /* the sweetspots */
        int32_t tx = x + 1 + (mc->centre[i] * (w - 3)) / 127;
        cv_rect(tx, y + 11, 1, 3, (int)i == z ? MC.rgb : C_GRAY);
    }
    ctext(x + w / 2, y + 26, &FONT_XS, mc->zone[z >= 0 ? z : zn], z >= 0 ? C_WHITE : C_DIM);
}

static void draw(void)
{
    uint32_t now = plat_ms(), i, j;
    int32_t s = eng_step();
    char t[24];
    /* header: the name, or a message; the tempo and the transport */
    cv_begin(240, 30, C_BLACK);
    if (now < ui.msg_until) {
        int32_t x = cv_text(8, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_HI);
    } else {
        cv_text(8, 6, &FONT_B, "SIX MACHINES", MACHINES[M_KICK].rgb);
        int_text(set.bpm, t);
        text_cat(t, " BPM");
        cv_text(200 - text_w(&FONT_XS, t), 10, &FONT_XS, t, C_AMB);
        if (ui.playing) {                /* a triangle: playing */
            for (i = 0; i < 6u; i++)
                cv_rect(214 + (int32_t)i, 9 + (int32_t)i, 1, 12 - 2 * (int32_t)i, C_HI);
        } else {
            cv_rect(213, 9, 10, 10, C_DIM);
        }
    }
    cv_rect(0, 29, 240, 1, C_LINE);
    cv_blit(0, 0);
    /* the rack, and the knobs (or a sweetspot just reached, big) */
    cv_begin(240, 110, C_BLACK);
    draw_rack(4);
    if (now < ui.zone_until && ui.zone_k >= 0) {
        const macro_t *mc = &MC.macro[ui.zone_k];
        ctext(120, 40, &FONT_XS, mc->name, C_GRAY);
        ctext(120, 54, &APP_BIG_FONT, mc->zone[ui.zone_z], MC.rgb);
        cv_rect(60, 96, 120, 2, MC.rgb);
    } else {
        for (i = 0; i < 4u; i++)
            draw_knob((int)i, 4 + (int32_t)i * 59, 40, now);
        cv_text(4, 84, &FONT_XS, "PRESET", C_DIM);
        cv_text(48, 84, &FONT_XS, MC.preset[TR.preset].name, C_AMB);
        t[0] = 0;
        text_cat(t, "SWING ");
        int_text(set.swing, t + 6);
        text_cat(t, "%");
        cv_text(236 - text_w(&FONT_XS, t), 84, &FONT_XS, t, C_AMB);
    }
    cv_blit(0, 30);
    /* the picked machine's 16 steps, two rows of eight, in its colour; the other machines' hits as small marks at the
     * cell's top; the step under way lit white */
    cv_begin(240, 100, C_BLACK);
    cv_rect(0, 0, 240, 1, C_LINE);
    for (i = 0; i < 16u; i++) {
        int32_t x = 6 + (int32_t)(i % 8u) * 29, y = 10 + (int32_t)(i / 8u) * 36, mx = x + 3;
        int on = TR.steps >> i & 1u, acc = on && (TR.acc >> i & 1u);
        uint16_t c = acc ? MC.rgb : on ? dim_of(MC.rgb) : C_LINE;
        cv_rect(x, y, 25, 28, c);
        if (!on)
            cv_rect(x + 1, y + 1, 23, 26, C_BLACK);
        if (acc)
            cv_rect(x + 4, y + 4, 17, 3, C_WHITE);
        for (j = 0; j < NMACHINES; j++)  /* the others */
            if (j != set.sel && MACHINES[j].ready && (set.tr[j].steps >> i & 1u)) {
                cv_rect(mx, y + 22, 4, 3, (ui.muted >> j & 1u) ? C_DIM : MACHINES[j].rgb);
                mx += 6;
            }
        if ((int32_t)i == s)
            cv_rect(x, y + 30, 25, 3, C_WHITE);
        else if (i % 4u == 0)
            cv_rect(x, y + 30, 3, 3, C_DIM);   /* the beats */
    }
    /* the audio load (the render's share of its time) and the dropouts since the start: never more than a glance */
    t[0] = 0;
    text_cat(t, "CPU ");
    int_text((int32_t)plat_cpu_pct(), t + 4);
    text_cat(t, "%");
    {
        int32_t x = cv_text(6, 84, &FONT_XS, t, plat_cpu_pct() > 80u ? C_WHITE : C_DIM);
        if (plat_xruns()) {
            char d[16] = "DROPS ";
            int_text((int32_t)plat_xruns(), d + 6);
            cv_text(x + 10, 84, &FONT_XS, d, C_WHITE);
        }
    }
    cv_text(234 - text_w(&FONT_XS, MC.name), 84, &FONT_XS, MC.name, MC.rgb);
    cv_blit(0, 140);
}

/* ------------------------------------------------------------- the contract --- */
void app_init(void)
{
    eng_init();
    settings_load();
    knobs_from_settings();
    apply();
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    eng_render(lr, n, (float)master_q12 * (1.0f / 4096.0f) * 8300000.0f);
}

void app_panic(void)
{
    ui.playing = 0;
    eng_panic();
}

void app_splash(void)                    /* the rack, powered: the name over six modules */
{
    uint32_t i;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    cv_begin(240, 120, C_BLACK);
    ctext(120, 52, &APP_BIG_FONT, "SIX", MACHINES[M_KICK].rgb);
    ctext(120, 84, &APP_BIG_FONT, "MACHINES", C_WHITE);
    cv_blit(0, 20);
    cv_begin(240, 80, C_BLACK);
    for (i = 0; i < NMACHINES; i++) {
        int32_t x = 3 + (int32_t)i * 39;
        uint16_t c = MACHINES[i].ready ? MACHINES[i].rgb : RGB(70, 70, 78);
        cv_rect(x, 6, 37, 26, c);
        cv_rect(x + 2, 8, 33, 22, C_BLACK);
        ctext(x + 18, 13, &FONT_XS, MACHINES[i].name, MACHINES[i].ready ? C_WHITE : RGB(120, 120, 128));
    }
    ctext(120, 52, &FONT_XS, "by swtdr34mz", RGB(150, 150, 158));
    cv_blit(0, 140);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.zone_k = -1;
    ui.step_drawn = -2;
    ui.redraw = 1;
}

void app_frame(void)
{
    uint32_t now = plat_ms(), i;
    int32_t s;
    uint8_t hit = 0;
    input();
    lights();
    if (ann_on() && ann_where()) {       /* a screen reader that just started listening hears the page */
        char t[64] = "SIX MACHINES. ";
        text_cat(t, MC.name);
        text_cat(t, ". ");
        text_cat(t, MC.preset[TR.preset].name);
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(now);
    s = eng_step();
    for (i = 0; i < NMACHINES; i++)
        if (eng_env((int)i) > 0.25f)
            hit |= (uint8_t)(1u << i);
    if (s != ui.step_drawn || hit != ui.hit_drawn) {
        ui.step_drawn = s;
        ui.hit_drawn = hit;
        ui.redraw = 1;
    }
    if ((ui.msg_until && now >= ui.msg_until) || (ui.zone_until && now >= ui.zone_until)) {
        if (now >= ui.msg_until)
            ui.msg_until = 0;
        if (now >= ui.zone_until)
            ui.zone_until = 0;
        ui.redraw = 1;
    }
    if (now - ui.load_at >= 500u) {
        uint32_t l = plat_cpu_pct() | plat_xruns() << 8;
        ui.load_at = now;
        if (l != ui.load_shown) {
            ui.load_shown = l;
            ui.redraw = 1;
        }
    }
    if (set_dirty && now - dirty_at > 4000u)   /* saved a few seconds after the last change */
        settings_save();
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    }
}

/* ------------------------------------------------------------- backup ------ */
int app_backup_playing(void) { return ui.playing; }
void app_backup_flush(void)
{
    if (set_dirty)
        settings_save();
}
void app_backup_hold(int on) { (void)on; }
void app_backup_restored(void)
{
    settings_load();
    knobs_from_settings();
    apply();
    ui.redraw = 1;
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "steps"))          /* the picked machine's */
        *got = TR.steps;
    else if (!strcmp(what, "acc"))
        *got = TR.acc;
    else if (!strcmp(what, "sel"))
        *got = set.sel;
    else if (!strcmp(what, "muted"))
        *got = ui.muted;
    else if (!strcmp(what, "bpm"))
        *got = set.bpm;
    else if (!strcmp(what, "swing"))
        *got = set.swing;
    else if (!strcmp(what, "preset"))
        *got = TR.preset;
    else if (!strcmp(what, "level"))
        *got = TR.level;
    else if (!strcmp(what, "playing"))
        *got = ui.playing;
    else if (!strcmp(what, "step"))
        *got = eng_step();
    else if (!strcmp(what, "hits"))
        *got = (int)eng_hits();
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strncmp(what, "steps", 5) && what[5] >= '0' && what[5] <= '5')   /* steps0..5: a machine's */
        *got = set.tr[what[5] - '0'].steps;
    else if (what[0] == 'm' && what[1] >= '1' && what[1] <= '4' && !what[2])   /* m1..m4: the picked machine's macros */
        *got = TR.m[what[1] - '1'];
    else if (!strncmp(what, "zone", 4) && what[4] >= '1' && what[4] <= '4')   /* zone1..4: on a sweetspot (-1 not) */
        *got = zone_at(&MC.macro[what[4] - '1'], TR.m[what[4] - '1']);
    else
        return 0;
    return 1;
}
#endif
