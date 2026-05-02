# Hitbox Scale Analysis — Phase 6.1.3 Field-Test Diagnosis

Untracked planning artefact for the Phase 6.1.3 root-cause
diagnosis. Live-test on Pterodactyl with a v0.3.3 diagnostic
build showed:

- ~10% of `mdx_hit_test` calls return real impactpoints (HEAD=1,
  CHEST=2, SHOULDER_LEFT=6, KNEE_LEFT=8) — the pipeline plumbing
  works.
- ~90% return `fraction=1.0, impactpoint=0 (UNUSED)` — meaning
  `mdx_hit_warp` was reached but no hit-area capsule was within
  reach of the trace, so `best_frac` stayed at the initial 2.0
  and got clamped to 1.0 on output, with `best_impactpoint`
  staying at the init `IMPACTPOINT_UNUSED`.

Hypothesis going in: the `radius`/`scale` values in
`etmain/animations/human_base.hit` are too small, or there's a
unit-system mismatch between the bone position and the capsule
geometry.

---

## §1 — `mdx_hit_test_sphere` (g_mdx.c:2706)

```c
static qboolean mdx_hit_test_sphere(const vec3_t p1)
{
    if (VectorLengthSquared(p1) > Square(1.0))
        return qfalse;
    return qtrue;
}
```

**Tests against unit-sphere.** `p1` is the trace's tangent point
**already transformed into the tag's local frame** by
`mdx_hit_warp`, which:

