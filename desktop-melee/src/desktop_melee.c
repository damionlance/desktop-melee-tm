/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "desktop_melee.h"

#include <math.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>

#include "pc/pc.h"
#include "pc/widescreen.h"

#include <aurora/gfx.h>
#include <dolphin/gx/GXAurora.h>

#include <stdio.h>
#include <string.h>
#include <dolphin/pad.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

static int s_overlay = -1;

/* Screen geometry in points (y up from the bottom edge). */
static float s_screen_w = 0.0f;
static float s_screen_h = 0.0f;
static float s_floor = 0.0f;
/* Notch island, in points from the screen's left/top edges. */
static float s_notch_left = 0.0f;
static float s_notch_right = 0.0f;
static float s_island_top = 0.0f;
static float s_island_h = 0.0f;
/* HUD camera (see desktop_melee_set_hud_camera) and the last HUD frame. */
static float s_hud_distance = 0.0f;
static float s_hud_fov = 0.0f;
static volatile Uint64 s_hud_last_ms = 0;
static volatile bool s_hud_visible = false;

/* Melee's stock group layout (IfAll stock model): icon k's offset from the
 * group origin is (-5.0 + 2.4 k, 7.5) HUD units. */
#define STOCK_X0 (-5.0f)
#define STOCK_Y (7.5f)
#define STOCK_PITCH (2.4f)
/* Icons shown per side of the notch (the match is 4 stocks). */
#define ISLAND_STOCKS 4

/* Melee's damage (percent) group layout: hundreds, tens and ones digits at
 * x = -4.68, -1.53, 1.62 and the % sign at 5.04. Their animation settles the
 * digits ~2.2 units above the group origin (the model's rest pose, -8.5, is
 * only the first frame). A digit is ~2.8 units tall. The number is
 * right-aligned, so its full extent (three digits and %) is reserved. */
#define PCT_LEFT (-6.2f)
#define PCT_RIGHT (6.3f)
#define PCT_Y (2.2f)
#define PCT_DIGIT_H (2.8f)

/* Island layout, as fractions of the island height. Per side, from the
 * notch outwards: pad, stocks, gap, percent, end. */
#define ISLAND_PAD (0.40f)     /* gap between the notch and the first icon */
#define ISLAND_PITCH (0.62f)   /* stock icon spacing */
#define ISLAND_GAP (0.35f)     /* between the stocks and the percent */
#define ISLAND_DIGIT_H (0.60f) /* percent digit height */
#define ISLAND_END (0.40f)     /* black margin past the percent */
#define ISLAND_PCT_W (ISLAND_DIGIT_H * (PCT_RIGHT - PCT_LEFT) / PCT_DIGIT_H)
#define ISLAND_SIDE                                                                                \
    (ISLAND_PAD + ISLAND_PITCH * ISLAND_STOCKS + ISLAND_GAP + ISLAND_PCT_W + ISLAND_END)

/* Dock bar edges in points; equal when unknown. */
static float s_dock_left = 0.0f;
static float s_dock_right = 0.0f;

bool desktop_melee_overlay_enabled(void) {
    if (s_overlay < 0) {
        const char* env = getenv("DESKTOP_MELEE_OVERLAY");
        s_overlay = env != NULL && env[0] != '\0' && env[0] != '0';
    }
    return s_overlay != 0;
}

bool desktop_melee_hide_stage(void) {
    return desktop_melee_overlay_enabled();
}

static bool trace_enabled(void);
#ifdef SIGUSR1
static void on_usr1(int sig);
#endif

void desktop_melee_configure(AuroraConfig* config) {
    if (!desktop_melee_overlay_enabled()) {
        return;
    }
    config->overlayWindow = true;
#ifdef SIGUSR1
    if (trace_enabled()) {
        /* Tests: SIGUSR1 presses Pause and hide (desktop_melee_trace_tick). */
        signal(SIGUSR1, on_usr1);
    }
#endif
    /* A menu bar app: SDL must not turn the process into a regular app with a
     * Dock icon (desktop_melee_platform_ui_install makes it an accessory
     * app). */
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    config->startFullscreen = false;
    /* The overlay never asks for focus, so losing it must not pause the game,
     * and controllers must keep working while other apps are in front. */
    config->pauseOnFocusLost = false;
    config->allowJoystickBackgroundEvents = true;
    desktop_melee_platform_configure(config);
}

static SDL_Window* s_window;

/* Display settings from the menu bar. Size and island are what the next
 * match uses (latched in desktop_melee_match_began); the screen applies while idle. */
#define DEFAULT_UNITS 490.0f
static float s_units_next = DEFAULT_UNITS;
static float s_units = DEFAULT_UNITS; /* the running (or last) match's */
/* Where stocks and percents go (DESKTOP_MELEE_HUD_*): the menu's choice, and the
 * running match's (latched when it starts). */
static int s_hud_next = DESKTOP_MELEE_HUD_ISLAND;
static int s_hud = DESKTOP_MELEE_HUD_ISLAND;
static char s_screen_name[128];
static bool s_screen_pending = false;

