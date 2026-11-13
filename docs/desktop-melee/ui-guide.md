# Desktop Melee's UI, screen by screen

This page describes the macOS app as it ships, so a Windows or Linux port can
offer the same thing. For each screen it lists every control, its range and
default, the setting it saves and the C call it makes. The calls are in
[`desktop-melee/ui/bridge/desktop_melee_bridge.h`](../../desktop-melee/ui/bridge/desktop_melee_bridge.h)
and [`desktop_melee_game.h`](../../desktop-melee/src/desktop_melee_game.h),
which the bridge includes. The macOS sources are in
[`desktop-melee/ui/`](../../desktop-melee/ui/), one Swift file per screen.

[porting.md](porting.md) covers the other half: the window, the floor and the
hotkey.

Contents:

1. [Behaviour and styling](#1-behaviour-and-styling)
2. [Game assets in the UI](#2-game-assets-in-the-ui)
3. [The menu bar popover](#3-the-menu-bar-popover)
4. [Match tab](#4-match-tab)
5. [Players tab](#5-players-tab)
6. [Controls tab](#6-controls-tab)
7. [Sound tab](#7-sound-tab)
8. [Display tab](#8-display-tab)
9. [The welcome window](#9-the-welcome-window)
10. [How to play](#10-how-to-play)
11. [The loading window](#11-the-loading-window)
12. [Pause and hide](#12-pause-and-hide)
13. [Every setting](#13-every-setting)
14. [Proposal: the UI on Windows and Linux](#14-proposal-the-ui-on-windows-and-linux)

---

## 1. Behaviour and styling

Each section below has two parts:

- **Behaviour** is what any port must do: the controls, their ranges and
  defaults, what they save, what they call and when the change takes effect.
  A player moving between a Mac and a PC should find the same settings with
  the same meaning.
- **Mac styling** is how the macOS app happens to look: a popover under a
  menu bar icon, SF Symbols, Control Center-style rounded groups, switches.
  A port should look at home on its own system instead: a tray menu and a
  settings window on Windows, whatever fits the desktop on Linux.

A few rules hold across every screen:

- **Changes apply at once and are saved at once.** There is no Save or Apply
  button anywhere. Each control saves its setting and calls into the game as
  soon as it changes.
- **Match settings apply at the next match.** Players, rules, fighter size,
  the HUD placement and the controller port are latched when a match starts.
  Each tab says so in a line of small text at the bottom: "Changes apply when
  the match starts." while idle, or "Changes apply at the next match.
  Restart applies them now." during a match. Buttons, jump settings, effects
  and sound apply mid-match.
- **The UI polls the game.** The game does not call the UI back. Tabs poll
  `desktop_melee_match_active()` and `desktop_melee_hidden()` twice a second;
  the Controls tab polls the plugged-in devices once a second.
- **Menu sounds.** Most clicks play one of Melee's own menu sounds,
  `desktop_melee_sound_menu(kind)`: 0 back, 1 confirm, 2 move, 3 denied. Only
  once the game's assets are loaded, and only while the Sound tab's "Menu
  sounds" switch is on.
- **Everything runs on the main thread.** The UI and the game loop share one
  thread, so the calls need no locking. A port has to keep that true, or
  queue its calls onto the game's thread.

## 2. Game assets in the UI

The macOS app draws a lot of Melee: character icons and portraits, the Smash
emblem, Melee's font and its menu sounds and announcer. **All of it comes from
the player's disc at runtime.** The game decodes it
([`desktop-melee/src/assets.c`](../../desktop-melee/src/assets.c)) and the UI
reads it through the bridge:

| Asset | Call | Before it loads |
|---|---|---|
| Select grid icons | `desktop_melee_roster_count`, `desktop_melee_roster_entry`, `desktop_melee_roster_icon` | "Loading characters..." |
| Portraits | `desktop_melee_portrait(ckind, costume, &w, &h)` | a person glyph |
| Melee's font | `desktop_melee_font_glyph_count`, `_glyph_info`, `_glyph_alpha` | the system font |
| The Smash emblem | `desktop_melee_named_alpha("emblem", ...)` | a stand-in drawn with shapes (`Emblem.swift`) |
| The "Melee" logo | `desktop_melee_named_alpha("melee", ...)` | none |
| Menu sounds, announcer | `desktop_melee_sound_menu`, `desktop_melee_sound_announce` | silence |

`desktop_melee_platform_ui_assets_ready()` says when they are ready.

These are Nintendo's. A port must load them the same way, at runtime, and
**must never put them in the repository**: no screenshots of the UI with game
art in them, no extracted icons, no copies of the font. That is why this guide
has no images. (A screenshot or clip attached to a pull request or issue, to
show a change, is fine: it just doesn't get committed.) Anything the UI needs before the disc is in (the menu icon, the
welcome window's first two steps) has to be drawn from scratch, as
`Emblem.swift` and the welcome window's desktop sketch are.

## 3. The menu bar popover

Source: [`MenuBar.swift`](../../desktop-melee/ui/MenuBar.swift),
[`Panel.swift`](../../desktop-melee/ui/Panel.swift).

**Behaviour**

- One icon in the system's status area: the Smash emblem (the stand-in
  until the disc's emblem loads). The app has no Dock icon and no app menu.
- Clicking the icon opens the panel; clicking it again or clicking elsewhere
  closes it. Opening it reloads the sound settings from `launcher.cfg`, since
  melee-pc's F1 menu can change them too.
- When the panel closes, the game window gets keyboard focus again
  (`desktop_melee_platform_ui_focus_game`): keyboard players would otherwise
  type into whatever is in front.
- The panel shows above the game overlay.
- **Header:** the emblem, "Desktop Melee", and "Running on your desktop"
  below it.
- **Tab bar**, in this order: Match, Players, Controls, Sound, Display. The
  panel opens on Match at launch and remembers the last tab after that, until
  the app quits. Switching tabs plays the move sound.
- **Body:** the tab. A tab taller than the screen scrolls; the panel never
  grows past the screen.
- **Footer:** "How to play" on the left (section 10) and "Quit" on the right
  (`desktop_melee_request_quit()`, which shuts the game down cleanly;
  Command-Q while the panel is open).

**Mac styling:** an `NSStatusItem` and an `NSPopover` 360 points wide; each
tab is an SF Symbol over its name, with the selected one tinted in the
accent colour; groups are rounded rectangles with a small uppercase title
above them.

## 4. Match tab

Source: [`MatchTab.swift`](../../desktop-melee/ui/MatchTab.swift).

| Control | Values | Default | Saved as | Calls | Applies |
|---|---|---|---|---|---|
| Start Match / Restart Match | button; "Restart" while a match runs | | | `desktop_melee_request_match()` | now |
| Pause and hide / Show | button; "Show" while hidden | | | `desktop_melee_toggle_pause_and_hide()` | now |
| Pause and hide shortcut | a key with two or more modifiers | Control-Option-M | `match.pauseKey` | (the hotkey, section 12) | now |
| Stocks | 1 to 99, steps of 1 | 4 | `match.stocks` | `desktop_melee_set_match_setup` (`stocks`) | next match |
| Time | 0 to 99 minutes; 0 shows "None" | 7 | `match.minutes` | `desktop_melee_set_match_setup` (`time_seconds` = minutes × 60) | next match |
| Items | always "Off" (not a control) | | | | |
| Start starts a match | on / off | off | `match.startOnController` | `desktop_melee_set_start_starts_match(on)` (off while the welcome window listens for presses) | now |
| CPU DI | Off, In, Out, Survival, Mix (steppers go round) | Off | `practice.di` | `desktop_melee_set_cpu_di(DESKTOP_MELEE_DI_*)` | now |
| CPU tech | Off, In place, Roll in, Roll out, Miss, Mix | Off | `practice.tech` | `desktop_melee_set_cpu_tech(DESKTOP_MELEE_TECH_*)` | now |
| CPU grab mash | Off, Low, Medium, High | Off | `practice.mash` | `desktop_melee_set_cpu_mash(DESKTOP_MELEE_MASH_*)` | now |

Below the rules, a "Time-out" note: when time runs out, the match ends and
Desktop Melee goes back to idle. At the bottom: "Rule changes apply to the
next match."

**Behaviour notes**

- `DesktopMeleeMatchSetup` holds the rules and both players. Each tab owns
  only its own fields: it reads the whole setup with
  `desktop_melee_match_setup(&setup)`, changes its fields, and writes the
  whole setup back. The Match tab owns `stocks` and `time_seconds`.
- The shortcut recorder: click it and it shows "Press keys...". Escape keeps
  the old shortcut. A key without at least two modifiers, one of them Control,
  Option or Command, is refused with a beep, so the shortcut can never take an
  everyday combination like Command-C from every app. If another app already
  holds the combination, the recorder shows "Taken" for 1.5 seconds and the
  old shortcut stays. It stops recording when the panel closes.
- The +/- buttons are disabled at the ends of their range.

**Mac styling:** Start is a full-width filled button, accent colour while
idle and orange while a match runs; Pause and hide sits in a row beside the
shortcut, which is drawn as an outlined key cap with macOS modifier symbols
(⌃⌥M).

## 5. Players tab

Source: [`PlayersTab.swift`](../../desktop-melee/ui/PlayersTab.swift).

Two player cards side by side, P1 (red, `#E83B38`) and P2 (blue, `#3D75F5`),
with Melee's character select grid under them.

| Control | Values | Default | Saved as | Applies |
|---|---|---|---|---|
| Character | the 26 playable kinds, `ckind` 0x00 to 0x19 | Fox (`ckind` 2) for both | `players.pN.ckind` | next match |
| Costume | 0 to the character's count minus 1 | P1 0, P2 1 | `players.pN.costume` | next match |
| Human / CPU | two-part switch | Human | `players.pN.cpu` (bool) | next match |
| CPU level | 1 to 9; only enabled for a CPU | 9 | `players.pN.level` | next match |

All four are written with `desktop_melee_set_match_setup` (the `ckind`,
`costume`, `cpu` and `cpu_level` of `players[0]` and `players[1]`). `N` is 1
or 2.

**Behaviour notes**

- **Which player the grid edits.** Clicking a card selects it (a pointing
  hand marks it, and its border takes the player's colour). Clicking a tile in
  the grid sets that player's character. The selection is not saved: it
  starts on P1.
- **The grid** is Melee's select screen order, in rows of 9, 9 and 7, from
  `desktop_melee_roster_entry(i, &ckind, &costumes)`. Melee has no Sheik
  tile, so the app adds one at the end of the last row: Zelda's icon with its
  name strip covered and relabelled "SHEIK", and Zelda's portraits. Each tile
  shows "P1" / "P2" badges for whoever picked it. Hovering a tile plays the
  move sound.
- **The announcer** calls out the picked character's name
  (`desktop_melee_sound_announce(roster_index)`), but not during a match: the
  match swaps in its own sound banks. Sheik has no callout.
- **Costumes**: one small portrait per costume, as many as
  `desktop_melee_costume_count(ckind)` reports. Two players on the same
  character can't wear the same costume: the other player's is marked with an
  ×, and clicking it plays the denied sound. When a pick causes a clash, the
  player being edited moves to the next free costume, as Melee's select
  screen does. Costumes out of range fall back to 0 once the roster has
  loaded.
- The card shows the character's name in Melee's font, shrunk to fit.

**Mac styling:** portraits sit on a gradient in the player's colour, flush
right (the art is cut off on its right edge); the Human/CPU switch is a
capsule in the player's colour.

## 6. Controls tab

Source: [`ControlsTab.swift`](../../desktop-melee/ui/ControlsTab.swift).

A P1 / P2 switch at the top picks whose controls the tab shows. It is not
saved. When that player is a CPU, a note says: "P1 is a CPU right now. These
apply when P1 plays as Human."

| Control | Values | Default | Saved as | Calls | Applies |
|---|---|---|---|---|---|
| Controller port | ports 1 to 4 | P1 port 1, P2 port 2 | `controls.pN.port` (0 to 3) | `desktop_melee_set_match_setup` (`players[i].port`) | next match |
| Tap jump | on / off | on | `controls.pN.tapjump` | `desktop_melee_set_tap_jump(player, on)` | now |
| Z = jump | on / off | off | part of `controls.pN.map` (Z maps to X) | `desktop_melee_set_button_map` | now |
| Button map | each of A B X Y Z L R acts as one of A B X Y Z L R, or Nothing | each as itself | `controls.pN.map`, 7 integers | `desktop_melee_set_button_map(player, button, target)` | now |
| Reset | button; disabled when the map is the default | | | resets tap jump and the map | now |
| UCF (all players) | on / off | from `launcher.cfg` | `launcher.cfg` | `pc_prefs_set_ucf(on)`, `pc_prefs_save()` | now |

**Behaviour notes**

- **Ports**: four buttons, each with an icon for what is plugged in
  (`desktop_melee_port_device(port)`: "GameCube controller", "WaveBird", the
  controller's name, or "" for nothing). Port 1 always has the keyboard as
  well. A line under them describes the chosen port: "Port 1: keyboard",
  "Port 1: DualSense Wireless Controller + keyboard", "Port 3: nothing
  plugged in". The other player's port has a small badge. **Two players never
  share a port:** picking the other player's swaps them.
- **The button map** uses `DESKTOP_MELEE_BTN_*` numbers: 0 A, 1 B, 2 X, 3 Y,
  4 Z, 5 L, 6 R, 7 Nothing. `map[physical] = target`. The menu for each
  button reads "A", "B", "X (jump)", "Y (jump)", "Z (grab)", "L (shield)",
  "R (shield)", "Nothing". A trigger keeps its analog light shield only while
  it still maps to L or R; the game handles that.
- **Z = jump** is a shortcut for `map[Z] = X`; turning it off sets `map[Z] = Z`.
- **UCF** (the Universal Controller Fix) is shared by all players and with
  melee-pc's launcher. When the `MELEE_UCF` environment variable decides it,
  `pc_prefs_get_ucf(&on, &locked)` reports `locked`; the switch is then
  disabled and the note reads "Set by MELEE_UCF in the environment."
  Otherwise: "Fixes dashback and shield drops on any controller."
- Bottom line: "Buttons and jump apply right away." while idle, or "Buttons
  and jump apply now. The port applies at the next match." during a match.

**Mac styling:** button badges in GameCube face colours (A green, B red, Z
purple, the rest grey); the port buttons fill with the player's colour when
chosen.

## 7. Sound tab

Source: [`Panel.swift`](../../desktop-melee/ui/Panel.swift) (`SoundTab`).

| Control | Values | Default | Saved as | Calls | Applies |
|---|---|---|---|---|---|
| Master | 0 to 100% | from `launcher.cfg` (100%) | `launcher.cfg` | `pc_prefs_set_audio` | now |
| Music | 0 to 100% | from `launcher.cfg` (100%) | `launcher.cfg` | `pc_prefs_set_audio` | now |
| Effects | 0 to 100% | from `launcher.cfg` (100%) | `launcher.cfg` | `pc_prefs_set_audio` | now |
| Mute | on / off | off | `launcher.cfg` | `pc_prefs_set_audio`, `pc_prefs_save` | now |
| Reverb | on / off | on | `launcher.cfg` | `pc_prefs_set_audio`, `pc_prefs_save` | now |
| Menu sounds | on / off | on | `sound.menu` | (the UI's own sounds only) | now |

**Behaviour notes**

- Volumes are floats from 0 to 1 in the C calls, shown as whole percents.
  `pc_prefs_get_audio(&volume, &music, &sfx, &mute, &reverb)` reads all five
  and `pc_prefs_set_audio(...)` sets all five.
- While a slider moves, each change is applied (`pc_prefs_set_audio`) so the
  player hears it; `pc_prefs_save()` writes `launcher.cfg` when the slider is
  released. The switches save at once.
- Master is dimmed and disabled while Mute is on.
- These settings are melee-pc's and are shared with its launcher and F1 menu,
  which is why they live in `launcher.cfg` and not with Desktop Melee's own
  settings.

## 8. Display tab

Source: [`DisplayTab.swift`](../../desktop-melee/ui/DisplayTab.swift).

| Control | Values | Default | Saved as | Calls | Applies |
|---|---|---|---|---|---|
| Fighter size | 50% to 200%, steps of 10 | 100% | `display.size` | `desktop_melee_set_fighter_size(percent)` | next match |
| Screen shake | on / off | on | `display.shake` | `desktop_melee_set_shake(on)` | now |
| Timer | Top / Hidden | Top | `display.timer` (`"top"`, `"hidden"`) | `desktop_melee_set_timer_mode(DESKTOP_MELEE_TIMER_TOP or _HIDDEN)` | now |
| Player tags | on / off | on | `display.tags` | `desktop_melee_set_tags(on)` | now |
| Stocks and percents | Island / Bottom / Hidden | Island | `display.hud` (`"island"`, `"bottom"`, `"hidden"`) | `desktop_melee_set_hud_mode(DESKTOP_MELEE_HUD_*)` | next match |
| Screen | each connected display | the primary | `display.screen` (a name; `""` = the primary) | `desktop_melee_set_screen(name)` | now when idle, else when the match ends |
| Dock size (macOS only) | "Exact", or "Estimated · Measure exactly" | | | `desktop_melee_ax_request()` | |

**Behaviour notes**

- **Fighter size**: 100% is 490 Melee units across the screen; bigger means
  fewer units, so bigger fighters. The screen edges are the blastzones at any
  size, so bigger fighters are KO'd sooner, and the note says so. When
  `DESKTOP_MELEE_SCALE` is set, `desktop_melee_fighter_size_locked()` is true:
  the slider is disabled and the note reads "Set by DESKTOP_MELEE_SCALE in the
  environment."
- **Stocks and percents**: "Island" puts them beside the notch in a black
  island ("Beside the notch, in a black island."). "Bottom" is Melee's own
  HUD ("Melee's own, at the bottom of the screen."). "Hidden" shows none
  ("No stocks or percents on screen."). On a system without an island
  (`desktop_melee_platform_notch` reports height 0), Island falls back to
  Melee's HUD; a port should then offer Bottom and Hidden only. Older builds
  saved a `display.island` bool, migrated once to `display.hud`.
- **Screen**: one row per display, with a checkmark on the one in use and a
  "main" badge on the primary. Choosing the primary saves `""`, so the
  overlay keeps following the primary if the arrangement changes. A saved
  screen that is unplugged falls back to the primary, and comes back when
  plugged in again. The name must be what `SDL_GetDisplayName` reports, since
  `desktop_melee.c` matches it against SDL's display list. When a display is
  added or removed, the UI calls `desktop_melee_set_screen` again. With only
  one display, the note reads "Connect another display to play on it."
- **Dock size** is macOS only: the Dock's exact width needs the Accessibility
  permission (`desktop_melee_ax_trusted()`, `desktop_melee_ax_request()`).
  When it is granted, the tab calls `desktop_melee_relayout()` to measure
  again, and `desktop_melee_dock_exact()` turns the row to "Exact". On Windows
  the taskbar can be measured without a permission, so a port can show the
  measured taskbar or leave the row out.

**Mac styling:** the size slider has a small figure at the left end and a
large one at the right; the choices are segmented pills.

## 9. The welcome window

Source: [`Welcome.swift`](../../desktop-melee/ui/Welcome.swift),
[`WelcomeSteps.swift`](../../desktop-melee/ui/WelcomeSteps.swift),
[`DiscMemory.swift`](../../desktop-melee/ui/DiscMemory.swift).

A small window, 440 × 560 points, with "Desktop Melee" and "N of 5" at the
top. It runs in two phases around the game's boot.

**When it shows**

`desktop_melee_platform_ui_choose_disc(candidate)` decides, before the game
boots:

- First run (`onboarding.done` is not set): the whole window, from step 1.
- Later runs, when `candidate` (the command line's disc, else the one booted
  last) is the same file that passed the check before (same path, size and
  modification date, `disc.verified`): no window at all, boot straight away.
- Later runs, when that disc is gone or changed: step 2 only, titled "Find
  your Melee disc again".
- `DESKTOP_MELEE_WELCOME` set to anything forces the first-run window;
  `DESKTOP_MELEE_WELCOME=disc` opens it on step 2 (for testing).

**Phase 1, before the game boots (modal).** The game waits for the window.
Closing it quits the game (`choose_disc` returns NULL). The overlay is hidden
meanwhile (`desktop_melee_platform_overlay_set_hidden(true)`), so the whole
desktop stays the player's.

1. **Welcome.** "Desktop Melee", a line saying what it is (your Dock is the
   stage, the screen edges are the blast zones), a drawing of a desktop
   showing that, and a "Get started" button.
2. **Your disc.** Title "Bring your own Melee"; a line explaining that
   Desktop Melee includes the game's code but none of Nintendo's data. A drop
   target ("Drop your disc image here", "Melee USA 1.02: .iso .gcm .ciso
   .rvz") with a "Choose file..." button that opens a file picker in the
   Downloads folder, limited to those four types. A remembered disc is
   checked as soon as the step shows. Then a status line:
   - checking: "Checking your disc… 62%", a progress bar and the file name
     (`desktop_melee_disc_check_start(path)`, then
     `desktop_melee_disc_check_poll` ten times a second);
   - passed: "Super Smash Bros. Melee · USA · 1.02 · verified";
   - refused: the check's own message (the PAL disc, a modified or bad copy,
     not a disc image), and the button becomes "Choose another...".

   "Continue" is enabled only once the disc passed. It saves `disc.verified`
   and `onboarding.done` and boots the game.

**Between the phases: "Getting Melee ready..."** On the first run the window
stays up while the game boots and builds its graphics pipelines: "The first
launch builds Melee's graphics, so matches never stutter. It takes a few
minutes, once. You can use your Mac in the meantime." The progress comes from
`desktop_melee_pipelines_pending()` (section 11). The window is no longer
modal and stays above the overlay. `desktop_melee_set_start_starts_match(false)`
keeps a button press from starting a match underneath it.

**Phase 2, once the assets are loaded** (`desktop_melee_platform_ui_assets_ready`):

3. **Grab a controller.** Three cards: "GameCube" (official adapter, or one
   in Wii U mode), "Xbox, PS, Switch" (USB or Bluetooth) and "Keyboard". Below,
   "Press any button": every port where a button is newly pressed appears as
   "P1 GameCube controller" and so on, and lights up its card. The window
   polls `desktop_melee_pad_buttons(port)` 30 times a second for all four
   ports, and names them with `desktop_melee_port_device(port)` ("Keyboard"
   on port 1 when nothing else is there). The Keyboard card opens the key
   list, read live from melee-pc's bindings with `desktop_melee_key_name(slot)`:
   A B X Y Z L R, Start, the stick ("Arrow keys or W A S D" when those are the
   bindings) and the C-stick. Two notes: ports can be changed in the Controls
   tab, and the keyboard works only while Desktop Melee is the active app.
   The game keeps keyboard focus during this step, so key presses reach it.
4. **Measure the Dock (macOS only).** Asks for Accessibility, with "Open
   System Settings" and "Not now: the Dock size will be estimated". Skipped
   when it is already granted; moves on by itself once granted. **A port
   skips this step,** and the window counts 4 steps.
5. **You're set.** "Desktop Melee lives in your menu bar..." with a drawing of
   the menu bar icon, a "Start a match" button (`desktop_melee_request_match()`,
   then the window closes) and "or press Start on your controller"
   (`desktop_melee_set_start_starts_match(true)` again).

Closing the window in phase 2 is fine: the game keeps running, and
`onboarding.done` is already set, so the window won't come back on its own.

**Mac styling:** a dark, borderless-looking panel that takes clicks without
taking the keyboard from the game; big titles in Melee's font once it has
loaded.

## 10. How to play

The popover's "How to play" reopens the welcome window at step 3, with the
game running. Step 3 polls the pads as above (with Start not starting a
match), Continue goes on to step 4 (or 5 when Accessibility is granted, which
on other systems is always), and step 5's "Start a match" starts one.

## 11. The loading window

Source: [`Loading.swift`](../../desktop-melee/ui/Loading.swift).

melee-pc builds its graphics pipelines before the game starts, and caches
them. Desktop Melee never shows melee-pc's own launcher screen: the overlay
stays faded while it waits, between `desktop_melee_platform_ui_loading_begin()`
and `_end()`.

- On the first run the welcome window shows the progress (section 9).
- On later runs a small window appears **only if the wait takes more than two
  seconds** (a new build or a system update rebuilds the cache): "Getting
  Melee ready...", "Rebuilding graphics after an update. Only this once.",
  a progress bar, "Graphics: N of M" and the time left at the current
  rate.
- Progress is `total - desktop_melee_pipelines_pending()` over `total`, where
  `total` is the most pipelines seen pending (the queue is full from the
  start).

## 12. Pause and hide

Source: [`Hotkey.swift`](../../desktop-melee/ui/Hotkey.swift); the game side
is `desktop_melee_toggle_pause_and_hide()` in
[`desktop_melee.c`](../../desktop-melee/src/desktop_melee.c).

**Behaviour**

- One toggle, from two places: the global shortcut and the Match tab's
  button. The shortcut works whichever app is in front.
- **Hiding** fades the overlay to fully transparent
  (`desktop_melee_platform_overlay_set_faded(true)`), and the game keeps
  drawing underneath. A running match pauses with Melee's own pause, as the
  first human player. The notch island fades out too.
- **Showing** fades the overlay back in, leaves the match paused until that
  player presses Start, and gives the game window keyboard focus
  (`desktop_melee_platform_ui_focus_game`).
- Starting a match always shows the overlay: nobody means to play unseen.
- **The shortcut**: Control-Option-M by default, changeable in the Match tab
  (section 4), saved as `match.pauseKey`. Two or more modifiers, one of them
  Control, Option or Command (on Windows and Linux: Ctrl, Alt or the
  Windows/Super key). If another app holds it, registering fails and the old
  one stays. It must not need any special permission.

**Mac styling:** a Carbon `RegisterEventHotKey`, which needs no
Accessibility permission; the label uses macOS's modifier symbols.

## 13. Every setting

Desktop Melee's own settings, in the UserDefaults suite `desktop-melee` on
macOS (`defaults read desktop-melee`):

| Key | Type | Default | Tab |
|---|---|---|---|
| `players.p1.ckind`, `players.p2.ckind` | int, 0x00 to 0x19 | 2 (Fox) | Players |
| `players.p1.costume`, `players.p2.costume` | int | 0, 1 | Players |
| `players.p1.cpu`, `players.p2.cpu` | bool | false | Players |
| `players.p1.level`, `players.p2.level` | int, 1 to 9 | 9 | Players |
| `controls.p1.port`, `controls.p2.port` | int, 0 to 3 | 0, 1 | Controls |
| `controls.p1.tapjump`, `controls.p2.tapjump` | bool | true | Controls |
| `controls.p1.map`, `controls.p2.map` | 7 ints, 0 to 7 | `[0,1,2,3,4,5,6]` | Controls |
| `display.size` | double, 50 to 200 | 100 | Display |
| `display.shake` | bool | true | Display |
| `display.timer` | `"top"` or `"hidden"` | `"top"` | Display |
| `display.tags` | bool | true | Display |
| `display.hud` | `"island"`, `"bottom"` or `"hidden"` | `"island"` | Display |
| `display.screen` | string; `""` = the primary | `""` | Display |
| `match.stocks` | int, 1 to 99 | 4 | Match |
| `match.minutes` | int, 0 to 99; 0 = no limit | 7 | Match |
| `match.startOnController` | bool | false | Match |
| `practice.di` | `off`, `in`, `out`, `survival`, `mix` | `off` | Match (Windows and Linux) |
| `practice.tech` | `off`, `place`, `in`, `out`, `miss`, `mix` | `off` | Match (Windows and Linux) |
| `practice.mash` | `off`, `low`, `medium`, `high` | `off` | Match (Windows and Linux) |
| `match.pauseKey` | `{key, mods, label}`: a macOS key code and Carbon modifiers | Control-Option-M | Match |
| `sound.menu` | bool | true | Sound |
| `onboarding.done` | bool | false | welcome window |
| `disc.verified` | `{path, size, mtime}` | none | welcome window |

melee-pc's own settings (volumes, mute, reverb, UCF and the rest) are in its
`launcher.cfg`, through `pc_prefs_*`. The game keeps none of Desktop Melee's
settings itself: at launch the UI reads its settings and pushes each one in
through the setters above. Until it does, the game uses the defaults in
`desktop_melee.c`, which match the table.

## 14. The UI on Windows and Linux

> **Built for Linux; Windows can reuse it.** The tray, the settings window
> and the settings file run on Linux (issue #12). Still missing on both: the
> welcome window, "How to play", the Pause and hide hotkey, and hiding the
> window.

The macOS UI is SwiftUI and won't run elsewhere. The other systems' UI is
built from pieces the build already has, so it is one C/C++ codebase for
Windows and Linux:

**The menu: SDL3's tray icon.** `SDL_CreateTray(icon, tooltip)`, in the SDL
the build already vendors, gives a notification-area icon on Windows and an
AppIndicator / StatusNotifierItem icon on Linux, with a menu built from
`SDL_InsertTrayEntryAt`. The menu holds what you need without opening
anything:

- Start match / Restart match
- Pause and hide (showing the shortcut)
- Settings...
- How to play
- Quit

The tray icon can't draw the disc's emblem before the disc is in, so it starts
with an icon drawn for the project (not Melee's emblem) and may switch to
the decoded emblem afterwards, as the macOS app does. GNOME shows tray icons
only with the AppIndicator extension; on GNOME without it, Settings has to be
reachable some other way, such as a desktop entry or launching the game a
second time.

**Built for Linux:** [`tray_sdl.c`](../../desktop-melee/src/tray_sdl.c). The
icon is drawn in code: a floor and two fighters in the player colours. The
menu is "Start or restart match" (the game doesn't call the UI back, so the
label can't switch; `desktop_melee_request_match()` does both), "Pause and
hide", "Settings..." and "Quit Desktop Melee"; "How to play" waits for the
welcome window. Without a tray, the settings window opens at startup instead.

**The settings window: RmlUi.** melee-pc's launcher is already RmlUi
(HTML/CSS-like documents, drawn by aurora's RmlUi backend in
`extern/aurora/lib/rmlui/`), so it's in the build and
can draw with Melee's font and portraits once they are decoded. A settings
window with the five tabs, in the same order and with the same controls,
ranges and defaults as sections 4 to 8, and the welcome window's steps 1 to
3 and 5 in the same window.

**Built for Linux:** [`settings_window.cpp`](../../desktop-melee/src/settings_window.cpp)
with [`settings.rml`](../../desktop-melee/resources/settings.rml), all five
tabs. How the two open questions were answered:

- **A second window.** aurora gained one
  ([`rmlui_window.cpp`](../../extern/aurora/lib/rmlui_window.cpp),
  `aurora::rmlui::open_window` in `include/aurora/rmlui.hpp`): its own SDL
  window, WebGPU surface, render interface and RmlUi context, recorded with
  the game's frame and presented after it. Its events never reach the game.
  Only the render thread touches its surface; closing it hides the window
  and destroys it once the render thread has let go.
- **The font.** It uses melee-pc's bundled Noto Sans
  (`resources/menu*.ttf`), so it works before the disc's assets load.
  Character icons and portraits come from the disc at runtime through
  aurora's texture providers (`dmicon://`, `dmportrait://`), requested only
  once the roster has loaded.

Where it differs from the macOS app: Stocks and percents offers Bottom and
Hidden only (no island); each button-map entry cycles through its targets on
click instead of opening a menu; sliders save shortly after they stop
moving.

A native toolkit per system (Win32 or WinUI, GTK or Qt) is the alternative:
it fits each desktop better, at the cost of two UIs to maintain.

**Settings: a file the C side reads.** UserDefaults is macOS only. Instead, a
plain text file in SDL's preferences folder,
`SDL_GetPrefPath(NULL, "Desktop Melee")` + `settings.cfg`
(`%APPDATA%\Desktop Melee\` on Windows, `~/.local/share/Desktop Melee/` on
Linux), one `key = value` per line with the same keys as section 13:

```ini
players.p1.ckind = 2
players.p1.costume = 0
controls.p1.map = 0 1 2 3 4 5 6
display.hud = bottom
match.pauseKey = ctrl+alt+M
```

**Built for Linux:** [`settings_file.h`](../../desktop-melee/src/settings_file.h)
reads and writes this file. As on macOS, the UI side owns the values:
`platform_linux.c`'s `ui_install` calls `desktop_melee_settings_load()`,
which reads the file and pushes each value in through the same setters the
macOS UI calls, so `desktop_melee.c` and macOS are unchanged. The settings
window will change `desktop_melee_settings()`, call the setter and
`desktop_melee_settings_save()`. A missing key keeps its default, a value out
of range is clamped, and unknown keys are logged and ignored.
`match.pauseKey` is a portable string such as `ctrl+alt+M`, and
`disc.verified` is three keys: `disc.verified.path`, `.size` and `.mtime`
(seconds). `desktop-melee/tests/settings_file_test.c` tests the parser and
writer (`ninja -C build/linux desktop_melee_settings_test`).

macOS could move to the same file later, leaving UserDefaults for the
Mac-only keys, but that would be a separate change.
