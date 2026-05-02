# Cgame Bone-Calc Recon — Phase A (v0.3.8 prep)

**Date:** 2026-04-29
**Trigger:** v0.3.7 Pterodactyl live-test — all 10 `Bip01 *`
bone-name lookups via cgame `trap_R_LerpTag` returned `< 0`.
`frameModel=414` (non-NULL), so `vg_BuildBodyRefent` is fine; the
failure is purely in the engine renderer's tag-list lookup.
**Goal:** decide the v0.3.8 fix path. Pure recon — no code changes.

> **Update 2026-04-29 (post-v0.4.1 follow-up, drives v0.4.2):**
> v0.4.1 picked the wrong axis for the HEAD anchor offset.
> Replicating the legacy `mdx_head_position`'s `+6.5` along
> `axis[2]` directly into the .hit file as `offset 0 0 6.5`
> assumed the offset was applied in world frame; in reality
> `mdx_tag_orientation`'s `vec3_rotate(tag->offset, tmpaxis, ...)`
> rotates by the BONE-LOCAL axis matrix returned by
> `mdx_bone_orientation`, and the bone-local axis convention
> for a 3DS-Max biped is +X = bone direction (parent → child),
> not +Z. Live-test verified: `0 0 6.5` placed the sphere on
> the medic-pack on the player's back. v0.4.2 corrects the
> offset to `6.5 0 0`. The `mdx_head_position` legacy code
> works with `axis[2]` because it operates on an *MDM tag*
> whose `axis` is authored at modelling time so that
> `axis[2] = up`; bones don't have that authored convention.
> v0.4.2 also includes a temporary `VG_DIAG: Bip01 Head bone-axis`
> one-shot print to verify the chosen axis empirically — if
> the print shows the bone-axis matrix is non-trivial, future
> hit-area tunings can read it directly to pick the right
> offset axis without trial-and-error.
>
> **Update 2026-04-29 (post-v0.4.0 follow-up, drives v0.4.1):**
> The Phase 6 multi-region `mdx_hit_test` does **not** apply the
> legacy realhead head offsets. The legacy `mdx_head_position`
> (`g_mdx.c:2946-2971`, used by `g_combat.c:G_BuildHead` when
> `g_realHead & REALHEAD_HEAD`) shifts the HEAD trace by `+6.5`
> units along the head bone's local Z plus `+0.5` along its local
> X — those constants are dead in our pipeline. v0.4.0 deployed
> with the HEAD sphere centered on the raw `Bip01 Head` bone
> origin, which is at the atlas / skull base, ~6.5 units below
> the visible skull centre. Live-test showed ~2% HEAD-shot rate
> as a consequence. v0.4.1 restores the legacy `+6.5 Z` anchor
> via a `TAG _vg_head ... offset 0 0 6.5` modifier in
> `etmain/animations/human_base.hit` (server side) plus a matching
> `offset1[3] = (0, 0, 6.5)` field on the cgame `vg_hit_areas[]`
> HEAD entry, applied by `vg_mdx_compute_bone_world_with_offset`
> in the bone's local frame (rotates with the head bone). This
> matters for any future region tuning: `Bip01 *` bone origins
> are *bone roots*, not *visible-mesh centroids*; the `.hit` file
> must compensate per-region with a `TAG offset` if the visible
> region differs from the bone-root location.

---

## TL;DR

There is **no engine syscall in cgame that exposes MDX skeleton-bone
positions by name**. The cgame `trap_R_LerpTag` is the engine
renderer's MDM-tag-list lookup — it iterates `mdm->tags[]` only, so
"Bip01 Head" cannot resolve no matter how `body->frameModel` /
`body->hModel` is set. Three viable paths:

| | Strategy | Effort | Accuracy vs server | Risk |
|---|---|---:|---|---|
| **I** | Port qagame's MDX loader + bone-math into cgame | **6–10 h** | 1:1 (same algo) | medium — file-path discovery + 600-line port |
| **II** | Tag-anchored visualisation using existing MDM tags (`tag_head`, `tag_chest`, `tag_armleft` …) | **1–2 h** | ~1–3 cm offset per area | low — uses already-working `trap_R_LerpTag` |
| **III** | Compile-time bone hierarchy + per-frame lerp local | n/a | n/a | rejected — see §5.III |

