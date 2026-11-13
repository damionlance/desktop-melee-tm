/* SPDX-License-Identifier: GPL-3.0-or-later */
/* desktop-melee hooks called from the decompiled game code (melee_game). */
#pragma once
#include <stdbool.h>

/* DESKTOP_MELEE_OVERLAY=1: run as a transparent, click-through window over the
 * whole primary display instead of a normal window. */
bool desktop_melee_overlay_enabled(void);

/* Desktop overlay: stage and background models are not drawn, so only the
 * fighters, effects and HUD appear over the desktop. */
bool desktop_melee_hide_stage(void);

/* Fixed desktop camera, replacing Melee's zooming one in overlay mode. Fills
 * the eye and interest points (Melee units) and the vertical field of view
 * (degrees) so that Melee y = 0 sits on top of the Dock, x = 0 is the middle
 * of the screen, and the screen is DESKTOP_MELEE_SCALE units wide (default 490,
 * Final Destination's blastzone width). False when not in overlay mode. */
bool desktop_melee_camera(float* eye_x, float* eye_y, float* eye_z, float* interest_x,
    float* interest_y, float* interest_z, float* fov);

/* Screen shake (Display tab): when off, Melee's quake offset is not applied
 * to the overlay camera. Applies immediately. */
bool desktop_melee_shake_enabled(void);
void desktop_melee_set_shake(bool on);

/* DESKTOP_MELEE_TRACE=1 (tests): the overlay camera reports the shake it applied
 * each frame; desktop_melee_trace_tick (once a second, from an AppKit timer, so also
 * while paused) logs a "desktop-melee: trace" line during a match. */
void desktop_melee_trace_shake(float x, float y);
void desktop_melee_trace_tick(void);
bool desktop_melee_trace_enabled(void);

/* Pause and hide (menu bar button, global shortcut): hides the overlay and
 * the island and pauses a running match with Melee's own pause, as
 * desktop_melee_set_pause_port's port (the first human player's, else port 1);
 * pressing it again shows them, still paused. */
void desktop_melee_toggle_pause_and_hide(void);
bool desktop_melee_hidden(void);
void desktop_melee_set_pause_port(int port);
/* The port to pause as, once, after a toggle asked for a pause (else -1). */
int desktop_melee_take_pause_request(void);
/* desktop-melee/game/gmdesktopmelee.c: whether a VS pause is on. */
bool desktop_melee_game_paused(void);

/* CPU practice (Match tab): how CPU players react when hit, so a human can
 * practice combos and chaingrabs on them. Off leaves Melee's own CPU.
 * Applies immediately. (desktop-melee/game/desktop_melee_cpu_practice.c) */
enum {
    DESKTOP_MELEE_DI_OFF = 0,        /* Melee's own: CPUs don't DI */
    DESKTOP_MELEE_DI_IN = 1,         /* full, toward the attacker */
    DESKTOP_MELEE_DI_OUT = 2,        /* full, away from the attacker */
    DESKTOP_MELEE_DI_SLIGHT_IN = 3,  /* half stick, toward the attacker */
    DESKTOP_MELEE_DI_SLIGHT_OUT = 4, /* half stick, away from the attacker */
    DESKTOP_MELEE_DI_SURVIVAL = 5,   /* perpendicular, up and toward the stage */
    DESKTOP_MELEE_DI_MIX = 6,        /* per hit: none, in, out, slight in or out */
    DESKTOP_MELEE_DI_COUNT = 7
};
enum {
    DESKTOP_MELEE_TECH_OFF = 0, /* Melee's own: CPUs rarely tech */
    DESKTOP_MELEE_TECH_PLACE = 1,
    DESKTOP_MELEE_TECH_IN = 2,  /* roll toward the attacker */
    DESKTOP_MELEE_TECH_OUT = 3, /* roll away from the attacker */
    DESKTOP_MELEE_TECH_MISS = 4,
    DESKTOP_MELEE_TECH_MIX = 5, /* one of the four at random, per hit */
    DESKTOP_MELEE_TECH_COUNT = 6
};
enum {
    DESKTOP_MELEE_MASH_OFF = 0, /* Melee's own grab escape */
    DESKTOP_MELEE_MASH_LOW = 1,
    DESKTOP_MELEE_MASH_MEDIUM = 2,
    DESKTOP_MELEE_MASH_HIGH = 3,
    DESKTOP_MELEE_MASH_COUNT = 4
};
/* What a CPU does once it can act again after being hit (out of hitstun,
 * a tech, or a missed tech after getting up). */
