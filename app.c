/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES: input, the pages of knobs and their sweetspots, the patterns (p-locks, conditions, retrigs), the
 * performance pads, the screen, settings. Part of the unity build (device/unity.c, host/plat_host.c), after the
 * platform: gfx.c and announce.c are in scope. The sound is dsp/engine.c.
 *
 * Controls
 *   black keys      the six machines (KICK SNARE TOMS METAL HATS CLAP): a key picks the machine (and plays it while the
 *                   sequencer is stopped); SEQ held + a key: the machine muted / back
 *   white keys      the picked machine's steps (the page of 16 shown): a tap turns a step on, again ACCENT, again off.
 *                   A step held: K1..K4 lock the machine's macros on it (p-locks), SELECT its condition, ALGORITHM its
 *                   retrigs, PRESET clears its locks. In PERF mode (ARP) the white keys are the performance pads
 *   K1..K4          the page's four knobs; turned slowly each rests a few detents on its sweetspots
 *   pages           HOME the machine's SOUND, SEL its SAT, ENV its ENV, LFO its LFO, EDIT its MIX; FX the MASTER (again:
 *                   DELAY); GLO PERFORM (the DJ filter, the scene, the build-up's length, the filter's resonance).
 *                   The same button again: back to SOUND
 *   SELECT          tempo (BPM)      ALGORITHM  swing      PRESET  a start preset for the machine's SOUND
 *   ARP             PERF mode: the white keys are the pads (repeat, tape stop, build-up, throws, kills, fill, scene,
 *                   break, freeze, drop), held
 *   SEQ             16 / 32 steps (to 32: the first 16 copied when the second half is empty)
 *   OCT- / OCT+     the page of steps shown (1-16, 17-32)
 *   SAVE            scene B: the six machines' SOUND now, for SCENE (PERFORM) and its pad to morph to; held 1 s: gone
 *   PLAY            the sequencer: start from step 1 / stop.   REC held 1 s: the machine's pattern cleared
 *   Settings save themselves a few seconds after the last change. */
#include "fastmath.h"
#include "engine.h"

#define SET_MAGIC 0x33584D36u            /* "6MX3": the settings record; another magic loads the defaults */
typedef struct {
    uint32_t magic;
    track_t tr[NMACHINES];
    master_t ms;
    uint8_t sel;                         /* the machine picked */
    uint8_t rsv[3];
} settings_t;
_Static_assert(sizeof(settings_t) <= PLAT_STORE_MAX, "the settings outgrew a store object");
static settings_t set;
static uint8_t set_dirty;
static uint32_t dirty_at;

static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
static const char *const PAD_SHORT[NPADS] = {"1/4", "1/8", "1/16", "1/32", "TAPE", "BLD", "RVB", "DLY",
                                             "K-LO", "K-MD", "K-HI", "FILL", "SCN", "BRK", "FRZ", "DROP"};

#define TR (set.tr[set.sel])             /* the picked machine's track */
#define MC (MACHINES[set.sel])           /* and the machine */

/* --------------------------------------------------------------- settings --- */
/* the first beat: four on the floor (the first accented), the snare on 2 and 4, the hats on the offbeats */
static const uint16_t DEF_STEPS[NMACHINES] = {0x1111u, 0x1010u, 0, 0, 0x4444u, 0};
static const uint16_t DEF_ACC[NMACHINES] = {0x0001u, 0, 0, 0, 0x4444u, 0};
static const uint8_t DEF_REVERB[NMACHINES] = {0, 30, 20, 30, 0, 40};

static void settings_defaults(void)
{
    uint32_t i, k, s;
    memset(&set, 0, sizeof set);
    set.magic = SET_MAGIC;
    for (i = 0; i < NMACHINES; i++) {
        track_t *t = &set.tr[i];
        for (k = 0; k < 4u; k++) {
            t->m[k] = MACHINES[i].preset[0].m[k];
            t->sat[k] = PAGE_DEFAULT[PG_SAT][k];
            t->env[k] = PAGE_DEFAULT[PG_ENV][k];
            t->lfo[k] = PAGE_DEFAULT[PG_LFO][k];
            t->mix[k] = PAGE_DEFAULT[PG_MIX][k];
            set.ms.scene[i][k] = t->m[k];
        }
        t->mix[2] = DEF_REVERB[i];
        t->steps = DEF_STEPS[i];
        t->acc = DEF_ACC[i];
        for (s = 0; s < NSTEPS; s++)
            for (k = 0; k < 4u; k++)
                t->lock[s][k] = NO_LOCK;
    }
    for (k = 0; k < 4u; k++) {
        set.ms.fx[k] = PAGE_DEFAULT[PG_FX][k];
        set.ms.fx2[k] = PAGE_DEFAULT[PG_FX2][k];
        set.ms.glo[k] = PAGE_DEFAULT[PG_GLO][k];
    }
    set.ms.length = 16;
    set.ms.bpm = 124;
    set.sel = M_KICK;
}

