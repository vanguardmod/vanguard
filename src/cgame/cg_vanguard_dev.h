/*
 * cg_vanguard_dev.h — VanguardMod client-side dev-mode renderer.
 *
 * Public API of the per-frame hitbox visualisation that runs entirely
 * on the client when the server is in dev mode (vanguard_dev=1). See
 * the .c file for the architectural rationale.
 */

#ifndef VANGUARD_CGAME_VANGUARD_DEV_H
#define VANGUARD_CGAME_VANGUARD_DEV_H

/**
 * @brief Per-frame entry. Renders wire boxes for every visible
 *        player when the server has vanguard_dev=1 and the local
 *        client has cg_vanguardDevHitboxes != 0. No-op otherwise.
 *        Call once per CG_DrawActiveFrame, after CG_AddPacketEntities
 *        (so lerpOrigin / lerpAngles are final for this frame).
 */
void CG_VanguardDev_DrawHitboxes(void);

#endif /* VANGUARD_CGAME_VANGUARD_DEV_H */