enum {
    DESKTOP_MELEE_ESCAPE_OFF = 0,   /* Melee's own CPU decides */
    DESKTOP_MELEE_ESCAPE_DODGE = 1, /* spot dodge on the ground, air dodge in the air */
    DESKTOP_MELEE_ESCAPE_COUNT = 2
};
int desktop_melee_cpu_di(void);
void desktop_melee_set_cpu_di(int mode);
int desktop_melee_cpu_tech(void);
void desktop_melee_set_cpu_tech(int mode);
int desktop_melee_cpu_mash(void);
void desktop_melee_set_cpu_mash(int mode);
int desktop_melee_cpu_escape(void);
void desktop_melee_set_cpu_escape(int mode);
/* A random number for practice choices, from outside the game state: a
 * reset rewinds the game, and Mix should not repeat itself after one. */
unsigned desktop_melee_practice_random(void);
/* Hooks from the decompiled fighter code, gobj a Fighter_GObj*.
 * src/melee/ft/fighter.c, after the CPU's own inputs for the frame: */
void desktop_melee_cpu_practice_frame(void* gobj);
/* ftCo_DownAttack.c's tech check: 1 tech, 0 no tech, -1 Melee decides. */
int desktop_melee_cpu_tech_override(void* gobj);
/* ftCo_PassiveStand.c's tech roll: -1 Melee decides (stick), 0 no roll (tech
 * in place), else the motion state to roll with. */
int desktop_melee_cpu_tech_roll(void* gobj);

/* Quick reset (Match tab): D-pad down on any controller puts the match back
 * the way it was when it started (positions, percents, stocks), from a
 * snapshot taken when the fighters could first move.
 * desktop-melee/src/practice_reset.c; the snapshot is melee-pc's rollback one. */
bool desktop_melee_quick_reset(void);
void desktop_melee_set_quick_reset(bool on);
/* src/melee/gm/gmscene.c, between ticks: take the snapshot, or restore it. */
void desktop_melee_practice_tick(void);
/* desktop-melee/game/desktop_melee_cpu_practice.c, for the tick: every
 * fighter is standing and can move (the match has started) ... */
bool desktop_melee_practice_fighters_ready(void);
/* ... and D-pad down was pressed on some controller in the last tick. */
bool desktop_melee_practice_reset_pressed(void);

/* Timer (Display tab): at the top at desktop_melee_timer_scale() of Melee's size, or
 * hidden (the last-seconds countdown too). Applies immediately. */
enum { DESKTOP_MELEE_TIMER_TOP = 0, DESKTOP_MELEE_TIMER_HIDDEN = 1 };
int desktop_melee_timer_mode(void);
void desktop_melee_set_timer_mode(int mode);
float desktop_melee_timer_scale(void);
/* src/melee/if/iftime.c: apply the mode (each frame), and the trace. */
void desktop_melee_timer_frame(void);
bool desktop_melee_timer_trace(float* scale);

/* Player tags (Display tab): the P1 / P2 / CPU markers over fighters, always
 * shown in the match or never. Applies immediately. */
bool desktop_melee_tags_enabled(void);
void desktop_melee_set_tags(bool on);
/* src/melee/if/ifnametag.c: tags showing now (trace). */
int desktop_melee_tags_trace(void);

/* Where stocks and percents go (Display tab): Melee's own HUD in the notch
 * island, at the bottom of the screen, or hidden. Set any time; a match
 * uses the mode it started with (desktop_melee_hud_mode). */
enum { DESKTOP_MELEE_HUD_ISLAND = 0, DESKTOP_MELEE_HUD_BOTTOM = 1, DESKTOP_MELEE_HUD_HIDDEN = 2 };
void desktop_melee_set_hud_mode(int mode);
int desktop_melee_hud_mode(void);
/* Trace: a stock or percent group was drawn. */
void desktop_melee_trace_hud_drawn(void);

/* The Dock's left and right edges in Melee units (x), for stretching the main
 * stage floor across it. False when not in overlay mode. */
bool desktop_melee_dock_span(float* left, float* right);

/* The blastzones (Melee units): the overlay's screen edges, at the size the
 * current match uses. False when not in overlay mode. */
bool desktop_melee_blastzones(float* left, float* right, float* top, float* bottom);
/* Window platform `index` (0 to DESKTOP_MELEE_WINDOW_PLATFORMS - 1): the top
 * edge of one of the player's windows, in stage units, or false for none.
 * *ledges: both ends are the window's top corners, so they can be grabbed. */
bool desktop_melee_window_platform(int index, float* left, float* right, float* y, bool* ledges);
/* How much wider than Melee's 4:3 view the overlay shows (melee-pc's
 * widescreen widens only what is drawn), or 1 outside the overlay: Melee's
 * "is the fighter on screen" test widens by it, so fighters in the extra
 * width get no magnifying-glass bubble. */
float desktop_melee_onscreen_widen(void);
/* False when the floor runs the whole width of the screen (Linux's panel):
 * the stage's own ledges would sit on the side blastzones. */
bool desktop_melee_floor_ledges(void);

/* HUD camera, reported once when the HUD scene loads: distance from the eye
 * to the HUD plane (Melee units) and vertical field of view (degrees). */