static int settings_ok(void)
{
    uint32_t i, k, s;
    for (i = 0; i < NMACHINES; i++) {
        const track_t *t = &set.tr[i];
        for (k = 0; k < 4u; k++)
            if (t->m[k] > 127u || t->sat[k] > 127u || t->env[k] > 127u || t->lfo[k] > 127u || t->mix[k] > 127u)
                return 0;
        if (t->preset >= NPRESETS)
            return 0;
        for (s = 0; s < NSTEPS; s++)
            if (t->cond[s] >= NCONDS || t->retrig[s] > 4u)
                return 0;
    }
    return set.magic == SET_MAGIC && set.ms.bpm >= 60u && set.ms.bpm <= 200u && set.ms.swing <= 100u
           && (set.ms.length == 16u || set.ms.length == 32u) && set.sel < NMACHINES;
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

static int zone_at(const macro_t *mc, int32_t v)   /* the zone the value is on (-1: between zones) */
{
    uint32_t z;
    for (z = 0; z < mc->nzones; z++)
        if (mc->centre[z] == v)
            return (int)z;
    return -1;
}

static int zone_near(const macro_t *mc, int32_t v)   /* the zone nearest the value */
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

/* ---------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][24];
    uint32_t msg_until;
    int8_t zone_k, zone_z;               /* a sweetspot just reached: knob, zone (shown big) */
    uint32_t zone_until;
    uint32_t knob_at[4];                 /* when each knob was last turned */
    uint32_t keys, btn;
    uint32_t rec_t, save_t, seq_t;       /* when REC, SAVE, SEQ went down (0: not, or already used) */
    uint8_t seq_used;                    /* a machine muted while SEQ was held: no length change on its release */
    uint32_t enc_at[NE];
    uint8_t page;                        /* PG_* */
    uint8_t view;                        /* the page of steps shown: 0 = 1-16, 1 = 17-32 */
    uint8_t perf;                        /* PERF mode: the white keys are the pads */
    uint32_t pads;
    int8_t held;                         /* the step held (-1 none), and whether it was edited (no toggle on release) */
    uint8_t held_edit, held_was_on;
    uint32_t held_at;
    uint8_t muted;                       /* bit per machine (live: not kept) */
    int32_t step_drawn;
    uint8_t hit_drawn;
    uint8_t playing;
    uint8_t redraw;
    uint32_t load_at, load_shown;        /* the audio load on the screen, looked at twice a second */
    uint32_t intro_at;                   /* the intro: the rack powering up, a module (and its sound) at a time */
    int8_t intro;                        /* the modules lit so far (-1: over) */
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

/* the page's knobs and the bytes they set; with a step held on SOUND, its locks */
static const macro_t *page_knob(int k)
{
    return ui.page == PG_SOUND ? &MC.macro[k] : &PAGE_KNOBS[ui.page][k];
}
static uint8_t *page_vals(void)
{
    switch (ui.page) {
    case PG_SAT: return TR.sat;
    case PG_ENV: return TR.env;
    case PG_LFO: return TR.lfo;
    case PG_MIX: return TR.mix;
    case PG_FX: return set.ms.fx;
    case PG_FX2: return set.ms.fx2;
    case PG_GLO: return set.ms.glo;
    default: return TR.m;
    }
}
static int locking(void) { return ui.held >= 0 && ui.page == PG_SOUND && !ui.perf; }
static int32_t knob_val(int k)
{
    if (locking()) {
        uint8_t l = TR.lock[ui.held][k];
        return l == NO_LOCK ? TR.m[k] : l;
    }
    return page_vals()[k];
}
static const char *zone_name(const macro_t *mc, int z)   /* a TARGET's zones: the machine's macro names */
{
    return mc->zone[z] ? mc->zone[z] : MC.macro[z].name;
}

static void knobs_sync(void)             /* the knobs' positions from the values they show now */
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        knob_pos[k] = pos_of_val(page_knob((int)k), knob_val((int)k));
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
    const macro_t *mc = page_knob(k);
    int32_t p = clamp(knob_pos[k] + accel(EN_K1 + k, e, 3, 6), 0, pos_max(mc)), v = val_of_pos(mc, p);
    int32_t was = knob_val(k);
    int zb = zone_at(mc, was), za = zone_at(mc, v);
    knob_pos[k] = p;
    ui.knob_at[k] = plat_ms();
    ui.redraw = 1;
    if (v == was)
        return;
    if (locking()) {                     /* a p-lock on the step held */
        TR.lock[ui.held][k] = (uint8_t)v;
        ui.held_edit = 1;
        if (!(TR.steps >> ui.held & 1u))
            TR.steps |= 1u << ui.held;
    } else {
        page_vals()[k] = (uint8_t)v;
    }
    eng_changed(set.sel);
    dirty();
    if (za >= 0 && za != zb) {           /* a sweetspot reached: its name, big */
        ui.zone_k = (int8_t)k;
        ui.zone_z = (int8_t)za;
        ui.zone_until = plat_ms() + 700u;
        if (ann_on())
            ann_say(zone_name(mc, za), 0, ANN_NOW);
    }
}

static void page_set(int pg)
{
    ui.page = (uint8_t)pg;
    ui.zone_until = 0;
    knobs_sync();
    ui.redraw = 1;
    app_say(pg == PG_SOUND ? MC.name : PAGE_NAME[pg], pg == PG_SOUND ? "SOUND" : pg >= PG_FX ? "" : MC.name);
}