/* DESKTOP_MELEE_SCALE: Melee units across the screen width; overrides the menu. */
static float env_units(void) {
    const char* env = getenv("DESKTOP_MELEE_SCALE");
    const float units = env != NULL ? (float)atof(env) : 0.0f;
    return units >= 50.0f && units <= 5000.0f ? units : 0.0f;
}

static bool s_dock_exact = false;
bool desktop_melee_dock_exact(void) {
    return s_dock_exact;
}

static void place_island(void);

/* Screen size, floor, Dock and notch island for the screen the overlay is
 * on. Run at startup and whenever the overlay changes screens. */
static void layout(void) {
    float w, h, floor_pt;
    desktop_melee_platform_screen_geometry(s_window, &w, &h, &floor_pt);
    float dock_left = 0.0f, dock_right = 0.0f, dock_top = floor_pt;
    bool exact = false;
    const bool dock =
        desktop_melee_platform_dock_frame(w, h, &dock_left, &dock_right, &dock_top, &exact);
    if (dock && exact) {
        floor_pt = dock_top;
    }
    s_dock_exact = dock && exact;
    desktop_melee_set_screen_geometry(w, h, floor_pt);
    s_dock_left = dock ? dock_left : 0.0f;
    s_dock_right = dock ? dock_right : 0.0f;
    if (dock) {
        SDL_Log("desktop-melee: Dock %.0f..%.0f pt (%s)", dock_left, dock_right,
            exact ? "exact" : "estimated; grant Accessibility for the exact size");
    } else {
        SDL_Log("desktop-melee: no Dock on this screen: Final Destination's own floor");
    }

    place_island();
}

float desktop_melee_island_side(float height) {
    return height * ISLAND_SIDE;
}

/* The island, around the platform's notch. Run by layout, and again when a
 * match starts with stocks and percents in the island: on Windows it is the
 * taskbar, whose free space changes as apps open and close. */
static void place_island(void) {
    desktop_melee_platform_notch(&s_notch_left, &s_notch_right, &s_island_top, &s_island_h);
    if (s_island_h <= 0.0f) {
        SDL_Log("desktop-melee: no notch island: stocks and percents stay in Melee's HUD");
        return;
    }
    const float side = desktop_melee_island_side(s_island_h);
    desktop_melee_platform_island_place(s_notch_left - side, s_notch_right + side, s_island_h);
    SDL_Log("desktop-melee: notch %.0f..%.0f pt, island %.0f..%.0f x %.0f..%.0f pt", s_notch_left,
        s_notch_right, s_notch_left - side, s_notch_right + side, s_island_top,
        s_island_top + s_island_h);
}

/* Moves the overlay onto the chosen screen (by name; the primary when it is
 * not connected) and lays it out there. */
static void apply_screen(void) {
    if (s_window == NULL) {
        return;
    }
    s_screen_pending = false;
    if (!desktop_melee_platform_window_covers_screen()) {
        layout(); /* a normal window: the player sizes it */
        return;
    }
    SDL_DisplayID target = SDL_GetPrimaryDisplay();
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    for (int i = 0; displays != NULL && i < count; i++) {
        const char* name = SDL_GetDisplayName(displays[i]);
        if (s_screen_name[0] != '\0' && name != NULL && strcmp(name, s_screen_name) == 0) {
            target = displays[i];
            break;
        }
    }
    SDL_free(displays);
    SDL_Rect bounds;
    if (!SDL_GetDisplayBounds(target, &bounds)) {
        return;
    }
    int x, y, w, h;
    SDL_GetWindowPosition(s_window, &x, &y);
    SDL_GetWindowSize(s_window, &w, &h);
    if (x != bounds.x || y != bounds.y || w != bounds.w || h != bounds.h) {
        SDL_SetWindowPosition(s_window, bounds.x, bounds.y);
        SDL_SetWindowSize(s_window, bounds.w, bounds.h);
        SDL_SyncWindow(s_window);
        SDL_Log("desktop-melee: overlay moved to \"%s\" (%dx%d at %d,%d)",
            SDL_GetDisplayName(target), bounds.w, bounds.h, bounds.x, bounds.y);
    }
    layout();
}

void desktop_melee_window_created(SDL_Window* window) {
    if (!desktop_melee_overlay_enabled() || window == NULL) {
        return;
    }
    desktop_melee_platform_overlay_setup(window);
    desktop_melee_platform_ui_install(window);
    s_window = window;
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    for (int i = 0; displays != NULL && i < count; i++) {
        SDL_Log("desktop-melee: screen %d: \"%s\"%s", i + 1, SDL_GetDisplayName(displays[i]),
            displays[i] == SDL_GetPrimaryDisplay() ? " (primary)" : "");
    }
    SDL_free(displays);
    apply_screen();
}

/* Measure the screen and Dock again (Accessibility was just granted): now
 * when idle, else when the running match ends (its floor is set). */
void desktop_melee_relayout(void) {
    if (s_window == NULL) {
        return;
    }
    if (desktop_melee_match_active()) {
        s_screen_pending = true;
    } else {
        layout();
    }
}

/* ---- tweaks (Display tab) ------------------------------------------------ */

static bool s_shake = true;
void desktop_melee_set_shake(bool on) {
    s_shake = on;
}
bool desktop_melee_shake_enabled(void) {
    return s_shake;
}

