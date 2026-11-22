/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES' sound engine, the machines, their macros and presets, the pages' knobs, the names. Part of dsp/engine.c (included there, in order: the device builds the engine
 * as one unit). */
/* ================================================================ the tables === */
/* The zones' centres are where the curves below have their breakpoints: at a zone every parameter a macro moves is
 * exactly the value chosen for it there; between two zones they move from one to the other. */
const machine_t MACHINES[NMACHINES] = {
    {"KICK", RGB565(255, 92, 40),
     {{"TUNE", 4, {20, 50, 75, 105}, {"SUB", "DEEP", "808", "TIGHT"}},
      {"PUNCH", 4, {12, 50, 80, 115}, {"SOFT", "SYNTAKT", "TECHNO", "ZAP"}},
      {"BOOM", 4, {20, 50, 85, 115}, {"TIGHT", "SYNTAKT", "808", "DRONE"}},
      {"GRIT", 5, {0, 35, 70, 100, 127}, {"CLEAN", "WARM", "RUMBLE", "FOLD", "DESTROY"}}},
     {{"SYNTAKT DEEP", {50, 50, 50, 35}}, {"808 CLASSIC", {75, 12, 85, 0}}, {"WAREHOUSE", {105, 80, 20, 70}},
      {"PSY ZAP", {60, 115, 20, 35}}}},
    {"SNARE", RGB565(70, 200, 255),
     {{"TUNE", 4, {20, 50, 80, 110}, {"FAT", "SYNTAKT", "TIGHT", "PICCOLO"}},
      {"SNAP", 4, {15, 50, 85, 115}, {"BRUSH", "SYNTAKT", "CRACK", "RIMSHOT"}},
      {"BODY", 4, {15, 50, 85, 118}, {"NOISY", "SYNTAKT", "FAT", "TONE"}},
      {"RATTLE", 5, {10, 45, 75, 105, 127}, {"DRY", "SYNTAKT", "TRAP", "GATED", "DIRT"}}},
     {{"SYNTAKT SD", {50, 50, 50, 45}}, {"TRAP SNARE", {80, 85, 15, 75}}, {"GATED 80S", {35, 50, 85, 105}},
      {"RIMSHOT", {110, 115, 85, 10}}}},
    {"TOMS", RGB565(120, 230, 110),
     {{"TUNE", 4, {20, 50, 80, 110}, {"FLOOR", "LOW", "HIGH", "TIMBAL"}},
      {"BEND", 4, {10, 45, 80, 115}, {"FLAT", "SYNTAKT", "DROP", "SIMMONS"}},
      {"DECAY", 4, {15, 50, 85, 118}, {"SHORT", "SYNTAKT", "RING", "BOOM"}},
      {"SKIN", 4, {10, 45, 80, 115}, {"SOFT", "SYNTAKT", "WOOD", "METAL"}}},
     {{"SYNTAKT TOM", {50, 45, 50, 45}}, {"FLOOR BOOM", {20, 45, 118, 10}}, {"SIMMONS", {80, 115, 85, 45}},
      {"WOOD BLOCK", {110, 10, 15, 80}}}},
    {"METAL", RGB565(200, 140, 255),
     {{"TUNE", 4, {20, 50, 85, 115}, {"LOW", "MID", "HIGH", "SPLASH"}},
      {"RING", 5, {10, 40, 70, 95, 122}, {"RIDE", "CRASH", "GONG", "BELL", "INDUSTRY"}},
      {"DECAY", 5, {10, 40, 70, 100, 127}, {"CHOKE", "SHORT", "RIDE", "CRASH", "WASH"}},
      {"SHIMMER", 4, {15, 50, 85, 118}, {"DARK", "SILK", "SHINE", "SIZZLE"}}},
     {{"FM RIDE", {50, 10, 70, 50}}, {"FM CRASH", {50, 40, 100, 85}}, {"GONG", {20, 70, 127, 15}},
      {"INDUSTRIAL", {85, 122, 40, 118}}}},
    {"HATS", RGB565(255, 214, 60),
     {{"TUNE", 4, {20, 50, 85, 115}, {"LOW", "808", "HIGH", "TINY"}},
      {"DECAY", 5, {10, 35, 70, 105, 127}, {"TICK", "CLOSED", "HALF", "OPEN", "WASH"}},
      {"METAL", 4, {15, 50, 85, 118}, {"NOISE", "SYNTAKT", "808", "RING"}},
      {"CRISP", 4, {15, 50, 85, 118}, {"SOFT", "CRISP", "SIZZLE", "HARSH"}}},
     {{"SYNTAKT HH", {50, 35, 50, 50}}, {"808 HAT", {50, 35, 85, 50}}, {"OPEN WASH", {50, 105, 50, 85}},
      {"TICK", {85, 10, 15, 85}}}},
    {"CLAP", RGB565(255, 90, 170),
     {{"TUNE", 4, {20, 50, 80, 110}, {"LOW", "909", "808", "HIGH"}},
      {"SPREAD", 4, {10, 40, 70, 110}, {"SINGLE", "TIGHT", "909", "CROWD"}},
      {"ROOM", 4, {10, 45, 80, 115}, {"DRY", "SHORT", "ROOM", "HALL"}},
      {"MORPH", 3, {15, 64, 115}, {"CLAP", "RIM", "COWBELL"}}},
     {{"909 CLAP", {50, 70, 45, 15}}, {"808 CLAP", {80, 40, 80, 15}}, {"RIM CLICK", {80, 10, 10, 64}},
      {"COWBELL", {50, 10, 45, 115}}}},
};

