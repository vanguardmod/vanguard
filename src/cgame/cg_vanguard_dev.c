/*
 * cg_vanguard_dev.c — VanguardMod client-side dev-mode hitbox renderer.
 *
 * The original implementation drove hitbox visualisation through the
 * server's g_debugPlayerHitboxes path, which broadcasts EV_RAILTRAIL
 * events at 24+ events per visible player per frame. With even two
 * players in view the snapshot saturation pushed observed pings from
 * ~30ms to ~900ms — non-viable for actual hitbox tuning.
 *
 * This module replaces that with a purely local renderer:
 *   - server only publishes vanguard_dev (CVAR_SERVERINFO) so the
 *     client knows it's authorised
 *   - per frame, this code reads the existing player snapshot data
 *     (cg_entities[].lerpOrigin/lerpAngles, currentState.eFlags) and
 *     draws wire boxes locally via CG_AddLineToScene
 *
 * Network impact: zero. The server continues to send the same player
 * snapshots as in vanguard_dev=0; the only added bytes are the
 * configstring chars for "vanguard_dev=1".
 *
 * The head- and leg-box origin math is ported from G_BuildHead /
 * G_BuildLeg in src/game/g_combat.c (the no-tag, no-MDX fallback).
 * For the local player we have the full playerState_t and pmext, so
 * the boxes match the server's antilag picks exactly. For other
 * players we synthesize viewheight from EF_* flags using BG public
 * constants — pmove sets ps.viewheight to exactly these values, so
 * the result is identical except for one edge: prone-crawling
 * players where pmext.proneLegsOffset is non-zero. That offset is
 * approximated as 0 for non-local players (a stationary prone target
 * matches the server perfectly; a crawling one is close).
 */

#include "cg_local.h"
#include "cg_vanguard_dev.h"

/* ================================================================== */
/* Constants                                                          */
/* ================================================================== */

/* Body footprint. Mirrors the upstream playerMins / playerMaxs in
 * src/game/g_client.c:55-56 — server-only globals we inline because
 * they are never modified at runtime. */
static const vec3_t vg_BodyMins = { -18.0f, -18.0f, -24.0f };
static const float  vg_BodyMaxsX = 18.0f;
static const float  vg_BodyMaxsY = 18.0f;

/* Body-box top per stance. Mirrors what pmove writes into ent->r.maxs[2]
 * (bg_pmove.c) plus DEAD_BODYHEIGHT_BBOX from the death path in
 * g_client.c:955. Constants from bg_public.h. */
#define VG_BODY_TOP_STANDING   48.0f                  /* upstream playerMaxs[2] */
#define VG_BODY_TOP_CROUCHING  ((float)CROUCH_BODYHEIGHT)
#define VG_BODY_TOP_PRONE      ((float)PRONE_BODYHEIGHT_BBOX)
#define VG_BODY_TOP_DEAD       ((float)DEAD_BODYHEIGHT_BBOX)

/* Head-box bounds. Mirror G_BuildHead's no-MDX fallback path
 * (g_combat.c:955-956). */
static const vec3_t vg_HeadMins = { -6.0f, -6.0f, -2.0f };
static const vec3_t vg_HeadMaxs = {  6.0f,  6.0f, 10.0f };

/* Colour palette. Matches the HITBOXBIT colour decode the
 * server-driven railtrail filter used to do, so visuals are
 * unchanged from the user's perspective. */
static const vec3_t vg_ColorBody = { 1.0f, 1.0f, 0.0f }; /* yellow */
static const vec3_t vg_ColorHead = { 1.0f, 0.0f, 0.0f }; /* red    */
static const vec3_t vg_ColorLegs = { 0.0f, 1.0f, 0.0f }; /* green  */

/* ================================================================== */
/* Multi-region wireframe rendering (Phase 6.x)                       */
/* ================================================================== */

/* Hit-area shape kinds — match the on-server primitive selection in
 * mdx_hit_test (g_mdx.c:2843+). Box2 / Cylinder are 2-tag primitives
 * with the local Z axis pointing from bone1 to bone2; Sphere is a
 * 1-tag primitive (no axis). */
typedef enum
{
	VG_SHAPE_SPHERE,
	VG_SHAPE_CYLINDER,
	VG_SHAPE_BOX2
} vg_shape_t;