**Recommendation:** Strategy II for v0.3.8, optionally upgrade to
Strategy I in a later release if the visual offset is judged
unacceptable during live-testing. **Confidence Strategy II works:
8/10.** **Confidence Strategy I works: 9/10.** See §6 for rationale.

---

## 1. Qagame `mdx_calculate_bones` — algorithm summary

Lives in `src/game/g_mdx.c`. Three functions form the core
hierarchy:

### 1.1 `mdx_calculate_bone` (line 1402)

```c
static void mdx_calculate_bone(vec3_t dest,
                               const struct bone *bone,
                               const struct frame_bone *frameBone)
{
    vec3_t tmp, axis[3];
    tmp[0] = bone->parent_dist;          /* bind-pose offset */
    tmp[1] = tmp[2] = 0;
    AnglesToAxisBroken(frameBone->offset_angles, axis);
    vec3_rotate(tmp, axis, dest);        /* rotate by per-frame delta */
}
```

Single bone's local position from the bind-pose `parent_dist` (one
float per bone, model-level) and the per-frame `offset_angles` (two
shorts per bone per frame). `AnglesToAxisBroken` is a static helper
(g_mdx.c:186) — 12-line function. `vec3_rotate` is in `q_math.c`,
already linked into cgame.

### 1.2 `mdx_calculate_bone_lerp` (line 1429)

Recursive over the parent hierarchy. Per bone `i`:

```
1. Pick frameModel + frame number based on bone[i].torso_weight
   (legs vs torso animation).
2. If recursive: recurse into bone[parent_index] first.
3. Compute current-frame point P = mdx_calculate_bone(bone[i],
   frame_bone[i] of current frame).
4. Add to parent: mdx_bones[i] = mdx_bones[parent] + P.
5. Repeat for old frame, lerp into mdx_bones[i] using backlerp.
```

Bone 0 (root, "Bip01 Pelvis") is special: `mdx_bones[0]` is the
lerped `parent_offset` from the frame header (no rotation calc).

The output goes to a **module-level scratch array**
`static vec3_t *mdx_bones` (g_mdx.c:75) sized once at startup
(`mdx_bones_max = max bone_count over all loaded MDX`).

### 1.3 `mdx_calculate_bones_single` (line 1549)

Public entry point for single-bone lookup. Calls
`mdx_calculate_bone_lerp(refent, …, i, qtrue)` → recurses up to
the root, populates `mdx_bones[parent ancestors]` and `mdx_bones[i]`.
Used by `trap_R_LerpTagNumber` (qagame's own LerpTag impl,
g_mdx.c:1776).

### 1.4 What feeds the math

Inputs to `mdx_calculate_bone_lerp`:

  * `refent->frame`, `refent->oldframe`, `refent->backlerp` (legs)
  * `refent->torsoFrame`, `refent->oldTorsoFrame`,
    `refent->torsoBacklerp` (torso)
  * `refent->frameModel`, `refent->oldframeModel`,
    `refent->torsoFrameModel`, `refent->oldTorsoFrameModel` —
    qagame's private mdx_models[] indices
  * `mdx_models[handle].bones[i].parent_dist` (bind pose)
  * `mdx_models[handle].bones[i].parent_index`
  * `mdx_models[handle].bones[i].torso_weight`
  * `mdx_models[handle].frames[frame].bones[i].offset_angles` (per-frame)
  * `mdx_models[handle].frames[frame].parent_offset` (root translation)

All of this comes from the parsed MDX file — qagame's
`mdx_load` (g_mdx.c:540). 380 lines. Reads MDXW header, then
iterates frames × bone_count to populate bone deltas, plus a
trailing bone array with bind-pose data.

### 1.5 Helpers needed

  * `AnglesToAxisBroken` (g_mdx.c:186) — 12 lines, static, **must be ported**.
  * `vec3_rotate` — in `q_math.c`, already in cgame.
  * `MatrixWeight` (g_mdx.c:162) — 8 lines, only used by `mdx_bone_orientation` (axis output, not needed for origin-only lookup), **optional**.
  * `mdx_lerp_matrix` (g_mdx.c:281) — only used by `mdx_tag_orientation`, optional.
  * `mdx_quaternion_*` — only used by deformation code we don't need.

