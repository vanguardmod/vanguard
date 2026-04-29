/*
 * VanguardMod cgame MDX skeleton loader (Phase 6 Strategy I).
 *
 * Public interface used by cg_vanguard_dev.c. The implementation
 * (cg_vanguard_mdx.c) maintains a private registry of parsed MDX
 * files and exposes a single bone-position lookup that mirrors
 * qagame's mdx_calculate_bone_lerp semantics for the hitbox
 * visualisation. See docs/notes/CGAME_BONE_CALC_RECON.md and
 * docs/notes/STRATEGY_I_PORT_NOTES.md for the design rationale.
 */

#ifndef VANGUARD_CGAME_VANGUARD_MDX_H
#define VANGUARD_CGAME_VANGUARD_MDX_H

#include "../qcommon/q_shared.h"
#include "../renderercommon/tr_types.h"

/**
 * @brief Lazy-load the MDX file backing an engine model handle.
 *
 *        Resolves the handle to a file path via vg_FindMDXPath()
 *        (declared in bg_public.h, populated by bg_animgroup.c at
 *        animation registration time), opens the file via
 *        trap_FS_*, and parses the MDX into the loader's private
 *        registry. Subsequent calls with the same handle are no-ops.
 *
 * @return qtrue on first or repeat success, qfalse if the handle
 *         is unknown to the path-table, the file cannot be opened,
 *         the registry is full (VG_MDX_MAX = 16), or the MDX bytes
 *         are malformed.
 */
qboolean vg_mdx_register_for_handle(qhandle_t engineHandle);

/**
 * @brief Compute a bone's world-space origin for a player refent.
 *
 *        Walks the bone hierarchy upwards from boneName to the
 *        root, applying the per-frame offset_angles delta to each
 *        parent_dist along the way and lerping between current and
 *        previous frame using body->backlerp / torsoBacklerp as
 *        per the bone's torso_weight. The result is then transformed
 *        into world space via body->origin + body->axis.
 *
 *        Both legs (frameModel/oldframeModel) and torso
 *        (torsoFrameModel/oldTorsoFrameModel) MDX files are read
 *        for bones with non-zero torso_weight; the caller does not
 *        need to register them separately — this function does so
 *        on demand. If any of the four handles is missing from the
 *        registry, the function falls back to the legs MDX (matches
 *        the qagame QHANDLETOINDEX_SAFE behaviour).
 *
 * @param[in]  body      refEntity built by vg_BuildBodyRefent
 * @param[in]  boneName  exact .mdx skeleton bone name, e.g.
 *                       "Bip01 Head" (case-sensitive strcmp)
 * @param[out] outWorld  world-space origin on success
 *
 * @return qtrue on success, qfalse if the legs MDX is not loaded,
 *         the bone is not in the skeleton, the registered MDX files
 *         disagree on bone count, or per-frame data is corrupted.
 */
qboolean vg_mdx_compute_bone_world(const refEntity_t *body,
                                    const char *boneName,
                                    vec3_t outWorld);

/**
 * @brief Same as vg_mdx_compute_bone_world but additionally applies
 *        a bone-local-frame offset before the world transform.
 *
 *        Mirrors qagame's mdx_tag_orientation chain (g_mdx.c:1726-1727)
 *        where `vec3_rotate(tag->offset, tmpaxis, ...)` rotates the
 *        offset by the bone's local axis matrix and adds it to the
 *        bone's model-local origin. Used by the cgame visualisation
 *        to track _vg_head's `+6.5 Z` anchor: the human_base.hit
 *        TAG line declares the offset bone-local, the server applies
 *        it via mdx_tag_orientation, and this function lets the
 *        cgame wireframe visualisation match the same trace point.
 *
 *        offset = (0, 0, 0) is identical to vg_mdx_compute_bone_world.
 *
 * @param[in]  body            refEntity built by vg_BuildBodyRefent
 * @param[in]  boneName        exact .mdx skeleton bone name
 * @param[in]  boneLocalOffset offset in the bone's local frame
 * @param[out] outWorld        world-space origin (bone position + rotated offset)
 *
 * @return same failure cases as vg_mdx_compute_bone_world.
 */
qboolean vg_mdx_compute_bone_world_with_offset(const refEntity_t *body,
                                                const char *boneName,
                                                const vec3_t boneLocalOffset,
                                                vec3_t outWorld);

#endif /* VANGUARD_CGAME_VANGUARD_MDX_H */
