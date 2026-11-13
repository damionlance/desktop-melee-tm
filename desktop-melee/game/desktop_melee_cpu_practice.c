/* SPDX-License-Identifier: GPL-3.0-or-later */
/* CPU practice (Players tab): CPU players that DI, tech and mash out of
 * grabs the way the Players tab says, so a human can practice combos and
 * chaingrabs on them.
 *
 * Melee's CPU writes its controller for the frame into fp->cpu (buttons,
 * sticks, triggers) in Fighter_procCpu, and Fighter_procInput copies that
 * into fp->input as if it came from a pad. desktop_melee_cpu_practice_frame
 * runs right after the CPU's own logic and overrides those inputs:
 *
 *   - DI: while the CPU is in hitlag from a hit, it holds the stick for the
 *     DI chosen for that hit. Melee applies DI when hitlag ends
 *     (ftCo_Damage_OnExitHitlag), from the stick at that moment.
 *   - Mash: while held in a grab, it alternates the stick left and right and
 *     taps A, at a rate set by the mash strength.
 *
 * Teching is decided, not inputted: Melee techs when L or R was pressed in
 * the 20 frames before touching the ground, wall or ceiling, which needs
 * the landing frame to be predicted. Instead, ftCo_DownAttack.c's tech check
 * and ftCo_PassiveStand.c's roll choice ask desktop_melee_cpu_tech_override
 * and desktop_melee_cpu_tech_roll, which answer from a plan rolled when the
 * CPU was hit. The result is the same as a perfect L press. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include <melee/ft/forward.h>
#include <melee/ft/inlines.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftCommon/ftCo_Damage.h>
#include <melee/ft/types.h>
#include <melee/pl/forward.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>

#include "desktop_melee_game.h"

/* Player slots (Gm_Player_NumMax) and a sub fighter each (Ice Climbers). */
#define SLOTS 6

typedef struct Plan {
    bool active;     /* a hit was planned for, and its tech not used yet */
    bool in_hitlag;  /* was in damage hitlag last frame */
    int di;          /* DESKTOP_MELEE_DI_*, never MIX */
    int tech;        /* DESKTOP_MELEE_TECH_*, never MIX */
    uint8_t mash;    /* mash phase: flips with each mash input */
} Plan;

static Plan s_plans[SLOTS][2];

/* A small RNG of our own, so practice doesn't move Melee's (HSD_Randi). */
static uint32_t s_rng = 0x2545F491u;
static int rand_below(int n) {
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return n > 0 ? (int) (s_rng % (uint32_t) n) : 0;
}

static Plan* plan_of(Fighter* fp) {
    if (fp->player_idx >= SLOTS) {
        return NULL;
    }
    return &s_plans[fp->player_idx][fp->is_sub_fighter ? 1 : 0];
}

/* Where the attacker is: the player that dealt the last hit, else the
 * nearest other fighter. False when there is none. */
static bool attacker_x(Fighter* fp, float* x) {
    const int source = fp->dmg.x18c4_source_ply;
    if (source >= 0 && source < SLOTS && source != fp->player_idx) {
        HSD_GObj* gobj = Player_GetEntity(source);
        if (gobj != NULL) {
            *x = GET_FIGHTER(gobj)->cur_pos.x;
            return true;
        }
    }
    bool found = false;
    float best = 0.0f;
    for (int slot = 0; slot < SLOTS; slot++) {
        HSD_GObj* gobj = slot == fp->player_idx ? NULL : Player_GetEntity(slot);
        if (gobj == NULL) {
            continue;
        }
        Fighter* other = GET_FIGHTER(gobj);
        const float d = fabsf(other->cur_pos.x - fp->cur_pos.x);
        if (!found || d < best) {
            found = true;
            best = d;
            *x = other->cur_pos.x;
        }
    }
    return found;
}

/* +1 when the attacker is to the right of fp, -1 to the left. With no
 * attacker, the stage's middle (x = 0) stands in. */
static float attacker_side(Fighter* fp) {
    float x = 0.0f;
    attacker_x(fp, &x);
    return x >= fp->cur_pos.x ? 1.0f : -1.0f;
}

/* A CPU player's fighters. Not ftCo_IsCpuControlled, which also says yes
 * for a human's Nana. */
static bool is_cpu(Fighter* fp) {
    return !fp->is_sleeping && Player_8003248C(fp->player_idx, false) == Gm_PKind_Cpu;
}

static bool in_damage_hitlag(Fighter* fp) {
    return fp->dmg.x195c_hitlag_frames > 0.0f && fp->hitlag_cb == ftCo_Damage_OnEveryHitlag;
}