typedef struct
{
	const char *bone1;
	const char *bone2;        /* NULL for sphere */
	vg_shape_t  shape;
	vec3_t      scale1;       /* radii or half-extents at bone1 end */
	vec3_t      scale2;       /* same at bone2 end (zero for sphere) */
	int         impactpoint;  /* IMPACTPOINT_* — used for hit-highlight match */
	vec3_t      color;        /* RGB per region, alpha applied at render time */
} vg_hit_area_t;

/* Mirrors etmain/animations/human_base.hit (Pass 1+2 retune). Keep
 * this table in sync manually on each .hit retune; until we codegen
 * from .hit at build time, drift between client and server here is
 * the cost of a quick visualisation. */
static const vg_hit_area_t vg_hit_areas[] = {
	/* HEAD — sphere radius 6 on Bip01 Head */
	{ "Bip01 Head",       NULL,                VG_SHAPE_SPHERE,
	  { 6, 6, 6 }, { 0, 0, 0 }, IMPACTPOINT_HEAD,
	  { 1.0f, 0.2f, 0.2f } },                                /* red */

	/* CHEST — box2 Spine1 -> Neck */
	{ "Bip01 Spine1",     "Bip01 Neck",        VG_SHAPE_BOX2,
	  { 9, 7, 5 }, { 9, 7, 5 }, IMPACTPOINT_CHEST,
	  { 1.0f, 1.0f, 0.2f } },                                /* yellow */

	/* GUT — box2 Pelvis -> Spine2 */
	{ "Bip01 Pelvis",     "Bip01 Spine2",      VG_SHAPE_BOX2,
	  { 9, 7, 5 }, { 9, 7, 5 }, IMPACTPOINT_GUT,
	  { 1.0f, 0.6f, 0.2f } },                                /* orange */

	/* GROIN — sphere radius 7 on Pelvis */
	{ "Bip01 Pelvis",     NULL,                VG_SHAPE_SPHERE,
	  { 7, 7, 7 }, { 0, 0, 0 }, IMPACTPOINT_GROIN,
	  { 1.0f, 0.4f, 0.6f } },                                /* pink */

	/* LEFT SHOULDER — cylinder Clavicle -> UpperArm */
	{ "Bip01 L Clavicle", "Bip01 L UpperArm",  VG_SHAPE_CYLINDER,
	  { 5, 5, 5 }, { 5, 5, 5 }, IMPACTPOINT_SHOULDER_LEFT,
	  { 0.4f, 0.6f, 1.0f } },                                /* blue */

	/* RIGHT SHOULDER */
	{ "Bip01 R Clavicle", "Bip01 R UpperArm",  VG_SHAPE_CYLINDER,
	  { 5, 5, 5 }, { 5, 5, 5 }, IMPACTPOINT_SHOULDER_RIGHT,
	  { 0.4f, 0.6f, 1.0f } },                                /* blue */

	/* LEFT KNEE — cylinder Thigh -> Calf */
	{ "Bip01 L Thigh",    "Bip01 L Calf",      VG_SHAPE_CYLINDER,
	  { 6, 6, 6 }, { 6, 6, 6 }, IMPACTPOINT_KNEE_LEFT,
	  { 0.4f, 1.0f, 0.4f } },                                /* green */

	/* RIGHT KNEE */
	{ "Bip01 R Thigh",    "Bip01 R Calf",      VG_SHAPE_CYLINDER,
	  { 6, 6, 6 }, { 6, 6, 6 }, IMPACTPOINT_KNEE_RIGHT,
	  { 0.4f, 1.0f, 0.4f } },                                /* green */

	/* LEFT LEG (calf->foot, shares IMPACTPOINT_LEGS with right) */
	{ "Bip01 L Calf",     "Bip01 L Foot",      VG_SHAPE_CYLINDER,
	  { 6, 6, 6 }, { 6, 6, 6 }, IMPACTPOINT_LEGS,
	  { 0.2f, 1.0f, 0.8f } },                                /* cyan */

	/* RIGHT LEG */
	{ "Bip01 R Calf",     "Bip01 R Foot",      VG_SHAPE_CYLINDER,
	  { 6, 6, 6 }, { 6, 6, 6 }, IMPACTPOINT_LEGS,
	  { 0.2f, 1.0f, 0.8f } }                                 /* cyan */
};
#define VG_HIT_AREA_COUNT ((int)(sizeof(vg_hit_areas) / sizeof(vg_hit_areas[0])))