For **origin-only lookup** (which is what `vg_GetBoneOrigin`
returns), the math reduces to: bind-pose offset rotated by per-frame
angles, translated by parent. ~2 floats × 53 bones × 2 frames ≈
~200 multiply-adds per frame per player.

---

## 2. Cgame VM — what's available

### 2.1 Animation script + paths

`cg_character.c:301` already calls `BG_R_RegisterAnimationGroup`,
which lives in `src/game/bg_animgroup.c`. That parses the
`.anim` file and for each MDX referenced, calls
`trap_R_RegisterModel(token.string)`. The `token.string` (the file
path) is the data we'd need but it's discarded after the `qhandle_t`
comes back.

`bg_public.h:1742` defines `animation_t` with
`#ifdef USE_MDXFILE qhandle_t mdxFile;` — qhandle is opaque. There
is **no `mdxFileName` field in cgame's view**.

So **path discovery for cgame** requires either:
  * Patching `bg_animgroup.c` to also store the path string in a
    parallel array (5 lines), or
  * Bypassing bg and re-parsing the .anim file on the cgame side, or
  * Hardcoding the canonical path "animations/human/base/*.mdx".

### 2.2 Refent state

`vg_BuildBodyRefent` already populates from `centity_t->pe.legs` /
`pe.torso`:

```
body->frame, body->oldframe, body->backlerp, body->frameModel, body->oldframeModel
body->torsoFrame, body->oldTorsoFrame, body->torsoBacklerp,
body->torsoFrameModel, body->oldTorsoFrameModel
body->origin (lerp), body->axis, body->torsoAxis
```

These are exactly the inputs `mdx_calculate_bone_lerp` reads from
`refent`. The `refEntity_t` field names match `grefEntity_t` 1:1.
**No data is missing from cgame's snapshot;** the only gap is that
`frameModel` is an engine handle, not a qagame handle.

### 2.3 Engine traps cgame has

`cg_syscalls.c` lists ~150 traps. Relevant ones:

  * `trap_FS_FOpenFile`, `trap_FS_Read`, `trap_FS_FCloseFile` (167–199)
    — same as qagame, can read MDX files directly.
  * `trap_R_RegisterModel` (engine handle — opaque).
  * `trap_R_LerpTag` (CG_R_LERPTAG syscall) — MDM-tag list only,
    confirmed by reading `R_LerpTag` (renderer/tr_model.c:2033) →
    `R_MDM_GetBoneTag` (renderer/tr_animation_mdm.c:2183), which
    iterates `mdm->numTags` from `mdm->ofsTags`, never touches
    skeleton bones by name.

  * **No** `trap_R_LerpBone`, `trap_R_GetBonePos`, or similar.
  * **No** trap to fetch path-by-handle or expose internal MDX data.

### 2.4 Existing skeleton-aware code in cgame

`cg_players.c` and `cg_character.c` already use the engine via
`trap_R_LerpTag` for **tag-name** lookups: `tag_head`, `tag_torso`,
`tag_chest`, `tag_back`, `tag_armleft`, `tag_armright`,
`tag_legleft`, `tag_legright`, `tag_footleft`, `tag_footright`,
`tag_ubelt`, `tag_weapon`, `tag_weapon2`, `tag_mouth`, `tag_bipod`,
plus weapon/effect tags. These all work today.

`tag_*` lookups go through the engine's MDM tag-list, which on
internal MDM tag resolution actually does call `R_CalcBones` to
compute bone positions and apply the tag's bone-anchored offset.
**The engine knows about bones internally — it just doesn't expose
them by name.**

---

## 3. The .hit file — server-side authority

Studied `etmain/animations/human_base.hit`. The authoritative
hit-area definitions are wired through **interntags** (qagame-private
synthetic tags), not MDM tags:

```
TAG _vg_head      "Bip01 Head"
TAG _vg_neck      "Bip01 Neck"
TAG _vg_spine_lo  "Bip01 Spine"
TAG _vg_spine_mid "Bip01 Spine1"
…
HIT 1 sphere _vg_head 6
HIT 2 box2 _vg_spine_mid _vg_neck …
…
```

