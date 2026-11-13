# Licensing

This repository contains code under three different situations. Read all
three before redistributing any part of it.

## 1. Game code — NOT licensed

`src/melee/` and `src/sysdolphin/` are a decompilation of Super Smash Bros.
Melee, derived from [doldecomp/melee](https://github.com/doldecomp/melee) and
adapted here to a 64-bit little-endian data model.

Super Smash Bros. Melee is copyright Nintendo / HAL Laboratory. The upstream
decompilation project publishes no license, and neither this repository nor
its authors can grant one. **No permission to copy, modify or redistribute
this code is offered or implied.**

The GPL in `COPYING` does not, and cannot, apply to this code. Because these
files cannot be relicensed, the repository as a whole is not distributable
under the GPL — only the port code in section 2 is.

No game assets are contained in this repository. Textures, audio, models,
text, and the two HSD font atlases are all read at runtime from a disc image
the user supplies; see `src/pc/discfont.c`.

## 2. Port code — GPL-3.0-or-later

`src/pc/`, `tools/`, `platforms/`, `cmake/` and `.github/` are original work
for this port.

    Copyright (C) 2026 the melee-pc contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.

The full text is in [`COPYING`](COPYING).

### Desktop Melee

`desktop-melee/` is Desktop Melee, the desktop overlay built on melee-pc. It is under
the same GPL-3.0-or-later terms.

    Copyright (C) 2026 Mwrnk

Desktop Melee also changes these melee-pc files (GPL-3.0 section 5a). Each
change is tagged `desktop-melee` in a comment and listed in
[`docs/desktop-melee/how-it-works.md`](../docs/desktop-melee/how-it-works.md#where-desktop-melee-touches-melee-pc):

- Build: `CMakeLists.txt` (includes `desktop-melee/desktop-melee.cmake`), `.gitignore`,
  `cmake/MeleeToolchainCheck.cmake` and `tools/preflight.py` (doc path only).
- Port code: `src/pc/main.c`, `src/pc/launcher.cpp`, `src/pc/vi.c`.
- Repository: `.github/workflows/build.yml` (manual only, release job removed),
  `.github/ISSUE_TEMPLATE/`; `.github/RELEASE_NOTES.md` and
  `.github/workflows/pages.yml` removed; melee-pc's `README.md` is now
  `MELEE-PC.md`, and its `ROADMAP.md`, `CODING_STYLE.md`, `docs/*.md`,
  `docs/screenshots/` and `docs/site/` moved to `docs/melee-pc/` (links fixed).
- aurora: `extern/aurora/include/aurora/aurora.h`, `extern/aurora/lib/aurora.cpp`,
  `extern/aurora/lib/window.cpp`, `extern/aurora/lib/webgpu/gpu.cpp`,
  `extern/aurora/lib/webgpu/gpu.hpp`, `extern/aurora/lib/gx/gx.cpp`,
  `extern/aurora/lib/gx/gx.hpp`, `extern/aurora/lib/gx/pipeline.hpp`,
  `extern/aurora/lib/gx/command_processor.cpp`,
  `extern/aurora/lib/gfx/recording.cpp`,
  `extern/aurora/lib/dolphin/gx/GXFrameBuffer.cpp`,
  `extern/aurora/cmake/AuroraSDL3Provider.cmake`,
  `extern/aurora/cmake/patches/apply-sdl3-macos-libusb-claim.cmake`,
  `extern/aurora/cmake/aurora_core.cmake`,
  `extern/aurora/include/aurora/rmlui.hpp`, `extern/aurora/lib/rmlui.cpp`,
  `extern/aurora/lib/rmlui.hpp`; and adds `extern/aurora/lib/rmlui_window.cpp`.
- Game code (section 1, still not licensed; the hooks are small and marked):
  `src/melee/cm/camera.c`, `src/melee/ft/fighter.c`,
  `src/melee/ft/kinds/ftCommon/ftCo_DownAttack.c`,
  `src/melee/ft/kinds/ftCommon/ftCo_Jump.c`,
  `src/melee/ft/kinds/ftCommon/ftCo_JumpAerial.c`,
  `src/melee/ft/kinds/ftCommon/ftCo_JumpAerialF1.c`,
  `src/melee/ft/kinds/ftCommon/ftCo_PassiveStand.c`, `src/melee/gm/forward.h`,
  `src/melee/gm/gmboot.c`, `src/melee/gm/gmscene.c`, `src/melee/gm/gmscdata.c`, `src/melee/gm/gmvs.c`,
  `src/melee/gm/gmvsmode.c` (a developer-only character override),
  `src/melee/gr/grdisplay.c`, `src/melee/gr/ground.c`, `src/melee/if/ifall.c`,
  `src/melee/if/ifnametag.c`, `src/melee/if/ifstatus.c`,
  `src/melee/if/ifstock.c`, `src/melee/if/iftime.c`, `src/melee/mp/mplib.c`,
  `src/sysdolphin/baselib/displayfunc.c`.

## 3. Third-party components

- `extern/aurora/` — aurora, MIT. See `extern/aurora/LICENSE`.
- `resources/font.ttf`, `resources/font-bold.ttf` — Liberation Sans, SIL Open
  Font License. See `resources/FONT-LICENSE.txt`.
- `platforms/android/app/src/main/java/org/libsdl/` — SDL3, zlib license.
- `extern/dht/dht.c`, `extern/dht/dht.h` — jech/dht, MIT (notice in the files).
- `extern/dht/sha1.c`, `extern/dht/sha1.h` — Steve Reid's SHA-1, public domain.
- `extern/monocypher/` — Monocypher 4.0.3, BSD-2-Clause or CC0 (notice in the
  files).
- `desktop-melee/resources/icons/` — Phosphor Icons 2.1.1, MIT. See
  `desktop-melee/resources/icons/PHOSPHOR-LICENSE.txt`. All of them except
  `device.*` and `emblem.standin`, which are Desktop Melee's own (section 2).
- Dawn, SDL3, nod, RmlUi and the other dependencies are fetched at build time
  under their own licenses.

MIT, zlib, BSD-2-Clause, CC0, public domain and the SIL OFL are all GPL-3.0
compatible, so combining them with the port code in section 2 is fine.