/**
 * @brief Build a minimal refEntity_t suitable for trap_R_LerpTag bone
 *        resolution. Pulls the player's current animation frame data
 *        from cent->pe (which CG_RunLerpFrameRate populates each
 *        frame), and sets origin/axis from the snapshot lerp values.
 *
 *        This is a reduced version of CG_Player's body refent setup
 *        (cg_players.c:CG_PlayerAnimation + CG_PlayerAngles). For
 *        diagnostic visualisation the simplification is acceptable —
 *        bone positions match within the precision of one player
 *        snapshot interpolation step.
 *
 * @return qfalse if the entity has no character / mdxFile (cannot
 *         resolve bones at all), else qtrue.
 */
static qboolean vg_BuildBodyRefent(const centity_t *cent, refEntity_t *body)
{
	const clientInfo_t   *ci;
	const bg_character_t *character;
	int                   clientNum;

	clientNum = cent->currentState.clientNum;
	if (clientNum < 0 || clientNum >= MAX_CLIENTS)
	{
		return qfalse;
	}

	ci = &cgs.clientinfo[clientNum];
	if (!ci->infoValid)
	{
		return qfalse;
	}

	character = CG_CharacterForClientinfo((clientInfo_t *)ci,
	                                      (centity_t *)cent);
	if (!character || !character->animModelInfo ||
	    !character->animModelInfo->animations[0])
	{
		return qfalse;
	}

	memset(body, 0, sizeof(*body));

	/* hModel is the MDM mesh; the engine's R_LerpTag
	 * dispatches via refent->hModel through R_GetModelByHandle,
	 * not via frameModel — without hModel the lookup hits the
	 * placeholder model and returns -1 for every tag. v0.3.6/.7
	 * left this unset; v0.3.8a probe + Strategy II both need
	 * it set. (cg_players.c:2969 / :3348 do the same.) */
	body->hModel             = character->mesh;

	body->frame              = cent->pe.legs.frame;
	body->oldframe           = cent->pe.legs.oldFrame;
	body->backlerp           = cent->pe.legs.backlerp;
	body->frameModel         = cent->pe.legs.frameModel;
	body->oldframeModel      = cent->pe.legs.oldFrameModel;
	body->torsoFrame         = cent->pe.torso.frame;
	body->oldTorsoFrame      = cent->pe.torso.oldFrame;
	body->torsoBacklerp      = cent->pe.torso.backlerp;
	body->torsoFrameModel    = cent->pe.torso.frameModel;
	body->oldTorsoFrameModel = cent->pe.torso.oldFrameModel;

	/* Fall back to animation[0] mdxFile if the per-frame model isn't
	 * set yet (first frame after spawn). */
	if (!body->frameModel)
	{
		body->frameModel    = character->animModelInfo->animations[0]->mdxFile;
		body->oldframeModel = body->frameModel;
	}
	if (!body->torsoFrameModel)
	{
		body->torsoFrameModel    = body->frameModel;
		body->oldTorsoFrameModel = body->frameModel;
	}

	VectorCopy(cent->lerpOrigin, body->origin);
	AnglesToAxis(cent->lerpAngles, body->axis);
	AxisCopy(body->axis, body->torsoAxis);

	return qtrue;
}

/**
 * @brief Look up a bone's world-space origin by name.
 *
 *        trap_R_LerpTag iterates both the .mdm tag list and the .mdx
 *        skeleton bones, so passing "Bip01 Head" resolves directly to
 *        the bone position in the parent refent's local frame. The
 *        canonical world transform pattern is from
 *        cg_ents.c:CG_PositionEntityOnTag.
 *
 * @return qfalse if the bone is not in the model.
 */
static qboolean vg_GetBoneOrigin(const refEntity_t *body, const char *bone,
                                 vec3_t outWorld)
{
	/* VG_DIAG (v0.3.7, Phase 6.x bug-hunt): if trap_R_LerpTag fails for
	 * a bone-name, log it once per second per name. v0.3.6 live-test on
	 * Pterodactyl showed only one of the ten multi-region capsules ever
	 * rendered, despite a render-loop with no highlight gating — the
	 * suspected cause is that the .mdm tag-list does not export the
	 * MDX skeleton bones ("Bip01 *") under those names. This print
	 * surfaces the failing bone + refEntity context so v0.3.8 can fix
	 * the real lookup path. Remove once the fix lands. Throttle uses
	 * pointer-equality on `bone` because the caller always passes
	 * pointers from vg_hit_areas[] (stable string literals). */
	static int         vg_diag_lastPrintTime[16];
	static const char *vg_diag_recentBones[16];
	int                slot;
	orientation_t      lerped;
	int                i;

	if (trap_R_LerpTag(&lerped, body, bone, 0) < 0)
	{
		slot = -1;
		for (i = 0; i < 16; i++)
		{
			if (vg_diag_recentBones[i] == bone)
			{
				slot = i;
				break;
			}
		}
		if (slot < 0)
		{
			for (i = 0; i < 16; i++)
			{
				if (!vg_diag_recentBones[i])
				{
					vg_diag_recentBones[i] = bone;
					slot                   = i;
					break;
				}
			}
		}
		if (slot >= 0 && cg.time - vg_diag_lastPrintTime[slot] > 1000)
		{
			CG_Printf("VG_DIAG: vg_GetBoneOrigin FAIL bone='%s' "
			          "frameModel=%d frame=%d torsoFrame=%d\n",
			          bone, (int)body->frameModel,
			          body->frame, body->torsoFrame);
			vg_diag_lastPrintTime[slot] = cg.time;
		}
		return qfalse;
	}

	VectorCopy(body->origin, outWorld);
	for (i = 0; i < 3; i++)
	{
		VectorMA(outWorld, lerped.origin[i], body->axis[i], outWorld);
	}
	return qtrue;
}