/* ---- CPU practice (Players tab) ------------------------------------------ */

static int s_cpu_di = DESKTOP_MELEE_DI_OFF;
static int s_cpu_tech = DESKTOP_MELEE_TECH_OFF;
static int s_cpu_mash = DESKTOP_MELEE_MASH_OFF;
static int clamp_mode(int mode, int count) {
    return mode >= 0 && mode < count ? mode : 0;
}
int desktop_melee_cpu_di(void) {
    return s_cpu_di;
}
void desktop_melee_set_cpu_di(int mode) {
    s_cpu_di = clamp_mode(mode, DESKTOP_MELEE_DI_COUNT);
}
int desktop_melee_cpu_tech(void) {
    return s_cpu_tech;
}
void desktop_melee_set_cpu_tech(int mode) {
    s_cpu_tech = clamp_mode(mode, DESKTOP_MELEE_TECH_COUNT);
}
int desktop_melee_cpu_mash(void) {
    return s_cpu_mash;
}
void desktop_melee_set_cpu_mash(int mode) {
    s_cpu_mash = clamp_mode(mode, DESKTOP_MELEE_MASH_COUNT);
}

static int s_timer_mode = DESKTOP_MELEE_TIMER_TOP;
void desktop_melee_set_timer_mode(int mode) {
    s_timer_mode = mode == DESKTOP_MELEE_TIMER_HIDDEN ? mode : DESKTOP_MELEE_TIMER_TOP;
}
int desktop_melee_timer_mode(void) {
    return s_timer_mode;
}
float desktop_melee_timer_scale(void) {
    return desktop_melee_overlay_enabled() ? 0.5f : 1.0f;
}

static bool s_tags = true;
void desktop_melee_set_tags(bool on) {
    s_tags = on;
}
bool desktop_melee_tags_enabled(void) {
    return s_tags;
}

static int s_trace = -1;
static float s_trace_shake;
static int s_hud_drawn;

static bool trace_enabled(void) {
    if (s_trace < 0) {
        const char* env = getenv("DESKTOP_MELEE_TRACE");
        s_trace = env != NULL && env[0] == '1';
    }
    return s_trace != 0;
}

bool desktop_melee_trace_enabled(void) {
    return trace_enabled();
}

void desktop_melee_trace_shake(float x, float y) {
    const float m = fmaxf(fabsf(x), fabsf(y));
    if (m > s_trace_shake) {
        s_trace_shake = m;
    }
}

/* ---- Pause and hide -------------------------------------------------------- */

static bool s_hidden = false;
static int s_pause_port = 0;
static bool s_pause_request = false;
static volatile sig_atomic_t s_usr1 = 0;

bool desktop_melee_hidden(void) {
    return s_hidden;
}

void desktop_melee_set_pause_port(int port) {
    s_pause_port = port >= 0 && port < 4 ? port : 0;
}

int desktop_melee_take_pause_request(void) {
    if (!s_pause_request) {
        return -1;
    }
    s_pause_request = false;
    return s_pause_port;
}

void desktop_melee_toggle_pause_and_hide(void) {
    s_hidden = !s_hidden;
    /* Hiding pauses a running match (Melee's own pause, as the first human
     * player); showing again leaves it paused until that player's Start. */
    s_pause_request = s_hidden && desktop_melee_match_active() && !desktop_melee_game_paused();
    desktop_melee_platform_overlay_set_faded(s_hidden);
    /* Back from another app: the game needs the keyboard again, or Start
     * on the keyboard would type into whatever app is in front. */
    if (!s_hidden && desktop_melee_match_active()) {
        desktop_melee_platform_ui_focus_game();
    }
    SDL_Log("desktop-melee: pause and hide %s (match %d)", s_hidden ? "on" : "off",
        desktop_melee_match_active() ? 1 : 0);
}

#ifdef SIGUSR1
static void on_usr1(int sig) {
    (void)sig;
    s_usr1 = 1;
}
#endif

/* Once a second (desktop_melee_platform_overlay_setup starts the timer), also
 * while the game is paused. */
void desktop_melee_trace_tick(void) {
    if (!trace_enabled()) {
        return;
    }
    if (s_usr1) {
        s_usr1 = 0;
        desktop_melee_toggle_pause_and_hide();
    }
    if (!desktop_melee_match_active()) {
        return;
    }
    float timer_scale = 1.0f;
    const bool timer_hidden = desktop_melee_timer_trace(&timer_scale);
    static const char* hud_names[] = {"island", "bottom", "hidden"};
    SDL_Log("desktop-melee: trace shake %.2f timer %s scale %.2f tags %d hud %s island %d "
            "paused %d hidden %d drawn %d",
        s_trace_shake, timer_hidden ? "hidden" : "top", timer_scale, desktop_melee_tags_trace(),
        hud_names[s_hud], desktop_melee_island_wanted() ? 1 : 0,
        desktop_melee_game_paused() ? 1 : 0, s_hidden ? 1 : 0, s_hud_drawn);
    s_hud_drawn = 0;
    s_trace_shake = 0.0f;
}