static void preset_load(int32_t d)
{
    uint32_t k;
    TR.preset = (uint8_t)(((int32_t)TR.preset + d % NPRESETS + NPRESETS) % NPRESETS);
    for (k = 0; k < 4u; k++)
        TR.m[k] = MC.preset[TR.preset].m[k];
    knobs_sync();
    eng_changed(set.sel);
    if (!ui.playing)
        eng_hit(set.sel, 1.0f);           /* stopped: hear it */
    dirty();
    app_say(MC.name, MC.preset[TR.preset].name);
}

static int32_t len_steps(void) { return set.ms.length == 32u ? 32 : 16; }

/* a step key down: off -> on at once (and held: its locks, condition, retrigs); an on step changes on release */
static void step_down(uint32_t i)
{
    int32_t s = (int32_t)i + 16 * ui.view;
    uint32_t bit = 1u << s;
    if (s >= len_steps())
        return;
    ui.held = (int8_t)s;
    ui.held_edit = 0;
    ui.held_at = plat_ms();
    ui.held_was_on = (TR.steps & bit) != 0;
    if (!ui.held_was_on) {
        TR.steps |= bit;
        TR.acc &= ~bit;
        if (!ui.playing)
            eng_hit(set.sel, 0.72f);      /* stopped: hear what the step will play */
        dirty();
    }
    knobs_sync();
    ui.redraw = 1;
}

static void step_up(void)                /* on -> accent -> off, unless it was held to edit */
{
    int32_t s = ui.held;
    uint32_t bit = 1u << s;
    if (ui.held_was_on && !ui.held_edit && plat_ms() - ui.held_at < 500u) {
        if (!(TR.acc & bit)) {
            TR.acc |= bit;
            if (!ui.playing)
                eng_hit(set.sel, 1.0f);
        } else {
            uint32_t k;
            TR.steps &= ~bit;
            TR.acc &= ~bit;
            for (k = 0; k < 4u; k++)     /* a step turned off forgets its locks, condition and retrigs */
                TR.lock[s][k] = NO_LOCK;
            TR.cond[s] = 0;
            TR.retrig[s] = 0;
        }
        dirty();
    }
    ui.held = -1;
    knobs_sync();
    ui.redraw = 1;
}

static void play_toggle(void)
{
    ui.playing = (uint8_t)!ui.playing;
    eng_play(ui.playing);
    ui.redraw = 1;
}

static void length_toggle(void)
{
    uint32_t t;
    if (set.ms.length == 16u) {
        int empty = 1;
        for (t = 0; t < NMACHINES; t++)
            if (set.tr[t].steps >> 16)
                empty = 0;
        if (empty)                       /* the second half empty: the first one copied into it */
            for (t = 0; t < NMACHINES; t++) {
                track_t *tr = &set.tr[t];
                uint32_t s;
                tr->steps |= (tr->steps & 0xFFFFu) << 16;
                tr->acc |= (tr->acc & 0xFFFFu) << 16;
                for (s = 0; s < 16u; s++) {
                    uint32_t k;
                    for (k = 0; k < 4u; k++)
                        tr->lock[s + 16u][k] = tr->lock[s][k];
                    tr->cond[s + 16u] = tr->cond[s];
                    tr->retrig[s + 16u] = tr->retrig[s];
                }
            }
        set.ms.length = 32;
    } else {
        set.ms.length = 16;
        ui.view = 0;
    }
    dirty();
    app_say("LENGTH", set.ms.length == 32u ? "32 STEPS" : "16 STEPS");
}

/* a black key: SEQ held, the machine muted or back; else the machine picked (and played while stopped) */
static void machine_key(uint32_t i, uint32_t btn)
{
    if (btn >> B_SEQ & 1u) {
        ui.muted ^= (uint8_t)(1u << i);
        ui.seq_used = 1;
        eng_mute((int)i, ui.muted >> i & 1u);
        app_say(MACHINES[i].name, (ui.muted >> i & 1u) ? "MUTED" : "ON");
        return;
    }
    if (!ui.playing)
        eng_hit((int)i, 1.0f);
    if (set.sel != i) {
        set.sel = (uint8_t)i;
        ui.held = -1;
        knobs_sync();
        ui.zone_until = 0;
        dirty();
    }
    ui.redraw = 1;
}

static void scene_save(void)
{
    uint32_t t, k;
    for (t = 0; t < NMACHINES; t++)
        for (k = 0; k < 4u; k++)
            set.ms.scene[t][k] = set.tr[t].m[k];
    set.ms.has_scene = 1;
    dirty();
    app_say("SCENE B", "SAVED");
}

