/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES: input, the knobs and their sweetspots, the pattern, the screen, settings. Part of the unity build
 * (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in scope. The sound is dsp/engine.c.
 *
 * Controls (v0.1: the first machine, KICK)
 *   white keys      the 16 steps: a tap turns a step on, again ACCENT (louder), again off
 *   black keys      the six machines, left to right: KICK plays the kick now; the others are coming (their names)
 *   K1..K4          the KICK's macros: TUNE, PUNCH, BOOM, GRIT. Each moves several parameters at once; turned slowly it
 *                   rests a few detents on each sweetspot (its name lights up); turned fast it runs past them
 *   SELECT          tempo (BPM)      ALGORITHM  swing      PRESET  a start preset for the four knobs
 *   PLAY            the sequencer: start from step 1 / stop
 *   REC held 1 s    the pattern cleared
 *   OCT- / OCT+     the kick's level
 *   Settings save themselves a few seconds after the last change. */
#include "fastmath.h"
#include "engine.h"

#define SET_MAGIC 0x31584D36u            /* "6MX1": the settings record; another magic loads the defaults */
typedef struct {
    uint32_t magic;
    uint8_t m[4];                        /* the KICK's macros 0..127 */
    uint16_t steps, acc;                 /* the pattern: steps that play, and the accented ones */
    uint8_t bpm;                         /* 60 .. 200 */
    uint8_t swing;                       /* 0 .. 100 % */
    uint8_t preset;                      /* the preset PRESET loaded last */
    uint8_t level;                       /* the kick's level 0 .. 10 */
    uint8_t rsv[4];
} settings_t;
static settings_t set;
static uint8_t set_dirty;
static uint32_t dirty_at;

static const char *const MACHINE[6] = {"KICK", "SNARE", "TOMS", "METAL", "HATS", "CLAP"};
static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;

/* --------------------------------------------------------------- settings --- */
static void settings_defaults(void)
{
    uint32_t i;
    set.magic = SET_MAGIC;
    for (i = 0; i < 4u; i++)
        set.m[i] = KICK_PRESETS[0].m[i];
    set.steps = 0x1111u;                 /* four on the floor, the first beat accented */
    set.acc = 0x0001u;
    set.bpm = 124;
    set.swing = 0;
    set.preset = 0;
    set.level = 8;
    for (i = 0; i < 4u; i++)
        set.rsv[i] = 0;
}

static int settings_ok(void)
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        if (set.m[i] > 127u)
            return 0;
    return set.magic == SET_MAGIC && set.bpm >= 60u && set.bpm <= 200u && set.swing <= 100u
           && set.preset < NKICK_PRESETS && set.level <= 10u;
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
    eng_kick_macros(set.m);
    eng_pattern(set.steps & 0xFFFFu, set.acc & set.steps);
    eng_tempo((float)set.bpm, (float)set.swing / 100.0f);
    eng_kick_level((float)set.level / 10.0f);
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

static void knobs_from_settings(void)
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        knob_pos[k] = pos_of_val(&KICK_MACROS[k], set.m[k]);
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
    int32_t step_drawn;
    uint8_t hit_drawn;
    uint8_t playing;
    uint8_t redraw;
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

/* a quick turn moves further: x1 slow, x3, x6 when the detents come close together */
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
    const macro_t *mc = &KICK_MACROS[k];
    int32_t p = clamp(knob_pos[k] + accel(EN_K1 + k, e, 3, 6), 0, pos_max(mc)), v = val_of_pos(mc, p);
    int zb = zone_at(mc, set.m[k]), za = zone_at(mc, v);
    knob_pos[k] = p;
    ui.knob_at[k] = plat_ms();
    ui.redraw = 1;
    if (v == set.m[k])
        return;
    set.m[k] = (uint8_t)v;
    eng_kick_macros(set.m);
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
    set.preset = (uint8_t)(((int32_t)set.preset + d % NKICK_PRESETS + NKICK_PRESETS) % NKICK_PRESETS);
    for (k = 0; k < 4u; k++)
        set.m[k] = KICK_PRESETS[set.preset].m[k];
    knobs_from_settings();
    eng_kick_macros(set.m);
    dirty();
    app_say("PRESET", KICK_PRESETS[set.preset].name);
}