/**
 * @brief Build an orthonormal frame [u, v, axis] given an arbitrary
 *        unit vector axis. Used to orient cylinder caps and box2
 *        cross-sections perpendicular to the bone-to-bone direction.
 */
static void vg_BuildPerpFrame(const vec3_t axis, vec3_t u, vec3_t v)
{
	vec3_t ref;

	/* Pick a reference axis non-parallel to `axis`. World-up works
	 * unless axis IS world-up, in which case use world-forward. */
	if (Q_fabs(axis[2]) > 0.9f)
	{
		VectorSet(ref, 1.0f, 0.0f, 0.0f);
	}
	else
	{
		VectorSet(ref, 0.0f, 0.0f, 1.0f);
	}

	CrossProduct(axis, ref, u);
	VectorNormalize(u);

	CrossProduct(axis, u, v);
	VectorNormalize(v);
}

/**
 * @brief Wire-sphere via three orthogonal great circles (XY, XZ, YZ).
 *        8 segments per circle = 24 line draws total per sphere.
 */
static void vg_DrawWireSphere(const vec3_t origin, float radius,
                              const vec3_t color, float alpha)
{
	const int segs = 8;
	vec4_t    rgba;
	int       plane, i;
	vec3_t    prev, next;
	float     a0, a1, c0, s0, c1, s1;

	rgba[0] = color[0]; rgba[1] = color[1]; rgba[2] = color[2];
	rgba[3] = alpha;

	for (plane = 0; plane < 3; plane++)
	{
		/* plane 0: XY (Z fixed)  plane 1: XZ (Y fixed)  plane 2: YZ (X fixed) */
		for (i = 0; i < segs; i++)
		{
			a0 = ((float)i        / (float)segs) * (float)(2.0 * M_PI);
			a1 = ((float)(i + 1)  / (float)segs) * (float)(2.0 * M_PI);
			c0 = cos(a0); s0 = sin(a0);
			c1 = cos(a1); s1 = sin(a1);

			VectorCopy(origin, prev);
			VectorCopy(origin, next);

			if (plane == 0) {
				prev[0] += c0 * radius; prev[1] += s0 * radius;
				next[0] += c1 * radius; next[1] += s1 * radius;
			} else if (plane == 1) {
				prev[0] += c0 * radius; prev[2] += s0 * radius;
				next[0] += c1 * radius; next[2] += s1 * radius;
			} else {
				prev[1] += c0 * radius; prev[2] += s0 * radius;
				next[1] += c1 * radius; next[2] += s1 * radius;
			}
			CG_AddLineToScene(prev, next, rgba);
		}
	}
}

/**
 * @brief Wire-cylinder/cone between two bone positions. Two ring caps
 *        (8 segments each = 16 lines) plus 4 vertical edges connecting
 *        the caps at 0/90/180/270 degrees = 20 line draws total.
 *        Tapered when r1 != r2 (cone-frustum).
 */
