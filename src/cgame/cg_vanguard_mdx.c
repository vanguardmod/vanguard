/*
 * VanguardMod cgame MDX skeleton loader (Phase 6 Strategy I).
 *
 * Loads MDX files directly in cgame and computes bone positions
 * for the multi-region hitbox visualisation. Mirrors the origin-
 * only path of qagame's mdx_calculate_bone_lerp + mdx_load
 * (src/game/g_mdx.c) using the same binary file format.
 *
 * Why we need our own loader: the engine's R_LerpTag — the only
 * path cgame has for bone-aware lookups — exposes only entries
 * from the MDM tag-list, never the raw MDX skeleton bones by
 * name. Our hit-area definitions ("Bip01 Head", "Bip01 Spine1",
 * etc.) live in the .mdx skeleton, not the .mdm tag list, so
 * trap_R_LerpTag returned -1 for every one of them in v0.3.6
 * through v0.3.8a. This file solves that by parsing the .mdx
 * itself and walking the bone hierarchy with the same math the
 * server uses for hit-detection.
 *
 * The mesh-deformation math (quaternions, matrix lerp, axis
 * output) is intentionally NOT ported — we render wireframes at
 * known bone origins, so we only need positions, not orientations.
 *
 * See docs/notes/STRATEGY_I_PORT_NOTES.md for the binary format
 * recap and helper porting list.
 */

#include "cg_local.h"
#include "cg_vanguard_mdx.h"

/* sintable[4096] — looked up by AnglesToAxisBroken below. The
 * header is upstream-vendored and has no FEATURE_SERVERMDX gate
 * around the table itself, so including it here adds ~16 KB of
 * static const data to the cgame module — acceptable given the
 * alternative is shipping a parallel sin LUT or computing sin()
 * per bone per frame. */
#include "../game/g_mdx_lut.h"

#define VG_MDX_MAX 16

typedef struct vg_mdx_bone_s
{
	char  name[64];
	int   parent_index;
	float parent_dist;
	float torso_weight;
} vg_mdx_bone_t;

typedef struct vg_mdx_frame_bone_s
{
	short offset_angles[2];
} vg_mdx_frame_bone_t;

typedef struct vg_mdx_frame_s
{
	vec3_t               parent_offset;
	vg_mdx_frame_bone_t *bones;
} vg_mdx_frame_t;

typedef struct vg_mdx_model_s
{
	qhandle_t       engineHandle;
	char            path[MAX_QPATH];
	int             bone_count;
	int             frame_count;
	int             torso_parent;
	vg_mdx_bone_t  *bones;
	vg_mdx_frame_t *frames;
} vg_mdx_model_t;

static vg_mdx_model_t vg_mdx_models[VG_MDX_MAX];
static int            vg_mdx_count;
static qboolean       vg_mdx_warned_capacity;

/* Per-frame scratch: model-local bone positions accumulated by
 * the recursive bone-lerper. Sized to the maximum bone count we
 * have ever seen across registered MDX files; reused per call. */
static vec3_t *vg_mdx_scratch;
static int     vg_mdx_scratch_max;

/* ============================================================= */
/* Little-endian binary readers — direct ports of g_mdx.c:456+    */
/* ============================================================= */

static int vg_mdx_read_int(const byte *data)
{
	return (int)(((unsigned int)data[0])       |
	             ((unsigned int)data[1] <<  8) |
	             ((unsigned int)data[2] << 16) |
	             ((unsigned int)data[3] << 24));
}

static short vg_mdx_read_short(const byte *data)
{
	return (short)(data[0] | (data[1] << 8));
}

static float vg_mdx_read_vec(const byte *data)
{
	union { unsigned int u; float f; } cast;
	cast.u = (unsigned int)vg_mdx_read_int(data);
	return cast.f;
}

/* ============================================================= */
/* AnglesToAxisBroken — direct port of g_mdx.c:186                */
/* ============================================================= */

