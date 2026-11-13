/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Desktop Melee's settings as a text file, for the systems without
 * UserDefaults (docs/desktop-melee/ui-guide.md sections 13 and 14).
 *
 * The file is SDL_GetPrefPath(NULL, "Desktop Melee") + "settings.cfg", one
 * "key = value" per line, with the keys of ui-guide.md section 13. Lines
 * starting with '#' and unknown keys are ignored; a missing key keeps its
 * default, and a value out of range is clamped.
 *
 * Like the macOS app, the UI owns these values and pushes each one into the
 * game through the bridge's setters (desktop_melee_settings_apply). macOS does
 * not use this file. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "desktop_melee_game.h"

typedef struct DesktopMeleeSettings {
    /* match.stocks, match.minutes (time_seconds = minutes x 60) and, per
     * player, players.pN.* and controls.pN.port. */
    DesktopMeleeMatchSetup match;
    bool tap_jump[2];                           /* controls.pN.tapjump */
    int button_map[2][DESKTOP_MELEE_BTN_COUNT]; /* controls.pN.map, 0..7 */
    float fighter_size;                         /* display.size, 50..200 (percent) */
    bool shake;                                 /* display.shake */
    int timer_mode;                             /* display.timer: DESKTOP_MELEE_TIMER_* */
    bool tags;                                  /* display.tags */
    int hud_mode;                               /* display.hud: DESKTOP_MELEE_HUD_* */
    char screen[128];                           /* display.screen; "" = the primary */
    char pause_key[32];                         /* match.pauseKey, such as "ctrl+alt+M" */
    bool start_on_controller;                   /* match.startOnController */
    int cpu_di;                                 /* practice.di: DESKTOP_MELEE_DI_* */
    int cpu_tech;                               /* practice.tech: DESKTOP_MELEE_TECH_* */
    int cpu_mash;                               /* practice.mash: DESKTOP_MELEE_MASH_* */
    int cpu_escape;                             /* practice.escape: DESKTOP_MELEE_ESCAPE_* */
    bool quick_reset;                           /* practice.reset */
    bool menu_sounds;                           /* sound.menu */
    bool onboarding_done;                       /* onboarding.done */
    char disc_path[1024];                       /* disc.verified.path; "" = none */
    long long disc_size;                        /* disc.verified.size */
    long long disc_mtime;                       /* disc.verified.mtime */
} DesktopMeleeSettings;

/* The defaults of ui-guide.md section 13, which match desktop_melee.c's. */
void desktop_melee_settings_defaults(DesktopMeleeSettings* settings);

/* Reads "key = value" lines from text over *settings. Returns the number of
 * keys it understood. */
int desktop_melee_settings_parse(DesktopMeleeSettings* settings, const char* text);

/* Writes every key as "key = value" lines into out (NUL-terminated). Returns
 * the length written, or 0 when out_size is too small. */
size_t desktop_melee_settings_format(
    const DesktopMeleeSettings* settings, char* out, size_t out_size);

/* The live settings: the defaults until desktop_melee_settings_load. */
DesktopMeleeSettings* desktop_melee_settings(void);

/* The file's path: SDL_GetPrefPath(NULL, "Desktop Melee") + "settings.cfg".
 * False when the preferences folder is unavailable or out_size too small. */
bool desktop_melee_settings_path(char* out, size_t out_size);

/* Reads the file into the live settings (defaults for what it lacks) and
 * pushes them into the game. A missing file is not an error. */
void desktop_melee_settings_load(void);

/* Writes the live settings to the file, through a temporary file so a crash
 * never leaves half of it. Returns false and logs on failure. */
bool desktop_melee_settings_save(void);

/* Pushes *settings into the game with the bridge's setters. */
void desktop_melee_settings_apply(const DesktopMeleeSettings* settings);
