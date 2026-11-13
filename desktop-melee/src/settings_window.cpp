/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Desktop Melee's settings window (settings_window.h). Each tab follows
 * docs/desktop-melee/ui-guide.md: the same controls, ranges, defaults, saved
 * keys and C calls as the macOS app, in this window's own look.
 *
 * Changes apply and save at once (sliders save shortly after they stop
 * moving). The window polls the game, which never calls it back: the match
 * state twice a second, the plugged-in devices and screens once a second. */
#include "settings_window.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <SDL3/SDL.h>
#include <aurora/rmlui.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

extern "C" {
#include "../ui/bridge/desktop_melee_bridge.h"
#include "hotkey_portal.h"
#include "melee_font.h"
#ifdef DESKTOP_MELEE_X11_OVERLAY
#include "overlay_x11.h"
#endif
#ifdef _WIN32
#include "overlay_windows.h"
#endif
#include "settings_file.h"
#include "welcome_window.h"
#include "window_icon.h"
}

namespace {

/* desktop_melee_sound_menu kinds (ui-guide.md section 1). */
enum { SOUND_BACK = 0, SOUND_CONFIRM = 1, SOUND_MOVE = 2, SOUND_DENIED = 3 };

constexpr const char* kContextName = "desktop-melee-settings";
constexpr const char* kTabs[] = {"match", "players", "controls", "sound", "display"};
constexpr int kZelda = 0x12;
constexpr int kSheik = 0x13;
constexpr const char* kNames[0x1A] = {"Captain Falcon", "Donkey Kong", "Fox", "Mr. Game & Watch",
    "Kirby", "Bowser", "Link", "Luigi", "Mario", "Marth", "Mewtwo", "Ness", "Peach", "Pikachu",
    "Ice Climbers", "Jigglypuff", "Samus", "Yoshi", "Zelda", "Sheik", "Falco", "Young Link",
    "Dr. Mario", "Roy", "Pichu", "Ganondorf"};
constexpr const char* kButtonBadges[DESKTOP_MELEE_BTN_COUNT + 1] = {
    "A", "B", "X", "Y", "Z", "L", "R", "-"};
constexpr const char* kButtonTargets[DESKTOP_MELEE_BTN_COUNT + 1] = {
    "A", "B", "X (jump)", "Y (jump)", "Z (grab)", "L (shield)", "R (shield)", "Nothing"};
constexpr Uint64 kSaveDelayMs = 400;

struct Fighter {
    int ckind;
    int icon;     /* roster index of its icon (Zelda's for Sheik) */
    int announce; /* roster index for the announcer, -1 for none (Sheik) */
};

Rml::ElementDocument* s_document = nullptr;
std::string s_tab = "match"; /* remembered until the game quits */
int s_editing = 0;           /* whose character the Players grid picks */
int s_control_player = 0;    /* whose controls the Controls tab shows */
std::vector<Fighter> s_roster;
bool s_updating = false; /* setting slider values: ignore their change events */
Uint64 s_last_fast_poll = 0;
Uint64 s_last_slow_poll = 0;
bool s_last_match_active = false;
std::string s_last_devices;
int s_last_display_count = -1;
Uint64 s_save_settings_at = 0; /* 0 = nothing to save */
Uint64 s_save_prefs_at = 0;
bool s_last_hidden = false;
bool s_emblem_shown = false;
bool s_how_to_play = false; /* clicked: opened on the next frame */
int s_menu_button = -1;     /* the button whose target list is open */

DesktopMeleeSettings& settings() {
    return *desktop_melee_settings();
}

std::string resources_path() {
    const char* base = SDL_GetBasePath();
    return std::string(base != nullptr ? base : "./") + "resources/";
}

void play_sound(int kind) {
    if (settings().menu_sounds) {
        desktop_melee_sound_menu(kind);
    }
}

std::string player_name(int player) {
    return player == 0 ? "P1" : "P2";
}

/* ---- elements ----------------------------------------------------------- */

Rml::Element* element(const std::string& id) {
    return s_document != nullptr ? s_document->GetElementById(id) : nullptr;
}

void set_text(const std::string& id, const std::string& text) {
    if (Rml::Element* e = element(id)) {
        e->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
    }
}

void set_class(const std::string& id, const char* name, bool on) {
    if (Rml::Element* e = element(id)) {
        e->SetClass(name, on);
    }
}

void set_attribute(const std::string& id, const char* name, const std::string& value) {
    if (Rml::Element* e = element(id)) {
        if (e->GetAttribute<Rml::String>(name, "") != value) {
            e->SetAttribute(name, value);
        }
    }
}

/* A switch (settings.rcss .toggle). */
void set_toggle(const std::string& id, bool on, bool enabled = true) {
    set_class(id, "on", on);
    set_class(id, "disabled", !enabled);
}

void set_slider(const std::string& id, int value, bool enabled = true) {
    auto* input = dynamic_cast<Rml::ElementFormControlInput*>(element(id));
    if (input == nullptr) {
        return;
    }
    s_updating = true;
    input->SetValue(std::to_string(value));
    s_updating = false;
    input->SetClass("disabled", !enabled);
    if (enabled) {
        input->RemoveAttribute("disabled");
    } else {
        input->SetAttribute("disabled", "");
    }
}

/* The id of the element clicked, or of its nearest parent with one (a click
 * on a button's text lands on the text). */
std::string clicked_id(Rml::Element* e) {
    for (; e != nullptr; e = e->GetParentNode()) {
        if (!e->GetId().empty()) {
            return e->GetId();
        }
    }
    return {};
}

bool starts_with(const std::string& text, const char* prefix) {
    return text.rfind(prefix, 0) == 0;
}

/* "port-3" -> 3 after prefix "port-". */
int id_number(const std::string& id, const char* prefix) {
    return std::atoi(id.c_str() + std::strlen(prefix));
}

/* ---- saving ------------------------------------------------------------- */

void save_settings_soon() {
    s_save_settings_at = SDL_GetTicks() + kSaveDelayMs;
}
void save_prefs_soon() {
    s_save_prefs_at = SDL_GetTicks() + kSaveDelayMs;
}

void flush_saves(bool now) {
    const Uint64 t = SDL_GetTicks();
    if (s_save_settings_at != 0 && (now || t >= s_save_settings_at)) {
        s_save_settings_at = 0;
        desktop_melee_settings_save();
    }
    if (s_save_prefs_at != 0 && (now || t >= s_save_prefs_at)) {
        s_save_prefs_at = 0;
        pc_prefs_save();
    }
}

/* The match setup (rules, players, ports) into the game and the file. */
void commit_match() {
    desktop_melee_set_match_setup(&settings().match);
    desktop_melee_settings_save();
}

/* ---- textures from the disc ----------------------------------------------- */

/* dmicon://<roster index> and dmportrait://<ckind>-<costume>, decoded by the
 * game from the player's disc (ui-guide.md section 2). Requested only once
 * the roster has loaded, so RmlUi never caches a missing image. */
std::optional<aurora::rmlui::RuntimeTexture> texture(const uint8_t* rgba, int w, int h) {
    if (rgba == nullptr || w <= 0 || h <= 0) {
        return std::nullopt;
    }
    return aurora::rmlui::RuntimeTexture{
        .width = static_cast<uint32_t>(w),
        .height = static_cast<uint32_t>(h),
        .rgba8 = {reinterpret_cast<const std::byte*>(rgba), static_cast<size_t>(w) * h * 4},
    };
}

void register_textures() {
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    aurora::rmlui::register_texture_provider(
        "dmicon", [](std::string_view source) -> std::optional<aurora::rmlui::RuntimeTexture> {
            const int index = std::atoi(std::string(source.substr(source.find("://") + 3)).c_str());
            int w = 0;
            int h = 0;
            const uint8_t* rgba = desktop_melee_roster_icon(index, &w, &h);
            return texture(rgba, w, h);
        });
    aurora::rmlui::register_texture_provider(
        "dmportrait", [](std::string_view source) -> std::optional<aurora::rmlui::RuntimeTexture> {
            int ckind = 0;
            int costume = 0;
            const std::string rest(source.substr(source.find("://") + 3));
            if (std::sscanf(rest.c_str(), "%d-%d", &ckind, &costume) != 2) {
                return std::nullopt;
            }
            int w = 0;
            int h = 0;
            const uint8_t* rgba =
                desktop_melee_portrait(ckind == kSheik ? kZelda : ckind, costume, &w, &h);
            return texture(rgba, w, h);
        });
}

bool roster_ready() {
    return !s_roster.empty();
}

/* Melee's select grid order, Sheik closing the last row with Zelda's icon. */
void load_roster() {
    if (roster_ready() || desktop_melee_roster_count() <= 0) {
        return;
    }
    int zelda = -1;
    for (int i = 0; i < desktop_melee_roster_count(); i++) {
        int ckind = 0;
        int costumes = 0;
        if (desktop_melee_roster_entry(i, &ckind, &costumes)) {
            s_roster.push_back({ckind, i, i});
            if (ckind == kZelda) {
                zelda = i;
            }
        }
    }
    if (zelda >= 0) {
        s_roster.push_back({kSheik, zelda, -1});
    }
}

int costume_count(int ckind) {
    int n = desktop_melee_costume_count(ckind);
    if (n <= 0 && ckind == kSheik) {
        n = desktop_melee_costume_count(kZelda);
    }
    return n;
}

const char* fighter_name(int ckind) {
    return ckind >= 0 && ckind < 0x1A ? kNames[ckind] : "?";
}

/* ---- Match tab ---------------------------------------------------------- */

/* The Pause and hide shortcut, as the desktop bound it (hotkey_portal.h). */
std::string s_pause_key;

/* The portal's shortcut, or the X11 key grab's when there is no portal. */
std::string pause_key_label() {
    std::string label;
#ifdef DESKTOP_MELEE_HOTKEY_PORTAL
    label = desktop_melee_hotkey_label();
#endif
#ifdef DESKTOP_MELEE_X11_OVERLAY
    if (label.empty()) {
        label = desktop_melee_x11_hotkey_label();
    }
#endif
#ifdef _WIN32
    label = desktop_melee_win_hotkey_label();
#endif
    return label;
}

void refresh_pause_key() {
    s_pause_key = pause_key_label();
    set_text("pause-key", s_pause_key);
    set_class("pause-key", "hidden", s_pause_key.empty());
}

void refresh_practice();

void refresh_match() {
    const DesktopMeleeMatchSetup& match = settings().match;
    const int minutes = match.time_seconds / 60;
    set_text("stocks-value", std::to_string(match.stocks));
    set_text("minutes-value", minutes == 0 ? "None" : std::to_string(minutes) + ":00");
    set_class("stocks-minus", "disabled", match.stocks <= 1);
    set_class("stocks-plus", "disabled", match.stocks >= 99);
    set_class("minutes-minus", "disabled", minutes <= 0);
    set_class("minutes-plus", "disabled", minutes >= 99);

    set_toggle("start-on-controller", settings().start_on_controller);
    refresh_practice();

    const bool active = desktop_melee_match_active();
    set_text("match-start-label", active ? "Restart Match" : "Start Match");
    set_attribute("match-start-icon", "src",
        active ? "icons/arrow.counterclockwise.png" : "icons/play.fill.png");
    set_class("match-start", "running", active);
    const bool hidden = desktop_melee_hidden();
    s_last_hidden = hidden;
    set_text("pause-hide-label", hidden ? "Show" : "Pause and hide");
    set_attribute("pause-hide-icon", "src", hidden ? "icons/eye.png" : "icons/eye.slash.png");
    refresh_pause_key();
}

/* ---- CPU practice (Match tab) -------------------------------------------- */

const char* const kDiLabels[DESKTOP_MELEE_DI_COUNT] = {"Off", "In", "Out", "Survival", "Mix"};
const char* const kTechLabels[DESKTOP_MELEE_TECH_COUNT] = {
    "Off", "In place", "Roll in", "Roll out", "Miss", "Mix"};
const char* const kMashLabels[DESKTOP_MELEE_MASH_COUNT] = {"Off", "Low", "Medium", "High"};

std::string practice_note() {
    const DesktopMeleeSettings& s = settings();
    if (s.cpu_di == DESKTOP_MELEE_DI_OFF && s.cpu_tech == DESKTOP_MELEE_TECH_OFF &&
        s.cpu_mash == DESKTOP_MELEE_MASH_OFF)
    {
        return "How CPUs react when you hit them. Off is Melee's own CPU.";
    }
    if (s.cpu_di == DESKTOP_MELEE_DI_MIX || s.cpu_tech == DESKTOP_MELEE_TECH_MIX) {
        return "Mix picks again at random for every hit.";
    }
    return "In and out are toward and away from whoever hit the CPU.";
}

void refresh_practice() {
    const DesktopMeleeSettings& s = settings();
    set_text("pr-di-value", kDiLabels[s.cpu_di]);
    set_text("pr-tech-value", kTechLabels[s.cpu_tech]);
    set_text("pr-mash-value", kMashLabels[s.cpu_mash]);
    set_text("practice-note", practice_note());
}

/* The steppers go round: past the last choice is the first. */
int cycle(int value, int delta, int count) {
    return ((value + delta) % count + count) % count;
}

bool click_practice(const std::string& id) {
    DesktopMeleeSettings& s = settings();
    const int delta = id.size() > 5 && id.compare(id.size() - 5, 5, "-plus") == 0 ? 1 : -1;
    if (id == "pr-di-minus" || id == "pr-di-plus") {
        s.cpu_di = cycle(s.cpu_di, delta, DESKTOP_MELEE_DI_COUNT);
        desktop_melee_set_cpu_di(s.cpu_di);
    } else if (id == "pr-tech-minus" || id == "pr-tech-plus") {
        s.cpu_tech = cycle(s.cpu_tech, delta, DESKTOP_MELEE_TECH_COUNT);
        desktop_melee_set_cpu_tech(s.cpu_tech);
    } else if (id == "pr-mash-minus" || id == "pr-mash-plus") {
        s.cpu_mash = cycle(s.cpu_mash, delta, DESKTOP_MELEE_MASH_COUNT);
        desktop_melee_set_cpu_mash(s.cpu_mash);
    } else {
        return false;
    }
    play_sound(SOUND_MOVE);
    desktop_melee_settings_save();
    refresh_practice();
    return true;
}

/* Steps a value within [lo, hi]; the denied sound at the ends. */
bool step_value(int& value, int delta, int lo, int hi) {
    const int next = value + delta;
    if (next < lo || next > hi) {
        play_sound(SOUND_DENIED);
        return false;
    }
    value = next;
    play_sound(SOUND_MOVE);
    return true;
}

bool click_match(const std::string& clicked) {
    DesktopMeleeMatchSetup& match = settings().match;
    /* The buttons' icons and labels have ids of their own. */
    std::string id = clicked;
    for (const char* button : {"match-start", "pause-hide"}) {
        if (starts_with(clicked, button)) {
            id = button;
        }
    }
    int minutes = match.time_seconds / 60;
    if (id == "stocks-minus" || id == "stocks-plus") {
        if (step_value(match.stocks, id == "stocks-plus" ? 1 : -1, 1, 99)) {
            commit_match();
        }
    } else if (id == "minutes-minus" || id == "minutes-plus") {
        if (step_value(minutes, id == "minutes-plus" ? 1 : -1, 0, 99)) {
            match.time_seconds = minutes * 60;
            commit_match();
        }
    } else if (id == "match-start") {
        play_sound(SOUND_CONFIRM);
        desktop_melee_request_match();
    } else if (id == "pause-hide") {
        play_sound(SOUND_CONFIRM);
        desktop_melee_toggle_pause_and_hide();
    } else if (id == "pause-key") {
#ifdef DESKTOP_MELEE_HOTKEY_PORTAL
        play_sound(desktop_melee_hotkey_configure() ? SOUND_CONFIRM : SOUND_DENIED);
#endif
    } else if (id == "start-on-controller") {
        settings().start_on_controller = !settings().start_on_controller;
        desktop_melee_set_start_starts_match(settings().start_on_controller);
        play_sound(SOUND_CONFIRM);
        desktop_melee_settings_save();
    } else {
        return false;
    }
    refresh_match();
    return true;
}

/* ---- Players tab -------------------------------------------------------- */

bool costume_taken(int player, int costume) {
    const DesktopMeleePlayerSetup& other = settings().match.players[1 - player];
    return other.ckind == settings().match.players[player].ckind && other.costume == costume;
}

/* Costumes in range, and two players on one character in different
 * costumes: the edited player moves to the next free one, as Melee's select
 * screen does (ui-guide.md section 5). */
void fix_costumes(int mover) {
    for (int i = 0; i < 2; i++) {
        DesktopMeleePlayerSetup& p = settings().match.players[i];
        const int n = costume_count(p.ckind);
        if (n > 0 && p.costume >= n) {
            p.costume = 0;
        }
    }
    DesktopMeleePlayerSetup& p = settings().match.players[mover];
    if (costume_taken(mover, p.costume)) {
        const int n = std::max(1, costume_count(p.ckind));
        for (int step = 1; step < n; step++) {
            const int c = (p.costume + step) % n;
            if (!costume_taken(mover, c)) {
                p.costume = c;
                break;
            }
        }
    }
}

std::string portrait_src(int ckind, int costume) {
    return "dmportrait://" + std::to_string(ckind) + "-" + std::to_string(costume);
}

/* A card's costumes: the other player's costume of the same character is
 * taken. */
std::string costume_row(int player) {
    const DesktopMeleePlayerSetup& p = settings().match.players[player];
    const std::string prefix = "costume-p" + std::to_string(player + 1) + "-";
    std::string row;
    const int count = costume_count(p.ckind);
    for (int c = 0; c < count; c++) {
        const bool taken = costume_taken(player, c);
        row += "<div id=\"" + prefix + std::to_string(c) + "\" class=\"costume" +
               (c == p.costume ? " selected" : "") + (taken ? " taken" : "") + "\"><img src=\"" +
               portrait_src(p.ckind, c) + "\"/>" +
               (taken ? "<img class=\"x\" src=\"icons/xmark.png\"/>" : "") +
               "<div class=\"ring\"></div></div>";
    }
    return row;
}

void refresh_players() {
    const bool ready = roster_ready();
    for (int i = 0; i < 2; i++) {
        const DesktopMeleePlayerSetup& p = settings().match.players[i];
        const std::string n = i == 0 ? "p1" : "p2";
        set_class("card-" + n, "editing", s_editing == i);
        set_text("fighter-" + n, fighter_name(p.ckind));
        set_class("human-" + n, "selected", p.cpu == 0);
        set_class("cpu-" + n, "selected", p.cpu != 0);
        set_text("level-" + n, std::to_string(p.cpu_level));
        set_class("level-row-" + n, "disabled", p.cpu == 0);
        set_class("level-minus-" + n, "disabled", p.cpu_level <= 1);
        set_class("level-plus-" + n, "disabled", p.cpu_level >= 9);
        set_class("portrait-box-" + n, "loaded", ready);
        if (Rml::Element* img = element("portrait-" + n)) {
            img->SetClass("hidden", !ready);
            if (ready) {
                img->SetAttribute("src", portrait_src(p.ckind, p.costume));
            }
        }
        if (Rml::Element* e = element("costumes-" + n)) {
            e->SetInnerRML(ready ? costume_row(i) : "");
        }
    }
    set_class("roster-loading", "hidden", ready);
    set_class("roster", "edit-p1", s_editing == 0);
    set_class("roster", "edit-p2", s_editing == 1);
    set_text("players-note", desktop_melee_match_active() ?
                                 "Changes apply at the next match. Restart applies them now." :
                                 "Changes apply when the match starts.");
    if (!ready) {
        return;
    }

    /* The grid, nine to a row, with P1/P2 badges on whoever picked each. */
    std::string grid;
    for (size_t i = 0; i < s_roster.size(); i++) {
        const Fighter& f = s_roster[i];
        std::string badges;
        for (int p = 0; p < 2; p++) {
            if (settings().match.players[p].ckind == f.ckind) {
                badges += "<span class=\"b" + std::to_string(p + 1) + "\">P" +
                          std::to_string(p + 1) + "</span>";
            }
        }
        grid += "<div id=\"pick-" + std::to_string(i) + "\" class=\"pick" +
                (i % 9 == 8 ? " end" : "") + "\"><img src=\"dmicon://" + std::to_string(f.icon) +
                "\"/>" + (f.ckind == kSheik ? "<span class=\"sheik\">SHEIK</span>" : "") +
                (badges.empty() ? "" : "<span class=\"badges\">" + badges + "</span>") +
                "<div class=\"ring\"></div></div>";
    }
    if (Rml::Element* e = element("roster")) {
        e->SetInnerRML(grid);
    }
}

/* Which card an id belongs to (its own controls aside), or -1. */
int card_of(const std::string& id) {
    for (const char* prefix : {"card-", "portrait-", "fighter-", "costumes-", "level-row-"}) {
        if (starts_with(id, prefix)) {
            return id.back() == '1' ? 0 : 1;
        }
    }
    return -1;
}

bool click_players(const std::string& id) {
    DesktopMeleeMatchSetup& match = settings().match;
    if (const int card = card_of(id); card >= 0) {
        if (card != s_editing) {
            s_editing = card;
            play_sound(SOUND_MOVE);
        }
    } else if (starts_with(id, "human-") || starts_with(id, "cpu-")) {
        const int player = id.back() == '1' ? 0 : 1;
        s_editing = player;
        match.players[player].cpu = starts_with(id, "cpu-") ? 1 : 0;
        play_sound(SOUND_CONFIRM);
        commit_match();
    } else if (starts_with(id, "level-minus-") || starts_with(id, "level-plus-")) {
        const int player = id.back() == '1' ? 0 : 1;
        s_editing = player;
        if (match.players[player].cpu == 0) {
            play_sound(SOUND_DENIED);
        } else if (step_value(match.players[player].cpu_level,
                       starts_with(id, "level-plus-") ? 1 : -1, 1, 9))
        {
            commit_match();
        }
    } else if (starts_with(id, "pick-")) {
        const size_t index = static_cast<size_t>(id_number(id, "pick-"));
        if (index >= s_roster.size()) {
            return true;
        }
        const Fighter& f = s_roster[index];
        match.players[s_editing].ckind = f.ckind;
        fix_costumes(s_editing);
        commit_match();
        play_sound(SOUND_CONFIRM);
        /* Not during a match: it swaps in its own sound banks. */
        if (f.announce >= 0 && !desktop_melee_match_active()) {
            desktop_melee_sound_announce(f.announce);
        }
    } else if (starts_with(id, "costume-p")) {
        /* costume-p1-3: P1's fourth costume. */
        const int player = id_number(id, "costume-p") == 1 ? 0 : 1;
        const int costume = std::atoi(id.c_str() + std::strlen("costume-p1-"));
        s_editing = player;
        if (costume_taken(player, costume)) {
            play_sound(SOUND_DENIED);
            refresh_players();
            return true;
        }
        match.players[player].costume = costume;
        play_sound(SOUND_CONFIRM);
        commit_match();
    } else {
        return false;
    }
    refresh_players();
    return true;
}

/* ---- Controls tab ------------------------------------------------------- */

std::string port_device(int port) {
    const char* device = desktop_melee_port_device(port);
    return device != nullptr ? device : "";
}

/* A controller plugged in, the keyboard on port 1, or nothing. */
std::string port_icon(int port) {
    if (!port_device(port).empty()) {
        return "icons/gamecontroller.fill.png";
    }
    return port == 0 ? "icons/keyboard.png" : "icons/circle.dashed.png";
}

std::string port_description(int port) {
    const std::string device = port_device(port);
    const std::string label = "Port " + std::to_string(port + 1) + ": ";
    if (port == 0) {
        return label + (device.empty() ? "keyboard" : device + " + keyboard");
    }
    return label + (device.empty() ? "nothing plugged in" : device);
}

void refresh_controls() {
    const int pl = s_control_player;
    const DesktopMeleeMatchSetup& match = settings().match;
    const DesktopMeleePlayerSetup& p = match.players[pl];
    set_class("ctl-p1", "selected", pl == 0);
    set_class("ctl-p2", "selected", pl == 1);
    if (Rml::Element* e = element("ctl-cpu-note")) {
        e->SetClass("hidden", p.cpu == 0);
        e->SetInnerRML(player_name(pl) + " is a CPU right now. These apply when " +
                       player_name(pl) + " plays as Human.");
    }
    if (Rml::Element* ports = element("ports")) {
        ports->SetClass("p1", pl == 0);
        ports->SetClass("p2", pl == 1);
    }
    for (int port = 0; port < 4; port++) {
        const std::string n = std::to_string(port);
        const bool other = match.players[1 - pl].port == port;
        set_class("port-" + n, "selected", p.port == port);
        set_class("port-" + n, "empty", port_device(port).empty() && port != 0);
        set_attribute("port-icon-" + n, "src", port_icon(port));
        set_class("port-other-" + n, "hidden", !other);
        set_text("port-other-" + n, player_name(1 - pl));
    }
    set_text("port-desc", port_description(p.port));
    set_attribute("port-desc-icon", "src", port_icon(p.port));

    const int* map = settings().button_map[pl];
    set_toggle("tapjump", settings().tap_jump[pl]);
    set_toggle("zjump", map[DESKTOP_MELEE_BTN_Z] == DESKTOP_MELEE_BTN_X);
    bool is_default = settings().tap_jump[pl];
    for (int b = 0; b < DESKTOP_MELEE_BTN_COUNT; b++) {
        const std::string id = "map-" + std::to_string(b);
        const std::string badge = "map-badge-" + std::to_string(b);
        const int target = map[b] >= 0 && map[b] <= DESKTOP_MELEE_BTN_NONE ? map[b] : b;
        set_text(badge, kButtonBadges[target]);
        for (int t = 0; t <= DESKTOP_MELEE_BTN_NONE; t++) {
            set_class(badge, ("b" + std::to_string(t)).c_str(), t == target);
        }
        set_class(id, "changed", target != b);
        is_default = is_default && target == b;
    }
    set_class("map-reset", "disabled", is_default);

    bool ucf = false;
    bool locked = false;
    pc_prefs_get_ucf(&ucf, &locked);
    set_toggle("ucf", ucf, !locked);
    set_text("ucf-note", locked ? "Set by MELEE_UCF in the environment." :
                                  "Fixes dashback and shield drops on any controller.");
    set_text(
        "controls-note", desktop_melee_match_active() ?
                             "Buttons and jump apply now. The port applies at the next match." :
                             "Buttons and jump apply right away.");
}

void set_button(int player, int button, int target) {
    settings().button_map[player][button] = target;
    desktop_melee_set_button_map(player, button, target);
}

/* ---- the target list ---------------------------------------------------- */

void close_map_menu() {
    if (s_menu_button >= 0) {
        set_class("map-" + std::to_string(s_menu_button), "open", false);
    }
    s_menu_button = -1;
    set_class("map-menu", "hidden", true);
    set_class("map-menu", "shown", false);
}

/* Opens the target list by button's target: under it, or over it when the
 * window's bottom is too close, and inside the window's sides. */
void open_map_menu(int button) {
    Rml::Element* anchor = element("map-" + std::to_string(button));
    Rml::Element* menu = element("map-menu");
    if (anchor == nullptr || menu == nullptr || s_document == nullptr) {
        return;
    }
    close_map_menu();
    s_menu_button = button;
    anchor->SetClass("open", true);
    const int current = settings().button_map[s_control_player][button];
    std::string items;
    for (int t = 0; t <= DESKTOP_MELEE_BTN_NONE; t++) {
        items += "<div id=\"map-pick-" + std::to_string(t) + "\" class=\"item" +
                 (t == current ? " current" : "") + "\"><span class=\"badge b" + std::to_string(t) +
                 "\">" + kButtonBadges[t] + "</span><span class=\"name\">" +
                 Rml::StringUtilities::EncodeRml(kButtonTargets[t]) +
                 "</span><img class=\"check\" src=\"icons/checkmark.png\"/></div>";
    }
    menu->SetInnerRML(items);
    menu->SetClass("hidden", false);
    s_document->UpdateDocument(); /* lays the menu out, to measure it */

    const Rml::Vector2f at = anchor->GetAbsoluteOffset(Rml::BoxArea::Border);
    const Rml::Vector2f size = anchor->GetBox().GetSize(Rml::BoxArea::Border);
    const Rml::Vector2f menu_size = menu->GetBox().GetSize(Rml::BoxArea::Border);
    const Rml::Vector2f window = s_document->GetBox().GetSize(Rml::BoxArea::Border);
    const float gap = 4.f;
    float top = at.y + size.y + gap;
    if (top + menu_size.y > window.y - gap) {
        top = std::max(gap, at.y - menu_size.y - gap);
    }
    const float left = std::clamp(at.x, gap, std::max(gap, window.x - menu_size.x - gap));
    menu->SetProperty("left", std::to_string(left) + "px");
    menu->SetProperty("top", std::to_string(top) + "px");
    menu->SetClass("shown", true);
}

bool click_controls(const std::string& id) {
    const int pl = s_control_player;
    DesktopMeleeMatchSetup& match = settings().match;
    if (id == "ctl-p1" || id == "ctl-p2") {
        const int next = id == "ctl-p1" ? 0 : 1;
        if (next != s_control_player) {
            s_control_player = next;
            play_sound(SOUND_MOVE);
        }
    } else if (starts_with(id, "port-") && !starts_with(id, "port-desc")) {
        /* port-2, or its icon or badge (port-icon-2, port-other-2). */
        const int port = id.back() - '0';
        /* Two players never share a port: picking the other's swaps them. */
        if (match.players[1 - pl].port == port) {
            match.players[1 - pl].port = match.players[pl].port;
        }
        match.players[pl].port = port;
        play_sound(SOUND_CONFIRM);
        commit_match();
    } else if (id == "tapjump") {
        settings().tap_jump[pl] = !settings().tap_jump[pl];
        desktop_melee_set_tap_jump(pl, settings().tap_jump[pl]);
        play_sound(SOUND_CONFIRM);
        desktop_melee_settings_save();
    } else if (id == "zjump") {
        const bool on = settings().button_map[pl][DESKTOP_MELEE_BTN_Z] == DESKTOP_MELEE_BTN_X;
        set_button(pl, DESKTOP_MELEE_BTN_Z, on ? DESKTOP_MELEE_BTN_Z : DESKTOP_MELEE_BTN_X);
        play_sound(SOUND_CONFIRM);
        desktop_melee_settings_save();
    } else if (starts_with(id, "map-pick-")) {
        const int target = id_number(id, "map-pick-");
        const int button = s_menu_button;
        close_map_menu();
        if (button >= 0 && target >= 0 && target <= DESKTOP_MELEE_BTN_NONE) {
            set_button(pl, button, target);
            play_sound(SOUND_CONFIRM);
            desktop_melee_settings_save();
        }
    } else if (starts_with(id, "map-") && id != "map-reset" && id != "map-grid" &&
               id != "map-target" && id != "map-menu")
    {
        /* A target: its list opens, or closes on a second click. */
        const int button = id_number(id, starts_with(id, "map-badge-") ? "map-badge-" : "map-");
        if (button == s_menu_button) {
            close_map_menu();
        } else {
            open_map_menu(button);
        }
        play_sound(SOUND_MOVE);
        return true;
    } else if (id == "map-reset") {
        if (element("map-reset") != nullptr && element("map-reset")->IsClassSet("disabled")) {
            play_sound(SOUND_DENIED);
            return true;
        }
        settings().tap_jump[pl] = true;
        desktop_melee_set_tap_jump(pl, true);
        for (int b = 0; b < DESKTOP_MELEE_BTN_COUNT; b++) {
            set_button(pl, b, b);
        }
        play_sound(SOUND_BACK);
        desktop_melee_settings_save();
    } else if (id == "ucf") {
        bool ucf = false;
        bool locked = false;
        pc_prefs_get_ucf(&ucf, &locked);
        if (locked) {
            play_sound(SOUND_DENIED);
            return true;
        }
        pc_prefs_set_ucf(!ucf);
        pc_prefs_save();
        play_sound(SOUND_CONFIRM);
    } else {
        return false;
    }
    refresh_controls();
    return true;
}

/* ---- Sound tab ---------------------------------------------------------- */

struct Audio {
    float volume = 1.f;
    float music = 1.f;
    float sfx = 1.f;
    bool mute = false;
    bool reverb = true;
};

Audio read_audio() {
    Audio a;
    pc_prefs_get_audio(&a.volume, &a.music, &a.sfx, &a.mute, &a.reverb);
    return a;
}

void write_audio(const Audio& a) {
    pc_prefs_set_audio(a.volume, a.music, a.sfx, a.mute, a.reverb);
}

int percent(float v) {
    return static_cast<int>(v * 100.f + 0.5f);
}

/* From launcher.cfg each time: melee-pc's F1 menu can change it too. */
void refresh_sound() {
    const Audio a = read_audio();
    set_slider("vol-master", percent(a.volume), !a.mute);
    set_slider("vol-music", percent(a.music));
    set_slider("vol-sfx", percent(a.sfx));
    set_text("vol-master-value", std::to_string(percent(a.volume)) + "%");
    set_text("vol-music-value", std::to_string(percent(a.music)) + "%");
    set_text("vol-sfx-value", std::to_string(percent(a.sfx)) + "%");
    set_class("row-master", "dim", a.mute);
    set_toggle("mute", a.mute);
    set_toggle("reverb", a.reverb);
    set_toggle("menu-sounds", settings().menu_sounds);
}

bool change_sound(const std::string& id, float value) {
    Audio a = read_audio();
    const float v = std::clamp(value / 100.f, 0.f, 1.f);
    if (id == "vol-master") {
        a.volume = v;
    } else if (id == "vol-music") {
        a.music = v;
    } else if (id == "vol-sfx") {
        a.sfx = v;
    } else {
        return false;
    }
    write_audio(a); /* heard at once, saved when the slider rests */
    save_prefs_soon();
    set_text(id + "-value", std::to_string(percent(v)) + "%");
    return true;
}

bool click_sound(const std::string& id) {
    Audio a = read_audio();
    if (id == "mute") {
        a.mute = !a.mute;
    } else if (id == "reverb") {
        a.reverb = !a.reverb;
    } else if (id == "menu-sounds") {
        settings().menu_sounds = !settings().menu_sounds;
        desktop_melee_settings_save();
        play_sound(SOUND_CONFIRM);
        refresh_sound();
        return true;
    } else {
        return false;
    }
    write_audio(a);
    pc_prefs_save();
    play_sound(SOUND_CONFIRM);
    refresh_sound();
    return true;
}

/* ---- Display tab -------------------------------------------------------- */

void refresh_screens() {
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    std::string rows;
    for (int i = 0; i < count; i++) {
        const char* raw = SDL_GetDisplayName(displays[i]);
        const std::string name = raw != nullptr ? raw : "Display";
        const bool is_primary = displays[i] == primary;
        /* "" follows the primary; a saved screen that is unplugged falls
         * back to the primary too. */
        const std::string saved = settings().screen;
        bool saved_present = false;
        for (int j = 0; j < count; j++) {
            const char* other = SDL_GetDisplayName(displays[j]);
            saved_present = saved_present || (other != nullptr && saved == other);
        }
        const bool chosen = saved.empty() || !saved_present ? is_primary : name == saved;
        rows += "<div id=\"screen-" + std::to_string(i) + "\" class=\"screen" +
                (chosen ? " selected" : "") +
                "\"><img class=\"icon\" src=\"icons/display.png\"/><span class=\"name\">" +
                Rml::StringUtilities::EncodeRml(name) + "</span>" +
                (is_primary ? "<span class=\"main\">main</span>" : "") +
                "<img class=\"check\" src=\"icons/checkmark.png\"/></div>";
    }
    SDL_free(displays);
    s_last_display_count = count;
    if (Rml::Element* e = element("screens")) {
        e->SetInnerRML(rows);
    }
    set_text("screen-note", count <= 1 ? "Connect another display to play on it." : "");
}

void refresh_display() {
    const bool locked = desktop_melee_fighter_size_locked();
    const int size = static_cast<int>(settings().fighter_size + 0.5f);
    set_slider("size", size, !locked);
    set_text("size-value", std::to_string(size) + "%");
    set_text("size-note",
        locked ? "Set by DESKTOP_MELEE_SCALE in the environment." :
                 "The screen edges are the blast zones: bigger fighters are KO'd sooner.");
    set_toggle("shake", settings().shake);
    set_toggle("tags", settings().tags);
    set_class("timer-top", "selected", settings().timer_mode == DESKTOP_MELEE_TIMER_TOP);
    set_class("timer-hidden", "selected", settings().timer_mode == DESKTOP_MELEE_TIMER_HIDDEN);
    /* On Windows the island is the taskbar. Linux has none: Island shows as
     * Melee's own HUD, which is what the game falls back to. */
#ifdef _WIN32
    const bool taskbar = true;
#else
    const bool taskbar = false;
#endif
    const int hud = settings().hud_mode;
    const bool island = taskbar && hud == DESKTOP_MELEE_HUD_ISLAND;
    const bool hidden = hud == DESKTOP_MELEE_HUD_HIDDEN;
    set_class("hud-island", "hidden", !taskbar);
    set_class("hud-island", "selected", island);
    set_class("hud-bottom", "selected", !island && !hidden);
    set_class("hud-hidden", "selected", hidden);
    set_text("hud-note",
        island ? "On the taskbar, beside its buttons. Melee's own at the bottom when there's no "
                 "room, or the taskbar hides." :
        hidden ? "No stocks or percents on screen." :
                 "Melee's own, at the bottom of the screen.");
    set_text("display-note", desktop_melee_match_active() ?
                                 "Fighter size, stocks and percents apply at the next match, "
                                 "and so does a new screen." :
                                 "Fighter size, stocks and percents apply when the match starts.");
    refresh_screens();
}

bool change_display(const std::string& id, float value) {
    if (id != "size" || desktop_melee_fighter_size_locked()) {
        return false;
    }
    const float size = std::clamp(value, 50.f, 200.f);
    settings().fighter_size = size;
    desktop_melee_set_fighter_size(size);
    save_settings_soon();
    set_text("size-value", std::to_string(static_cast<int>(size + 0.5f)) + "%");
    return true;
}

bool click_display(const std::string& id) {
    DesktopMeleeSettings& s = settings();
    if (id == "shake") {
        s.shake = !s.shake;
        desktop_melee_set_shake(s.shake);
    } else if (id == "tags") {
        s.tags = !s.tags;
        desktop_melee_set_tags(s.tags);
    } else if (id == "timer-top" || id == "timer-hidden") {
        s.timer_mode = id == "timer-top" ? DESKTOP_MELEE_TIMER_TOP : DESKTOP_MELEE_TIMER_HIDDEN;
        desktop_melee_set_timer_mode(s.timer_mode);
    } else if (id == "hud-island" || id == "hud-bottom" || id == "hud-hidden") {
        s.hud_mode = id == "hud-island" ? DESKTOP_MELEE_HUD_ISLAND :
                     id == "hud-bottom" ? DESKTOP_MELEE_HUD_BOTTOM :
                                          DESKTOP_MELEE_HUD_HIDDEN;
        desktop_melee_set_hud_mode(s.hud_mode);
    } else if (starts_with(id, "screen-")) {
        int count = 0;
        SDL_DisplayID* displays = SDL_GetDisplays(&count);
        const int index = id_number(id, "screen-");
        if (index >= 0 && index < count) {
            const char* name = SDL_GetDisplayName(displays[index]);
            /* The primary is saved as "", so the overlay keeps following it. */
            const bool primary = displays[index] == SDL_GetPrimaryDisplay();
            snprintf(s.screen, sizeof s.screen, "%s", primary || name == nullptr ? "" : name);
            desktop_melee_set_screen(s.screen);
        }
        SDL_free(displays);
    } else {
        return false;
    }
    play_sound(SOUND_CONFIRM);
    desktop_melee_settings_save();
    refresh_display();
    return true;
}

/* ---- the window --------------------------------------------------------- */

/* The stand-in emblem until the game has decoded Melee's own, as on macOS. */
void refresh_emblem() {
    int w = 0;
    int h = 0;
    if (s_emblem_shown || desktop_melee_named_alpha("emblem", &w, &h) == nullptr) {
        return;
    }
    s_emblem_shown = true;
    desktop_melee_emblem_texture_register();
    set_attribute("emblem-img", "src", "dmemblem://emblem");
    set_class("emblem-img", "hidden", false);
    if (s_document != nullptr) {
        s_document->SetClass("ready", true);
    }
}

void refresh_all() {
    refresh_emblem();
    refresh_match();
    refresh_players();
    refresh_controls();
    refresh_sound();
    refresh_display();
}

void show_tab(const std::string& tab) {
    s_tab = tab;
    for (const char* name : kTabs) {
        const bool shown = tab == name;
        set_class("tab-" + std::string(name), "selected", shown);
        set_class("page-" + std::string(name), "shown", shown);
    }
    if (tab == "sound") {
        refresh_sound();
    }
}

class Listener : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event& event) override {
        const std::string id = clicked_id(event.GetTargetElement());
        if (event.GetId() == Rml::EventId::Change) {
            if (!s_updating) {
                const float value = event.GetParameter<float>("value", 0.f);
                change_sound(id, value) || change_display(id, value);
            }
            return;
        }
        if (event.GetId() == Rml::EventId::Scroll) {
            close_map_menu(); /* it would stay put while the page moves */
            return;
        }
        if (event.GetId() == Rml::EventId::Keydown) {
            if (event.GetParameter<int>("key_identifier", 0) == Rml::Input::KI_ESCAPE) {
                close_map_menu();
            }
            return;
        }
        /* A click anywhere else closes the target list. */
        const bool on_target =
            starts_with(id, "map-") && id != "map-reset" && id != "map-grid" && id != "map-target";
        if (s_menu_button >= 0 && !on_target) {
            close_map_menu();
        }
        if (starts_with(id, "tab-")) {
            if (id.substr(4) != s_tab) {
                play_sound(SOUND_MOVE);
                show_tab(id.substr(4));
            }
        } else if (id == "how-to-play") {
            play_sound(SOUND_CONFIRM);
            s_how_to_play = true; /* it closes this window: not inside its event */
        } else if (id == "quit") {
            play_sound(SOUND_BACK);
            flush_saves(true);
            desktop_melee_request_quit();
        } else {
            click_practice(id) || click_match(id) || click_players(id) || click_controls(id) ||
                click_sound(id) || click_display(id);
        }
    }
};
Listener s_listener;