#define TGT {0, 0, 0, 0}
#define BIPOLAR(n) {n, 5, {4, 34, 64, 94, 124}, {"-MAX", "-HALF", "OFF", "+HALF", "+MAX"}}
const macro_t PAGE_KNOBS[NPAGES][4] = {
    {{0}},
    {{"TYPE", 4, {15, 50, 85, 118}, {"TAPE", "TUBE", "FOLD", "CRUSH"}},
     {"AMOUNT", 4, {0, 35, 70, 110}, {"OFF", "LIGHT", "HOT", "MAX"}},
     {"TONE", 3, {20, 64, 105}, {"DARK", "OPEN", "BRIGHT"}},
     {"MIX", 3, {0, 64, 127}, {"DRY", "HALF", "WET"}}},
    {BIPOLAR("DEPTH"),
     {"ATTACK", 4, {5, 45, 90, 122}, {"SNAP", "SOFT", "SWELL", "SLOW"}},
     {"DECAY", 4, {15, 50, 85, 120}, {"SHORT", "MID", "LONG", "HOLD"}},
     {"TARGET", 4, {15, 50, 85, 118}, TGT}},
    {{"RATE", 5, {10, 35, 60, 85, 110}, {"4 BARS", "1 BAR", "1/4", "1/8", "1/16"}},
     BIPOLAR("DEPTH"),
     {"WAVE", 5, {10, 40, 70, 95, 120}, {"SINE", "TRI", "SAW", "SQUARE", "S&H"}},
     {"TARGET", 4, {15, 50, 85, 118}, TGT}},
    {{"LEVEL", 4, {40, 80, 100, 120}, {"SOFT", "MIX", "FULL", "HOT"}},
     {"PAN", 5, {0, 32, 64, 96, 127}, {"LEFT", "L/2", "CENTER", "R/2", "RIGHT"}},
     {"REVERB", 5, {0, 30, 64, 100, 127}, {"DRY", "TOUCH", "ROOM", "WET", "DROWN"}},
     {"DELAY", 5, {0, 30, 64, 100, 127}, {"DRY", "TOUCH", "ECHO", "WET", "DUB"}}},
    {{"COMP", 4, {0, 35, 70, 110}, {"OFF", "GLUE", "PUNCH", "PUMP"}},
     {"WARMTH", 4, {0, 40, 80, 120}, {"CLEAN", "TAPE", "HOT", "CRUSHED"}},
     {"TILT", 3, {20, 64, 108}, {"DARK", "FLAT", "BRIGHT"}},
     {"SPACE", 4, {20, 55, 90, 120}, {"ROOM", "PLATE", "HALL", "CAVE"}}},
    {{"TIME", 4, {15, 45, 75, 105}, {"1/16", "1/8", "3/16", "1/4"}},
     {"FEEDBK", 4, {20, 55, 90, 120}, {"SHORT", "ECHO", "DUB", "LOOP"}},
     {"TONE", 3, {20, 60, 105}, {"DARK", "TAPE", "CLEAR"}},
     {"RETURN", 4, {0, 40, 80, 115}, {"OFF", "LOW", "MIX", "LOUD"}}},
    {{"FILTER", 5, {5, 35, 64, 93, 122}, {"LP MAX", "LP", "OPEN", "HP", "HP MAX"}},
     {"SCENE", 3, {0, 64, 127}, {"A", "HALF", "B"}},
     {"BUILD", 3, {20, 64, 108}, {"1 BAR", "2 BARS", "4 BARS"}},
     {"RES", 3, {20, 64, 110}, {"SOFT", "ACID", "SCREAM"}}},
};
const char *const PAGE_NAME[NPAGES] = {"SOUND", "SAT", "ENV", "LFO", "MIX", "MASTER", "DELAY", "PERFORM"};
const uint8_t PAGE_DEFAULT[NPAGES][4] = {
    {0}, {15, 0, 64, 127}, {64, 5, 50, 15}, {35, 64, 10, 15}, {80, 64, 0, 0}, {0, 0, 64, 55}, {75, 55, 60, 80},
    {64, 0, 64, 20}};
const char *const COND_NAME[NCONDS] = {"ALWAYS", "1:2", "2:2", "1:4", "2:4", "3:4", "4:4", "50%", "25%", "75%",
                                       "FILL", "NOT FILL", "FIRST"};
const char *const PAD_NAME[NPADS] = {"REPEAT 1/4", "REPEAT 1/8", "REPEAT 1/16", "REPEAT 1/32", "TAPE STOP",
                                     "BUILD-UP", "REVERB THROW", "DELAY THROW", "KILL LOW", "KILL MID", "KILL HIGH",
                                     "FILL", "SCENE B", "BREAK", "FREEZE", "DROP", "GATE", "HALF TIME", "LO-FI",
                                     "PITCH DOWN", "SWEEP"};
