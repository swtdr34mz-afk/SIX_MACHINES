/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES' sound engine. v0.1: the first machine, KICK (a sine with a pitch sweep, a click, a body of two decays,
 * drive and a wavefolder), a 16-step sequencer with swing that plays it, and a soft limiter on the master.
 *
 * Every machine has four MACROS, one per knob (K1..K4), each 0..127. A macro moves several of the sound's parameters at
 * once, each along its own curve, and has SWEETSPOT ZONES: a few positions where everything it moves lands on a value
 * that sounds right together (the zone's name is shown on the screen). The app makes the knob rest on a zone for a few
 * detents (kick_macros[].zone: centres and names); the engine turns the four values into the sound (eng_kick_macros).
 *
 * The main loop talks to the engine through eng_* calls; eng_render runs in the audio interrupt. */
#pragma once
#include <stdint.h>

#define ENG_SR 44100.0f
#define NSTEPS 16

/* a macro: its name and its sweetspot zones (centre 0..127, name), left to right */
#define MACRO_ZONES 5
typedef struct {
    const char *name;
    uint8_t nzones;
    uint8_t centre[MACRO_ZONES];
    const char *zone[MACRO_ZONES];
} macro_t;
extern const macro_t KICK_MACROS[4];

/* start presets: the four macros' values, and a name */
typedef struct {
    const char *name;
    uint8_t m[4];
} kick_preset_t;
#define NKICK_PRESETS 4
extern const kick_preset_t KICK_PRESETS[NKICK_PRESETS];

void eng_init(void);
/* the KICK's four macros (TUNE, PUNCH, BOOM, GRIT), each 0..127: main loop (the parameters are worked out here) */
void eng_kick_macros(const uint8_t m[4]);
void eng_kick_level(float level);        /* 0..1 */
/* the pattern: bit s = step s plays; accents: bit s = step s plays louder */
void eng_pattern(uint16_t steps, uint16_t accents);
void eng_tempo(float bpm, float swing);  /* swing 0..1 (0 straight, 1 = the hardest shuffle) */
void eng_play(int on);                   /* the sequencer runs from step 0 / stops (a sounding kick rings out) */
void eng_hit(float vel);                 /* the kick now (a live key) */
void eng_panic(void);
/* for the screen: the step under way (-1 stopped), the kick's level now 0..1, hits played since the start */
int eng_step(void);
float eng_kick_env(void);
uint32_t eng_hits(void);
/* the render: lr interleaved stereo, n frames, gain = full scale of the output */
void eng_render(int32_t *lr, uint32_t n, float gain);

/* the parameters a set of macros gives (eng_kick_params: for the tests and the screen) */
typedef struct {
    float hz;            /* the tone at rest */
    float sweep_oct;     /* the pitch starts this many octaves above */
    float sweep_s;       /* and falls back with this time constant */
    float click;         /* the attack's click 0..1 */
    float decay_s;       /* the body's decay (time constant) */
    float sub;           /* the share of the long, deep tail 0..1 */
    float droop_oct;     /* the tone drops this far (octaves, negative) as the body fades: the 808's glide */
    float drive;         /* into the saturation (1 = clean) */
    float fold;          /* the wavefolder 0..1 */
    float tone_hz;       /* the low-pass after the saturation */
    float crush;         /* 0..1 the bitcrusher (DESTROY) */
    float makeup;        /* the level after it all: the knobs change the character, not the loudness */
} kick_params_t;
void eng_kick_params(const uint8_t m[4], kick_params_t *p);