static void vg_DrawWireCylinder(const vec3_t o1, const vec3_t o2,
                                float r1, float r2,
                                const vec3_t color, float alpha)
{
	const int segs = 8;
	vec4_t    rgba;
	vec3_t    axis, u, v;
	vec3_t    p1prev, p1next, p2prev, p2next;
	float     a0, a1, c0, s0, c1, s1;
	int       i;

	rgba[0] = color[0]; rgba[1] = color[1]; rgba[2] = color[2];
	rgba[3] = alpha;

	VectorSubtract(o2, o1, axis);
	if (VectorNormalize(axis) < 0.001f)
	{
		return; /* degenerate — bones overlap */
	}
	vg_BuildPerpFrame(axis, u, v);

	for (i = 0; i < segs; i++)
	{
		a0 = ((float)i        / (float)segs) * (float)(2.0 * M_PI);
		a1 = ((float)(i + 1)  / (float)segs) * (float)(2.0 * M_PI);
		c0 = cos(a0); s0 = sin(a0);
		c1 = cos(a1); s1 = sin(a1);

		/* Bottom cap segment */
		VectorMA(o1, c0 * r1, u, p1prev);
		VectorMA(p1prev, s0 * r1, v, p1prev);
		VectorMA(o1, c1 * r1, u, p1next);
		VectorMA(p1next, s1 * r1, v, p1next);
		CG_AddLineToScene(p1prev, p1next, rgba);

		/* Top cap segment */
		VectorMA(o2, c0 * r2, u, p2prev);
		VectorMA(p2prev, s0 * r2, v, p2prev);
		VectorMA(o2, c1 * r2, u, p2next);
		VectorMA(p2next, s1 * r2, v, p2next);
		CG_AddLineToScene(p2prev, p2next, rgba);

		/* 4 verticals every 2 segments (at 0/90/180/270 = i 0,2,4,6) */
		if ((i & 1) == 0)
		{
			CG_AddLineToScene(p1prev, p2prev, rgba);
		}
	}
}

/**
 * @brief Wire-box2 between two bone positions. Both endpoints have
 *        their own cross-section (scale1 / scale2), so this is a
 *        possibly-tapered "frustum-of-rectangle" outline. Bottom face
 *        4 edges + top face 4 edges + 4 verticals = 12 lines.
 *        scale[0] = u-extent, scale[1] = v-extent at each end.
 */
static void vg_DrawWireBox2(const vec3_t o1, const vec3_t o2,
                            const vec3_t scale1, const vec3_t scale2,
                            const vec3_t color, float alpha)
{
	vec4_t rgba;
	vec3_t axis, u, v;
	vec3_t bottom[4], top[4];
	int    i;
	static const float corners[4][2] = {
		{ -1, -1 }, {  1, -1 }, {  1,  1 }, { -1,  1 }
	};

	rgba[0] = color[0]; rgba[1] = color[1]; rgba[2] = color[2];
	rgba[3] = alpha;

	VectorSubtract(o2, o1, axis);
	if (VectorNormalize(axis) < 0.001f)
	{
		return;
	}
	vg_BuildPerpFrame(axis, u, v);

	for (i = 0; i < 4; i++)
	{
		VectorMA(o1, corners[i][0] * scale1[0], u, bottom[i]);
		VectorMA(bottom[i], corners[i][1] * scale1[1], v, bottom[i]);

		VectorMA(o2, corners[i][0] * scale2[0], u, top[i]);
		VectorMA(top[i], corners[i][1] * scale2[1], v, top[i]);
	}

	for (i = 0; i < 4; i++)
	{
		CG_AddLineToScene(bottom[i], bottom[(i + 1) & 3], rgba);
		CG_AddLineToScene(top[i],    top[(i + 1) & 3],    rgba);
		CG_AddLineToScene(bottom[i], top[i],              rgba);
	}
}

/**
 * @brief Render all 10 multi-region capsules for a single player slot.
 */
