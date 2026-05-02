# Strategy I Port Notes — Cgame MDX skeleton loader

## Binary format (recap of `g_mdx.h`)

MDX file = `MDXW` magic + `mdx_hdr` + `bone_count` × `mdx_bone` + `frame_count` × (`mdx_frame` + `bone_count` × `mdx_frame_bone`).

Header (`struct mdx_hdr`):
- `ident[4]`, `version[4]`, `filename[MAX_QPATH]`
- `frame_count` (u32), `bone_count` (u32)
- `frame_offset` (u32), `bone_offset` (u32)
- `torso_parent` (u32) — bone index of the torso anchor
- `eof_offset` (u32)

Bone (per-MDX, bind pose):
- `name[64]`, `parent_index` (i32)
- `torso_weight` (f32) — 0.0 = legs animation drives me, 1.0 = torso
- `parent_dist` (f32) — bind-pose offset along parent's local X
- `is_tag` (u32) — unused for our purpose

Frame (per animation frame):
- `mins`, `maxs`, `origin`, `radius` (skipped in our port — bbox info)
- `parent_offset[3]` (f32×3) — root-bone world translation for this frame
- followed by `bone_count` × `mdx_frame_bone`

Frame bone (per bone per frame):
- `angles[3][2]` (i16×3) — global bone rotation, NOT used for origin-only
- `unused[2]` (i16)
- `offset_angles[2][2]` (i16×2) — direction-from-parent rotation. **THIS is what `mdx_calculate_bone` reads.**

Per the qagame `g_mdx.c:1402-1417`:
```c
mdx_calculate_bone(dest, bone, frameBone):
    tmp = (bone->parent_dist, 0, 0)
    AnglesToAxisBroken(frameBone->offset_angles, axis)
    vec3_rotate(tmp, axis, dest)   /* rotate by per-frame delta */
```

So a bone's local-to-parent vector = `(parent_dist, 0, 0)` rotated by the
two short angles in offset_angles. The offset_angles are *not* normal
Euler angles — `AnglesToAxisBroken` (`g_mdx.c:186-218`) does a custom
LUT lookup with `sintable[(angles[0] >> 4) + 4096 if negative]` and
constructs an unusual axis matrix. Must port verbatim.

## Bone-calc recursion (`mdx_calculate_bone_lerp`, `g_mdx.c:1429-1505`)

```
calc_lerp(refent, [legs_mdx, old_legs, torso_mdx, old_torso], i):
    bone = (legs_mdx if torso_weight[i]==0 else torso_mdx).bones[i]
    frame, oldframe, backlerp = (legs/torso fields based on torso_weight)

    if i == 0 (root):
        mdx_bones[0] = lerp(legs_mdx.frames[frame].parent_offset,
                            old_legs.frames[oldframe].parent_offset,
                            backlerp)
        return

    if recursive: calc_lerp(parent_index, qtrue)

    point     = mdx_calculate_bone(bone, current frame_bone)
    oldpoint  = mdx_calculate_bone(bone, old frame_bone)

    mdx_bones[i] = mdx_bones[parent_index] + point
    mdx_bones[i] += backlerp * (oldpoint - point)   /* lerp local component only */
```

Result is in **model-local space**. To get world-space:
```
world = body->origin + sum_axis(mdx_bones[i][k] * body->axis[k])
```

That's what the qagame `mdx_bone_orientation`+`trap_R_LerpTagNumber`
chain produces, but those also compute axis (which we don't need).

## Edge-case: torso_weight ∈ (0,1)

A bone with `torso_weight == 0.5` would be partially driven by both
torso and legs animations. Looking at qagame: in `mdx_calculate_bone_lerp`
the check is `if (torso_weight != 0.f)` — so ANY non-zero weight makes
the torso-side win. The fractional weighting only kicks in via the
*matrix* path (`mdx_bone_orientation:1629`), not the origin-only path.

For Vanguard's hit-areas (Bip01 Head, Spine1, Pelvis, Clavicle, etc.)
we just inherit qagame's behaviour: non-zero weight → torso side.

## Refent fields we need from cgame

`vg_BuildBodyRefent` already populates everything we need:
- `frame`, `oldframe`, `backlerp`, `frameModel`, `oldframeModel`
- `torsoFrame`, `oldTorsoFrame`, `torsoBacklerp`, `torsoFrameModel`, `oldTorsoFrameModel`
- `origin`, `axis`, `torsoAxis`

Plus we set `hModel = character->mesh` (added in v0.3.8a).

## Helpers porting list

| Function | Source | Port? |
|---|---|---|
| `mdx_read_int/short/vec` | `g_mdx.c:456-482` | ✔ port (3×8 lines) |
| `AnglesToAxisBroken` | `g_mdx.c:186-218` | ✔ port (32 lines) — needs `sintable[]` |
| `sintable[4096]` | `g_mdx_lut.h` | `#include "../game/g_mdx_lut.h"` |
| `vec3_rotate` | `q_math.c` | ✔ already in cgame |
| `mdx_calculate_bone` | `g_mdx.c:1402-1417` | ✔ port (16 lines) |
| `mdx_calculate_bone_lerp` | `g_mdx.c:1429-1505` | ✔ port (~75 lines, simplified — origin only, no quaternion path) |
| `mdx_load` | `g_mdx.c:540-627` | ✔ port (~90 lines) |
| `mdx_quaternion_*`, `MatrixWeight`, `mdx_lerp_matrix` | various | ✘ skip (mesh-deform paths) |
| `mdm_load`, `mdm_tag_lookup` | various | ✘ skip (we don't need MDM-side) |

## Allocation

Native build (.so/.dll) — `malloc`/`free` work. Realloc not strictly
needed: we know all sizes upfront from the file header.

For the global scratch `mdx_bones[]` (per-frame work area), one shared
array is fine — all per-cgame-frame work is single-threaded. Allocate
on first model load, grow as needed.

For the per-MDX persistent storage (parsed bones + frames), one
allocation per MDX file. ~13 files × (53 bones × 80B + ~30 frames ×
(53 × 16B + 12B header)) = ~13 × 30 KB = ~400 KB. Acceptable.

## Path-table extension (`bg_animgroup.c`)

Both qagame and cgame compile this file (it's in `bg`). The patch
adds a global table of `(qhandle, path)` pairs populated at every
`trap_R_RegisterModel` call inside `BG_RAG_ParseAnimFile`. Cgame uses
it via `vg_FindMDXPath()` to translate the engine-side handle that
arrives in `body->frameModel` to a file path it can open.

Side-effect on qagame: qagame's own `trap_R_RegisterModel` (which
returns a qagame-internal index, NOT the engine handle) gets logged
too. Harmless — cgame won't ever look up a qagame-internal handle.

But we should make sure the table doesn't fill up from the qagame
side filling slots cgame later needs. Current animation script for
human_base lists ~13 distinct `.mdx` files. `VG_MDX_PATH_MAX = 64`
is comfortable headroom; if exceeded we drop later additions
silently with a CG_Printf warning.

## Build flag

The active root is upstream's CMakeLists.txt. New `.c` files in
`src/cgame/` are auto-globbed (`cmake/ETLSources.cmake:84`
`FILE(GLOB CGAME_SRC "src/cgame/*.c")`). **No CMake edit needed.**


---

**Copyright Notice**

Copyright (c) 2026 wahke <info@wahke.lu> (https://wahke.lu)  
Copyright (c) 2026 VanguardMod Project Contributors

Licensed under GPL-3.0-or-later. Part of VanguardMod project.
Built on ETLegacy (https://www.etlegacy.com).