void desktop_melee_set_fighter_size(float percent) {
    if (percent >= 25.0f && percent <= 400.0f) {
        s_units_next = DEFAULT_UNITS * 100.0f / percent;
    }
}

bool desktop_melee_fighter_size_locked(void) {
    return env_units() > 0.0f;
}

void desktop_melee_set_hud_mode(int mode) {
    s_hud_next = mode >= DESKTOP_MELEE_HUD_ISLAND && mode <= DESKTOP_MELEE_HUD_HIDDEN ?
                     mode :
                     DESKTOP_MELEE_HUD_ISLAND;
}
int desktop_melee_hud_mode(void) {
    return s_hud;
}

void desktop_melee_trace_hud_drawn(void) {
    s_hud_drawn++;
}

void desktop_melee_set_screen(const char* name) {
    snprintf(s_screen_name, sizeof s_screen_name, "%s", name != NULL ? name : "");
    if (s_window == NULL) {
        return; /* startup: desktop_melee_window_created applies it */
    }
    if (desktop_melee_match_active()) {
        s_screen_pending = true; /* the camera and floor are set for this one */
    } else {
        apply_screen();
    }
}

void desktop_melee_set_hud_camera(float distance, float fov_deg) {
    s_hud_distance = distance;
    s_hud_fov = fov_deg;
}

void desktop_melee_hud_frame(bool visible) {
    s_hud_last_ms = SDL_GetTicks();
    s_hud_visible = visible;
}

bool desktop_melee_island_wanted(void) {
    return desktop_melee_overlay_enabled() && s_hud == DESKTOP_MELEE_HUD_ISLAND && s_hud_visible &&
           !s_hidden && SDL_GetTicks() - s_hud_last_ms < 250;
}

/* Points per HUD unit on the HUD plane. The projection is widened to the
 * window's aspect, so the scale is the same horizontally. 0 when unknown. */
static float hud_ppu(int player) {
    if (!desktop_melee_overlay_enabled() || s_hud != DESKTOP_MELEE_HUD_ISLAND ||
        (player != 0 && player != 1) || s_island_h <= 0.0f || s_hud_distance <= 0.0f ||
        s_hud_fov <= 0.0f || s_screen_h <= 0.0f)
    {
        return 0.0f;
    }
    const float half_h = s_hud_distance * tanf(0.5f * s_hud_fov * (float)M_PI / 180.0f);
    return s_screen_h / (2.0f * half_h);
}

/* Screen point (from the left/top edges) to HUD coordinates. */
static float hud_x(float x_pt, float ppu) {
    return (x_pt - 0.5f * s_screen_w) / ppu;
}
static float hud_y(float y_pt, float ppu) {
    return (0.5f * s_screen_h - y_pt) / ppu;
}

bool desktop_melee_hud_stock_anchor(int player, float* x, float* y, float* scale) {
    const float ppu = hud_ppu(player);
    if (ppu <= 0.0f) {
        return false;
    }
    const float h = s_island_h;
    const float pitch_pt = h * ISLAND_PITCH;
    /* Centre of the first icon: P1's stocks run leftwards from the notch, so
     * its first stock is the outermost; P2's start right next to it. */
    const float first_pt = player == 0 ?
                               s_notch_left - h * ISLAND_PAD - pitch_pt * (ISLAND_STOCKS - 0.5f) :
                               s_notch_right + h * ISLAND_PAD + 0.5f * pitch_pt;
    const float s = pitch_pt / (STOCK_PITCH * ppu);
    *scale = s;
    *x = hud_x(first_pt, ppu) - STOCK_X0 * s;
    *y = hud_y(s_island_top + 0.5f * h, ppu) - STOCK_Y * s;
    return true;
}

bool desktop_melee_hud_percent_anchor(int player, float* x, float* y, float* scale) {
    const float ppu = hud_ppu(player);
    if (ppu <= 0.0f) {
        return false;
    }
    const float h = s_island_h;
    const float inner = h * (ISLAND_PAD + ISLAND_PITCH * ISLAND_STOCKS + ISLAND_GAP);
    const float s = h * ISLAND_DIGIT_H / (PCT_DIGIT_H * ppu);
    /* The percent's reserved box sits just outside the stocks. */
    float origin_pt;
    if (player == 0) {
        const float box_right = s_notch_left - inner;
        origin_pt = box_right - PCT_RIGHT * s * ppu;
    } else {
        const float box_left = s_notch_right + inner;
        origin_pt = box_left - PCT_LEFT * s * ppu;
    }
    *scale = s;
    *x = hud_x(origin_pt, ppu);
    *y = hud_y(s_island_top + 0.5f * h, ppu) - PCT_Y * s;
    return true;
}

bool desktop_melee_hud_in_island(int player) {
    return hud_ppu(player) > 0.0f;
}

void desktop_melee_set_screen_geometry(float width_pt, float height_pt, float floor_pt) {
    s_screen_w = width_pt;
    s_screen_h = height_pt;
    s_floor = floor_pt;
    SDL_Log("desktop-melee: screen %.0fx%.0f pt, floor (Dock top) at %.0f pt", width_pt, height_pt,
        floor_pt);
}

