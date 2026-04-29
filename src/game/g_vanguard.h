/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 wahke <info@wahke.lu> (https://wahke.lu)
 * SPDX-FileCopyrightText: 2026 VanguardMod Project Contributors
 *
 * This file is part of VanguardMod.
 *
 * VanguardMod is built on ETLegacy (https://www.etlegacy.com),
 * which is licensed under GPL-3.0-or-later.
 *
 * VanguardMod is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * VanguardMod is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with VanguardMod. If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * g_vanguard.h — VanguardMod server-side feature module entry points.
 *
 * Master header for VanguardMod-specific qagame extensions that sit on
 * top of the imported ETLegacy SDK. Anything Vanguard-only (dev mode,
 * future hitbox tuning, future movement tweaks, ...) declares its
 * public surface here under a vg_<Subsystem>_<Func> namespace so the
 * upstream-resync diff stays compact and readable.
 *
 * Subsystem prefixes in use / reserved:
 *   vg_DevMode_*    server-controlled developer/debug visualisation
 *   vg_Hitbox_*     (future) hitbox geometry tuning
 *   vg_Movement_*   (future) movement / strafe behaviour tweaks
 *
 * Conventions:
 *   - One vg_<Subsystem>_Init / _Shutdown / _OnFrame triple per subsystem.
 *   - Cvars owned by a subsystem are registered in its _Init.
 *   - All transitions and admin-visible warnings go through G_Printf.
 */

#ifndef VANGUARD_G_VANGUARD_H
#define VANGUARD_G_VANGUARD_H

#include "../qcommon/q_shared.h"
#include "bg_public.h"   /* animScriptImpactPoint_t for vg_Hitbox_*. */

/* ------------------------------------------------------------------ */
/* Dev mode — hitbox / bullet visualisation gated by vanguard_dev.    */
/* ------------------------------------------------------------------ */

/**
 * @brief Register the vanguard_dev cvar and reset internal state.
 *        Call once per map from G_InitGame, after WG_Init.
 */
void vg_DevMode_Init(void);

/**
 * @brief Restore any cvars dev mode took ownership of and clear state.
 *        Call once per map from G_ShutdownGame.
 */
void vg_DevMode_Shutdown(void);

/**
 * @brief Detect vanguard_dev transitions, drive the periodic admin
 *        reminder, and emit the public-server warning. Cheap; safe to
 *        call every frame.
 *
 * @param[in] leveltime  level.time at the point of the call
 */
void vg_DevMode_OnFrame(int leveltime);

/* ------------------------------------------------------------------ */
/* Multi-box hitbox — bridge between the BONE_HITTESTS pipeline       */
/* (activated in Phase 6.0) and VanguardMod's per-region damage       */
/* multiplier configuration.                                          */
/*                                                                    */
/* Phase 6.1.1: lifecycle + cvar registration (this commit).          */
/* Phase 6.1.2: API implementation (DamageMultiplierFor / RegionFor / */
/*              RegionName flesh out — currently no-op stubs).        */
/* Phase 6.1.3: G_Damage integration (apply multiplier on hit).       */
/* ------------------------------------------------------------------ */

/**
 * @brief Register the 11 vanguard_hitbox_* / vanguard_dmg_* cvars
 *        and reset internal state. Call once per map from G_InitGame.
 */
void vg_Hitbox_Init(void);

/**
 * @brief Returns qtrue iff vanguard_hitbox_mode >= 1, i.e. the
 *        multi-box damage path is active for this map. CVAR_LATCH
 *        on the underlying cvar means the result is stable for
 *        the map lifetime; cheap to call from a hot path.
 */
qboolean vg_Hitbox_IsActive(void);

/**
 * @brief Returns qtrue iff vanguard_hitbox_debug is non-zero. Gates
 *        the per-trace VG_DIAG server-log print in g_combat.c.
 *        Cheap; safe to call from the damage hot path.
 */
qboolean vg_Hitbox_DebugActive(void);

/**
 * @brief Returns qtrue iff vanguard_hitbox_strict is non-zero —
 *        i.e. shots that pass the engine's broad-phase player AABB
 *        but fail to land in any of the multi-region capsules
 *        defined by human_base.hit must be REJECTED rather than
 *        falling through to the legacy chain (which would credit
 *        them as ordinary body shots with full damage).
 *
 *        v0.4.3 default 1: strict on. AABB tolerance can be ~6 units
 *        wider than the visible mesh in some poses, and the legacy
 *        fallback turning that tolerance into damage is not a
 *        behaviour competitive players want. Cup admins or anyone
 *        wanting byte-identical legacy behaviour can flip it to 0.
 *
 *        Only meaningful when vg_Hitbox_IsActive() is qtrue and the
 *        weapon's MOD has isHeadshot set; non-headshot weapons
 *        (explosives etc.) skip the multi-region branch entirely
 *        and are not affected by strict mode.
 */
qboolean vg_Hitbox_StrictMode(void);

/**
 * @brief Tear down. No-op currently — vmCvars have module lifetime.
 *        Call once per map from G_ShutdownGame for symmetry / hook
 *        point for any future teardown.
 */
void vg_Hitbox_Shutdown(void);

/**
 * @brief Damage multiplier for a hit-area's impactpoint. Returns the
 *        registered cvar value for the region, falling back to
 *        vanguard_dmg_default for IMPACTPOINT_UNUSED / out-of-range.
 *
 * @param[in] impactpoint  resolved impactpoint from mdx_hit_test
 * @return    multiplier in [0, infinity); 1.0 == no scaling
 *
 * @note Phase 6.1.1 stub returns 1.0f unconditionally.
 */
float vg_Hitbox_DamageMultiplierFor(animScriptImpactPoint_t impactpoint);

/**
 * @brief Coarse hit-region category (HR_HEAD / HR_ARMS / HR_BODY /
 *        HR_LEGS), suitable for direct use with the existing
 *        hitRegions[] stats array. Returns HR_NUM_HITREGIONS as
 *        the "no/unknown region" sentinel for IMPACTPOINT_UNUSED.
 *
 * @note  Mapping (Phase 6.1.2):
 *          HR_HEAD <- HEAD
 *          HR_BODY <- CHEST, GUT, GROIN
 *          HR_ARMS <- SHOULDER_L/R
 *          HR_LEGS <- KNEE_L/R, LEGS
 */
hitRegion_t vg_Hitbox_RegionFor(animScriptImpactPoint_t impactpoint);

/**
 * @brief Display name for an impactpoint, suitable for log lines and
 *        future stats output. Returned string is statically allocated.
 *
 * @note Phase 6.1.1 stub returns "unknown".
 */
const char *vg_Hitbox_RegionName(animScriptImpactPoint_t impactpoint);

#endif /* VANGUARD_G_VANGUARD_H */
