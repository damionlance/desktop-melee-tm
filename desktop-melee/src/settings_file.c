/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Desktop Melee's settings file (settings_file.h). */
#include "settings_file.h"

#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ui/bridge/desktop_melee_bridge.h"

static DesktopMeleeSettings s_settings;
static bool s_settings_ready = false;

void desktop_melee_settings_defaults(DesktopMeleeSettings* settings) {
    memset(settings, 0, sizeof *settings);
    settings->match.stocks = 4;
    settings->match.time_seconds = 7 * 60;
    for (int i = 0; i < 2; i++) {
        DesktopMeleePlayerSetup* player = &settings->match.players[i];
        player->ckind = 2; /* Fox */
        player->costume = i;
        player->cpu = 0;
        player->cpu_level = 9;
        player->port = i;
        settings->tap_jump[i] = true;
        for (int button = 0; button < DESKTOP_MELEE_BTN_COUNT; button++) {
            settings->button_map[i][button] = button;
        }
    }
    settings->fighter_size = 100.0f;
    settings->shake = true;
    settings->timer_mode = DESKTOP_MELEE_TIMER_TOP;
    settings->tags = true;
    settings->hud_mode = DESKTOP_MELEE_HUD_ISLAND;
    settings->cpu_di = DESKTOP_MELEE_DI_OFF;
    settings->cpu_tech = DESKTOP_MELEE_TECH_OFF;
    settings->cpu_mash = DESKTOP_MELEE_MASH_OFF;
    snprintf(settings->pause_key, sizeof settings->pause_key, "%s", "ctrl+alt+M");
    settings->menu_sounds = true;
}

/* ---- parsing ------------------------------------------------------------ */

static int clamp_int(long value, int lo, int hi) {
    return value < lo ? lo : value > hi ? hi : (int)value;
}

static bool parse_long(const char* value, long* out) {
    char* end = NULL;
    const long parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0') {
        return false;
    }
    *out = parsed;
    return true;
}

static bool parse_bool(const char* value, bool* out) {
    if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
        *out = true;
        return true;
    }
    if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
        *out = false;
        return true;
    }
    return false;
}

/* "players.p1.ckind" -> player 0 and "ckind" after prefix "players.p". */
static bool player_key(const char* key, const char* prefix, int* player, const char** field) {
    const size_t n = strlen(prefix);
    if (strncmp(key, prefix, n) != 0 || (key[n] != '1' && key[n] != '2') || key[n + 1] != '.') {
        return false;
    }
    *player = key[n] - '1';
    *field = key + n + 2;
    return true;
}

/* Applies one key; returns whether it was understood. */
/* practice.*: the DESKTOP_MELEE_DI_*, _TECH_* and _MASH_* names, in order. */
static const char* const di_names[DESKTOP_MELEE_DI_COUNT] = {"off", "in", "out", "survival", "mix"};
static const char* const tech_names[DESKTOP_MELEE_TECH_COUNT] = {
    "off", "place", "in", "out", "miss", "mix"};
static const char* const mash_names[DESKTOP_MELEE_MASH_COUNT] = {"off", "low", "medium", "high"};

static bool parse_name(const char* value, const char* const* names, int count, int* out) {
    for (int i = 0; i < count; i++) {
        if (strcmp(value, names[i]) == 0) {
            *out = i;
            return true;
        }
    }
    return false;
}

static const char* name_of(const char* const* names, int count, int value) {
    return names[value >= 0 && value < count ? value : 0];
}