/* Melee units across the whole screen width (fighter size: fewer units,
 * bigger fighters), as the current match started with. */
static float screen_units(void) {
    return s_units;
}

bool desktop_melee_camera(float* eye_x, float* eye_y, float* eye_z, float* interest_x,
    float* interest_y, float* interest_z, float* fov) {
    if (!desktop_melee_overlay_enabled() || s_screen_w <= 0.0f || s_screen_h <= 0.0f) {
        return false;
    }
    /* A narrow perspective looking straight down -z: the z = 0 plane (where the
     * fighters and the stage collision live) maps linearly onto the screen,
     * while depth effects such as Star KO still shrink into the distance. */
    const float fov_deg = 30.0f;
    const float units_per_pt = screen_units() / s_screen_w;
    const float view_h = s_screen_h * units_per_pt;
    const float center_y = (0.5f * s_screen_h - s_floor) * units_per_pt;
    const float distance = 0.5f * view_h / tanf(0.5f * fov_deg * (float)M_PI / 180.0f);
    *interest_x = 0.0f;
    *interest_y = center_y;
    *interest_z = 0.0f;
    *eye_x = 0.0f;
    *eye_y = center_y;
    *eye_z = distance;
    *fov = fov_deg;
    return true;
}

/* Off-screen room beside the screen, as a share of its width, where the floor
 * runs on past the edges (desktop_melee_blastzones). */
#define OFFSCREEN_ROOM 0.2f

/* The floor runs (nearly) the whole width of the screen: a panel, not a Dock
 * with gaps beside it (Linux and Windows). */
static bool floor_across_screen(void) {
    return s_screen_w > 0.0f && s_dock_right - s_dock_left >= 0.9f * s_screen_w;
}

bool desktop_melee_dock_span(float* left, float* right) {
    if (!desktop_melee_overlay_enabled() || s_screen_w <= 0.0f || s_dock_right <= s_dock_left) {
        return false;
    }
    const float units_per_pt = screen_units() / s_screen_w;
    *left = (s_dock_left - 0.5f * s_screen_w) * units_per_pt;
    *right = (s_dock_right - 0.5f * s_screen_w) * units_per_pt;
    SDL_Log("desktop-melee: floor %.0f..%.0f units (Dock)", *left, *right);
    return true;
}

bool desktop_melee_blastzones(float* left, float* right, float* top, float* bottom) {
    if (!desktop_melee_overlay_enabled() || s_screen_w <= 0.0f || s_screen_h <= 0.0f) {
        return false;
    }
    /* The camera maps x = 0 to the screen's middle and y = 0 to the floor. */
    const float units_per_pt = screen_units() / s_screen_w;
    *left = -0.5f * screen_units();
    *right = 0.5f * screen_units();
    *top = (s_screen_h - s_floor) * units_per_pt;
    *bottom = -s_floor * units_per_pt;
    if (floor_across_screen()) {
        /* Nobody falls past a floor as wide as the screen, so the bottom
         * blastzone goes well under it: a spiked fighter bounces on the
         * ground instead of being KO'd just under the panel. */
        *bottom -= 0.5f * screen_units();
    }
    if (s_dock_left < 0.0f && s_dock_right > s_screen_w) {
        /* The floor runs on past the screen's edges (Windows' taskbar): a
         * fighter may go a little way off screen, where Melee shows them in
         * its magnifying-glass bubble and deals its off-screen damage, before
         * the side blastzones. They stay inside the floor's ends, so nobody
         * drops off an end before being KO'd. */
        const float floor_left = (s_dock_left - 0.5f * s_screen_w) * units_per_pt;
        const float floor_right = (s_dock_right - 0.5f * s_screen_w) * units_per_pt;
        const float room = OFFSCREEN_ROOM * screen_units();
        *left = fmaxf(*left - room, floor_left + 10.0f);
        *right = fminf(*right + room, floor_right - 10.0f);
    }
    SDL_Log("desktop-melee: blastzones x %.0f..%.0f, y %.0f..%.0f units (screen edges)", *left,
        *right, *bottom, *top);
    return true;
}

/* ---- window platforms ------------------------------------------------------ */

/* The tops of the player's windows as drop-through platforms (the game side
 * is desktop-melee/game/desktop_melee_platforms.c): each frame the game asks
 * for platform i; the windows are read about 30 times a second. */
#define PLATFORMS 8 /* DESKTOP_MELEE_WINDOW_PLATFORMS */
#define MAX_WINDOWS 64
#define MIN_WINDOW_PT 120.0f /* narrower windows are no platform */
#define MIN_SEGMENT_PT 60.0f /* nor shorter visible pieces of a top */
#define POLL_MS 33

typedef struct {
    bool on;
    unsigned long long id;  /* the window it is the top of */
    float left, right, top; /* points, y down */
    bool ledges;            /* both ends are the window's corners */
} WindowPlatform;

static WindowPlatform s_platforms[PLATFORMS];
static Uint64 s_platforms_polled = 0;

/* The visible pieces of window k's top edge: [left, right) minus what the
 * windows above it (later in the list) cover at that height. */
#define MAX_PIECES 16

