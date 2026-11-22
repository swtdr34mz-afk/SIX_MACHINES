/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES' sound engine: six drum machines (v0.2: KICK, SNARE and HATS play; TOMS, METAL and CLAP are coming), a
 * 16-step sequencer with a pattern per machine, accents and swing, and a soft limiter on the master.
 *
 * Every machine has four MACROS, one per knob (K1..K4), each 0..127. A macro moves several of the sound's parameters at
 * once, each along its own curve, and has SWEETSPOT ZONES: a few positions where everything it moves lands on a value
 * that sounds right together (the zone's name is shown on the screen). The app makes the knob rest on a zone for a few
 * detents (MACHINES[].macro[].centre: the zones and their names); the engine turns the four values into the sound.
 *
 * The main loop talks to the engine through eng_* calls; eng_render runs in the audio interrupt. */
#pragma once
#include <stdint.h>

#define ENG_SR 44100.0f
#define NSTEPS 16
#define NMACHINES 6
enum { M_KICK, M_SNARE, M_TOMS, M_METAL, M_HATS, M_CLAP };

/* a macro: its name and its sweetspot zones (centre 0..127, name), left to right */
#define MACRO_ZONES 5
typedef struct {
    const char *name;
    uint8_t nzones;
    uint8_t centre[MACRO_ZONES];
    const char *zone[MACRO_ZONES];
} macro_t;

/* a start preset: the four macros' values, and a name */
typedef struct {
    const char *name;
    uint8_t m[4];
} preset_t;
#define NPRESETS 4

typedef struct {
    const char *name;
    uint8_t ready;                       /* 0: not built yet (its key says COMING SOON) */
    uint16_t rgb;                        /* its colour on the screen (RGB565) */
    macro_t macro[4];
    preset_t preset[NPRESETS];
} machine_t;
extern const machine_t MACHINES[NMACHINES];

void eng_init(void);
/* a machine's four macros, each 0..127: main loop (the parameters are worked out here) */
void eng_macros(int machine, const uint8_t m[4]);
void eng_level(int machine, float level);   /* 0..1 */
void eng_mute(int machine, int on);         /* its steps silent (a live hit still plays) */
/* a machine's pattern: bit s = step s plays; accents: bit s = step s plays louder */
void eng_pattern(int machine, uint16_t steps, uint16_t accents);
void eng_tempo(float bpm, float swing);     /* swing 0..1 (0 straight, 1 = the hardest shuffle) */
void eng_play(int on);                      /* the sequencer runs from step 0 / stops (what sounds rings out) */
void eng_hit(int machine, float vel);       /* a machine now (a live key) */
void eng_panic(void);
/* for the screen: the step under way (-1 stopped), a machine's level now 0..1, hits played since the start */
int eng_step(void);
float eng_env(int machine);
uint32_t eng_hits(void);
/* the render: lr interleaved stereo, n frames, gain = full scale of the output */
void eng_render(int32_t *lr, uint32_t n, float gain);