static void vg_DrawPlayerMultibox(int clientNum, float alpha)
{
	const centity_t *cent = &cg_entities[clientNum];
	refEntity_t      body;
	vec3_t           o1, o2;
	int              i;

	if (!vg_BuildBodyRefent(cent, &body))
	{
		return;
	}

	/* VG_DIAG (v0.3.8a, Strategy II prep): probe the MDM tag list
	 * once per client. Each candidate is a tag we plan to use as an
	 * anchor when remapping vg_hit_areas[] in v0.3.8 — see
	 * docs/notes/CGAME_BONE_CALC_RECON.md §5.II. result >= 0 means
	 * the tag exists on this player's MDM and the lerp produced a
	 * valid origin in body-local space; result == -1 means the tag
	 * is not exported. Remove this whole block in v0.3.8 once the
	 * remap lands. */
	{
		static qboolean         vg_diag_probed[MAX_CLIENTS];
		static const char *const vg_diag_candidate_tags[] = {
			"tag_head",
			"tag_chest",
			"tag_torso",
			"tag_back",
			"tag_armleft",
			"tag_armright",
			"tag_legleft",
			"tag_legright",
			"tag_footleft",
			"tag_footright",
			"tag_ubelt",
			"tag_weapon",
			"tag_weapon2",
			"tag_mouth",
			"tag_bipod",
			NULL
		};

		if (clientNum >= 0 && clientNum < MAX_CLIENTS &&
		    !vg_diag_probed[clientNum])
		{
			int           t;
			orientation_t lerped;
			int           result;

			CG_Printf("VG_DIAG: MDM tag probe client=%d "
			          "hModel=%d frameModel=%d frame=%d\n",
			          clientNum, (int)body.hModel,
			          (int)body.frameModel, body.frame);
			for (t = 0; vg_diag_candidate_tags[t]; t++)
			{
				result = trap_R_LerpTag(&lerped, &body,
				                        vg_diag_candidate_tags[t], 0);
				CG_Printf("VG_DIAG:   '%s' result=%d "
				          "origin=(%.1f,%.1f,%.1f)\n",
				          vg_diag_candidate_tags[t], result,
				          lerped.origin[0], lerped.origin[1],
				          lerped.origin[2]);
			}
			vg_diag_probed[clientNum] = qtrue;
		}
	}

	for (i = 0; i < VG_HIT_AREA_COUNT; i++)
	{
		const vg_hit_area_t *area = &vg_hit_areas[i];

		if (!vg_GetBoneOrigin(&body, area->bone1, o1))
		{
			continue;
		}

		switch (area->shape)
		{
		case VG_SHAPE_SPHERE:
			vg_DrawWireSphere(o1, area->scale1[0], area->color, alpha);
			break;

		case VG_SHAPE_CYLINDER:
			if (!vg_GetBoneOrigin(&body, area->bone2, o2))
			{
				break;
			}
			vg_DrawWireCylinder(o1, o2,
			                    area->scale1[0], area->scale2[0],
			                    area->color, alpha);
			break;

		case VG_SHAPE_BOX2:
			if (!vg_GetBoneOrigin(&body, area->bone2, o2))
			{
				break;
			}
			vg_DrawWireBox2(o1, o2,
			                area->scale1, area->scale2,
			                area->color, alpha);
			break;
		}
	}
}

/* ================================================================== */
/* Helpers                                                            */
/* ================================================================== */

/**
 * @brief Skip-self gate. The local client's own player slot must NOT
 *        render in first-person (the box would float invisibly in
 *        front of the camera), but MUST render in third-person and
 *        in any spectator-follow mode (where ps.clientNum names the
 *        target being watched, not us).
 */
static qboolean vg_ShouldSkipSelf(int clientNum)
{
	if (clientNum != cg.snap->ps.clientNum)
	{
		return qfalse;
	}
	if (cg.snap->ps.pm_flags & PMF_FOLLOW)
	{
		return qfalse;
	}
	if (cg.renderingThirdPerson)
	{
		return qfalse;
	}
	return qtrue;
}

/**
 * @brief Pick the correct body-box top for a given stance. eFlags
 *        precedence is dead > prone > crouching > standing — same
 *        as the server's ClientHitboxMaxZ effective ordering.
 */
static float vg_BodyMaxZForFlags(int eFlags)
{
	if (eFlags & EF_DEAD)      { return VG_BODY_TOP_DEAD;      }
	if (eFlags & EF_PRONE)     { return VG_BODY_TOP_PRONE;     }
	if (eFlags & EF_CROUCHING) { return VG_BODY_TOP_CROUCHING; }
	return VG_BODY_TOP_STANDING;
}

/**
 * @brief Compute the head-box origin. Direct port of G_BuildHead's
 *        no-MDX fallback math (g_combat.c:980-1044). For the local
 *        player isLocal=qtrue lets us read the exact viewheight /
 *        crouchViewHeight / pm_flags from cg.predictedPlayerState;
 *        for others we synthesize them from eFlags via BG constants
 *        — pmove sets ps.viewheight to exactly these values, so
 *        the result matches.
 */