/* Adds [left, right) to a piece list that holds at most MAX_PIECES: more
 * pieces than that are dropped, never written past the end. */
static void add_piece(float* lefts, float* rights, int* n, float left, float right) {
    if (*n < MAX_PIECES) {
        lefts[*n] = left;
        rights[*n] = right;
        (*n)++;
    }
}

/* The visible pieces of window k's top edge: [left, right) minus what the
 * windows above it (later in the list) cover at that height. `lefts` and
 * `rights` hold MAX_PIECES. */
static int visible_pieces(
    const DesktopMeleeWindow* w, int count, int k, float* lefts, float* rights) {
    int n = 1;
    lefts[0] = w[k].x;
    rights[0] = w[k].x + w[k].w;
    const float y = w[k].y;
    for (int j = k + 1; j < count && n > 0; j++) {
        if (!(w[j].y <= y && y < w[j].y + w[j].h)) {
            continue;
        }
        const float cl = w[j].x;
        const float cr = w[j].x + w[j].w;
        int m = 0;
        float nl[MAX_PIECES], nr[MAX_PIECES];
        for (int p = 0; p < n; p++) {
            if (cr <= lefts[p] || cl >= rights[p]) {
                add_piece(nl, nr, &m, lefts[p], rights[p]);
                continue;
            }
            if (cl > lefts[p]) {
                add_piece(nl, nr, &m, lefts[p], cl);
            }
            if (cr < rights[p]) {
                add_piece(nl, nr, &m, cr, rights[p]);
            }
        }
        n = m;
        memcpy(lefts, nl, sizeof(float) * (size_t)n);
        memcpy(rights, nr, sizeof(float) * (size_t)n);
    }
    return n;
}

static void poll_window_platforms(void) {
    DesktopMeleeWindow windows[MAX_WINDOWS];
    const int count = desktop_melee_platform_windows(windows, MAX_WINDOWS);
    WindowPlatform found[PLATFORMS * 4];
    int nfound = 0;
    const float floor_top = s_screen_h - s_floor;
    for (int k = 0; k < count; k++) {
        const DesktopMeleeWindow* w = &windows[k];
        /* Maximized or fullscreen windows reach the top of the screen, and
         * windows whose top is behind the panel have nothing to stand on. */
        if (w->w < MIN_WINDOW_PT || w->y <= 1.0f || w->y >= floor_top - 8.0f) {
            continue;
        }
        float lefts[MAX_PIECES], rights[MAX_PIECES];
        const int pieces = visible_pieces(windows, count, k, lefts, rights);
        for (int p = 0; p < pieces && nfound < PLATFORMS * 4; p++) {
            const float l = lefts[p] < 0.0f ? 0.0f : lefts[p];
            const float r = rights[p] > s_screen_w ? s_screen_w : rights[p];
            if (r - l >= MIN_SEGMENT_PT) {
                const bool corners = l == w->x && r == w->x + w->w;
                found[nfound++] = (WindowPlatform){true, w->id, l, r, w->y, corners};
            }
        }
    }
    /* The highest on screen first, when there are more than the platforms. */
    for (int i = 1; i < nfound; i++) {
        for (int j = i; j > 0 && found[j].top < found[j - 1].top; j--) {
            const WindowPlatform t = found[j];
            found[j] = found[j - 1];
            found[j - 1] = t;
        }
    }
    if (nfound > PLATFORMS) {
        nfound = PLATFORMS;
    }
    /* Each piece keeps the slot it had (the same window, the nearest piece of
     * it), so a platform never swaps under a fighter. */
    WindowPlatform next[PLATFORMS] = {0};
    bool used[PLATFORMS * 4] = {false};
    for (int i = 0; i < PLATFORMS; i++) {
        if (!s_platforms[i].on) {
            continue;
        }
        int best = -1;
        float best_d = 0.0f;
        for (int f = 0; f < nfound; f++) {
            if (used[f] || found[f].id != s_platforms[i].id) {
                continue;
            }
            const float d = fabsf(found[f].left - s_platforms[i].left) +
                            fabsf(found[f].right - s_platforms[i].right) +
                            fabsf(found[f].top - s_platforms[i].top);
            if (best < 0 || d < best_d) {
                best = f;
                best_d = d;
            }
        }
        if (best >= 0) {
            next[i] = found[best];
            used[best] = true;
        }
    }
    for (int f = 0; f < nfound; f++) {
        for (int i = 0; i < PLATFORMS && !used[f]; i++) {
            if (!next[i].on) {
                next[i] = found[f];
                used[f] = true;
            }
        }
    }
    memcpy(s_platforms, next, sizeof s_platforms);
}

float desktop_melee_onscreen_widen(void) {
    u32 width = 0;
    u32 height = 0;
    if (!desktop_melee_overlay_enabled()) {
        return 1.0f;
    }
    AuroraGetRenderSize(&width, &height);
    if (width == 0 || height == 0) {
        return 1.0f;
    }
    /* melee-pc's widescreen scale (pc_widescreen_scale), outside a render
     * pass too: the frame's aspect over Melee's 73:60. */
    const float scale = ((float)width / (float)height) / (73.0f / 60.0f);
    return scale > 1.0f ? scale : 1.0f;
}