/* Once a frame (aurora's window frame callback). */
void on_frame(void*) {
    if (s_document == nullptr) {
        return;
    }
    flush_saves(false);
    if (s_how_to_play) {
        s_how_to_play = false;
        flush_saves(true);
        desktop_melee_welcome_how_to_play(); /* closes this window */
        return;
    }
    const Uint64 now = SDL_GetTicks();
    if (now - s_last_fast_poll >= 500) {
        s_last_fast_poll = now;
        const bool active = desktop_melee_match_active();
        if (active != s_last_match_active) {
            s_last_match_active = active;
            refresh_match();
            refresh_players();
            refresh_controls();
            refresh_display();
        }
        if (desktop_melee_hidden() != s_last_hidden) {
            refresh_match();
        }
        if (s_pause_key != pause_key_label()) {
            refresh_pause_key(); /* the desktop answered, or the player changed it */
        }
    }
    if (now - s_last_slow_poll >= 1000) {
        s_last_slow_poll = now;
        refresh_emblem();
        if (!roster_ready()) {
            load_roster();
            if (roster_ready()) {
                for (int i = 0; i < 2; i++) {
                    fix_costumes(i); /* costume counts are known now */
                }
                refresh_players();
            }
        }
        std::string devices;
        for (int port = 0; port < 4; port++) {
            devices += port_device(port) + "|";
        }
        if (devices != s_last_devices) {
            s_last_devices = devices;
            refresh_controls();
        }
        int count = 0;
        SDL_free(SDL_GetDisplays(&count));
        if (count != s_last_display_count) {
            refresh_screens();
            desktop_melee_set_screen(settings().screen); /* a display came or went */
        }
    }
}