Each `TAG _vg_*` declares an interntag that wraps a real MDX
skeleton bone (resolved by `mdx_bone_lookup` in qagame). The HIT
blocks then reference these interntags. The interntags are stored
in `interntags[]` array in qagame-private memory and are **invisible
to the engine renderer**. They're consumed by `mdx_tag_orientation`
during `mdx_hit_test`, which recurses through `mdx_bone_orientation`
+ `mdx_calculate_bones_single` to produce the actual world position.

**Implication:** for cgame to render a capsule at the *exact*
server-side hit-area position, cgame must replicate either (a) the
full bone-math chain, or (b) compose tags + interntag offsets and
trust they map to bones the same way. (a) is Strategy I; (b) is
infeasible because interntags are qagame-private.

---

## 4. Gap analysis

| What `mdx_calculate_bone_lerp` needs | Cgame has | Cgame can get |
|---|---|---|
| `refent->frame`, `oldframe`, `backlerp` | ✔ from `cent->pe.legs` | — |
| `refent->torsoFrame`, `oldTorsoFrame`, `torsoBacklerp` | ✔ from `cent->pe.torso` | — |
| `refent->frameModel` etc. (qhandle) | engine handle, **not qagame index** | — would need own registry |
| `mdx_t.bones[i].parent_dist` (bind pose) | ✘ engine doesn't expose | parse MDX file ourselves |
| `mdx_t.bones[i].parent_index` | ✘ | parse |
| `mdx_t.bones[i].torso_weight` | ✘ | parse |
| `mdx_t.frames[N].bones[i].offset_angles` (per-frame) | ✘ | parse |
| `mdx_t.frames[N].parent_offset` (root xlation) | ✘ | parse |
| File path of currently-active MDX | ✘ — path discarded after RegisterModel | small `bg_animgroup.c` patch |

The single missing piece is **the MDX file content**. Once cgame
parses `body.mdx` / `mortar.mdx` / `akimbo.mdx` / etc. into its own
`vg_mdx_t` array, all bone math works the same.

The math itself ports verbatim. C99 mixed-declaration usage in
`g_mdx.c` is fine because we currently build native `.so`/`.dll`
modules, not QVMs (per CLAUDE.md note: bootstrap uses
`BUILD_MOD=ON BUILD_MOD_PK3=ON`, no `BUILD_MOD_QVM`). For QVM
forward-compat the math could be tweaked to C89 — straightforward.

---

## 5. Three implementation strategies

### 5.I — Full MDX port to cgame

Architecture:

  1. **Parallel path table** — patch `bg_animgroup.c:230-280` so
     `BG_RAG_ParseAnimFile` records the MDX file path next to the
     `qhandle_t`. Five lines + a small extern in `bg_public.h`.
     Visible to both qagame and cgame; qagame ignores it.
  2. **`vg_mdx.c` in cgame** — new file, ~600 lines:
     * `static vg_mdx_t vg_mdx_models[VG_MDX_MAX]` (16 entries
       suffices — game ships ~13 MDX files for the human rig).
     * `vg_mdx_register_for_handle(qhandle_t engineHandle)` — looks
       up the path from the parallel table, opens via
       `trap_FS_FOpenFile`, parses MDX with adapted `mdx_load`,
       returns its own internal index.
     * `vg_mdx_calculate_bone_lerp` — verbatim port.
     * `vg_mdx_resolve_bone(int vgHandle, const char *boneName,
       refEntity_t *body, vec3_t outOrigin)` — bone-name lookup
       plus single-bone calc + return world-space origin.
  3. **`vg_GetBoneOrigin` rewrite** — instead of `trap_R_LerpTag`,
     translate `body->frameModel` (engine handle) → vg internal
     handle (via path-table lookup, lazy on miss), then call
     `vg_mdx_resolve_bone`.
  4. **State** — `mdx_bones[]` scratch array per cgame instance,
     same allocation pattern as qagame (max bone_count over loaded
     MDX, allocated once, reused per frame).

