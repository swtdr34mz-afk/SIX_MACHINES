/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES' sound engine: six drum machines (KICK, SNARE, TOMS, METAL, HATS, CLAP), a sequencer of up to 32 steps
 * with a pattern per machine (accents, parameter locks, trig conditions, retrigs) and swing, each machine's own
 * saturation, envelope, LFO and mix (level, pan, reverb and delay sends), a master (compressor, warmth, tilt EQ, reverb,
 * ping-pong delay, DJ filter, a 3-band kill, a soft limiter) and the performance effects (beat repeat, tape stop,
 * build-up, throws, freeze, fill, scene morph, break, drop).
 *
 * Every knob is a MACRO, 0..127: it moves several parameters at once, each along its own curve, and has SWEETSPOT
 * ZONES: a few positions where everything it moves lands on a value that sounds right together (the zone's name is
 * shown on the screen). The app makes the knob rest on a zone for a few detents (macro_t: the zones and their names).
 *
 * The app owns the settings (tracks and master, below) and binds them (eng_bind): the main loop writes them, the
 * render reads them (a byte or a word at a time). eng_render runs in the audio interrupt. */
#pragma once
#include <stdint.h>

#define ENG_SR 44100.0f
#define NSTEPS 32
#define NMACHINES 6
enum { M_KICK, M_SNARE, M_TOMS, M_METAL, M_HATS, M_CLAP };
#define NO_LOCK 255u

/* a macro: its name and its sweetspot zones (centre 0..127, name), left to right. A zone name of 0 on a TARGET knob:
 * the machine's own macro names (K1..K4) */
#define MACRO_ZONES 5
typedef struct {
    const char *name;
    uint8_t nzones;
    uint8_t centre[MACRO_ZONES];
    const char *zone[MACRO_ZONES];
} macro_t;

typedef struct {                         /* a start preset: the four macros' values, and a name */
    const char *name;
    uint8_t m[4];
} preset_t;
#define NPRESETS 4

typedef struct {
    const char *name;
    uint16_t rgb;                        /* its colour on the screen (RGB565) */
    macro_t macro[4];
    preset_t preset[NPRESETS];
} machine_t;
extern const machine_t MACHINES[NMACHINES];

/* the pages of knobs besides a machine's own (SOUND): per track SAT, ENV, LFO, MIX; the master FX and FX 2; GLO */
enum { PG_SOUND, PG_SAT, PG_ENV, PG_LFO, PG_MIX, PG_FX, PG_FX2, PG_GLO, NPAGES };
extern const macro_t PAGE_KNOBS[NPAGES][4];   /* (PG_SOUND: the machine's, MACHINES[].macro) */
extern const char *const PAGE_NAME[NPAGES];
extern const uint8_t PAGE_DEFAULT[NPAGES][4];

/* trig conditions (a step's): when it plays */
enum { C_ALWAYS, C_1_2, C_2_2, C_1_4, C_2_4, C_3_4, C_4_4, C_P50, C_P25, C_P75, C_FILL, C_NOFILL, C_FIRST, NCONDS };
extern const char *const COND_NAME[NCONDS];

typedef struct {                         /* a machine's track: its sound and its pattern (kept in the settings) */
    uint8_t m[4];                        /* SOUND: the machine's macros */
    uint8_t sat[4];                      /* SAT: type, amount, tone, mix */
    uint8_t env[4];                      /* ENV: depth (64 = none), attack, decay, target */
    uint8_t lfo[4];                      /* LFO: rate, depth (64 = none), wave, target */
    uint8_t mix[4];                      /* MIX: level, pan, reverb send, delay send */
    uint8_t preset;
    uint8_t rsv[3];
    uint32_t steps, acc;                 /* bit s: step s plays / is accented */
    uint8_t lock[NSTEPS][4];             /* the macros a step plays with (NO_LOCK: the track's) */
    uint8_t cond[NSTEPS];                /* C_* */
    uint8_t retrig[NSTEPS];              /* hits in the step: 0 or 1 one, 2..4 */
} track_t;

typedef struct {                         /* the master (kept in the settings) */
    uint8_t fx[4];                       /* FX: compressor, warmth, tilt, space (the reverb) */
    uint8_t fx2[4];                      /* FX 2: delay time, feedback, tone, the effects' return */
    uint8_t glo[4];                      /* GLO: DJ filter, scene, build-up length, filter resonance */
    uint8_t scene[NMACHINES][4];         /* scene B: the macros SCENE morphs to */
    uint8_t has_scene;
    uint8_t length;                      /* 16 or 32 */
    uint8_t bpm;                         /* 60 .. 200 */
    uint8_t swing;                       /* 0 .. 100 */
} master_t;

/* the performance pads (PERF mode: the white keys, held), bit by bit */
enum { P_REP4, P_REP8, P_REP16, P_REP32, P_TAPE, P_BUILD, P_RTHROW, P_DTHROW, P_KLOW, P_KMID, P_KHIGH, P_FILL,
       P_SCENE, P_BREAK, P_FREEZE, P_DROP, NPADS };
extern const char *const PAD_NAME[NPADS];

void eng_init(void);
void eng_bind(const track_t *tracks, const master_t *master);
void eng_changed(int machine);           /* its sound changed (a knob, a preset): worked out at the next block */
void eng_mute(int machine, int on);      /* its steps silent (a live hit still plays) */
void eng_play(int on);                   /* the sequencer runs from step 0 / stops (what sounds rings out) */
void eng_hit(int machine, float vel);    /* a machine now (a live key) */
void eng_pads(uint32_t held);            /* the performance pads held now (P_*) */
void eng_panic(void);
/* for the screen: the step under way (-1 stopped), a machine's level now 0..1, hits played since the start, the
 * modulation on a machine's macros now (-127..127 each), the build-up's progress 0..1 */
int eng_step(void);
float eng_env(int machine);
uint32_t eng_hits(void);
void eng_mod(int machine, int8_t out[4]);
float eng_build(void);
/* the render: lr interleaved stereo, n frames, gain = full scale of the output */
void eng_render(int32_t *lr, uint32_t n, float gain);