static void button(uint32_t i)
{
    static const int8_t PAGE_OF[NB] = {PG_FX, PG_SAT, PG_ENV, PG_LFO, PG_MIX, PG_GLO, PG_SOUND, -1, -1, -1, -1, -1,
                                       -1, -1};
    if (PAGE_OF[i] >= 0) {
        int pg = PAGE_OF[i];
        if (i == B_FX)
            pg = ui.page == PG_FX ? PG_FX2 : ui.page == PG_FX2 ? PG_SOUND : PG_FX;
        else if (ui.page == pg)
            pg = PG_SOUND;
        page_set(pg);
        return;
    }
    switch (i) {
    case B_PLAY: play_toggle(); break;
    case B_REC: ui.rec_t = plat_ms(); break;
    case B_SAVE: ui.save_t = plat_ms(); break;
    case B_SEQ:
        ui.seq_t = plat_ms();
        ui.seq_used = 0;
        break;
    case B_ARP:
        ui.perf = (uint8_t)!ui.perf;
        ui.held = -1;
        ui.pads = 0;
        eng_pads(0);
        if (ui.perf && ui.page != PG_GLO)
            page_set(PG_GLO);
        app_say("PERFORM", ui.perf ? "PADS ON" : "PADS OFF");
        break;
    case B_OCTDN:
    case B_OCTUP:
        if (set.ms.length == 32u) {
            ui.view = i == B_OCTUP ? 1 : 0;
            app_say("STEPS", ui.view ? "17-32" : "1-16");
        } else {
            app_say("LENGTH", "16 STEPS");
        }
        break;
    default: break;
    }
}

static void held_buttons(uint32_t btn)   /* the ones that act on release, or after a hold */
{
    uint32_t now = plat_ms();
    if (ui.rec_t) {                      /* REC held a second: the machine's pattern cleared */
        if (!(btn >> B_REC & 1u))
            ui.rec_t = 0;
        else if (now - ui.rec_t >= 1000u) {
            uint32_t s, k;
            ui.rec_t = 0;
            TR.steps = TR.acc = 0;
            for (s = 0; s < NSTEPS; s++) {
                for (k = 0; k < 4u; k++)
                    TR.lock[s][k] = NO_LOCK;
                TR.cond[s] = TR.retrig[s] = 0;
            }
            dirty();
            app_say(MC.name, "CLEARED");
        }
    }
    if (ui.save_t) {                     /* SAVE: scene B; held a second: none */
        if (!(btn >> B_SAVE & 1u)) {
            ui.save_t = 0;
            scene_save();
        } else if (now - ui.save_t >= 1000u) {
            ui.save_t = 0;
            set.ms.has_scene = 0;
            dirty();
            app_say("SCENE B", "CLEARED");
        }
    }
    if (ui.seq_t && !(btn >> B_SEQ & 1u)) {   /* SEQ let go: the length, unless it muted something */
        if (!ui.seq_used && now - ui.seq_t < 600u)
            length_toggle();
        ui.seq_t = 0;
    }
}

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), i, pads = 0;
    int32_t e;
    for (i = 0; i < 16u; i++) {          /* the steps, or the pads */
        uint32_t bit = 1u << WK[i];
        if (ui.perf) {
            if (keys & bit)
                pads |= 1u << i;
            continue;
        }
        if ((keys & bit) && !(ui.keys & bit) && ui.held < 0)
            step_down(i);
        else if (!(keys & bit) && (ui.keys & bit) && ui.held == (int8_t)(i + 16u * ui.view))
            step_up();
    }
    if (ui.perf && pads != ui.pads) {
        uint32_t on = pads & ~ui.pads;
        for (i = 0; i < NPADS; i++)
            if (on >> i & 1u)
                app_say("PAD", PAD_NAME[i]);
        ui.pads = pads;
        eng_pads(pads);
        ui.redraw = 1;
    }
    for (i = 0; i < NMACHINES; i++) {    /* the machines */
        uint32_t bit = 1u << BK[i];
        if ((keys & bit) && !(ui.keys & bit))
            machine_key(i, btn);
    }
    for (i = 0; i < NB; i++)
        if ((btn >> i & 1u) && !(ui.btn >> i & 1u))
            button(i);
    held_buttons(btn);
    for (i = 0; i < 4u; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0)
            knob((int)i, e);
    if ((e = plat_enc(EN_SELECT)) != 0) {
        if (ui.held >= 0) {              /* the step's condition */
            int32_t c = clamp((int32_t)TR.cond[ui.held] + (e > 0 ? 1 : -1), 0, NCONDS - 1);
            TR.cond[ui.held] = (uint8_t)c;
            ui.held_edit = 1;
            dirty();
            app_say("CONDITION", COND_NAME[c]);
        } else {
            char t[8];
            set.ms.bpm = (uint8_t)clamp((int32_t)set.ms.bpm + accel(EN_SELECT, e, 2, 5), 60, 200);
            dirty();
            int_text(set.ms.bpm, t);
            app_say("BPM", t);
        }
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        char t[8];
        if (ui.held >= 0) {              /* the step's retrigs */
            int32_t r = clamp((TR.retrig[ui.held] < 2u ? 1 : (int32_t)TR.retrig[ui.held]) + (e > 0 ? 1 : -1), 1, 4);
            TR.retrig[ui.held] = (uint8_t)(r < 2 ? 0 : r);
            ui.held_edit = 1;
            dirty();
            int_text(r, t);
            text_cat(t, r == 1 ? " HIT" : " HITS");
            app_say("RETRIG", t);
        } else {
            set.ms.swing = (uint8_t)clamp((int32_t)set.ms.swing + accel(EN_ALGO, e, 2, 4), 0, 100);
            dirty();
            int_text(set.ms.swing, t);
            text_cat(t, "%");
            app_say("SWING", t);
        }
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        if (ui.held >= 0) {              /* the step's locks cleared */
            uint32_t k;
            for (k = 0; k < 4u; k++)
                TR.lock[ui.held][k] = NO_LOCK;
            ui.held_edit = 1;
            knobs_sync();
            dirty();
            app_say("LOCKS", "CLEARED");
        } else {
            preset_load(e > 0 ? 1 : -1);
        }
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
}