1. Un-translates: `start - origin`, `end - origin` (origin is the
   tag's world position from `mdx_tag_orientation`).
2. Un-rotates: `start * transpose(axis)`, ditto for `end`.
3. **Un-scales:** `start[i] /= scale[i]`, ditto for `end`.

So the sphere check `|p1|² > 1.0²` after un-scaling by `scale =
(R, R, R)` is equivalent to `|world-tangent - origin|² > R²` in
world coordinates. **A `radius 6` is a 6-Quake-unit-radius sphere
in world space**, no hidden multipliers.

**Reference:** `g_combat.c:947` `G_BuildHead` with
`g_realHead.integer & REALHEAD_HEAD` uses
`mins=(-6,-6,-6) maxs=(6,6,6)` — also a 12-unit cube, sphere-
inscribed radius 6. Our HEAD `radius 6` matches the legacy
realhead size exactly. **HEAD works in field-test → confirms
the unit interpretation is correct.**

---

## §2 — `mdx_hit_test_cylinder` (g_mdx.c:2621)

```c
if (p1[2] < 0)  return qfalse;          // above tag1
if (p2[2] > 0)  return qfalse;          // below tag2
lerpt  = p1[2] + -p2[2];                // local-Z extent
lerp1  = (lerpt - p1[2]) / lerpt;       // weight tag1
lerp2  = 1.0f - lerp1;                  // weight tag2
distx  = (p1[0]*lerp1 + p2[0]*lerp2)²;
disty  = (p1[1]*lerp1 + p2[1]*lerp2)²;
if (distx + disty > 1.0)  return qfalse;
return qtrue;
```

`p1` and `p2` are tangent points **separately un-scaled** by
`scale[0]` and `scale[1]` respectively. The local-Z axis is
constructed from `o2 - o1` (g_mdx.c:2809) — the bone-to-bone
direction.

**Z-extent semantics — critical insight:**

In tag1's local frame after un-translate, tag2 sits at
`(o2 - o1) / scale[0]` in z ≈ `bone_distance / scale[0][2]`.
The cylinder is bounded by the line segment from `0` to `tag2_z`
in tag1's local frame.

`p1[2] >= 0` requires the tangent be on the tag1→tag2 side.
`p2[2] <= 0` requires the tangent be on the tag2→tag1 side
(in tag2's local frame).

**The cylinder only covers the segment between the two
tag origins** — nothing above tag1 or below tag2 is inside
the capsule. This is a finite cylinder, not infinite.

The XY check `distx² + disty² > 1.0` after un-scale is equivalent
to a radial distance check against an interpolated radius
between `scale[0][0]` (at tag1 end) and `scale[1][0]` (at tag2
end) in world-space units.

→ `radius 3, radius 3` for a Cylinder produces a **constant
3-Quake-unit-radius cylinder** between the tags.

**Reference:** Player AABB (g_client.c:65-66):
- `playerMins = (-16, -16, -24)`
- `playerMaxs = (16, 16, 48)`
- Total: 32×32×72 Quake-units (W×D×H standing).

A leg cylinder with `radius 3` is a **6-unit-diameter** cylinder.
The full leg is roughly the lower 30 units of the player AABB
(from the pelvis around z≈0 down to feet at z=-24 in the
player's own frame). A 6-unit-wide leg cylinder is plausible
geometrically (legs are ~6-8 units thick anatomically) — but
that's **the lateral coverage**. The Z-extent depends on the
TAG-PAIR DISTANCE, which is a separate axis.

---

## §3 — `mdx_tag_orientation` (g_mdx.c:1686)

Returns `origin` in world coordinates by chaining
`mdx_bone_orientation` (bone-local pose) through
`refent->origin` and `refent->axis` (the player's world
transform). Tag offsets/axes from the .hit's TAG block are
applied multiplicatively.

→ **Output is world-space Quake-units.** Confirmed.

For our TAG bridges (`TAG _vg_head "Bip01 Head"`), the tag's
`offset = (0,0,0)` and `axis = identity` (no offset/axis
keywords used), so the tag origin equals the bone origin.

---

## §4 — `hit_load` scale storage (g_mdx.c:962-1006)

`hit->scale[tagidx]` initialised to `(1, 1, 1)`. Then:

- `radius N` → `scale[i] *= (N, N, N)` (multiplicative, not
  assignment).
- `scale X Y Z` → `scale[i] *= (X, Y, Z)` (multiplicative).

→ **No hidden conversion factor.** `radius 6` literally puts
`(6, 6, 6)` in `scale[0]`. Our values are stored verbatim.

---

## §5 — Reference `.hit` files in history

```
find . -name "*.hit" -not -path "./build*"
→ ./etmain/animations/human_base.hit (ours)

git log --all --oneline -- "*.hit" "*.hits"
→ 3ee52cd, c598e7d (only our two commits)
```

**No upstream / historical `.hit` file in the codebase.** No
reference values to compare against — our hit-areas are the
first ones the codebase has ever shipped. The `BONE_HITTESTS`
pipeline was upstream-disabled, so nobody had reason to write
them.

---

## §6 — Player skeleton dimensions

From bone dump (`docs/notes/bone_dump_2026-04-27.txt`, body.mdx,
53 bones):

```
[0] Bip01 Pelvis   parent=-1
[1] Bip01 Spine    parent=0
[2] Bip01 Spine1   parent=1
[3] Bip01 Spine2   parent=2
[4] Bip01 Spine3   parent=3
[5] Bip01 Neck     parent=4
[6] Bip01 Head     parent=5
[7] Bip01 L Clavicle  parent=5
[8] Bip01 L UpperArm  parent=7
...
[45] Bip01 L Thigh    parent=0
[46] Bip01 L Calf     parent=45
[47] Bip01 L Foot     parent=46
```

**Distance numbers are NOT in the dump** (only names + parent
indices). Need to read the `.mdx` binary frame data to get
`parent_dist` floats — out of scope for this paper recon.

But by the typical 3DS Max Biped convention scaled to a Q3-
sized player (72-unit-tall AABB):

| Pair | Estimated bone-distance (Quake-units) |
|---|---|
| Pelvis ↔ Spine | ~3-4 |
| Spine ↔ Spine1 | ~3 |
| Spine1 ↔ Spine2 | ~3 |
| Spine2 ↔ Spine3 | ~3 |
| Spine3 ↔ Neck | ~2-3 |
| Neck ↔ Head | ~3-4 |
| Sum (Pelvis → Head) | ~17-20 |
| Clavicle ↔ UpperArm | ~5 |
| UpperArm ↔ Forearm | ~10 |
| Thigh ↔ Calf | ~14-16 |
| Calf ↔ Foot | ~14-16 |

These are **estimates**, not verified from this skeleton. A
bone-distance dump would confirm.

---

## §7 — Diagnosis

### What our `human_base.hit` does today

```
HIT body _vg_spine_up _vg_spine_top scale 8 6 4 scale 8 6 4 impactpoint chest box
         ↑Spine2     ↑Spine3
HIT body _vg_spine_lo _vg_spine_mid scale 8 6 4 scale 8 6 4 impactpoint gut box
         ↑Spine      ↑Spine1
HIT body _vg_pelvis radius 5 impactpoint groin
         ↑Pelvis (single sphere)
HIT arm_R _vg_clav_r _vg_uarm_r radius 3 radius 4 impactpoint shoulder_right
HIT leg_R _vg_thigh_r _vg_calf_r radius 3 radius 3 impactpoint knee_right
HIT leg_R _vg_calf_r _vg_foot_r radius 3 radius 2 impactpoint legs
```

### The smoking gun

**Body capsules are picked between adjacent vertebrae** (Spine2
↔ Spine3 for chest, Spine ↔ Spine1 for gut). Bone distance
~3 Quake-units, so the box covers only a 3-unit-tall slice of
the chest. The rest of the torso vertically — including the
sternum / heart area between Spine1 and Spine2 — is **outside
the capsule's Z-bounds** and produces `mdx_hit_warp` returning
`qfalse` early via `p1[2] < 0` or `p2[2] > 0`.

Quick sanity check with rough numbers (assuming Spine2 sits
~50% of the way up the torso and Spine3 ~60%):

- Player torso height: ~30 units (from pelvis ~z=0 to neck ~z=30)
- Spine2 to Spine3 vertical distance: ~3 units
- Chest box Z-coverage: 3 units in a 30-unit torso
- ⇒ ~10% of vertical chest-shots hit the capsule

This matches the field-test "10% real hits" ratio almost
exactly.

**Limb cylinders** are picked between joint bones (Thigh ↔ Calf
for knee, Calf ↔ Foot for shin) — those have larger natural
distances (~14-16 units) so their Z-coverage is fine. Their
**radius 3** is plausible anatomically. They likely under-
register because:

- The trace must pass within 3 Quake-units of the *bone axis*
  for a hit. Player limbs are visually ~6-8 units thick, so
  the bone axis is ~3-4 units inside the silhouette. A
  hitbox-radius-3 cylinder is *just* covering the bone, missing
  the muscle / clothing volume around it.

### Hypothesis (confidence: 7/10)

1. **Primary cause — body-box Z-extent is too short.**
   Adjacent-vertebra tag pairs (Spine2↔Spine3, Spine↔Spine1)
   give ~3-unit-tall boxes. Most chest/gut shots miss vertically.
   Fix: pick wider-spaced pairs (Spine↔Spine3 for chest,
   Pelvis↔Spine1 for gut) to span ~12-15 units of torso each.

2. **Secondary cause — limb cylinder radii are bone-tight.**
   `radius 3` covers the bone but not the anatomical limb
   silhouette. Fix: bump leg/arm cylinder radii to ~5-6, which
   matches the visible limb diameter while still being smaller
   than the player AABB at the limb's position.

3. **HEAD works (radius 6 sphere)** because (a) it's a sphere,
   not a capsule, so no Z-bound check, and (b) radius 6 matches
   the legacy `g_realHead`/`REALHEAD_HEAD` size — already a
   battle-tested choice.

### Confidence breakdown

- Static analysis chain (sphere check → un-scale → tag world-
  space) is firmly understood, no ambiguity. **+3**
- The `p1[2]<0 / p2[2]>0` early-out on the cylinder is exactly
  the constraint that produces "fraction=1.0 / impactpoint=
  UNUSED" without registering a hit-area. **+2**
- HEAD-sphere reference matches `REALHEAD_HEAD` legacy size
  → confirms unit convention. **+2**
- **Unverified:** actual bone-distance values in body.mdx are
  estimates, not measured. Could be 2 units or 5 units.
  **−1 confidence**
- **Unverified:** the field-test 10% ratio is approximate, not
  a clean "10/100 fraction-checked" measurement. **−1
  confidence**

### Next-step proposal

Before tuning blind, **measure bone distances** by adding a
temporary debug-print in `mdx_load` or `mdx_RegisterHits` that
dumps `mdx->bones[i]` parent_dist values for body.mdx. One
12-second testserver run produces the data, removes guesswork,
and lets us pick tag pairs with the right Z-coverage.

Then tune in two passes:

**Pass 1 — coverage fix (re-pick TAG pairs):**
- Chest: `Bip01 Spine` ↔ `Bip01 Spine3` (was Spine2↔Spine3)
- Gut: `Bip01 Pelvis` ↔ `Bip01 Spine1` (was Spine↔Spine1)
- Leave Groin (single sphere on Pelvis) as-is — already 5-unit
  radius, plausible size.

**Pass 2 — radius widen:**
- Bump all limb cylinder radii from 3 → ~5-6.
- Keep HEAD radius 6 (proven).

After both passes, a re-test should show >50% real-hit ratio.
Iterate from there.

---

*End. Confidence 7/10 for the diagnosis. Bone-distance dump
would push this to 9/10. Tuning without the dump is workable
but slower (more iterations).*