Unknowns / risks:

  * Memory: 13 files × 53 bones × ~30 frames × ~12 bytes per
    frame_bone ≈ ~250 KB total. Fine for cgame.
  * Path-table coupling: `bg_animgroup.c` patch is fully shared with
    qagame. Risk that qagame stops compiling — mitigated by guarded
    additions (`#ifdef USE_MDXFILE_PATH_TABLE` or just an unused
    extra field).
  * One subtle correctness item: qagame's `mdx_PlayerAngles` (g_mdx.c:2493)
    computes torsoAxis differently from cgame's CG_PlayerAngles. If
    we feed cgame's `body->torsoAxis` to a verbatim port of the
    bone-math, results may drift by 1–3° vs server-side. Probably
    cosmetic; verify in live-test.

Confidence the visualization will match server hit-detection: **9/10**.
Estimated effort: **6–10 h** (port + path-patch + symbol-disambiguation
across qagame/cgame + live-test iteration).

### 5.II — Tag-anchored hybrid

Map our 10 hit-areas to the closest available **MDM tag** that the
engine's `trap_R_LerpTag` already resolves:

| Hit-area | Bone (.hit) | Proposed MDM tag | Offset needed |
|---|---|---|---|
| HEAD sphere r=6 | Bip01 Head | `tag_head` | none (tag is on the head bone) |
| CHEST box2 (top) | Bip01 Spine1 | `tag_torso` | small Z offset |
| CHEST box2 (bot) | Bip01 Neck | `tag_chest` | small Z offset |
| GUT box2 (top) | Bip01 Pelvis | `tag_ubelt` | small Z offset |
| GUT box2 (bot) | Bip01 Spine2 | `tag_torso` | between ubelt and torso |
| GROIN sphere r=7 | Bip01 Pelvis | `tag_ubelt` | none |
| L SHOULDER cyl (top) | Bip01 L Clavicle | `tag_armleft` | shoulder-end |
| L SHOULDER cyl (bot) | Bip01 L UpperArm | `tag_armleft` | elbow-end (need second tag or offset) |
| L THIGH cyl (top) | Bip01 L Thigh | `tag_legleft` | hip-end |
| L THIGH cyl (bot) | Bip01 L Calf | between `tag_legleft` and `tag_footleft` | knee — interpolate |
| L CALF cyl (top) | Bip01 L Calf | between `tag_legleft` and `tag_footleft` | knee |
| L CALF cyl (bot) | Bip01 L Foot | `tag_footleft` | none |
| R THIGH/CALF | Bip01 R *  | mirror | mirror |

Code change:

  * Replace the `vg_hit_areas[]` table's bone1/bone2 with tag names
    plus an axial offset (vec3 in tag-local space).
  * Pre-compute the offsets as **constants** from
    `bone_distance_dump_2026-04-28.txt` (e.g. clavicle midpoint =
    tag_armleft + (-2.99, 0, 0) along arm axis).
  * Where two adjacent areas share a tag (e.g. CALF top and THIGH
    bot both at the knee), pick the cheaper interpolation.
  * `vg_GetBoneOrigin` becomes
    `vg_GetTagOrigin(body, tagName, vec3_t offset, outWorld)`.
    Internally calls the existing `trap_R_LerpTag`, then applies
    the offset rotated by tag's axis.

Pros:

  * Uses only existing engine traps. No file parsing, no new state.
  * One file edited (`cg_vanguard_dev.c`), small diff.
  * Live-debuggable in 1–2 hours.

Cons:

  * Capsule positions can drift 1–3 cm from the server-side hit-area
    when the model bends (tag attachment ≠ bone center exactly,
    plus dorsal/ventral asymmetry).
  * Some areas (CALF top, THIGH bot at the knee) need
    **interpolation between two tags**; if the tags are not coplanar
    with the bone they straddle, the visual diverges.
  * Weapon-pose MDX files have slightly different `parent_dist`
    values (mortar.mdx Thigh `parent_dist = 6.233` vs body.mdx
    `5.854`) — the offset constants are an *average* and won't
    match perfectly across animations.

Confidence the visualization will be approximately correct (≤3 cm
drift): **8/10**. Confidence that the visualization will be
*indistinguishable* from server-side: **5/10** — there will be
visible drift in some poses.

Estimated effort: **1–2 h**.

