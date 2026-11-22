/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (SIX MACHINES) */
/* SIX MACHINES by swtdr34mz: a performance drum machine for the M-VAVE FM-1 (on fm1-base), six synthesized drum
 * machines whose four knobs each move a lot at once and rest on sweetspots. v0.1: the first machine, KICK, and its
 * 16-step sequencer.
 * This header is the app's half of the contract with the platform (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "SIX MACHINES"
#define APP_SPLASH_RGB RGB(255, 92, 40)
#define APP_SPLASH                       /* app_splash: the rack powering up, not the platform's page */
#define APP_TICK_FAST                    /* app_tick_fast: MIDI clock out, on time */
void app_tick_fast(void);
#define APP_BLOCK 128u                   /* frames per audio block: 2.9 ms at 44.1 kHz (a drum machine: tight) */

/* the store: the app's settings (the knobs, the patterns, tempo), the panel calibration (the platform's) and the user
 * presets (apart from the settings, so a new settings layout never takes them away) */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_USER, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u}

/* backup and restore over USB: the settings */
#define APP_BACKUP_OBJS {OBJ_SETTINGS, OBJ_USER}
#define APP_BACKUP_MAGIC 0x584D3653u            /* "S6MX" */
#define APP_BACKUP_VERSION 1u
#define APP_BACKUP_WHAT "replaces patterns, sounds, presets"
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
void app_splash(void);

#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
