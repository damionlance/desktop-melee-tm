/* SPDX-License-Identifier: GPL-3.0-or-later */
/* CPU practice (Match tab): CPU players that DI, tech, mash out of grabs and
 * dodge the moment they can act, the way the Match tab says, so a human can
 * practice combos and chaingrabs on them and see which are true.
 *
 * Melee's CPU writes its controller for the frame into fp->cpu (buttons,
 * sticks, triggers) in Fighter_procCpu, and Fighter_procInput copies that
 * into fp->input as if it came from a pad; the motion state's interrupts
 * (IASA) read it later in the same frame. desktop_melee_cpu_practice_frame
 * runs right after the CPU's own logic and overrides those inputs:
 *
 *   - DI: while being thrown, and in hitlag from a hit, the CPU holds the
 *     stick for the DI chosen for that hit. Melee applies DI when hitlag ends
 *     (ftCo_Damage_OnExitHitlag), from the stick at that moment; a throw
 *     releases with little or no hitlag, so its DI is held through the throw.
 *   - Mash: while held in a grab, it alternates the stick left and right and
 *     taps A, at a rate set by the mash strength.
 *   - Escape: after a hit, it keeps its controller neutral until the first
 *     frame it can act, then dodges: spot dodge (L with the stick freshly
 *     down) on the ground, air dodge (a fresh L press) in the air. A missed
 *     tech gets up at once (stick up), then dodges out of the getup. Tumble
 *     can't air dodge in Melee (ftCo_DamageFall_IASA has no air dodge), so
 *     it wiggles out into a fall and air dodges the frame after.
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
#include <sysdolphin/baselib/gobj.h>

#include "desktop_melee_game.h"

/* Player slots (Gm_Player_NumMax) and a sub fighter each (Ice Climbers). */
#define SLOTS 6
/* Slight DI: half the stick, well past the deadzone. Melee's DI turns the
 * launch by up to 18 degrees with the square of the stick across it. */
#define SLIGHT 0.5f
/* A Mix roll of DI that holds the stick neutral, unlike Off. */
#define DI_NONE (-1)

typedef struct Plan {
    bool active;     /* a hit was planned for */
    bool in_hitlag;  /* was in damage hitlag last frame */
    bool was_thrown; /* was in a thrown state last frame */
    bool escape;     /* waiting to dodge at the first actionable frame */
    bool pressed;    /* the dodge input went in last frame */
    int di;          /* DESKTOP_MELEE_DI_* or DI_NONE, never MIX */
    int tech;        /* DESKTOP_MELEE_TECH_*, never MIX */
    uint8_t mash;    /* mash phase: flips with each mash input */
} Plan;

static Plan s_plans[SLOTS][2];