static void lights(void)
{
    uint32_t lit = 0, mid = 0, dim = 0, i, btn = 0, dimb;
    int32_t s = eng_step();
    static const uint8_t PAGE_BTN[NPAGES] = {B_HOME, B_SEL, B_ENV, B_LFO, B_EDIT, B_FX, B_FX, B_GLO};
    if (ui.perf) {                       /* the pads: held lit, the others glow */
        for (i = 0; i < 16u; i++)
            if (ui.pads >> i & 1u)
                lit |= 1u << WK[i];
            else
                dim |= 1u << WK[i];
    } else {
        for (i = 0; i < 16u; i++) {      /* the steps shown: accents lit, steps half */
            uint32_t st = i + 16u * ui.view, bit = 1u << WK[i];
            if (TR.steps >> st & 1u) {
                if (TR.acc >> st & 1u)
                    lit |= bit;
                else
                    mid |= bit;
            }
        }
        if (s >= 0 && (s >> 4) == ui.view)   /* the step under way: lit, or out if it plays */
            lit ^= 1u << WK[s & 15];
    }
    for (i = 0; i < NMACHINES; i++) {    /* the machines: the picked one lit, a hit flashes, the others glow */
        uint32_t bit = 1u << BK[i];
        if (ui.muted >> i & 1u)
            continue;
        if (i == set.sel || eng_env((int)i) > 0.3f)
            lit |= bit;
        else
            dim |= bit;
    }
    if (ui.playing)
        btn |= LED_PLAY_GREEN;
    btn |= 1u << PAGE_BTN[ui.page];
    if (ui.perf)
        btn |= 1u << B_ARP;
    dimb = (1u << B_PLAY) | (1u << B_REC) | (1u << B_SEQ) | (1u << B_SAVE) | (1u << B_ARP) | (1u << B_FX)
           | (1u << B_SEL) | (1u << B_ENV) | (1u << B_LFO) | (1u << B_EDIT) | (1u << B_GLO) | (1u << B_HOME);
    if (set.ms.length == 32u)
        dimb |= (1u << B_OCTDN) | (1u << B_OCTUP);
    plat_lights(btn | (ui.btn & ~(1u << B_PLAY)), lit, dimb, dim, 1);
    plat_lights_mid(0, mid & ~lit);
}

void app_input_only(void)
{
    if (ui.intro < 0)
        input();
}

/* ------------------------------------------------------------------ drawing --- */
static uint16_t dim_of(uint16_t c)       /* a colour at about a third */
{
    uint32_t r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
    return (uint16_t)(((r * 3u / 8u) << 11) | ((g * 3u / 8u) << 5) | (b * 3u / 8u));
}

static void ctext(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}

/* the rack: six modules; the picked one framed in its colour, each flashing with its hits; muted ones crossed out */
static void draw_rack(int32_t y)
{
    uint32_t i;
    for (i = 0; i < NMACHINES; i++) {
        const machine_t *m = &MACHINES[i];
        int32_t x = 3 + (int32_t)i * 39;
        float env = eng_env((int)i);
        int sel = i == set.sel, mute = ui.muted >> i & 1u;
        cv_rect(x, y, 37, 24, sel ? m->rgb : env > 0.25f ? dim_of(m->rgb) : C_LINE);
        cv_rect(x + (sel ? 2 : 1), y + (sel ? 2 : 1), sel ? 33 : 35, sel ? 20 : 22, C_BLACK);
        if (!mute)
            cv_rect(x + 4, y + 17, (int32_t)(29.0f * fm_clampf(env * 1.4f, 0.0f, 1.0f)), 3, m->rgb);
        ctext(x + 18, y + 4, &FONT_XS, m->name, mute ? C_DIM : sel ? C_WHITE : C_GRAY);
        if (mute)
            cv_line(x + 4, y + 19, x + 32, y + 4, C_GRAY);
    }
}

/* one knob: its name, a bar 0..127 with the sweetspots as ticks (a bipolar one from the middle), the value, the zone
 * it is on (lit) or near (dim); the modulation on a SOUND macro as a white mark; a p-lock in white */