### 5.III — Compile-time hierarchy + per-frame lerp local

Rejected. The bone_distance_dump clearly shows `parent_dist` is **not
constant** across MDX files: the same bone has different bind-pose
distances in body.mdx (5.854) vs mortar.mdx (6.233) vs akimbo.mdx
(5.529). A static LUT would only match one pose; weapon-class-dependent
visualizations would drift. To handle this we'd need *per-MDX-file*
LUTs, but at that point we may as well parse the MDX file (which is
Strategy I).

The per-frame `offset_angles` are also model-data only — there's no
shortcut around reading them from the file.

---

## 6. Recommendation

**Ship Strategy II as v0.3.8 to unblock visual feedback fast.**

  * 1–2 hour port instead of 6–10. Given that we're still tuning
    the .hit geometry (Pass 1+2 retune was just last week), buying
    a fast iteration loop matters more than 1:1 server match
    accuracy.
  * The capsule shapes/sizes are ours to define (`vg_hit_areas[]`
    in cgame); approximate position is fine for "is the HEAD-sphere
    roughly where the head is?" — which is the actual question the
    visualization answers.
  * If live-test on Pterodactyl shows the drift is acceptable
    (3 cm or less), Strategy II becomes the permanent answer and
    Strategy I is unneeded.
  * If drift is unacceptable (>5 cm in some poses, or a capsule
    visibly outside the player mesh), schedule Strategy I for a
    later release. The Strategy II code can be retained as a
    fallback for cgame builds where vg_mdx fails to register.

**Confidence Strategy II will visually work: 8/10.** The MDM tag
list maps almost 1:1 to our hit-areas (verified by enumerating tag
names in `cg_*.c`).

**Risk to track:** if any of `tag_armleft`, `tag_armright`,
`tag_legleft`, `tag_legright`, `tag_ubelt` is **NOT** in the actual
human MDM (we've confirmed they're *referenced* in cgame, but the
human-base mesh might selectively export only a subset), Strategy II
falls back to a 7-of-10 working areas. We can validate this
upfront with a 5-minute test: a one-shot build that prints the full
MDM tag list at character-load time. If a tag is missing, we either
synthesize it from neighbors (interpolation) or live without that
specific capsule.

---

## 7. Proposed v0.3.8 work plan (Strategy II)

  1. **Diagnostic print** — keep the v0.3.7 VG_DIAG hook in
     `vg_GetBoneOrigin`, but add a one-shot dump of the MDM
     tag list at first character-load. Confirms which `tag_*`
     names actually exist on the human mesh.
  2. **Remap `vg_hit_areas[]`** — change `bone1`/`bone2` from
     bone names to tag names; add a `vec3_t offset1`/`offset2`
     field for axial sliding along the tag axis (e.g. shoulder-end
     vs elbow-end of an upper-arm cylinder).
  3. **`vg_GetTagOriginWithOffset(body, tagName, offset, outWorld)`** —
     new helper that wraps `trap_R_LerpTag` plus offset transform.
  4. **Live-test** — Pterodactyl deploy, walk around, observe.
     Iterate offsets until visually correct.
  5. **Remove VG_DIAG** in same release once tag list confirmed.

Estimated total: **2–3 h** including iteration on offsets.

---

## 8. Open questions for the user before committing

1. **Accuracy bar:** Is "visually approximate within ~3 cm of the
   server-side hit-area" acceptable for v0.3.8, or do we need 1:1
   match? (Drives Strategy II vs I.)
2. **QVM compat horizon:** native-only is fine right now; if the
   Protected build will ship as QVM in v0.4+, Strategy I's port
   should be written C89-clean from day one. Cheap to do upfront,
   expensive to retrofit.
3. **Build bloat:** Strategy I adds ~600 lines + ~250 KB runtime
   memory to the cgame module. Strategy II adds ~30 lines. No
   real concern either way — flagged for completeness.


---

**Copyright Notice**

Copyright (c) 2026 wahke <info@wahke.lu> (https://wahke.lu)  
Copyright (c) 2026 VanguardMod Project Contributors

Licensed under GPL-3.0-or-later. Part of VanguardMod project.
Built on ETLegacy (https://www.etlegacy.com).