static int rand_below(int n) {
    return n > 0 ? (int)(desktop_melee_practice_random() % (unsigned)n) : 0;
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

static bool in_grab(int m) {
    return m >= ftCo_MS_CapturePulledHi && m <= ftCo_MS_CaptureDamageLw;
}

static bool is_thrown(int m) {
    return (m >= ftCo_MS_ThrownF && m <= ftCo_MS_ThrownlwWomen) ||
           (m >= ftCo_MS_ThrownFF && m <= ftCo_MS_ThrownFLw);
}

static void roll_plan(Plan* plan) {
    static const int mix[] = {DI_NONE, DESKTOP_MELEE_DI_IN, DESKTOP_MELEE_DI_OUT,
        DESKTOP_MELEE_DI_SLIGHT_IN, DESKTOP_MELEE_DI_SLIGHT_OUT};
    const int di = desktop_melee_cpu_di();
    const int tech = desktop_melee_cpu_tech();
    plan->di = di == DESKTOP_MELEE_DI_MIX ? mix[rand_below(5)] : di;
    plan->tech = tech == DESKTOP_MELEE_TECH_MIX ? DESKTOP_MELEE_TECH_PLACE + rand_below(4) : tech;
    plan->escape = desktop_melee_cpu_escape() == DESKTOP_MELEE_ESCAPE_DODGE;
    plan->pressed = false;
    plan->active = true;
}

static signed char stick(float v) {
    v = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
    return (signed char)(v * 127.0f);
}

/* No buttons, sticks or triggers this frame. */
static void neutral(Fighter* fp) {
    fp->cpu.buttons = 0;
    fp->cpu.lstick.x = fp->cpu.lstick.y = 0;
    fp->cpu.cstick.x = fp->cpu.cstick.y = 0;
    fp->cpu.ltrigger = fp->cpu.rtrigger = 0;
}

/* The stick for the plan's DI, from the knockback the hit set. */
static void hold_di(Fighter* fp, const Plan* plan) {
    float x = 0.0f;
    float y = 0.0f;
    const float kx = fp->x8c_kb_vel.x;
    const float ky = fp->x8c_kb_vel.y;
    switch (plan->di) {
    case DI_NONE:
        break;
    case DESKTOP_MELEE_DI_IN:
        x = attacker_side(fp);
        break;
    case DESKTOP_MELEE_DI_OUT:
        x = -attacker_side(fp);
        break;
    case DESKTOP_MELEE_DI_SLIGHT_IN:
        x = SLIGHT * attacker_side(fp);
        break;
    case DESKTOP_MELEE_DI_SLIGHT_OUT:
        x = -SLIGHT * attacker_side(fp);
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
        return; /* Off: Melee's own CPU */
    }
    neutral(fp);
    fp->cpu.lstick.x = stick(x);
    fp->cpu.lstick.y = stick(y);
}

/* One new input every few frames: the stick swings to the other side and A
 * taps. Each counts toward Melee's grab escape. */
static void mash(Fighter* fp, Plan* plan) {
    static const int every[DESKTOP_MELEE_MASH_COUNT] = {0, 6, 3, 1};
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

/* Where a CPU waiting to dodge stands, by its motion state. */
enum {
    ESC_WAIT,   /* can't act yet: keep the controller neutral */
    ESC_ACT,    /* can act now: dodge */
    ESC_WIGGLE, /* tumble: wiggle out, to air dodge next frame */
    ESC_GETUP,  /* lying down after a missed tech: get up */
    ESC_DONE,   /* dodged, or somewhere a dodge doesn't belong */
};

static int escape_stage(Fighter* fp) {
    const int m = fp->motion_id;
    const bool hitstun = fp->x221C_b6;
    if (m >= ftCo_MS_EscapeF && m <= ftCo_MS_EscapeAir) {
        return ESC_DONE;
    }
    /* DamageFly* and DamageFall are tumble: their interrupts are
     * ftCo_DamageFall_IASA's once hitstun ends. */
    if ((m >= ftCo_MS_DamageFlyHi && m <= ftCo_MS_DamageFlyRoll) || m == ftCo_MS_DamageFall) {
        if (m != ftCo_MS_DamageFall && hitstun) {
            return ESC_WAIT;
        }
        return fp->ground_or_air == GA_Air ? ESC_WIGGLE : ESC_ACT;
    }
    if (m >= ftCo_MS_DamageHi1 && m < ftCo_MS_DamageFlyHi) {
        return hitstun ? ESC_WAIT : ESC_ACT;
    }
    switch (m) {
    case ftCo_MS_Wait:
    case ftCo_MS_Fall:
    case ftCo_MS_FallF:
    case ftCo_MS_FallB:
    case ftCo_MS_Squat:
    case ftCo_MS_SquatWait:
    case ftCo_MS_Landing:
    case ftCo_MS_LandingFallSpecial:
        return ESC_ACT;
    case ftCo_MS_DownWaitU:
    case ftCo_MS_DownWaitD:
        return ESC_GETUP;
    case ftCo_MS_DownBoundU:
    case ftCo_MS_DownBoundD:
    case ftCo_MS_DownDamageU:
    case ftCo_MS_DownDamageD:
    case ftCo_MS_DownStandU:
    case ftCo_MS_DownStandD:
    case ftCo_MS_Passive:
    case ftCo_MS_PassiveStandF:
    case ftCo_MS_PassiveStandB:
        return ESC_WAIT;
    default:
        return ESC_DONE;
    }
}

static void escape(Fighter* fp, Plan* plan) {
    const int stage = escape_stage(fp);
    if (stage == ESC_DONE) {
        plan->escape = false; /* Melee's own CPU again */
        return;
    }
    neutral(fp);
    if (stage == ESC_GETUP) {
        fp->cpu.lstick.y = 127; /* ftCo_800980BC: stand up */
        return;
    }
    if (stage == ESC_WIGGLE) {
        /* ftCo_DamageFall_IASA: a fresh sideways stick leaves tumble. */
        fp->cpu.lstick.x = plan->pressed ? 0 : stick(-attacker_side(fp));
        plan->pressed = !plan->pressed;
        return;
    }
    if (stage == ESC_WAIT || plan->pressed) {
        /* A frame of neutral before each try, so the next press is new. */
        plan->pressed = false;
        return;
    }
    /* Spot dodge wants L held with the stick freshly down
     * (ftCo_80099794); air dodge wants a fresh L press (ftCo_80099A58). */
    fp->cpu.buttons = HSD_PAD_L;
    if (fp->ground_or_air != GA_Air) {
        fp->cpu.lstick.y = -128;
    }
    plan->pressed = true;
}

void desktop_melee_cpu_practice_frame(void* gobj) {
    Fighter* fp = GET_FIGHTER((HSD_GObj*)gobj);
    Plan* plan = plan_of(fp);
    if (plan == NULL || !is_cpu(fp)) {
        return;
    }
    const int m = fp->motion_id;
    const bool hitlag = in_damage_hitlag(fp);
    const bool thrown = is_thrown(m);
    /* A new plan for each hit and each throw. A throw's release is the hit
     * the throw's plan was for, so it keeps that plan. */
    if ((thrown && !plan->was_thrown) || (hitlag && !plan->in_hitlag && !plan->was_thrown)) {
        roll_plan(plan);
    }
    plan->in_hitlag = hitlag;
    plan->was_thrown = thrown;
    if (hitlag || thrown) {
        hold_di(fp, plan);
    } else if (in_grab(m)) {
        mash(fp, plan);
    } else if (plan->escape) {
        escape(fp, plan);
    }
}

int desktop_melee_cpu_tech_override(void* gobj) {
    Fighter* fp = GET_FIGHTER((HSD_GObj*)gobj);
    Plan* plan = plan_of(fp);
    if (plan == NULL || !is_cpu(fp) || !plan->active || plan->tech == DESKTOP_MELEE_TECH_OFF) {
        return -1;
    }
    return plan->tech == DESKTOP_MELEE_TECH_MISS ? 0 : 1;
}

int desktop_melee_cpu_tech_roll(void* gobj) {
    Fighter* fp = GET_FIGHTER((HSD_GObj*)gobj);
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

/* ---- quick reset (desktop-melee/src/practice_reset.c) --------------------- */

bool desktop_melee_practice_fighters_ready(void) {
    int count = 0;
    for (HSD_GObj* cur = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; cur != NULL; cur = cur->next) {
        Fighter* fp = GET_FIGHTER(cur);
        if (fp->is_sleeping) {
            continue;
        }
        if (fp->input_disabled || fp->motion_id != ftCo_MS_Wait) {
            return false;
        }
        count++;
    }
    return count >= 2;
}

bool desktop_melee_practice_reset_pressed(void) {
    for (int port = 0; port < 4; port++) {
        if (HSD_PadGameStatus[port].trigger & HSD_PAD_DPADDOWN) {
            return true;
        }
    }
    return false;
}
