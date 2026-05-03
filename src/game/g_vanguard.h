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
 * @brief Test-and-clear the manual VG_DIAG_DUMP trigger cvar
 *        (vanguard_diag_dump). Returns qtrue exactly once after an
 *        admin sets the cvar to 1; subsequent calls return qfalse
 *        until the cvar is set again. Phase 7.0.1 capsule recon
 *        (v0.5.2-rc3+); supplements the existing
 *        vanguard_hitbox_debug 0->1 transition re-arm path.
 *
 *        Implementation: reads cvar, returns qtrue + writes "0"
 *        back via trap_Cvar_Set if non-zero; qfalse otherwise. The
 *        write is fast and the cvar isn't ARCHIVE, so no map.cfg
 *        churn either. Safe to call from the damage hot path.
 */
qboolean vg_Hitbox_ConsumeDiagDumpRequest(void);

/**
 * @brief Returns qtrue if the given means-of-death is a self / non-
 *        bullet damage source — falldamage, drowning, lava, crush,
 *        suicide, trigger_hurt, telefrag. These callers pass
 *        `point=NULL` to G_Damage (g_active.c:170/191/1014, lots of
 *        g_props.c / g_mover.c sites), and the multi-region capsule
 *        trace has no meaningful "impact point" for them anyway.
 *        Phase 8.0 v0.5.2.2 SIGSEGV fix.
 *
 *        Used by the multi-region branch entry gate in g_combat.c
 *        as defense-in-depth alongside an explicit `point != NULL &&
 *        attacker != NULL && attacker->client != NULL` check. The
 *        explicit pointer checks alone prevent the crash; this
 *        predicate documents the intent ("multi-region semantics
 *        don't apply to self-damage") and catches future call sites
 *        that might pass non-NULL placeholder points for these MODs.
 */
qboolean vg_Hitbox_IsSelfDamageMod(meansOfDeath_t mod);

/**
 * @brief Phase 13 (v0.7.2.1): splash-damage MOD detection. Returns
 *        qtrue for explosion-origin MODs (grenades, Panzerfaust,
 *        rifle-grenades, dynamite, satchel, mortar, airstrike,
 *        landmines). Used by g_combat.c strict-rejection bypass at
 *        line ~2040 — splash hits resolve to IMPACTPOINT_UNUSED
 *        because the explosion centre is outside the player volume,
 *        and would otherwise be rejected by strict-mode → 0 damage.
 *        Distinct from vg_Hitbox_IsSelfDamageMod (Phase 8.0a) which
 *        handles MODs with NULL points filtered earlier.
 */
qboolean vg_Hitbox_IsSplashMod(meansOfDeath_t mod);

/**
 * @brief Phase 13 combined predicate — true if the MOD should bypass
 *        the strict-mode capsule rejection. Currently
 *        IsSelfDamageMod || IsSplashMod. Future MOD-class bypass
 *        additions should extend this helper.
 */
qboolean vg_Hitbox_IsBypassMod(meansOfDeath_t mod);

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

/* ------------------------------------------------------------------ */
/* Netcode profile — cup-vs-public preset surface.                    */
/*                                                                    */
/* Phase 7.2: introduces vanguard_netcode_profile, a CVAR_LATCH cvar  */
/* whose value at G_InitGame time decides whether to apply a "cup"    */
/* preset (bumps sv_fps to 40, asserts g_antilag/g_antiwarp on),      */
/* leave engine defaults alone ("public"), or hand the wheel back     */
/* entirely to the admin's server.cfg ("custom").                     */
/* ------------------------------------------------------------------ */

/**
 * @brief Register the vanguard_netcode_profile cvar and apply the
 *        chosen profile's overrides exactly once per map. Call from
 *        G_InitGame, after the engine has finished its own cvar
 *        registration so trap_Cvar_Set on sv_fps actually takes
 *        effect on hosts that allow it.
 */
void vg_Netcode_Init(void);

/**
 * @brief Tear down for symmetry with Init. No-op currently —
 *        vmCvars are static. Call from G_ShutdownGame.
 */
void vg_Netcode_Shutdown(void);

/**
 * @brief Returns the active profile name as a literal string:
 *        "cup", "public", or "custom". Used by the server-info
 *        path so cgame can eventually surface the active profile
 *        in HUD / disclaimer (Phase 7.4 territory).
 */
const char *vg_Netcode_ProfileName(void);

/* ------------------------------------------------------------------ */
/* vg_fun mode + helper API (Phase 9.0 foundation, v0.7.0).           */
/*                                                                    */
/* Master-switch + helper API for cup-orthodox vs fun-public modes.   */
/* v0.7.0 ships infrastructure only; first controlled feature         */
/* (Falldamage, Phase 8.0b) lands in v0.7.1.                          */
/*                                                                    */
/* The master cvar `vg_fun` is registered through gameCvarTable in    */
/* g_cvars.c (CVAR_LATCH | CVAR_ARCHIVE | CVAR_SERVERINFO, default 0).*/
/* ------------------------------------------------------------------ */

extern vmCvar_t vg_fun;

/**
 * @brief Initialise the vg_fun subsystem. Reads vg_fun.integer (set
 *        from cvarTable), emits the boot-line (`VG_Fun: mode=...`).
 *        Call from G_InitGame AFTER vg_Netcode_Init so the mode line
 *        appears in chronological log order with the other VG_*
 *        subsystem boot lines.
 */
void vg_Fun_Init(void);

/**
 * @brief Print the full vg_fun status (mode + registered features +
 *        per-feature cup-defaults). Backs the `vg_status` server
 *        console command. Mirror of WG_PrintStartupBanner in spirit
 *        but renders the registry table instead of a fixed banner.
 */
void vg_Fun_PrintStatus(void);

/**
 * @brief Helper API — Strategy C lock-mechanism (Phase 9.0 audit §1).
 *        ALL `vg_fun_*` cvar reads MUST go through these helpers,
 *        never `.integer` / `.value` direct. CI grep enforces this
 *        at PR review (see audit §9 discipline gate).
 *
 *        Returns the cup_default if vg_fun=0; the cvar value if
 *        vg_fun=1. The cvar is auto-registered on first read so
 *        ad-hoc lookups don't require pre-registration via
 *        vg_Fun_RegisterCvar (though the registry path is preferred
 *        for introspection).
 */
int   vg_Fun_GetInt(const char *cvar_name, int cup_default);
float vg_Fun_GetFloat(const char *cvar_name, float cup_default);

/**
 * @brief Pattern γ — feature self-registration. Subsystems call this
 *        at their own Init time to register a sub-cvar with the
 *        introspection registry. Pre-registers the cvar with the
 *        engine using the supplied default + flags so admins can
 *        `\set vg_fun_<feature>_<param> X` immediately.
 *        v0.7.0 ships zero callers; v0.7.1 Falldamage is the first.
 */
void vg_Fun_RegisterCvar(const char *name, const char *cup_default, int cvar_flags);

/**
 * @brief Mode query — fast path for log-line tagging and similar
 *        hot uses. Equivalent to `(vg_fun.integer != 0)` but reads
 *        more cleanly at call sites.
 */
qboolean vg_Fun_IsActive(void);

/**
 * @brief Returns the active mode name as a literal string: "cup"
 *        (vg_fun=0) or "fun" (vg_fun=1). Used by stats log-tagging
 *        sites in g_combat.c / g_client.c / g_main.c so future
 *        leaderboard endpoints can bucket events per mode.
 */
const char *vg_Fun_ModeString(void);

#endif /* VANGUARD_G_VANGUARD_H */