void desktop_melee_set_hud_camera(float distance, float fov_deg);
/* Called every frame the HUD camera renders; the notch island is shown while
 * these keep coming and the HUD is visible. */
void desktop_melee_hud_frame(bool visible);
/* Stock icons inside the notch island: the translation and uniform scale (HUD
 * units) for a player's stock group, P1 left of the notch and P2 right of it.
 * False keeps Melee's own placement. */
bool desktop_melee_hud_stock_anchor(int player, float* x, float* y, float* scale);
/* The same for a player's percent (damage) group. */
bool desktop_melee_hud_percent_anchor(int player, float* x, float* y, float* scale);
/* True when this player's HUD lives in the notch island (its series emblem
 * is then not drawn). */
bool desktop_melee_hud_in_island(int player);

/* ---- GM_DESKTOP_MELEE match flow (desktop-melee/game/gmdesktopmelee.c) ------------------ */

typedef struct DesktopMeleePlayerSetup {
    int ckind;     /* CharacterKind (2 = Fox) */
    int costume;   /* costume/color index */
    int cpu;       /* 0 human, 1 CPU */
    int cpu_level; /* 1..9 */
    int port;      /* controller port 0..3 that plays this player (humans) */
} DesktopMeleePlayerSetup;

typedef struct DesktopMeleeMatchSetup {
    int stocks;       /* 1..99 */
    int time_seconds; /* 0 = no time limit */
    DesktopMeleePlayerSetup players[2];
} DesktopMeleeMatchSetup;

/* The next match's setup, as the menu bar left it. */
void desktop_melee_match_setup(DesktopMeleeMatchSetup* out);
/* Ask for a match: starts one from idle, restarts a running one. */
void desktop_melee_request_match(void);
/* Whether Start on a controller begins a match from idle (off while the
 * welcome window asks the player to press buttons). */
bool desktop_melee_start_starts_match(void);
void desktop_melee_set_start_starts_match(bool on);
/* Idle scene: a match has been asked for. */
bool desktop_melee_match_wanted(void);
/* VS scene: a new match was asked for while this one runs. */
bool desktop_melee_match_restart_wanted(void);
/* The Desktop Melee VS state entered / left. */
void desktop_melee_match_began(void);
void desktop_melee_match_ended(void);

/* ---- per-player controls (desktop-melee/src/desktop_melee.c) ----------------------------- */
/* Set from the menu bar's Controls tab; read every frame, so changes apply
 * mid-match. Players are fighter slots (P1 = 0). */

/* The Melee buttons a player's button map covers. */
enum {
    DESKTOP_MELEE_BTN_A,
    DESKTOP_MELEE_BTN_B,
    DESKTOP_MELEE_BTN_X,
    DESKTOP_MELEE_BTN_Y,
    DESKTOP_MELEE_BTN_Z,
    DESKTOP_MELEE_BTN_L,
    DESKTOP_MELEE_BTN_R,
    DESKTOP_MELEE_BTN_COUNT,
    DESKTOP_MELEE_BTN_NONE = DESKTOP_MELEE_BTN_COUNT,
};

/* Tap jump: stick up jumps (Melee's own behavior, on by default). Called
 * where a stick-up jump would start; false blocks it. */
bool desktop_melee_tap_jump(int player);
/* A human player's held buttons (HSD_PAD_* bits) after their button map.
 * Also says which analog triggers still shield: bit 0 the physical L, bit 1
 * the physical R (a trigger mapped to another button no longer does). */
unsigned desktop_melee_map_buttons(int player, unsigned held, int* analog_mask);

/* ---- game assets (desktop-melee/game/desktop_melee_assets.c) ----------------------------- */

struct GcTexture;
/* A texture found in a game archive: tag names where it came from (model and
 * texture-animation table), index is its position there. */
void desktop_melee_asset_found(const char* tag, int index, const struct GcTexture* tex);

/* The character select grid: 25 characters, Melee's order. */
#define DESKTOP_MELEE_ROSTER_SIZE 25
#define DESKTOP_MELEE_MAX_COSTUMES 6
/* Roster entry i: character kind, costume count and face icon. */
void desktop_melee_asset_roster(int i, int ckind, int costumes, const struct GcTexture* icon);
/* How many costumes a character kind has (every playable kind, Sheik too). */
void desktop_melee_asset_costumes(int ckind, int costumes);
/* A character's portrait for one costume. */
void desktop_melee_asset_portrait(int ckind, int costume, const struct GcTexture* portrait);
/* The roster and font are loaded: tells the menu bar app. */
void desktop_melee_assets_ready(void);

/* Melee's menu sounds, for the menu bar: 0 back, 1 confirm, 2 cursor move,
 * 3 not allowed. */
void desktop_melee_sound_menu(int kind);
/* The announcer says the name of roster entry i. */
void desktop_melee_sound_announce(int roster_index);
/* A named graphic for the menu bar ("emblem", "melee"). */
void desktop_melee_asset_named(const char* name, const struct GcTexture* tex);