static void draw_knob(int k, int32_t x, int32_t y, uint32_t now, uint16_t col)
{
    const macro_t *mc = page_knob(k);
    int32_t v = knob_val(k), w = 54, z = zone_at(mc, v), zn = zone_near(mc, v), fill = (v * (w - 2)) / 127;
    uint32_t i;
    int hot = ui.knob_at[k] && now - ui.knob_at[k] < 600u, lock = locking() && TR.lock[ui.held][k] != NO_LOCK;
    int bip = mc->nzones == 5 && mc->centre[2] == 64 && mc->centre[0] == 4;
    char t[8];
    cv_text(x, y, &FONT_XS, mc->name, lock ? C_WHITE : hot ? C_WHITE : C_GRAY);
    cv_rect(x, y + 14, w, 8, C_LINE);
    if (bip) {
        int32_t c = x + w / 2, f = ((v - 64) * (w / 2 - 1)) / 63;
        if (f >= 0)
            cv_rect(c, y + 15, f, 6, z >= 0 ? col : C_HI);
        else
            cv_rect(c + f, y + 15, -f, 6, z >= 0 ? col : C_HI);
    } else {
        cv_rect(x + 1, y + 15, fill, 6, lock ? C_WHITE : z >= 0 ? col : C_HI);
    }
    for (i = 0; i < mc->nzones; i++) {   /* the sweetspots */
        int32_t tx = x + 1 + (mc->centre[i] * (w - 3)) / 127;
        cv_rect(tx, y + 11, 1, 3, (int)i == z ? col : C_GRAY);
    }
    if (ui.page == PG_SOUND) {           /* the modulation now */
        int8_t md[4];
        int32_t mv;
        eng_mod(set.sel, md);
        if (md[k]) {
            mv = clamp(v + md[k], 0, 127);
            cv_rect(x + 1 + (mv * (w - 3)) / 127, y + 13, 2, 10, C_WHITE);
        }
    }
    if (hot && z < 0) {                  /* turning between zones: the value */
        int_text(bip ? v - 64 : v, t);
        ctext(x + w / 2, y + 26, &FONT_XS, t, C_HI);
    } else {
        ctext(x + w / 2, y + 26, &FONT_XS, zone_name(mc, z >= 0 ? z : zn), z >= 0 ? C_WHITE : C_DIM);
    }
}

static void draw_page(uint32_t now)      /* the page's knobs, and a line under them */
{
    uint32_t i;
    char t[32];
    uint16_t col = ui.page >= PG_FX ? C_HI : MC.rgb;
    for (i = 0; i < 4u; i++)
        draw_knob((int)i, 4 + (int32_t)i * 59, 38, now, col);
    if (ui.held >= 0) {                  /* a step held: its number, condition, retrigs */
        int32_t r = TR.retrig[ui.held] < 2u ? 1 : TR.retrig[ui.held];
        t[0] = 0;
        text_cat(t, "STEP ");
        int_text(ui.held + 1, t + 5);
        cv_text(4, 82, &FONT_XS, t, C_WHITE);
        cv_text(60, 82, &FONT_XS, COND_NAME[TR.cond[ui.held]], C_HI);
        t[0] = 'x';
        int_text(r, t + 1);
        cv_text(130, 82, &FONT_XS, t, C_HI);
        cv_text(236 - text_w(&FONT_XS, "LOCKS"), 82, &FONT_XS, "LOCKS", ui.page == PG_SOUND ? C_AMB : C_DIM);
        return;
    }
    if (ui.page == PG_SOUND) {
        cv_text(4, 82, &FONT_XS, "PRESET", C_DIM);
        cv_text(48, 82, &FONT_XS, MC.preset[TR.preset].name, C_AMB);
    } else {
        cv_text(4, 82, &FONT_XS, PAGE_NAME[ui.page], C_HI);
        if (ui.page < PG_FX)
            cv_text(10 + text_w(&FONT_XS, PAGE_NAME[ui.page]), 82, &FONT_XS, MC.name, MC.rgb);
        if (ui.page == PG_GLO && set.ms.has_scene)
            cv_text(100, 82, &FONT_XS, "SCENE B SET", C_AMB);
    }
    t[0] = 0;
    text_cat(t, "SWING ");
    int_text(set.ms.swing, t + 6);
    text_cat(t, "%");
    cv_text(236 - text_w(&FONT_XS, t), 82, &FONT_XS, t, C_AMB);
}

static void draw_steps(int32_t s)        /* the page of steps shown, in the machine's colour; the others' hits as marks */
{
    uint32_t i, j;
    for (i = 0; i < 16u; i++) {
        int32_t st = (int32_t)i + 16 * ui.view, x = 6 + (int32_t)(i % 8u) * 29, y = 10 + (int32_t)(i / 8u) * 36;
        int32_t mx = x + 3;
        int on = TR.steps >> st & 1u, acc = on && (TR.acc >> st & 1u);
        uint16_t c = acc ? MC.rgb : on ? dim_of(MC.rgb) : C_LINE;
        if (st >= len_steps()) {
            cv_rect(x, y, 25, 28, C_BLACK);
            continue;
        }
        cv_rect(x, y, 25, 28, st == ui.held ? C_WHITE : c);
        if (!on || st == ui.held)
            cv_rect(x + 1, y + 1, 23, 26, on ? c : C_BLACK);
        if (acc)
            cv_rect(x + 4, y + 4, 17, 3, C_WHITE);
        if (on) {                        /* a lock, a condition, retrigs */
            uint32_t k;
            int lk = 0;
            for (k = 0; k < 4u; k++)
                if (TR.lock[st][k] != NO_LOCK)
                    lk = 1;
            if (lk)
                cv_rect(x + 19, y + 10, 4, 4, C_WHITE);
            if (TR.cond[st])
                cv_rect(x + 3, y + 10, 4, 4, C_BLACK);
            for (k = 1; k < TR.retrig[st]; k++)
                cv_rect(x + 3 + (int32_t)k * 4, y + 16, 2, 3, C_WHITE);
        }
        for (j = 0; j < NMACHINES; j++)  /* the others */
            if (j != set.sel && (set.tr[j].steps >> st & 1u)) {
                cv_rect(mx, y + 22, 3, 3, (ui.muted >> j & 1u) ? C_DIM : MACHINES[j].rgb);
                mx += 4;
            }
        if (st == s)
            cv_rect(x, y + 30, 25, 3, C_WHITE);
        else if (i % 4u == 0)
            cv_rect(x, y + 30, 3, 3, C_DIM);
    }
}

