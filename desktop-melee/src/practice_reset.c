/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Quick reset (Match tab): D-pad down puts the match back the way it was
 * when the fighters could first move, so practice doesn't wait on a
 * respawn.
 *
 * The match is saved with melee-pc's rollback snapshot (src/pc/net_snapshot.c):
 * the decomp's statics and heaps, everything the simulation is. It is taken
 * between ticks, the first tick every fighter stands ready
 * (desktop_melee_practice_fighters_ready), and restored between ticks when a
 * controller pressed D-pad down in the tick before. Like net.c's rollback_to,
 * the restore keeps the live pad queue, which belongs to the present.
 *
 * Not while netplay runs (the rollback owns the snapshots then), and never
 * across a match: the snapshot is dropped when the match ends, and
 * snapshot_unusable refuses one from another scene. */
#include "pc/compat.h"
#include "pc/net_internal.h"

#include <SDL3/SDL_log.h>
#include <dolphin/ar.h>
#include <dolphin/dvd.h>
#include <dolphin/os.h>
#include <string.h>
#include <sysdolphin/baselib/controller.h>

#include "desktop_melee.h"
#include "desktop_melee_game.h"

static Snapshot s_start;
static bool s_have;    /* s_start holds this match's start */
static bool s_pending; /* a reset was asked for and waits for a quiet tick */

/* A disc or ARAM transfer in flight writes memory the snapshot covers. */
static bool transfers_busy(void) {
    return aurora_dvd_inflight() > 0 || aurora_arq_inflight() > 0;
}

static void restore(void) {
    bool intr = OSDisableInterrupts();
    PadLibData pad = HSD_PadLibData;
    HSD_PadData queue[8];
    int qn = pad.qnum > 8 ? 8 : pad.qnum;
    memcpy(queue, pad.queue, qn * sizeof *queue);
    snapshot_restore(&s_start);
    memcpy(pad.queue, queue, qn * sizeof *queue);
    pad.rumble_info = HSD_PadLibData.rumble_info;
    HSD_PadLibData = pad;
    OSRestoreInterrupts(intr);
}

void desktop_melee_practice_tick(void) {
    if (pc_net_active() || !desktop_melee_match_active()) {
        s_have = false;
        s_pending = false;
        return;
    }
    if (!s_have) {
        if (desktop_melee_practice_fighters_ready() && !transfers_busy()) {
            s_have = snapshot_take(&s_start, 0);
        }
        return;
    }
    if (desktop_melee_quick_reset() && !desktop_melee_game_paused() &&
        desktop_melee_practice_reset_pressed())
    {
        s_pending = true;
    }
    if (!s_pending || transfers_busy()) {
        return;
    }
    s_pending = false;
    const char* why = snapshot_unusable(&s_start);
    if (why != NULL) {
        SDL_Log("desktop-melee: reset: the start of the match can't be restored (%s)", why);
        s_have = false;
        return;
    }
    restore();
}