static void vg_mdx_AnglesToAxisBroken(const short angles[2], vec3_t matrix[3])
{
	int   idx;
	float sp, sy, cp, cy;

	idx = angles[0] >> 4;
	if (idx < 0)
	{
		idx += 4096;
	}
	sp = sintable[idx];
	cp = sintable[(idx + 1024) & 0x0FFF];

	idx = angles[1] >> 4;
	if (idx < 0)
	{
		idx += 4096;
	}
	sy = sintable[idx];
	cy = sintable[(idx + 1024) & 0x0FFF];

	matrix[0][0] = cp * cy;
	matrix[0][1] = cp * sy;
	matrix[0][2] = -sp;

	matrix[1][0] = -sy;
	matrix[1][1] = cy;
	matrix[1][2] = 0;

	matrix[2][0] = sp * cy;
	matrix[2][1] = sp * sy;
	matrix[2][2] = cp;
}

/* ============================================================= */
/* Per-bone local position from bind-pose + per-frame delta       */
/* ============================================================= */

static void vg_mdx_calculate_bone(vec3_t dest,
                                  const vg_mdx_bone_t *bone,
                                  const vg_mdx_frame_bone_t *frameBone)
{
	vec3_t tmp;
	vec3_t axis[3];

	tmp[0] = bone->parent_dist;
	tmp[1] = 0.0f;
	tmp[2] = 0.0f;
	vg_mdx_AnglesToAxisBroken(frameBone->offset_angles, axis);
	vec3_rotate(tmp, axis, dest);
}

/* ============================================================= */
/* Recursive bone-position lerper (model-local)                   */
/*                                                                */
/* Direct port of mdx_calculate_bone_lerp (g_mdx.c:1429), origin- */
/* only. Populates vg_mdx_scratch[0..i] up the parent chain.      */
/* ============================================================= */

static void vg_mdx_calculate_bone_lerp(vg_mdx_model_t *legsModel,
                                       vg_mdx_model_t *oldLegsModel,
                                       vg_mdx_model_t *torsoModel,
                                       vg_mdx_model_t *oldTorsoModel,
                                       const refEntity_t *body,
                                       int i,
                                       qboolean recursive)
{
	vg_mdx_model_t            *boneFrameModel;
	vg_mdx_model_t            *oldBoneFrameModel;
	int                        frame;
	int                        oldframe;
	float                      backlerp;
	const vg_mdx_bone_t       *bone;
	const vg_mdx_frame_bone_t *frameBone;
	const vg_mdx_frame_bone_t *oldFrameBone;
	vec3_t                     point;
	vec3_t                     oldpoint;
	float                      forwardlerp;

	/* legsModel is the canonical skeleton — its torso_weight values
	 * decide which (legs vs torso) frame data drives this bone.
	 * That matches the qagame impl where frameModel->bones[i] is
	 * read first, then boneFrameModel->bones[i] used for math. */
	if (legsModel->bones[i].torso_weight != 0.0f)
	{
		boneFrameModel    = torsoModel;
		oldBoneFrameModel = oldTorsoModel;
		frame             = body->torsoFrame;
		oldframe          = body->oldTorsoFrame;
		backlerp          = body->torsoBacklerp;
	}
	else
	{
		boneFrameModel    = legsModel;
		oldBoneFrameModel = oldLegsModel;
		frame             = body->frame;
		oldframe          = body->oldframe;
		backlerp          = body->backlerp;
	}

	/* Defence against stale or out-of-range snapshot frame indices.
	 * qagame doesn't bother because it builds the refent itself; we
	 * inherit cgame's snapshot lerp which can briefly index past the
	 * end during animation transitions. */
	if (frame < 0)                                  { frame = 0; }
	if (frame >= boneFrameModel->frame_count)       { frame = boneFrameModel->frame_count - 1; }
	if (oldframe < 0)                               { oldframe = 0; }
	if (oldframe >= oldBoneFrameModel->frame_count) { oldframe = oldBoneFrameModel->frame_count - 1; }

	bone = &boneFrameModel->bones[i];

	if (i == 0)
	{
		/* Root: lerp the per-frame parent_offset (the only data
		 * point that actually translates the skeleton in space). */
		forwardlerp = 1.0f - backlerp;
		VectorScale(boneFrameModel->frames[frame].parent_offset, forwardlerp, vg_mdx_scratch[i]);
		VectorMA(vg_mdx_scratch[i], backlerp,
		         oldBoneFrameModel->frames[oldframe].parent_offset, vg_mdx_scratch[i]);
		return;
	}

	if (recursive)
	{
		vg_mdx_calculate_bone_lerp(legsModel, oldLegsModel,
		                           torsoModel, oldTorsoModel,
		                           body, bone->parent_index, qtrue);
	}

	frameBone    = &boneFrameModel->frames[frame].bones[i];
	oldFrameBone = &oldBoneFrameModel->frames[oldframe].bones[i];

	vg_mdx_calculate_bone(point,    bone, frameBone);
	vg_mdx_calculate_bone(oldpoint, bone, oldFrameBone);

	/* mdx_bones[i] = mdx_bones[parent] + point + backlerp * (oldpoint - point) */
	VectorAdd(vg_mdx_scratch[bone->parent_index], point, vg_mdx_scratch[i]);
	VectorSubtract(oldpoint, point, oldpoint);
	VectorMA(vg_mdx_scratch[i], backlerp, oldpoint, vg_mdx_scratch[i]);
}