static bool parse_entry(DesktopMeleeSettings* s, const char* key, const char* value) {
    long n = 0;
    bool b = false;
    int player = 0;
    const char* field = NULL;

    if (player_key(key, "players.p", &player, &field)) {
        DesktopMeleePlayerSetup* p = &s->match.players[player];
        if (strcmp(field, "ckind") == 0 && parse_long(value, &n)) {
            p->ckind = clamp_int(n, 0x00, 0x19);
        } else if (strcmp(field, "costume") == 0 && parse_long(value, &n)) {
            p->costume = clamp_int(n, 0, 5);
        } else if (strcmp(field, "cpu") == 0 && parse_bool(value, &b)) {
            p->cpu = b ? 1 : 0;
        } else if (strcmp(field, "level") == 0 && parse_long(value, &n)) {
            p->cpu_level = clamp_int(n, 1, 9);
        } else {
            return false;
        }
        return true;
    }
    if (player_key(key, "controls.p", &player, &field)) {
        if (strcmp(field, "port") == 0 && parse_long(value, &n)) {
            s->match.players[player].port = clamp_int(n, 0, 3);
        } else if (strcmp(field, "tapjump") == 0 && parse_bool(value, &b)) {
            s->tap_jump[player] = b;
        } else if (strcmp(field, "map") == 0) {
            int map[DESKTOP_MELEE_BTN_COUNT];
            const char* cursor = value;
            for (int i = 0; i < DESKTOP_MELEE_BTN_COUNT; i++) {
                char* end = NULL;
                const long target = strtol(cursor, &end, 10);
                if (end == cursor) {
                    return false;
                }
                map[i] = clamp_int(target, 0, DESKTOP_MELEE_BTN_NONE);
                cursor = end;
            }
            memcpy(s->button_map[player], map, sizeof map);
        } else {
            return false;
        }
        return true;
    }
    if (strcmp(key, "display.size") == 0) {
        char* end = NULL;
        const float size = strtof(value, &end);
        if (end == value || *end != '\0') {
            return false;
        }
        s->fighter_size = size < 50.0f ? 50.0f : size > 200.0f ? 200.0f : size;
    } else if (strcmp(key, "display.shake") == 0 && parse_bool(value, &b)) {
        s->shake = b;
    } else if (strcmp(key, "display.timer") == 0) {
        if (strcmp(value, "top") == 0) {
            s->timer_mode = DESKTOP_MELEE_TIMER_TOP;
        } else if (strcmp(value, "hidden") == 0) {
            s->timer_mode = DESKTOP_MELEE_TIMER_HIDDEN;
        } else {
            return false;
        }
    } else if (strcmp(key, "display.tags") == 0 && parse_bool(value, &b)) {
        s->tags = b;
    } else if (strcmp(key, "display.hud") == 0) {
        if (strcmp(value, "island") == 0) {
            s->hud_mode = DESKTOP_MELEE_HUD_ISLAND;
        } else if (strcmp(value, "bottom") == 0) {
            s->hud_mode = DESKTOP_MELEE_HUD_BOTTOM;
        } else if (strcmp(value, "hidden") == 0) {
            s->hud_mode = DESKTOP_MELEE_HUD_HIDDEN;
        } else {
            return false;
        }
    } else if (strcmp(key, "practice.di") == 0) {
        return parse_name(value, di_names, DESKTOP_MELEE_DI_COUNT, &s->cpu_di);
    } else if (strcmp(key, "practice.tech") == 0) {
        return parse_name(value, tech_names, DESKTOP_MELEE_TECH_COUNT, &s->cpu_tech);
    } else if (strcmp(key, "practice.mash") == 0) {
        return parse_name(value, mash_names, DESKTOP_MELEE_MASH_COUNT, &s->cpu_mash);
    } else if (strcmp(key, "display.screen") == 0) {
        snprintf(s->screen, sizeof s->screen, "%s", value);
    } else if (strcmp(key, "match.stocks") == 0 && parse_long(value, &n)) {
        s->match.stocks = clamp_int(n, 1, 99);
    } else if (strcmp(key, "match.minutes") == 0 && parse_long(value, &n)) {
        s->match.time_seconds = clamp_int(n, 0, 99) * 60;
    } else if (strcmp(key, "match.startOnController") == 0 && parse_bool(value, &b)) {
        s->start_on_controller = b;
    } else if (strcmp(key, "match.pauseKey") == 0) {
        snprintf(s->pause_key, sizeof s->pause_key, "%s", value);
    } else if (strcmp(key, "sound.menu") == 0 && parse_bool(value, &b)) {
        s->menu_sounds = b;
    } else if (strcmp(key, "onboarding.done") == 0 && parse_bool(value, &b)) {
        s->onboarding_done = b;
    } else if (strcmp(key, "disc.verified.path") == 0) {
        snprintf(s->disc_path, sizeof s->disc_path, "%s", value);
    } else if (strcmp(key, "disc.verified.size") == 0 && parse_long(value, &n)) {
        s->disc_size = n;
    } else if (strcmp(key, "disc.verified.mtime") == 0 && parse_long(value, &n)) {
        s->disc_mtime = n;
    } else {
        return false;
    }
    return true;
}

