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
	float alpha;
	int   i;

	/* Server-authority gate: nothing renders unless vanguard_dev=1
	 * is published in the serverinfo configstring. */
	if (!cgs.vanguardDev)
	{
		return;
	}

	/* Client filter. Treat any non-zero value as "on" — the cvar is
	 * effectively binary; the legacy 0/1/2 schema is left tolerant for
	 * configs floating around with mode=2. */
	if (cg_vanguardDevHitboxes.integer == 0)
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

		vg_DrawPlayerHitboxes(i, alpha);
	}
}
