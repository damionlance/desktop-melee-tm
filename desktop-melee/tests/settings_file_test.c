/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Tests desktop-melee/src/settings_file.c: defaults, parsing, clamping and a
 * write-then-read round trip. Build and run (Linux):
 *   ninja -C build/linux desktop_melee_settings_test
 *   build/linux/desktop_melee_settings_test */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "settings_file.h"

/* The game's setters, stubbed: the test only checks the file. */
void desktop_melee_set_match_setup(const DesktopMeleeMatchSetup* setup) {
    (void)setup;
}
void desktop_melee_set_tap_jump(int player, bool on) {
    (void)player;
    (void)on;
}
void desktop_melee_set_button_map(int player, int button, int target) {
    (void)player;
    (void)button;
    (void)target;
}
void desktop_melee_set_fighter_size(float percent) {
    (void)percent;
}
void desktop_melee_set_shake(bool on) {
    (void)on;
}
void desktop_melee_set_timer_mode(int mode) {
    (void)mode;
}
void desktop_melee_set_tags(bool on) {
    (void)on;
}
void desktop_melee_set_hud_mode(int mode) {
    (void)mode;
}
void desktop_melee_set_screen(const char* name) {
    (void)name;
}
void desktop_melee_set_start_starts_match(bool on) {
    (void)on;
}
void desktop_melee_set_cpu_di(int mode) {
    (void)mode;
}
void desktop_melee_set_cpu_tech(int mode) {
    (void)mode;
}
void desktop_melee_set_cpu_mash(int mode) {
    (void)mode;
}
void desktop_melee_set_cpu_escape(int mode) {
    (void)mode;
}
void desktop_melee_set_quick_reset(bool on) {
    (void)on;
}

static void test_defaults(void) {
    DesktopMeleeSettings s;
    desktop_melee_settings_defaults(&s);
    assert(s.match.stocks == 4);
    assert(s.match.time_seconds == 7 * 60);
    assert(s.match.players[0].ckind == 2 && s.match.players[1].ckind == 2);
    assert(s.match.players[0].costume == 0 && s.match.players[1].costume == 1);
    assert(s.match.players[0].port == 0 && s.match.players[1].port == 1);
    assert(s.match.players[1].cpu == 0 && s.match.players[1].cpu_level == 9);
    assert(s.tap_jump[0] && s.tap_jump[1]);
    assert(s.button_map[1][DESKTOP_MELEE_BTN_Z] == DESKTOP_MELEE_BTN_Z);
    assert(s.fighter_size == 100.0f);
    assert(s.hud_mode == DESKTOP_MELEE_HUD_ISLAND);
    assert(strcmp(s.pause_key, "ctrl+alt+M") == 0);
    assert(!s.start_on_controller);
    assert(s.menu_sounds && !s.onboarding_done && s.disc_path[0] == '\0');
    assert(s.cpu_di == DESKTOP_MELEE_DI_OFF && s.cpu_tech == DESKTOP_MELEE_TECH_OFF);
    assert(s.cpu_mash == DESKTOP_MELEE_MASH_OFF);
    assert(s.cpu_escape == DESKTOP_MELEE_ESCAPE_OFF && s.quick_reset);
}