void on_closed(void*) {
    flush_saves(true);
    s_document = nullptr; /* destroyed with its context */
    s_emblem_shown = false;
}

}  // namespace

bool desktop_melee_settings_window_open(void) {
    if (aurora::rmlui::get_window_context() != nullptr && s_document != nullptr) {
        aurora::rmlui::open_window(kContextName, "Desktop Melee", 360, 700); /* raises it */
        return true;
    }
    /* One second window at a time: the welcome window keeps it while it
     * picks the disc or shows the loading progress, and gives it up after. */
    if (desktop_melee_welcome_busy()) {
        /* Raises the welcome window. */
        aurora::rmlui::open_window(kContextName, "Desktop Melee", 360, 700);
        return false;
    }
    desktop_melee_welcome_close();
    Rml::Context* context = aurora::rmlui::open_window(kContextName, "Desktop Melee", 360, 700);
    if (context == nullptr) {
        SDL_Log("desktop-melee: settings window: could not open");
        return false;
    }
    desktop_melee_set_window_icons();
    /* The bundled free fonts (melee-pc's menu fonts): the disc's font isn't
     * needed, so the window works before the game's assets load. RmlUi
     * skips faces it already has. */
    const std::string resources = resources_path();
    Rml::LoadFontFace(resources + "menu.ttf", true);
    Rml::LoadFontFace(resources + "menu-bold.ttf");
    desktop_melee_font_install();
    register_textures();
    s_document = context->LoadDocument(resources + "desktop-melee/settings.rml");
    if (s_document == nullptr) {
        SDL_Log(
            "desktop-melee: settings window: no %sdesktop-melee/settings.rml", resources.c_str());
        aurora::rmlui::close_window();
        return false;
    }
    s_document->AddEventListener(Rml::EventId::Click, &s_listener);
    s_document->AddEventListener(Rml::EventId::Change, &s_listener);
    s_document->AddEventListener(Rml::EventId::Keydown, &s_listener);
    if (Rml::Element* body = element("body")) {
        body->AddEventListener(Rml::EventId::Scroll, &s_listener);
    }
    s_menu_button = -1;
    aurora::rmlui::set_window_closed_callback(on_closed, nullptr);
    aurora::rmlui::set_window_frame_callback(on_frame, nullptr);
    aurora::rmlui::set_window_drop_callback(nullptr, nullptr);
    load_roster();
    s_last_match_active = desktop_melee_match_active();
    s_last_devices.clear();
    s_emblem_shown = false;
    show_tab(s_tab);
    refresh_all();
    s_document->Show();
    return true;
}

void desktop_melee_settings_window_close(void) {
    flush_saves(true);
    s_document = nullptr;
    s_emblem_shown = false;
    aurora::rmlui::close_window();
}