/* ============================================================= */
/* MDX file parser — direct port of mdx_load (g_mdx.c:540)        */
/*                                                                */
/* Binary layout per g_mdx.h:                                     */
/*   header (mdx_hdr): 96 bytes                                   */
/*     ident[4], version[4], filename[64], frame_count[4],        */
/*     bone_count[4], frame_offset[4], bone_offset[4],            */
/*     torso_parent[4], eof_offset[4]                             */
/*   bone[bone_count] at bone_offset, 80 bytes each:              */
/*     name[64], parent_index[4], torso_weight[4],                */
/*     parent_dist[4], is_tag[4]                                  */
/*   frame[frame_count] at frame_offset, (52 + 12*bone_count)     */
/*   bytes each:                                                  */
/*     mins[12], maxs[12], origin[12], radius[4],                 */
/*     parent_offset[12], frame_bone[bone_count]                  */
/*   frame_bone: 12 bytes (angles[6], unused[2], offset_angles[4])*/
/* ============================================================= */

#define VG_MDX_HDR_SIZE        96
#define VG_MDX_BONE_SIZE       80
#define VG_MDX_FRAME_HDR_SIZE  52
#define VG_MDX_FRAME_BONE_SIZE 12

static void vg_mdx_free_model(vg_mdx_model_t *m)
{
	int i;

	if (m->frames)
	{
		for (i = 0; i < m->frame_count; i++)
		{
			if (m->frames[i].bones)
			{
				free(m->frames[i].bones);
				m->frames[i].bones = NULL;
			}
		}
		free(m->frames);
		m->frames = NULL;
	}
	if (m->bones)
	{
		free(m->bones);
		m->bones = NULL;
	}
	m->bone_count  = 0;
	m->frame_count = 0;
}