static char* trim(char* text) {
    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }
    char* end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    return text;
}

int desktop_melee_settings_parse(DesktopMeleeSettings* settings, const char* text) {
    int understood = 0;
    while (text != NULL && *text != '\0') {
        const char* newline = strchr(text, '\n');
        const size_t length = newline != NULL ? (size_t)(newline - text) : strlen(text);
        char line[1280];
        if (length < sizeof line) {
            memcpy(line, text, length);
            line[length] = '\0';
            char* entry = trim(line);
            char* equals = strchr(entry, '=');
            if (entry[0] != '#' && equals != NULL) {
                *equals = '\0';
                const char* key = trim(entry);
                const char* value = trim(equals + 1);
                if (parse_entry(settings, key, value)) {
                    understood++;
                } else {
                    SDL_Log("desktop-melee: settings: ignored \"%s = %s\"", key, value);
                }
            }
        }
        text = newline != NULL ? newline + 1 : NULL;
    }
    return understood;
}

/* ---- writing ------------------------------------------------------------ */

static const char* bool_text(bool on) {
    return on ? "true" : "false";
}

size_t desktop_melee_settings_format(const DesktopMeleeSettings* s, char* out, size_t out_size) {
    static const char* const timer_names[] = {"top", "hidden"};
    static const char* const hud_names[] = {"island", "bottom", "hidden"};
    size_t used = 0;
    bool fits = true;
#define EMIT(...)                                                                                  \
    do {                                                                                           \
        const int written = snprintf(out + used, out_size - used, __VA_ARGS__);                    \
        if (written < 0 || (size_t)written >= out_size - used) {                                   \
            fits = false;                                                                          \
        } else {                                                                                   \
            used += (size_t)written;                                                               \
        }                                                                                          \
    } while (0)

    if (out_size == 0) {
        return 0;
    }
    out[0] = '\0';
    EMIT("# Desktop Melee settings (docs/desktop-melee/ui-guide.md section 13).\n");
    for (int i = 0; i < 2 && fits; i++) {
        const DesktopMeleePlayerSetup* p = &s->match.players[i];
        EMIT("players.p%d.ckind = %d\n", i + 1, p->ckind);
        EMIT("players.p%d.costume = %d\n", i + 1, p->costume);
        EMIT("players.p%d.cpu = %s\n", i + 1, bool_text(p->cpu != 0));
        EMIT("players.p%d.level = %d\n", i + 1, p->cpu_level);
    }
    for (int i = 0; i < 2 && fits; i++) {
        const int* map = s->button_map[i];
        EMIT("controls.p%d.port = %d\n", i + 1, s->match.players[i].port);
        EMIT("controls.p%d.tapjump = %s\n", i + 1, bool_text(s->tap_jump[i]));
        EMIT("controls.p%d.map = %d %d %d %d %d %d %d\n", i + 1, map[0], map[1], map[2], map[3],
            map[4], map[5], map[6]);
    }
    EMIT("display.size = %g\n", (double)s->fighter_size);
    EMIT("display.shake = %s\n", bool_text(s->shake));
    EMIT("display.timer = %s\n", timer_names[s->timer_mode == DESKTOP_MELEE_TIMER_HIDDEN]);
    EMIT("display.tags = %s\n", bool_text(s->tags));
    EMIT("display.hud = %s\n", hud_names[s->hud_mode >= DESKTOP_MELEE_HUD_ISLAND &&
                                                 s->hud_mode <= DESKTOP_MELEE_HUD_HIDDEN ?
                                             s->hud_mode :
                                             DESKTOP_MELEE_HUD_ISLAND]);
    EMIT("display.screen = %s\n", s->screen);
    EMIT("practice.di = %s\n", name_of(di_names, DESKTOP_MELEE_DI_COUNT, s->cpu_di));
    EMIT("practice.tech = %s\n", name_of(tech_names, DESKTOP_MELEE_TECH_COUNT, s->cpu_tech));
    EMIT("practice.mash = %s\n", name_of(mash_names, DESKTOP_MELEE_MASH_COUNT, s->cpu_mash));
    EMIT("match.stocks = %d\n", s->match.stocks);
    EMIT("match.minutes = %d\n", s->match.time_seconds / 60);
    EMIT("match.startOnController = %s\n", bool_text(s->start_on_controller));
    EMIT("match.pauseKey = %s\n", s->pause_key);
    EMIT("sound.menu = %s\n", bool_text(s->menu_sounds));
    EMIT("onboarding.done = %s\n", bool_text(s->onboarding_done));
    if (s->disc_path[0] != '\0') {
        EMIT("disc.verified.path = %s\n", s->disc_path);
        EMIT("disc.verified.size = %lld\n", s->disc_size);
        EMIT("disc.verified.mtime = %lld\n", s->disc_mtime);
    }
#undef EMIT
    if (!fits) {
        out[0] = '\0';
        return 0;
    }
    return used;
}