static void draw_pads(void)              /* PERF mode: the sixteen pads, held ones lit */
{
    uint32_t i;
    float b = eng_build();
    for (i = 0; i < 16u; i++) {
        int32_t x = 6 + (int32_t)(i % 8u) * 29, y = 10 + (int32_t)(i / 8u) * 36;
        int on = ui.pads >> i & 1u;
        cv_rect(x, y, 25, 28, on ? C_HI : C_LINE);
        if (!on)
            cv_rect(x + 1, y + 1, 23, 26, C_BLACK);
        ctext(x + 13, y + 9, &FONT_XS, PAD_SHORT[i], on ? C_BLACK : C_GRAY);
        if (i == P_BUILD && b > 0.0f)
            cv_rect(x + 2, y + 24, (int32_t)(21.0f * b), 2, on ? C_BLACK : C_HI);
    }
}

static void draw(void)
{
    uint32_t now = plat_ms();
    int32_t s = eng_step();
    char t[24];
    /* header: the name, or a message; the tempo and the transport */
    cv_begin(240, 30, C_BLACK);
    if (now < ui.msg_until) {
        int32_t x = cv_text(8, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_HI);
    } else {
        int32_t i;
        cv_text(8, 6, &FONT_B, "SIX MACHINES", MACHINES[M_KICK].rgb);
        int_text(set.ms.bpm, t);
        text_cat(t, " BPM");
        cv_text(200 - text_w(&FONT_XS, t), 10, &FONT_XS, t, C_AMB);
        if (ui.playing)
            for (i = 0; i < 6; i++)
                cv_rect(214 + i, 9 + i, 1, 12 - 2 * i, C_HI);
        else
            cv_rect(213, 9, 10, 10, C_DIM);
    }
    cv_rect(0, 29, 240, 1, C_LINE);
    cv_blit(0, 0);
    /* the rack, and the page's knobs (or a sweetspot just reached, big) */
    cv_begin(240, 110, C_BLACK);
    draw_rack(4);
    if (now < ui.zone_until && ui.zone_k >= 0) {
        const macro_t *mc = page_knob(ui.zone_k);
        uint16_t col = ui.page >= PG_FX ? C_HI : MC.rgb;
        ctext(120, 38, &FONT_XS, mc->name, C_GRAY);
        ctext(120, 52, &APP_BIG_FONT, zone_name(mc, ui.zone_z), col);
        cv_rect(60, 94, 120, 2, col);
    } else {
        draw_page(now);
    }
    cv_blit(0, 30);
    /* the steps, or the pads */
    cv_begin(240, 100, C_BLACK);
    cv_rect(0, 0, 240, 1, C_LINE);
    if (ui.perf)
        draw_pads();
    else
        draw_steps(s);
    /* the audio load and dropouts; the steps' page; the machine */
    t[0] = 0;
    text_cat(t, "CPU ");
    int_text((int32_t)plat_cpu_pct(), t + 4);
    text_cat(t, "%");
    {
        int32_t x = cv_text(6, 84, &FONT_XS, t, plat_cpu_pct() > 80u ? C_WHITE : C_DIM);
        if (plat_xruns()) {
            char d[16] = "DROPS ";
            int_text((int32_t)plat_xruns(), d + 6);
            x = cv_text(x + 8, 84, &FONT_XS, d, C_WHITE);
        }
        if (set.ms.length == 32u && !ui.perf)
            cv_text(x + 10, 84, &FONT_XS, ui.view ? "17-32" : "1-16", C_AMB);
    }
    cv_text(234 - text_w(&FONT_XS, ui.perf ? "PADS" : MC.name), 84, &FONT_XS, ui.perf ? "PADS" : MC.name,
            ui.perf ? C_HI : MC.rgb);
    cv_blit(0, 140);
}

/* ------------------------------------------------------------- the contract --- */
void app_init(void)
{
    plat_cpu2_set(1);                    /* the second core renders half the machines (if it cannot: all on one) */
    eng_init();
    settings_load();
    eng_bind(set.tr, &set.ms);
    ui.held = -1;
    knobs_sync();
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
        cv_rect(x, 6, 37, 26, MACHINES[i].rgb);
        cv_rect(x + 2, 8, 33, 22, C_BLACK);
        ctext(x + 18, 13, &FONT_XS, MACHINES[i].name, C_WHITE);
    }
    ctext(120, 52, &FONT_XS, "by swtdr34mz", RGB(150, 150, 158));
    cv_blit(0, 140);
}

/* the intro: the name, and the six modules switching on one after another, each with its own sound; any key or
 * button ends it */