static void step_tap(uint32_t s)         /* off -> on -> accent -> off */
{
    uint16_t bit = (uint16_t)(1u << s);
    if (!(set.steps & bit)) {
        set.steps |= bit;
        set.acc &= (uint16_t)~bit;
    } else if (!(set.acc & bit)) {
        set.acc |= bit;
    } else {
        set.steps &= (uint16_t)~bit;
        set.acc &= (uint16_t)~bit;
    }
    eng_pattern(set.steps, set.acc & set.steps);
    if (!ui.playing && (set.steps & bit))
        eng_hit((set.acc & bit) ? 1.0f : 0.72f);   /* stopped: hear what the step will play */
    dirty();
    ui.redraw = 1;
}

static void play_toggle(void)
{
    ui.playing = (uint8_t)!ui.playing;
    eng_play(ui.playing);
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
    for (i = 0; i < 6u; i++) {           /* the machines */
        uint32_t bit = 1u << BK[i];
        if (!(keys & bit) || (ui.keys & bit))
            continue;
        if (i == 0)
            eng_hit(1.0f);
        else
            app_say(MACHINE[i], "COMING SOON");
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
            int32_t l = clamp((int32_t)set.level + (i == B_OCTUP ? 1 : -1), 0, 10);
            char t[8];
            set.level = (uint8_t)l;
            eng_kick_level((float)l / 10.0f);
            dirty();
            int_text(l * 10, t);
            text_cat(t, "%");
            app_say("LEVEL", t);
        }
    }
    if (ui.rec_t) {                      /* REC held a second: the pattern cleared */
        if (!(btn >> B_REC & 1u))
            ui.rec_t = 0;
        else if (plat_ms() - ui.rec_t >= 1000u) {
            ui.rec_t = 0;
            set.steps = set.acc = 0;
            eng_pattern(0, 0);
            dirty();
            app_say("PATTERN", "CLEARED");
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
    uint32_t lit = 0, mid = 0, i, btn = 0, dimb;
    int32_t s = eng_step();
    for (i = 0; i < 16u; i++) {
        uint32_t bit = 1u << WK[i];
        if (set.steps >> i & 1u) {
            if (set.acc >> i & 1u)
                lit |= bit;
            else
                mid |= bit;
        }
    }
    if (s >= 0)                          /* the step under way: lit, or out if it plays (it flickers past) */
        lit ^= 1u << WK[s];
    if (eng_kick_env() > 0.3f)
        lit |= 1u << BK[0];
    if (ui.playing)
        btn |= LED_PLAY_GREEN;
    dimb = (1u << B_PLAY) | (1u << B_REC) | (1u << B_OCTDN) | (1u << B_OCTUP);
    plat_lights(btn | (ui.btn & ~(1u << B_PLAY)), lit | (ui.keys & ((1u << BK[0]) | 0)), dimb,
                (1u << BK[0]) | (1u << BK[1]) | (1u << BK[2]) | (1u << BK[3]) | (1u << BK[4]) | (1u << BK[5]), 1);
    plat_lights_mid(0, mid & ~lit);
}

void app_input_only(void) { input(); }

/* ------------------------------------------------------------------ drawing --- */
#define C_KICK RGB(255, 92, 40)          /* the KICK's colour: the rack's first module */
#define C_KICK_DIM RGB(90, 34, 18)

static void ctext(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}

/* the rack: six modules, KICK alive (it flashes with each hit), the others waiting */
static void draw_rack(int32_t y, float env)
{
    uint32_t i;
    for (i = 0; i < 6u; i++) {
        int32_t x = 3 + (int32_t)i * 39;
        if (i == 0) {
            uint16_t c = env > 0.25f ? C_KICK : C_KICK_DIM;
            cv_rect(x, y, 37, 26, c);
            cv_rect(x + 2, y + 2, 33, 22, C_BLACK);
            cv_rect(x + 4, y + 18, (int32_t)(29.0f * fm_clampf(env * 1.4f, 0.0f, 1.0f)), 3, C_KICK);
            ctext(x + 18, y + 5, &FONT_XS, MACHINE[i], C_WHITE);
        } else {
            cv_rect(x, y, 37, 26, C_LINE);
            cv_rect(x + 1, y + 1, 35, 24, C_BLACK);
            ctext(x + 18, y + 8, &FONT_XS, MACHINE[i], C_DIM);
        }
    }
}

/* one knob: its name, a bar 0..127 with the sweetspots as ticks, the value, and the zone it is on (lit) or near (dim) */
static void draw_knob(int k, int32_t x, int32_t y, uint32_t now)
{
    const macro_t *mc = &KICK_MACROS[k];
    int32_t v = set.m[k], w = 54, z = zone_at(mc, v), zn = zone_near(mc, v), fill = (v * (w - 2)) / 127;
    uint32_t i;
    int hot = ui.knob_at[k] && now - ui.knob_at[k] < 600u;
    char t[8];
    cv_text(x, y, &FONT_XS, mc->name, hot ? C_WHITE : C_GRAY);
    int_text(v, t);
    cv_text(x + w - text_w(&FONT_XS, t), y, &FONT_XS, t, hot ? C_HI : C_AMB);
    cv_rect(x, y + 14, w, 8, C_LINE);
    cv_rect(x + 1, y + 15, fill, 6, z >= 0 ? C_KICK : C_HI);
    for (i = 0; i < mc->nzones; i++) {   /* the sweetspots */
        int32_t tx = x + 1 + (mc->centre[i] * (w - 3)) / 127;
        cv_rect(tx, y + 11, 1, 3, (int)i == z ? C_KICK : C_GRAY);
    }
    ctext(x + w / 2, y + 26, &FONT_XS, mc->zone[z >= 0 ? z : zn], z >= 0 ? C_WHITE : C_DIM);
}

static void draw(void)
{
    uint32_t now = plat_ms(), i;
    int32_t s = eng_step();
    float env = eng_kick_env();
    char t[24];
    /* header: the name, or a message; the tempo and the transport */
    cv_begin(240, 30, C_BLACK);
    if (now < ui.msg_until) {
        int32_t x = cv_text(8, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_HI);
    } else {
        cv_text(8, 6, &FONT_B, "SIX MACHINES", C_KICK);
        int_text(set.bpm, t);
        text_cat(t, " BPM");
        cv_text(200 - text_w(&FONT_XS, t), 10, &FONT_XS, t, C_AMB);
        if (ui.playing) {                /* a triangle: playing */
            for (i = 0; i < 6u; i++)
                cv_rect(214 + (int32_t)i, 9 + (int32_t)i / 1, 1, 12 - 2 * (int32_t)i, C_HI);
        } else {
            cv_rect(213, 9, 10, 10, C_DIM);
        }
    }
    cv_rect(0, 29, 240, 1, C_LINE);
    cv_blit(0, 0);
    /* the rack, and the knobs (or a sweetspot just reached, big) */
    cv_begin(240, 110, C_BLACK);
    draw_rack(4, env);
    if (now < ui.zone_until && ui.zone_k >= 0) {
        const macro_t *mc = &KICK_MACROS[ui.zone_k];
        ctext(120, 40, &FONT_XS, mc->name, C_GRAY);
        ctext(120, 54, &APP_BIG_FONT, mc->zone[ui.zone_z], C_KICK);
        cv_rect(60, 96, 120, 2, C_KICK);
    } else {
        for (i = 0; i < 4u; i++)
            draw_knob((int)i, 4 + (int32_t)i * 59, 40, now);
        cv_text(4, 84, &FONT_XS, "PRESET", C_DIM);
        cv_text(48, 84, &FONT_XS, KICK_PRESETS[set.preset].name, C_AMB);
        t[0] = 0;
        text_cat(t, "SWING ");
        int_text(set.swing, t + 6);
        text_cat(t, "%");
        cv_text(236 - text_w(&FONT_XS, t), 84, &FONT_XS, t, C_AMB);
    }
    cv_blit(0, 30);
    /* the 16 steps, two rows of eight; the step under way lit white */
    cv_begin(240, 100, C_BLACK);
    cv_rect(0, 0, 240, 1, C_LINE);
    for (i = 0; i < 16u; i++) {
        int32_t x = 6 + (int32_t)(i % 8u) * 29, y = 10 + (int32_t)(i / 8u) * 36;
        int on = set.steps >> i & 1u, acc = on && (set.acc >> i & 1u);
        uint16_t c = acc ? C_KICK : on ? C_KICK_DIM : C_LINE;
        cv_rect(x, y, 25, 28, c);
        if (!on)
            cv_rect(x + 1, y + 1, 23, 26, C_BLACK);
        if (acc)
            cv_rect(x + 4, y + 4, 17, 3, C_WHITE);
        if ((int32_t)i == s)
            cv_rect(x, y + 30, 25, 3, C_WHITE);
        else if (i % 4u == 0)
            cv_rect(x, y + 30, 3, 3, C_DIM);   /* the beats */
    }
    cv_text(6, 84, &FONT_XS, "STEPS 16", C_DIM);
    cv_text(234 - text_w(&FONT_XS, "KICK"), 84, &FONT_XS, "KICK", C_KICK);
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
    ctext(120, 52, &APP_BIG_FONT, "SIX", C_KICK);
    ctext(120, 84, &APP_BIG_FONT, "MACHINES", C_WHITE);
    cv_blit(0, 20);
    cv_begin(240, 80, C_BLACK);
    for (i = 0; i < 6u; i++) {
        int32_t x = 3 + (int32_t)i * 39;
        cv_rect(x, 6, 37, 26, i == 0 ? C_KICK : RGB(70, 70, 78));
        cv_rect(x + 2, 8, 33, 22, C_BLACK);
        ctext(x + 18, 13, &FONT_XS, MACHINE[i], i == 0 ? C_WHITE : RGB(120, 120, 128));
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
    uint32_t now = plat_ms();
    int32_t s;
    uint8_t hit;
    input();
    lights();
    if (ann_on() && ann_where()) {       /* a screen reader that just started listening hears the page */
        char t[64] = "SIX MACHINES. KICK. ";
        text_cat(t, KICK_PRESETS[set.preset].name);
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(now);
    s = eng_step();
    hit = eng_kick_env() > 0.25f;
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
    if (!strcmp(what, "steps"))
        *got = set.steps;
    else if (!strcmp(what, "acc"))
        *got = set.acc;
    else if (!strcmp(what, "bpm"))
        *got = set.bpm;
    else if (!strcmp(what, "swing"))
        *got = set.swing;
    else if (!strcmp(what, "preset"))
        *got = set.preset;
    else if (!strcmp(what, "level"))
        *got = set.level;
    else if (!strcmp(what, "playing"))
        *got = ui.playing;
    else if (!strcmp(what, "step"))
        *got = eng_step();
    else if (!strcmp(what, "hits"))
        *got = (int)eng_hits();
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strncmp(what, "m", 1) && what[1] >= '1' && what[1] <= '4')   /* m1..m4: the macros */
        *got = set.m[what[1] - '1'];
    else if (!strncmp(what, "zone", 4) && what[4] >= '1' && what[4] <= '4')   /* zone1..4: on a sweetspot (-1 not) */
        *got = zone_at(&KICK_MACROS[what[4] - '1'], set.m[what[4] - '1']);
    else
        return 0;
    return 1;
}
#endif