bool desktop_melee_floor_ledges(void) {
    /* Its ledges would sit by the side blastzones. */
    return !floor_across_screen();
}

bool desktop_melee_window_platform(int index, float* left, float* right, float* y, bool* ledges) {
    if (!desktop_melee_overlay_enabled() || index < 0 || index >= PLATFORMS || s_screen_w <= 0.0f ||
        !desktop_melee_platform_window_covers_screen())
    {
        return false;
    }
    const Uint64 now = SDL_GetTicks();
    if (index == 0 && now - s_platforms_polled >= POLL_MS) {
        s_platforms_polled = now;
        poll_window_platforms();
    }
    const WindowPlatform* p = &s_platforms[index];
    if (!p->on) {
        return false;
    }
    /* The camera maps x = 0 to the screen's middle and y = 0 to the floor. */
    const float units_per_pt = screen_units() / s_screen_w;
    *left = (p->left - 0.5f * s_screen_w) * units_per_pt;
    *right = (p->right - 0.5f * s_screen_w) * units_per_pt;
    *y = (s_screen_h - s_floor - p->top) * units_per_pt;
    *ledges = p->ledges;
    return true;
}

void desktop_melee_launcher_wait_begin(void) {
    if (!desktop_melee_overlay_enabled()) {
        return;
    }
    desktop_melee_platform_overlay_set_faded(true);
    desktop_melee_platform_ui_loading_begin();
}

void desktop_melee_launcher_wait_end(void) {
    if (!desktop_melee_overlay_enabled()) {
        return;
    }
    desktop_melee_platform_ui_loading_end();
    /* Pause and hide may have been pressed meanwhile: it stays hidden. */
    if (!s_hidden) {
        desktop_melee_platform_overlay_set_faded(false);
    }
}

/* Graphics pipelines still being built (the menu bar's loading progress). */
unsigned desktop_melee_pipelines_pending(void) {
    return aurora_wait_pipelines(0);
}

void desktop_melee_before_exit(void) {
    if (desktop_melee_overlay_enabled()) {
        desktop_melee_platform_ui_before_exit();
    }
}

void desktop_melee_after_menu_init(void) {
    if (!desktop_melee_overlay_enabled()) {
        return;
    }
    /* "Auto (window aspect)": the match fills the whole screen, not a 4:3 box. */
    pc_widescreen_set_mode(2);
}

void desktop_melee_request_quit(void) {
    SDL_Log("desktop-melee: quit requested from the menu bar");
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
}

/* ---- match flow (GM_DESKTOP_MELEE) -------------------------------------------------- */

static DesktopMeleeMatchSetup s_setup = {
    .stocks = 4,
    .time_seconds = 7 * 60,
    .players =
        {
            {.ckind = 2, .costume = 0, .cpu = 0, .cpu_level = 9, .port = 0}, /* Fox */
            {.ckind = 2, .costume = 1, .cpu = 0, .cpu_level = 9, .port = 1}, /* alt costume */
        },
};
static bool s_match_requested = false;
static bool s_in_match = false;

void desktop_melee_match_setup(DesktopMeleeMatchSetup* out) {
    *out = s_setup;
}

void desktop_melee_set_match_setup(const DesktopMeleeMatchSetup* setup) {
    s_setup = *setup;
}

void desktop_melee_request_match(void) {
    s_match_requested = true;
}

/* Start on a controller starts a match from idle: off until the menu bar
 * app applies the Match tab's switch (StartButton in MatchTab.swift). */
static bool s_start_starts_match = false;
bool desktop_melee_start_starts_match(void) {
    return s_start_starts_match;
}
void desktop_melee_set_start_starts_match(bool on) {
    s_start_starts_match = on;
}

/* A keyboard binding's key name ("X", "Up", "Tab"), slot as in
 * pc_get_key_binding: A B X Y Start Z L R, D-pad up down left right, stick
 * right left up down, C-stick right left up down, analog L R. */
const char* desktop_melee_key_name(int slot) {
    if (slot < 0 || slot > 21) {
        return "";
    }
    const char* name = SDL_GetScancodeName((SDL_Scancode)pc_get_key_binding(slot));
    return name != NULL ? name : "";
}

bool desktop_melee_match_wanted(void) {
    return s_match_requested && !s_in_match;
}

bool desktop_melee_match_restart_wanted(void) {
    return desktop_melee_overlay_enabled() && s_in_match && s_match_requested;
}

void desktop_melee_match_began(void) {
    s_match_requested = false;
    s_pause_request = false;
    /* Starting a match shows the fighters: nobody means to play unseen. */
    if (s_hidden) {
        s_hidden = false;
        desktop_melee_platform_overlay_set_faded(false);
        SDL_Log("desktop-melee: pause and hide off (a match started)");
    }
    s_in_match = true;
    const float env = env_units();
    s_units = env > 0.0f ? env : s_units_next;
    s_hud = s_hud_next;
    if (s_hud == DESKTOP_MELEE_HUD_ISLAND && s_window != NULL) {
        place_island();
    }
    SDL_Log("desktop-melee: match display: %.0f units across (size %.0f%%), island %s", s_units,
        100.0f * DEFAULT_UNITS / s_units,
        s_hud == DESKTOP_MELEE_HUD_ISLAND ? "island" :
        s_hud == DESKTOP_MELEE_HUD_BOTTOM ? "bottom" :
                                            "hidden");
}