static void vg_ComputeHeadOrigin(const vec3_t origin, const vec3_t viewangles,
                                 int eFlags, qboolean isLocal,
                                 vec3_t outOrigin)
{
	float  height;
	float  dest;
	vec3_t angles, forward, right, up, v;
	int    viewheight;
	int    crouchViewHeight;
	int    pm_flags;

	if (isLocal)
	{
		viewheight       = cg.predictedPlayerState.viewheight;
		crouchViewHeight = cg.predictedPlayerState.crouchViewHeight;
		pm_flags         = cg.predictedPlayerState.pm_flags;
	}
	else
	{
		/* pmove unconditionally writes one of these four into
		 * ps.viewheight depending on stance. Same constants. */
		if      (eFlags & EF_DEAD)      { viewheight = DEAD_VIEWHEIGHT;    }
		else if (eFlags & EF_PRONE)     { viewheight = PRONE_VIEWHEIGHT;   }
		else if (eFlags & EF_CROUCHING) { viewheight = CROUCH_VIEWHEIGHT;  }
		else                            { viewheight = DEFAULT_VIEWHEIGHT; }
		crouchViewHeight = CROUCH_VIEWHEIGHT;
		pm_flags         = (eFlags & EF_CROUCHING) ? PMF_DUCKED : 0;
	}

	if (eFlags & EF_PRONE)
	{
		height = (float)(viewheight - 60);
	}
	else if (eFlags & EF_DEAD)
	{
		height = (float)(viewheight - 64);
	}
	else if (pm_flags & PMF_DUCKED)
	{
		height = (float)(crouchViewHeight - 12);
	}
	else
	{
		height = (float)viewheight;
	}

	VectorCopy(viewangles, angles);
	if (angles[PITCH] > 180.0f)
	{
		dest = (-360.0f + angles[PITCH]) * 0.75f;
	}
	else
	{
		dest = angles[PITCH] * 0.75f;
	}
	angles[PITCH] = dest;

	if (eFlags & EF_PRONE)
	{
		angles[PITCH] = -10.0f;
	}

	AngleVectors(angles, forward, right, up);
	if (eFlags & EF_PRONE)
	{
		VectorScale(forward, 24.0f, v);
	}
	else if (eFlags & EF_DEAD)
	{
		VectorScale(forward, -26.0f, v);
		VectorMA(v, 5.0f, right, v);
	}
	else
	{
		VectorScale(forward, 5.0f, v);
		VectorMA(v, 5.0f, right, v);
	}
	VectorMA(v, 18.0f, up, v);

	VectorAdd(v, origin, outOrigin);
	outOrigin[2] += height / 2.0f;
}

/**
 * @brief Compute the legs-box origin for a prone (or dead) player.
 *        Direct port of G_BuildLeg's no-MDX fallback (g_combat.c:1097-1114).
 *        For the local player we read pmext.proneLegsOffset for an
 *        exact match; for others we approximate it as 0 — visually
 *        correct for stationary prone targets, slightly off when
 *        crawling.
 */
static void vg_ComputeLegsOrigin(const vec3_t origin, const vec3_t viewangles,
                                 int eFlags, qboolean isLocal,
                                 vec3_t outOrigin)
{
	vec3_t flatforward;
	float  legsOffsetZ;

	AngleVectors(viewangles, flatforward, NULL, NULL);
	flatforward[2] = 0.0f;
	VectorNormalizeFast(flatforward);

	if (eFlags & EF_PRONE)
	{
		outOrigin[0] = origin[0] + flatforward[0] * -32.0f;
		outOrigin[1] = origin[1] + flatforward[1] * -32.0f;
	}
	else
	{
		outOrigin[0] = origin[0] + flatforward[0] * 32.0f;
		outOrigin[1] = origin[1] + flatforward[1] * 32.0f;
	}

	legsOffsetZ = isLocal ? cg.pmext.proneLegsOffset : 0.0f;
	outOrigin[2] = origin[2] + legsOffsetZ;
}

/**
 * @brief Issue 12 line draws to outline an axis-aligned wire box.
 *        Each call to CG_AddLineToScene writes a single-frame
 *        refEntity_t with RT_RAIL_CORE — no localEntity slot is
 *        consumed, so iterating all visible players per frame is
 *        cheap.
 */
static void vg_DrawWireBox(const vec3_t origin, const vec3_t mins, const vec3_t maxs,
                           const vec3_t color, float alpha)
{
	vec3_t a, b;
	vec3_t corners[8];
	vec4_t rgba;
	int    i;
	/* 12 edges of an AABB, indexed into corners[0..7] */
	static const int edges[12][2] = {
		{ 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },   /* bottom face */
		{ 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },   /* top face */
		{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }    /* verticals */
	};

	VectorAdd(origin, mins, a);
	VectorAdd(origin, maxs, b);

	/* corners[i] selects a or b component-wise based on bits of i */
	for (i = 0; i < 8; i++)
	{
		corners[i][0] = (i & 1) ? b[0] : a[0];
		corners[i][1] = (i & 2) ? b[1] : a[1];
		corners[i][2] = (i & 4) ? b[2] : a[2];
	}

	rgba[0] = color[0];
	rgba[1] = color[1];
	rgba[2] = color[2];
	rgba[3] = alpha;

	for (i = 0; i < 12; i++)
	{
		CG_AddLineToScene(corners[edges[i][0]], corners[edges[i][1]], rgba);
	}
}