static void test_parse(void) {
    DesktopMeleeSettings s;
    desktop_melee_settings_defaults(&s);
    const int keys = desktop_melee_settings_parse(&s, "# a comment\n"
                                                      "players.p2.cpu = true\n"
                                                      "  players.p2.level=3  \n"
                                                      "players.p1.ckind = 9\r\n"
                                                      "controls.p1.map = 0 1 2 3 2 5 6\n"
                                                      "display.hud = bottom\n"
                                                      "display.screen = DELL U2720Q\n"
                                                      "match.minutes = 0\n"
                                                      "match.stocks = 500\n" /* clamped to 99 */
                                                      "display.size = 10\n"  /* clamped to 50 */
                                                      "practice.di = survival\n"
                                                      "practice.tech = mix\n"
                                                      "practice.escape = dodge\n"
                                                      "practice.reset = false\n"
                                                      "practice.mash = sometimes\n" /* ignored */
                                                      "display.timer = sideways\n"  /* ignored */
                                                      "unknown.key = 1\n"           /* ignored */
                                                      "no equals sign\n");
    assert(keys == 13);
    assert(s.cpu_escape == DESKTOP_MELEE_ESCAPE_DODGE && !s.quick_reset);
    assert(s.cpu_di == DESKTOP_MELEE_DI_SURVIVAL && s.cpu_tech == DESKTOP_MELEE_TECH_MIX);
    assert(s.cpu_mash == DESKTOP_MELEE_MASH_OFF);
    assert(s.match.players[1].cpu == 1 && s.match.players[1].cpu_level == 3);
    assert(s.match.players[0].ckind == 9);
    assert(s.button_map[0][DESKTOP_MELEE_BTN_Z] == DESKTOP_MELEE_BTN_X);
    assert(s.hud_mode == DESKTOP_MELEE_HUD_BOTTOM);
    assert(strcmp(s.screen, "DELL U2720Q") == 0);
    assert(s.match.time_seconds == 0);
    assert(s.match.stocks == 99);
    assert(s.fighter_size == 50.0f);
    assert(s.timer_mode == DESKTOP_MELEE_TIMER_TOP);
}

static void test_round_trip(void) {
    DesktopMeleeSettings a;
    desktop_melee_settings_defaults(&a);
    a.match.players[0].ckind = 0x13;
    a.match.players[1].cpu = 1;
    a.match.players[1].port = 3;
    a.tap_jump[1] = false;
    a.button_map[1][DESKTOP_MELEE_BTN_L] = DESKTOP_MELEE_BTN_NONE;
    a.fighter_size = 150.0f;
    a.shake = false;
    a.timer_mode = DESKTOP_MELEE_TIMER_HIDDEN;
    a.hud_mode = DESKTOP_MELEE_HUD_HIDDEN;
    snprintf(a.pause_key, sizeof a.pause_key, "%s", "ctrl+super+P");
    a.onboarding_done = true;
    a.start_on_controller = true;
    a.cpu_di = DESKTOP_MELEE_DI_OUT;
    a.cpu_tech = DESKTOP_MELEE_TECH_IN;
    a.cpu_mash = DESKTOP_MELEE_MASH_HIGH;
    a.cpu_di = DESKTOP_MELEE_DI_SLIGHT_OUT;
    a.cpu_escape = DESKTOP_MELEE_ESCAPE_DODGE;
    a.quick_reset = false;
    snprintf(a.disc_path, sizeof a.disc_path, "%s", "/home/player/Melee (USA).iso");
    a.disc_size = 1459978240LL;
    a.disc_mtime = 1793636428LL;

    char text[4096];
    assert(desktop_melee_settings_format(&a, text, sizeof text) > 0);
    assert(desktop_melee_settings_format(&a, text, 64) == 0); /* too small */
    assert(desktop_melee_settings_format(&a, text, sizeof text) > 0);

    DesktopMeleeSettings b;
    desktop_melee_settings_defaults(&b);
    desktop_melee_settings_parse(&b, text);
    assert(memcmp(&a.match, &b.match, sizeof a.match) == 0);
    assert(memcmp(a.tap_jump, b.tap_jump, sizeof a.tap_jump) == 0);
    assert(memcmp(a.button_map, b.button_map, sizeof a.button_map) == 0);
    assert(a.fighter_size == b.fighter_size && a.shake == b.shake);
    assert(a.timer_mode == b.timer_mode && a.tags == b.tags && a.hud_mode == b.hud_mode);
    assert(strcmp(a.screen, b.screen) == 0 && strcmp(a.pause_key, b.pause_key) == 0);
    assert(a.menu_sounds == b.menu_sounds && a.onboarding_done == b.onboarding_done);
    assert(a.start_on_controller == b.start_on_controller);
    assert(a.cpu_di == b.cpu_di && a.cpu_tech == b.cpu_tech && a.cpu_mash == b.cpu_mash);
    assert(a.cpu_escape == b.cpu_escape && a.quick_reset == b.quick_reset);
    assert(strcmp(a.disc_path, b.disc_path) == 0);
    assert(a.disc_size == b.disc_size && a.disc_mtime == b.disc_mtime);
}

int main(void) {
    test_defaults();
    test_parse();
    test_round_trip();
    printf("settings_file_test: all passed\n");
    return 0;
}