/* ---- the live settings and the file ------------------------------------- */

DesktopMeleeSettings* desktop_melee_settings(void) {
    if (!s_settings_ready) {
        desktop_melee_settings_defaults(&s_settings);
        s_settings_ready = true;
    }
    return &s_settings;
}

bool desktop_melee_settings_path(char* out, size_t out_size) {
    char* folder = SDL_GetPrefPath(NULL, "Desktop Melee");
    if (folder == NULL) {
        SDL_Log("desktop-melee: settings: no preferences folder: %s", SDL_GetError());
        return false;
    }
    const int written = snprintf(out, out_size, "%ssettings.cfg", folder);
    SDL_free(folder);
    return written > 0 && (size_t)written < out_size;
}

void desktop_melee_settings_load(void) {
    DesktopMeleeSettings* settings = desktop_melee_settings();
    char path[1024];
    if (desktop_melee_settings_path(path, sizeof path)) {
        size_t size = 0;
        char* text = (char*)SDL_LoadFile(path, &size);
        if (text != NULL) {
            const int keys = desktop_melee_settings_parse(settings, text);
            SDL_free(text);
            SDL_Log("desktop-melee: settings: %d keys from %s", keys, path);
        } else {
            SDL_Log("desktop-melee: settings: none yet at %s, using the defaults", path);
        }
    }
    desktop_melee_settings_apply(settings);
}

bool desktop_melee_settings_save(void) {
    char path[1024];
    char temporary[1040];
    char text[4096];
    if (!desktop_melee_settings_path(path, sizeof path)) {
        return false;
    }
    const size_t length =
        desktop_melee_settings_format(desktop_melee_settings(), text, sizeof text);
    if (length == 0) {
        SDL_Log("desktop-melee: settings: too long to write");
        return false;
    }
    snprintf(temporary, sizeof temporary, "%s.tmp", path);
    if (!SDL_SaveFile(temporary, text, length) || !SDL_RenamePath(temporary, path)) {
        SDL_Log("desktop-melee: settings: could not write %s: %s", path, SDL_GetError());
        SDL_RemovePath(temporary);
        return false;
    }
    return true;
}

void desktop_melee_settings_apply(const DesktopMeleeSettings* s) {
    desktop_melee_set_match_setup(&s->match);
    for (int player = 0; player < 2; player++) {
        desktop_melee_set_tap_jump(player, s->tap_jump[player]);
        for (int button = 0; button < DESKTOP_MELEE_BTN_COUNT; button++) {
            desktop_melee_set_button_map(player, button, s->button_map[player][button]);
        }
    }
    desktop_melee_set_fighter_size(s->fighter_size);
    desktop_melee_set_shake(s->shake);
    desktop_melee_set_timer_mode(s->timer_mode);
    desktop_melee_set_tags(s->tags);
    desktop_melee_set_hud_mode(s->hud_mode);
    desktop_melee_set_screen(s->screen);
    desktop_melee_set_start_starts_match(s->start_on_controller);
    desktop_melee_set_cpu_di(s->cpu_di);
    desktop_melee_set_cpu_tech(s->cpu_tech);
    desktop_melee_set_cpu_mash(s->cpu_mash);
}