/**
 * @brief Render body / head / legs boxes for a single player slot.
 */
static void vg_DrawPlayerHitboxes(int clientNum, float alpha)
{
	const centity_t *cent = &cg_entities[clientNum];
	qboolean         isLocal = (clientNum == cg.snap->ps.clientNum &&
	                            !(cg.snap->ps.pm_flags & PMF_FOLLOW)) ? qtrue : qfalse;
	const float     *origin;
	const float     *angles;
	int              eFlags;
	vec3_t           bodyMaxs;
	vec3_t           headOrigin;
	vec3_t           legsOrigin;

	/* For the predicted local player, lerpOrigin / lerpAngles are not
	 * authoritative — predicted state is. For everyone else the
	 * lerped snapshot data is what the renderer is showing the user. */
	if (isLocal)
	{
		origin = cg.predictedPlayerState.origin;
		angles = cg.predictedPlayerState.viewangles;
		eFlags = cg.predictedPlayerState.eFlags;
	}
	else
	{
		origin = cent->lerpOrigin;
		angles = cent->lerpAngles;
		eFlags = cent->currentState.eFlags;
	}

	/* --- body --- */
	bodyMaxs[0] = vg_BodyMaxsX;
	bodyMaxs[1] = vg_BodyMaxsY;
	if (isLocal && (eFlags & EF_CROUCHING))
	{
		/* Local player has the exact crouch top from pmove. */
		bodyMaxs[2] = cg.predictedPlayerState.crouchMaxZ;
	}
	else
	{
		bodyMaxs[2] = vg_BodyMaxZForFlags(eFlags);
	}
	vg_DrawWireBox(origin, vg_BodyMins, bodyMaxs, vg_ColorBody, alpha);

	/* --- head --- (skip wounded / dead, matching G_BuildHead behaviour) */
	if (!(eFlags & EF_DEAD))
	{
		vg_ComputeHeadOrigin(origin, angles, eFlags, isLocal, headOrigin);
		vg_DrawWireBox(headOrigin, vg_HeadMins, vg_HeadMaxs, vg_ColorHead, alpha);
	}

	/* --- legs --- (only when prone, matching G_BuildLeg) */
	if (eFlags & EF_PRONE)
	{
		vg_ComputeLegsOrigin(origin, angles, eFlags, isLocal, legsOrigin);
		vg_DrawWireBox(legsOrigin, playerlegsProneMins, playerlegsProneMaxs,
		               vg_ColorLegs, alpha);
	}
}

/* ================================================================== */
/* Public entry point                                                 */
/* ================================================================== */

void CG_VanguardDev_DrawHitboxes(void)
{
	float    alpha;
	int      i;
	qboolean drawAabb;
	qboolean drawMultibox;

	/* Server-authority gate: nothing renders unless vanguard_dev=1
	 * is published in the serverinfo configstring. */
	if (!cgs.vanguardDev)
	{
		return;
	}

	/* Two independent client filters — admin can render legacy AABB
	 * boxes alone, multi-region capsules alone, both, or neither. */
	drawAabb     = (cg_vanguardDevHitboxes.integer != 0) ? qtrue : qfalse;
	drawMultibox = (cg_vanguardDevMultibox.integer  != 0) ? qtrue : qfalse;

	if (!drawAabb && !drawMultibox)
	{
		return;
	}

	if (!cg.snap)
	{
		return;
	}

	alpha = cg_vanguardDevAlpha.value;
	if (alpha < 0.0f) { alpha = 0.0f; }
	if (alpha > 1.0f) { alpha = 1.0f; }

	for (i = 0; i < cgs.maxclients; i++)
	{
		const centity_t *cent = &cg_entities[i];

		if (!cent->currentValid)               { continue; }
		if (cent->currentState.eType != ET_PLAYER) { continue; }
		if (vg_ShouldSkipSelf(i))              { continue; }

		if (drawAabb)
		{
			vg_DrawPlayerHitboxes(i, alpha);
		}
		if (drawMultibox)
		{
			vg_DrawPlayerMultibox(i, alpha);
		}
	}
}