static bool in_grab(Fighter* fp) {
    return fp->motion_id >= ftCo_MS_CapturePulledHi && fp->motion_id <= ftCo_MS_CaptureDamageLw;
}

static void roll_plan(Plan* plan) {
    const int di = desktop_melee_cpu_di();
    const int tech = desktop_melee_cpu_tech();
    plan->di = di == DESKTOP_MELEE_DI_MIX ? DESKTOP_MELEE_DI_IN + rand_below(3) : di;
    plan->tech = tech == DESKTOP_MELEE_TECH_MIX ? DESKTOP_MELEE_TECH_PLACE + rand_below(4) : tech;
    plan->active = true;
}

static signed char stick(float v) {
    v = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
    return (signed char) (v * 127.0f);
}

/* The stick for the plan's DI, from the knockback the hit set. */
static void hold_di(Fighter* fp, const Plan* plan) {
    float x = 0.0f;
    float y = 0.0f;
    const float kx = fp->x8c_kb_vel.x;
    const float ky = fp->x8c_kb_vel.y;
    switch (plan->di) {
    case DESKTOP_MELEE_DI_IN:
        x = attacker_side(fp);
        break;
    case DESKTOP_MELEE_DI_OUT:
        x = -attacker_side(fp);
        break;
    case DESKTOP_MELEE_DI_SURVIVAL: {
        /* Only the part of the stick across the knockback counts. Of the two
         * perpendiculars, pick the one that sends a sideways launch higher,
         * or an upward one back toward the middle of the stage. */
        const float len = sqrtf(kx * kx + ky * ky);
        if (len <= 0.0f) {
            return;
        }
        x = -ky / len;
        y = kx / len;
        const bool flip = fabsf(kx) >= fabsf(ky) ? y < 0.0f : x * fp->cur_pos.x > 0.0f;
        if (flip) {
            x = -x;
            y = -y;
        }
        break;
    }
    default:
        return;
    }
    fp->cpu.lstick.x = stick(x);
    fp->cpu.lstick.y = stick(y);
    fp->cpu.cstick.x = 0;
    fp->cpu.cstick.y = 0;
}

/* One new input every few frames: the stick swings to the other side and A
 * taps. Each counts toward Melee's grab escape. */
static void mash(Fighter* fp, Plan* plan) {
    static const int every[DESKTOP_MELEE_MASH_COUNT] = { 0, 6, 3, 1 };
    const int n = every[desktop_melee_cpu_mash()];
    if (n == 0) {
        return;
    }
    if (rand_below(n) == 0) {
        plan->mash ^= 1;
    }
    fp->cpu.lstick.x = plan->mash ? 127 : -128;
    fp->cpu.lstick.y = 0;
    fp->cpu.buttons = plan->mash ? HSD_PAD_A : 0;
}

void desktop_melee_cpu_practice_frame(void* gobj) {
    Fighter* fp = GET_FIGHTER((HSD_GObj*) gobj);
    Plan* plan = plan_of(fp);
    if (plan == NULL || !is_cpu(fp)) {
        return;
    }
    const bool hitlag = in_damage_hitlag(fp);
    if (hitlag && !plan->in_hitlag) {
        roll_plan(plan); /* each hit of a multi-hit move rolls again */
    }
    plan->in_hitlag = hitlag;
    if (hitlag) {
        hold_di(fp, plan);
    } else if (in_grab(fp)) {
        mash(fp, plan);
    }
}

int desktop_melee_cpu_tech_override(void* gobj) {
    Fighter* fp = GET_FIGHTER((HSD_GObj*) gobj);
    Plan* plan = plan_of(fp);
    if (plan == NULL || !is_cpu(fp) || !plan->active ||
        plan->tech == DESKTOP_MELEE_TECH_OFF)
    {
        return -1;
    }
    return plan->tech == DESKTOP_MELEE_TECH_MISS ? 0 : 1;
}

int desktop_melee_cpu_tech_roll(void* gobj) {
    Fighter* fp = GET_FIGHTER((HSD_GObj*) gobj);
    Plan* plan = plan_of(fp);
    if (desktop_melee_cpu_tech_override(gobj) != 1) {
        return -1;
    }
    if (plan->tech == DESKTOP_MELEE_TECH_PLACE) {
        return 0;
    }
    /* PassiveStandF rolls the way the fighter faces. */
    float toward = attacker_side(fp);
    if (plan->tech == DESKTOP_MELEE_TECH_OUT) {
        toward = -toward;
    }
    return toward * fp->facing_dir >= 0.0f ? ftCo_MS_PassiveStandF : ftCo_MS_PassiveStandB;
}