static qboolean vg_mdx_parse(vg_mdx_model_t *out, const byte *mem, int len)
{
	int          frame_count;
	int          bone_count;
	int          frame_offset;
	int          bone_offset;
	const byte  *bones_raw;
	const byte  *frames_raw;
	int          i;
	int          j;

	if (len < VG_MDX_HDR_SIZE)                                                { return qfalse; }
	if (mem[0] != 'M' || mem[1] != 'D' || mem[2] != 'X' || mem[3] != 'W')     { return qfalse; }

	frame_count        = vg_mdx_read_int(mem + 72);
	bone_count         = vg_mdx_read_int(mem + 76);
	frame_offset       = vg_mdx_read_int(mem + 80);
	bone_offset        = vg_mdx_read_int(mem + 84);
	out->torso_parent  = vg_mdx_read_int(mem + 88);

	if (bone_count <= 0 || frame_count <= 0)                                  { return qfalse; }
	if (bone_offset < VG_MDX_HDR_SIZE  || bone_offset >= len)                 { return qfalse; }
	if (frame_offset < VG_MDX_HDR_SIZE || frame_offset >= len)                { return qfalse; }
	if (bone_offset + bone_count * VG_MDX_BONE_SIZE > len)                    { return qfalse; }

	out->bone_count  = bone_count;
	out->frame_count = frame_count;

	bones_raw  = mem + bone_offset;
	out->bones = (vg_mdx_bone_t *)malloc(bone_count * sizeof(*out->bones));
	if (!out->bones)                                                          { return qfalse; }
	for (i = 0; i < bone_count; i++)
	{
		const byte *b = bones_raw + i * VG_MDX_BONE_SIZE;

		Q_strncpyz(out->bones[i].name, (const char *)b, sizeof(out->bones[i].name));
		out->bones[i].parent_index = vg_mdx_read_int(b + 64);
		out->bones[i].torso_weight = vg_mdx_read_vec(b + 68);
		out->bones[i].parent_dist  = vg_mdx_read_vec(b + 72);

		if (out->bones[i].parent_index >= i)
		{
			/* Same constraint qagame G_Errors on (g_mdx.c:580):
			 * parent must be earlier in the array so the recursive
			 * accumulator works in single forward pass. We bail
			 * softly here and let the caller fall back — cgame is
			 * client-side and a corrupt MDX must not crash the game. */
			CG_Printf("VG_MDX: bone[%d].parent_index=%d invalid\n",
			          i, out->bones[i].parent_index);
			return qfalse;
		}
	}

	frames_raw  = mem + frame_offset;
	out->frames = (vg_mdx_frame_t *)malloc(frame_count * sizeof(*out->frames));
	if (!out->frames)                                                         { return qfalse; }
	memset(out->frames, 0, frame_count * sizeof(*out->frames));
	for (i = 0; i < frame_count; i++)
	{
		int         frame_stride = VG_MDX_FRAME_HDR_SIZE + bone_count * VG_MDX_FRAME_BONE_SIZE;
		const byte *fr           = frames_raw + i * frame_stride;
		const byte *fb;

		if ((fr - mem) + frame_stride > len)                                  { return qfalse; }

		/* parent_offset starts at byte 40 within the frame header
		 * (mins[12] + maxs[12] + origin[12] + radius[4] = 40). */
		out->frames[i].parent_offset[0] = vg_mdx_read_vec(fr + 40);
		out->frames[i].parent_offset[1] = vg_mdx_read_vec(fr + 44);
		out->frames[i].parent_offset[2] = vg_mdx_read_vec(fr + 48);

		out->frames[i].bones = (vg_mdx_frame_bone_t *)
		                       malloc(bone_count * sizeof(*out->frames[i].bones));
		if (!out->frames[i].bones)                                            { return qfalse; }

		fb = fr + VG_MDX_FRAME_HDR_SIZE;
		for (j = 0; j < bone_count; j++)
		{
			/* offset_angles starts at byte 8 within the frame_bone
			 * (angles[6] + unused[2] = 8). */
			const byte *oa = fb + j * VG_MDX_FRAME_BONE_SIZE + 8;

			out->frames[i].bones[j].offset_angles[0] = vg_mdx_read_short(oa);
			out->frames[i].bones[j].offset_angles[1] = vg_mdx_read_short(oa + 2);
		}
	}

	return qtrue;
}

/* ============================================================= */
/* Public API                                                     */
/* ============================================================= */

static vg_mdx_model_t *vg_mdx_get(qhandle_t engineHandle)
{
	int i;

	if (engineHandle == 0)                                                    { return NULL; }
	for (i = 0; i < vg_mdx_count; i++)
	{
		if (vg_mdx_models[i].engineHandle == engineHandle)
		{
			return &vg_mdx_models[i];
		}
	}
	return NULL;
}