#define INTRO_STEP 170u
static void draw_intro(int lit)
{
    uint32_t i;
    cv_begin(240, 120, C_BLACK);
    ctext(120, 52, &APP_BIG_FONT, "SIX", MACHINES[M_KICK].rgb);
    ctext(120, 84, &APP_BIG_FONT, "MACHINES", C_WHITE);
    cv_blit(0, 20);
    cv_begin(240, 80, C_BLACK);
    for (i = 0; i < NMACHINES; i++) {
        int32_t x = 3 + (int32_t)i * 39;
        int on = (int)i < lit;
        cv_rect(x, 6, 37, 26, on ? MACHINES[i].rgb : RGB(40, 40, 46));
        cv_rect(x + 2, 8, 33, 22, (int)i == lit - 1 ? dim_of(MACHINES[i].rgb) : C_BLACK);
        ctext(x + 18, 13, &FONT_XS, MACHINES[i].name, on ? C_WHITE : RGB(70, 70, 78));
    }
    ctext(120, 52, &FONT_XS, "by swtdr34mz", RGB(150, 150, 158));
    cv_blit(0, 140);
}

static int intro_frame(uint32_t now)     /* 1 while the intro runs */
{
    int k;
    if (ui.intro < 0)
        return 0;
    if (plat_keys() || plat_buttons()) { /* skipped */
        ui.intro = -1;
        ui.keys = plat_keys();
        ui.btn = plat_buttons();
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.redraw = 1;
        return 0;
    }
    k = (int)((now - ui.intro_at) / INTRO_STEP);
    if (k > NMACHINES + 3) {
        ui.intro = -1;
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.redraw = 1;
        return 0;
    }
    if (k > ui.intro && k <= NMACHINES) {
        ui.intro = (int8_t)k;
        eng_hit(k - 1, 0.55f);
        draw_intro(k);
    }
    return 1;
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.zone_k = -1;
    ui.held = -1;
    ui.step_drawn = -2;
    ui.redraw = 1;
    ui.intro = 0;
    ui.intro_at = plat_ms();
    draw_intro(0);
}

void app_frame(void)
{
    uint32_t now = plat_ms(), i;
    int32_t s;
    uint8_t hit = 0;
    if (intro_frame(now)) {
        plat_lights(0, 0, 0, 0, 0);
        return;
    }
    input();
    lights();
    if (ann_on() && ann_where()) {       /* a screen reader that just started listening hears the page */
        char t[64] = "SIX MACHINES. ";
        text_cat(t, MC.name);
        text_cat(t, ". ");
        text_cat(t, ui.page == PG_SOUND ? MC.preset[TR.preset].name : PAGE_NAME[ui.page]);
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
    if (ui.page == PG_SOUND && (TR.env[0] != 64u || TR.lfo[1] != 64u))
        ui.redraw = 1;                   /* the modulation marks move */
    if (eng_build() > 0.0f)
        ui.redraw = 1;
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
    eng_bind(set.tr, &set.ms);
    knobs_sync();
    ui.redraw = 1;
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "steps"))          /* the picked machine's */
        *got = (int)TR.steps;
    else if (!strcmp(what, "acc"))
        *got = (int)TR.acc;
    else if (!strcmp(what, "sel"))
        *got = set.sel;
    else if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "perf"))
        *got = ui.perf;
    else if (!strcmp(what, "pads"))
        *got = (int)ui.pads;
    else if (!strcmp(what, "muted"))
        *got = ui.muted;
    else if (!strcmp(what, "bpm"))
        *got = set.ms.bpm;
    else if (!strcmp(what, "swing"))
        *got = set.ms.swing;
    else if (!strcmp(what, "length"))
        *got = set.ms.length;
    else if (!strcmp(what, "view"))
        *got = ui.view;
    else if (!strcmp(what, "scene"))
        *got = set.ms.has_scene;
    else if (!strcmp(what, "preset"))
        *got = TR.preset;
    else if (!strcmp(what, "playing"))
        *got = ui.playing;
    else if (!strcmp(what, "step"))
        *got = eng_step();
    else if (!strcmp(what, "hits"))
        *got = (int)eng_hits();
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strncmp(what, "steps", 5) && what[5] >= '0' && what[5] <= '5')   /* steps0..5: a machine's */
        *got = (int)set.tr[what[5] - '0'].steps;
    else if (what[0] == 'm' && what[1] >= '1' && what[1] <= '4' && !what[2])   /* m1..m4: the picked machine's macros */
        *got = TR.m[what[1] - '1'];
    else if (what[0] == 'k' && what[1] >= '1' && what[1] <= '4' && !what[2])   /* k1..k4: the knobs' values now */
        *got = knob_val(what[1] - '1');
    else if (!strncmp(what, "lock", 4) && what[4] >= '1' && what[4] <= '4')   /* lockK.S: step S's lock on macro K */
        *got = TR.lock[atoi(what + 6) - 1][what[4] - '1'];
    else if (!strncmp(what, "cond", 4))  /* condS, retrigS */
        *got = TR.cond[atoi(what + 4) - 1];
    else if (!strncmp(what, "retrig", 6))
        *got = TR.retrig[atoi(what + 6) - 1];
    else if (!strncmp(what, "zone", 4) && what[4] >= '1' && what[4] <= '4')   /* zone1..4: on a sweetspot (-1 not) */
        *got = zone_at(page_knob(what[4] - '1'), knob_val(what[4] - '1'));
    else
        return 0;
    return 1;
}
#endif