void desktop_melee_match_ended(void) {
    s_in_match = false;
    s_pause_request = false;
    if (s_screen_pending) {
        apply_screen();
    }
}

bool desktop_melee_match_active(void) {
    return s_in_match;
}

/* ---- per-player controls --------------------------------------------------- */

#define DESKTOP_MELEE_PLAYERS 2

/* HSD_PAD_* bits of the buttons a map covers, in DESKTOP_MELEE_BTN_* order. */
static const unsigned s_btn_bits[DESKTOP_MELEE_BTN_COUNT] = {
    1u << 8, 1u << 9, 1u << 10, 1u << 11, /* A B X Y */
    1u << 4, 1u << 6, 1u << 5,            /* Z L R */
};

static struct {
    bool tap_jump;
    unsigned char map[DESKTOP_MELEE_BTN_COUNT]; /* physical button -> DESKTOP_MELEE_BTN_* or NONE */
} s_controls[DESKTOP_MELEE_PLAYERS] = {
    {true, {0, 1, 2, 3, 4, 5, 6}},
    {true, {0, 1, 2, 3, 4, 5, 6}},
};

/* DESKTOP_MELEE_INPUT_TRACE=1: log blocked tap jumps and remapped presses. */
static bool input_trace(void) {
    static int on = -1;
    if (on < 0) {
        const char* env = getenv("DESKTOP_MELEE_INPUT_TRACE");
        on = env != NULL && env[0] != '\0' && env[0] != '0';
    }
    return on;
}

void desktop_melee_set_tap_jump(int player, bool on) {
    if (player >= 0 && player < DESKTOP_MELEE_PLAYERS) {
        s_controls[player].tap_jump = on;
    }
}

void desktop_melee_set_button_map(int player, int button, int target) {
    if (player >= 0 && player < DESKTOP_MELEE_PLAYERS && button >= 0 &&
        button < DESKTOP_MELEE_BTN_COUNT && target >= 0 && target <= DESKTOP_MELEE_BTN_NONE)
    {
        s_controls[player].map[button] = (unsigned char)target;
    }
}

bool desktop_melee_tap_jump(int player) {
    if (player < 0 || player >= DESKTOP_MELEE_PLAYERS || s_controls[player].tap_jump) {
        return true;
    }
    if (input_trace()) {
        SDL_Log("desktop-melee: P%d tap jump blocked", player + 1);
    }
    return false;
}

unsigned desktop_melee_map_buttons(int player, unsigned held, int* analog_mask) {
    *analog_mask = 3;
    if (player < 0 || player >= DESKTOP_MELEE_PLAYERS) {
        return held;
    }
    unsigned out = held;
    for (int b = 0; b < DESKTOP_MELEE_BTN_COUNT; b++) {
        out &= ~s_btn_bits[b];
    }
    for (int b = 0; b < DESKTOP_MELEE_BTN_COUNT; b++) {
        const int target = s_controls[player].map[b];
        if ((held & s_btn_bits[b]) != 0 && target < DESKTOP_MELEE_BTN_COUNT) {
            out |= s_btn_bits[target];
        }
    }
    /* A physical trigger keeps its analog (light) shield only while it is
     * still a shield button. */
    const int l = s_controls[player].map[DESKTOP_MELEE_BTN_L];
    const int r = s_controls[player].map[DESKTOP_MELEE_BTN_R];
    *analog_mask = (l == DESKTOP_MELEE_BTN_L || l == DESKTOP_MELEE_BTN_R ? 1 : 0) |
                   (r == DESKTOP_MELEE_BTN_L || r == DESKTOP_MELEE_BTN_R ? 2 : 0);
    if (out != held && input_trace()) {
        SDL_Log("desktop-melee: P%d buttons %04x -> %04x", player + 1, held & 0xFFFF, out & 0xFFFF);
    }
    return out;
}

/* What is plugged into a controller port, for the Controls tab. */
const char* desktop_melee_port_device(int port) {
    static char name[4][96];
    if (port < 0 || port > 3) {
        return "";
    }
    uint8_t raw[6];
    bool wireless = false;
    const char* pad = PADGetName((u32)port);
    if (pc_gcadapter_raw(port, raw, &wireless)) {
        snprintf(
            name[port], sizeof name[port], "%s", wireless ? "WaveBird" : "GameCube controller");
    } else if (pad != NULL && pad[0] != '\0') {
        snprintf(name[port], sizeof name[port], "%s", pad);
    } else {
        name[port][0] = '\0';
    }
    return name[port];
}

/* ---- disc ------------------------------------------------------------------ */

const char* pc_prefs_get_disc(void);

const char* desktop_melee_choose_disc(const char* command_line_disc) {
    if (!desktop_melee_overlay_enabled()) {
        return command_line_disc;
    }
    return desktop_melee_platform_ui_choose_disc(
        command_line_disc != NULL ? command_line_disc : pc_prefs_get_disc());
}