qboolean vg_mdx_register_for_handle(qhandle_t engineHandle)
{
	const char     *path;
	fileHandle_t    fh = 0;
	int             len;
	byte           *buf = NULL;
	vg_mdx_model_t *slot;
	qboolean        ok;

	if (engineHandle == 0)                                                    { return qfalse; }
	if (vg_mdx_get(engineHandle) != NULL)                                     { return qtrue; }
	if (vg_mdx_count >= VG_MDX_MAX)
	{
		if (!vg_mdx_warned_capacity)
		{
			CG_Printf("VG_MDX: registry full (%d entries) — fallback path "
			          "will be used for further models\n", VG_MDX_MAX);
			vg_mdx_warned_capacity = qtrue;
		}
		return qfalse;
	}

	path = vg_FindMDXPath(engineHandle);
	if (!path)                                                                { return qfalse; }

	len = trap_FS_FOpenFile(path, &fh, FS_READ);
	if (len <= 0 || !fh)
	{
		if (fh)                                                               { trap_FS_FCloseFile(fh); }
		CG_Printf("VG_MDX: cannot open '%s' (handle=%d, len=%d)\n",
		          path, (int)engineHandle, len);
		return qfalse;
	}

	buf = (byte *)malloc(len);
	if (!buf)
	{
		trap_FS_FCloseFile(fh);
		return qfalse;
	}
	trap_FS_Read(buf, len, fh);
	trap_FS_FCloseFile(fh);

	slot = &vg_mdx_models[vg_mdx_count];
	memset(slot, 0, sizeof(*slot));
	slot->engineHandle = engineHandle;
	Q_strncpyz(slot->path, path, sizeof(slot->path));

	ok = vg_mdx_parse(slot, buf, len);
	free(buf);
	if (!ok)
	{
		vg_mdx_free_model(slot);
		memset(slot, 0, sizeof(*slot));
		CG_Printf("VG_MDX: parse failed for '%s'\n", path);
		return qfalse;
	}

	if (slot->bone_count > vg_mdx_scratch_max)
	{
		vec3_t *fresh = (vec3_t *)realloc(vg_mdx_scratch,
		                                  slot->bone_count * sizeof(vec3_t));
		if (!fresh)
		{
			vg_mdx_free_model(slot);
			memset(slot, 0, sizeof(*slot));
			CG_Printf("VG_MDX: scratch realloc failed (%d bones)\n",
			          slot->bone_count);
			return qfalse;
		}
		vg_mdx_scratch     = fresh;
		vg_mdx_scratch_max = slot->bone_count;
	}

	vg_mdx_count++;
	CG_Printf("VG_MDX: registered '%s' (handle=%d, %d bones, %d frames)\n",
	          path, (int)engineHandle, slot->bone_count, slot->frame_count);
	return qtrue;
}

static int vg_mdx_find_bone(const vg_mdx_model_t *mdx, const char *name)
{
	int i;

	for (i = 0; i < mdx->bone_count; i++)
	{
		if (!strcmp(mdx->bones[i].name, name))
		{
			return i;
		}
	}
	return -1;
}

qboolean vg_mdx_compute_bone_world(const refEntity_t *body,
                                    const char *boneName,
                                    vec3_t outWorld)
{
	vg_mdx_model_t *legs;
	vg_mdx_model_t *oldLegs;
	vg_mdx_model_t *torso;
	vg_mdx_model_t *oldTorso;
	int             boneIndex;
	int             k;

	if (!body || !boneName || !outWorld)                                      { return qfalse; }

	/* Lazy-register all four MDX handles attached to this refent.
	 * vg_mdx_register_for_handle is idempotent and skips already-
	 * registered handles. */
	vg_mdx_register_for_handle(body->frameModel);
	vg_mdx_register_for_handle(body->oldframeModel);
	vg_mdx_register_for_handle(body->torsoFrameModel);
	vg_mdx_register_for_handle(body->oldTorsoFrameModel);

	legs = vg_mdx_get(body->frameModel);
	if (!legs)                                                                { return qfalse; }

	oldLegs  = vg_mdx_get(body->oldframeModel);
	torso    = vg_mdx_get(body->torsoFrameModel);
	oldTorso = vg_mdx_get(body->oldTorsoFrameModel);

	/* Match qagame's QHANDLETOINDEX_SAFE fallback chain — if a
	 * secondary model failed to register (e.g. mid-anim load),
	 * substitute the next stable handle. */
	if (!oldLegs)                                                             { oldLegs  = legs; }
	if (!torso)                                                               { torso    = legs; }
	if (!oldTorso)                                                            { oldTorso = torso; }

	if (legs->bone_count    != torso->bone_count    ||
	    legs->bone_count    != oldLegs->bone_count  ||
	    legs->bone_count    != oldTorso->bone_count ||
	    legs->bone_count    > vg_mdx_scratch_max)
	{
		return qfalse;
	}

	boneIndex = vg_mdx_find_bone(legs, boneName);
	if (boneIndex < 0)                                                        { return qfalse; }

	vg_mdx_calculate_bone_lerp(legs, oldLegs, torso, oldTorso,
	                           body, boneIndex, qtrue);

	/* Transform model-local origin into world-space:
	 *   world = body->origin + sum_k(scratch[boneIndex][k] * body->axis[k])
	 * matching mdx_tag_orientation's tail (g_mdx.c:1734-1738). */
	VectorCopy(body->origin, outWorld);
	for (k = 0; k < 3; k++)
	{
		VectorMA(outWorld, vg_mdx_scratch[boneIndex][k], body->axis[k], outWorld);
	}
	return qtrue;
}
