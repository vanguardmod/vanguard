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

#endif /* VANGUARD_G_VANGUARD_H */
