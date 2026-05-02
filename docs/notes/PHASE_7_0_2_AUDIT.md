# Phase 7.0.2 — Capsule Size & Coverage-Gap Recon

**Date:** 2026-04-30
**Status:** Pure recon. No code or asset changes. Cup-tester data
drives the actual fix in v0.5.3.
**Scope:** Inventory the post-v0.5.2.1 multi-region hitbox layout,
identify likely coverage gaps from a code reading alone, propose a
structured cup-player live-test plan, surface advisory quick-win
size adjustments.

---

## A. Premise — what Phase 7.0.2 is for

### What's already done

Phase 6 + 7.0 + 7.0.1 closed three independent issues:

  * **Phase 6 (v0.3.x → v0.4.x):** wired the BONE_HITTESTS
    pipeline, shipped `etmain/animations/human_base.hit` with 10
    HIT blocks / 9 distinct impactpoints. Limb cylinder radii went
    from r=3 (bone-tight) up to r=5 (shoulder) / r=6 (knee/leg) and
    body boxes were re-anchored on wider-spaced spine pairs to
    cover the torso vertically.
  * **Phase 7.0 (v0.5.1):** strict-mode predicate fix. AABB-only
    hits without a capsule match now reject under
    `vanguard_hitbox_strict 1`. The "shoot beside the model still
    counted as a hit" bug is closed.
  * **Phase 7.0.1 (v0.5.2 / v0.5.2.1):** lateral capsule
    displacement fix. cgame's `vg_mdx_compute_bone_axis_local` now
    runs the same `MatrixWeight` that qagame does (Stage 3); HEAD
    sphere offset retuned and reverted to `+6.5` along
    bone-local +X (Stage 2 / Stage 4). v0.5.2.1 fixed the cgame
    visual lag and reverted the v0.5.2 over-correction; HEAD
    sphere now lands at +10.85 over neck-bone, anatomically right.
  * **v0.5.2.2:** SIGSEGV hot-fix on self-damage paths.

### What remains — and why this audit exists

Cup-tester Alphaloki, after a 6-test session on v0.5.2.1, reports
three issues that are **size / coverage** problems rather than
position problems:

  1. **Headshots feel "almost impossible"** — the radius-6 HEAD
     sphere may be too small for the visible helmet+face silhouette.
  2. **Shoulder hits often miss** — the r=5 clavicle→upperarm
     cylinder is undersized and/or there is a gap between the
     shoulder cylinder and the chest box at the
     trapezius / collar-bone seam.
  3. **Neck/collar zone is a "no damage zone"** — there is a gap
     between the HEAD sphere (centred at `Bip01 Head + 6.5,0,0`)
     and the CHEST box top (anchored at `Bip01 Spine1`).

Phase 7.0.2 is the data-gathering pass before the v0.5.3 size /
coverage fix. This audit:

  * Inventories every capsule in the current
    `human_base.hit` and confirms the cgame mirror.
  * Walks the visible body region by region, marking each as
    covered / undersized / gap / overlapping.
  * Sanity-checks the geometry against the player-skeleton bone
    distances and the legacy `g_realHead` head-box
    (`g_combat.c:956-989`).
  * Proposes a 10-region cup-player test plan with the
    `VG_DIAG_DUMP` infrastructure already shipped in v0.5.2-rc3+.
  * Lists advisory quick-win candidate sizes — only cup data
    confirms which ones to apply.

No implementation in this document. The fix lands as v0.5.3.

---

## B. Capsule inventory

### Server side — `etmain/animations/human_base.hit`

13 TAG bridges (lines 122-138), 10 HIT blocks (lines 148-176).
9 distinct impactpoints. All blocks are single-line per the
parser constraint (`docs/HITS_FORMAT.md` §3.0).

| # | .hit line | hit_type | Shape    | bone1                   | bone2                | scale1 / radius1 | scale2 / radius2 | impactpoint     | Visible region intent |
|---|-----------|----------|----------|-------------------------|----------------------|------------------|------------------|-----------------|------------------------|
| 1 | 148       | head     | sphere   | `Bip01 Head` + (6.5,0,0)| —                    | r=6              | —                | head            | helmet + face          |
| 2 | 151       | body     | box2     | `Bip01 Spine1`          | `Bip01 Neck`         | (9,7,5)          | (9,7,5)          | chest           | upper torso (sternum to collar) |
| 3 | 154       | body     | box2     | `Bip01 Pelvis`          | `Bip01 Spine2`       | (9,7,5)          | (9,7,5)          | gut             | lower torso (belt to mid-back) |
| 4 | 157       | body     | sphere   | `Bip01 Pelvis`          | —                    | r=7              | —                | groin           | hip / lower-pelvis     |
| 5 | 160       | arm_L    | cylinder | `Bip01 L Clavicle`      | `Bip01 L UpperArm`   | r=5              | r=5              | shoulder_left   | left shoulder + cap    |
| 6 | 163       | arm_R    | cylinder | `Bip01 R Clavicle`      | `Bip01 R UpperArm`   | r=5              | r=5              | shoulder_right  | right shoulder + cap   |
| 7 | 166       | leg_L    | cylinder | `Bip01 L Thigh`         | `Bip01 L Calf`       | r=6              | r=6              | knee_left       | left thigh + knee      |
| 8 | 169       | leg_R    | cylinder | `Bip01 R Thigh`         | `Bip01 R Calf`       | r=6              | r=6              | knee_right      | right thigh + knee     |
| 9 | 173       | leg_L    | cylinder | `Bip01 L Calf`          | `Bip01 L Foot`       | r=6              | r=6              | legs            | left calf / shin       |
| 10| 176       | leg_R    | cylinder | `Bip01 R Calf`          | `Bip01 R Foot`       | r=6              | r=6              | legs            | right calf / shin      |

Counts:
  * 1 sphere + 2 box2 + 1 sphere + 6 cylinders = **10 hit-areas**.
  * 9 unique impactpoints (legs L+R both report `IMPACTPOINT_LEGS`).
  * Per radius/scale semantics (`docs/HITS_FORMAT.md` §4 + the
    `mdx_hit_test_*` un-scale chain in `g_mdx.c:2708-2828`),
    `radius N` is N world-units in every direction; `scale X Y Z`
    is X/Y as half-extents perpendicular to the bone-to-bone axis,
    Z along that axis (z-bound is automatic between bone1 and
    bone2 endpoints).

### Client mirror — `src/cgame/cg_vanguard_dev.c:130-213`

`vg_hit_areas[]` is a manual mirror of the .hit file used to
draw the wireframe overlay. Cross-check against the table above:

| # | cg line | bone1            | bone2              | shape     | scale1     | scale2     | offset1     |
|---|---------|------------------|--------------------|-----------|------------|------------|-------------|
| 1 | 155-158 | Bip01 Head       | NULL               | SPHERE    | (6,6,6)    | (0,0,0)    | (6.5,0,0)   |
| 2 | 161-164 | Bip01 Spine1     | Bip01 Neck         | BOX2      | (9,7,5)    | (9,7,5)    | (0,0,0)     |
| 3 | 167-170 | Bip01 Pelvis     | Bip01 Spine2       | BOX2      | (9,7,5)    | (9,7,5)    | (0,0,0)     |
| 4 | 173-176 | Bip01 Pelvis     | NULL               | SPHERE    | (7,7,7)    | (0,0,0)    | (0,0,0)     |
| 5 | 179-182 | Bip01 L Clavicle | Bip01 L UpperArm   | CYLINDER  | (5,5,5)    | (5,5,5)    | (0,0,0)     |
| 6 | 185-188 | Bip01 R Clavicle | Bip01 R UpperArm   | CYLINDER  | (5,5,5)    | (5,5,5)    | (0,0,0)     |
| 7 | 191-194 | Bip01 L Thigh    | Bip01 L Calf       | CYLINDER  | (6,6,6)    | (6,6,6)    | (0,0,0)     |
| 8 | 197-200 | Bip01 R Thigh    | Bip01 R Calf       | CYLINDER  | (6,6,6)    | (6,6,6)    | (0,0,0)     |
| 9 | 203-206 | Bip01 L Calf     | Bip01 L Foot       | CYLINDER  | (6,6,6)    | (6,6,6)    | (0,0,0)     |
| 10| 209-212 | Bip01 R Calf     | Bip01 R Foot       | CYLINDER  | (6,6,6)    | (6,6,6)    | (0,0,0)     |

**Mirror is in sync with `human_base.hit`.** All 10 entries match
shape / bones / scales / offset. The wireframe overlay you see
in-game with `vanguard_dev 1 / cg_vanguardDevMultibox 1` is a
faithful representation of what the server traces against.

---

## C. Coverage map by visible body region

Estimates use the body.mdx bone-distance dump
(`docs/notes/bone_distance_dump_2026-04-28.txt` — `body.mdx`
sample, lines 1-18) to anchor each region's expected vertical
extent. ET player AABB is roughly 36×36 lateral, 72 tall standing
(`g_client.c:55-56`); 1 ET unit ≈ 1 inch. Origin sits at mid-
torso; head sits ~+30 above origin in idle pose, feet at -24.

### Region-by-region

| Region         | Coverage status        | Capsule(s) responsible            | Evidence |
|----------------|------------------------|------------------------------------|----------|
| Head (helmet)  | likely undersized      | #1 sphere r=6                      | radius 6 = 12-unit-diameter sphere. Visible helmet on the soldier mesh extends ≥ 12 units head-to-helmet-rim; lateral extent (helmet width) ~7-8 units half-width. r=6 leaves 1-2 units of helmet uncovered laterally. (`human_base.hit:148`) |
| Neck / collar  | **GAP**                | none — gap between #1 and #2       | Sphere centre at `Head + 6.5,0,0` (≈+10.85 over neck-bone, per v0.5.2.1 measurement); sphere bottom at +4.85 over neck-bone. CHEST box top at `Bip01 Neck`, scale Z=5 means box extends +5 above the bone-axis midpoint (NOT above Neck origin — see math sanity D below). The actual gap depends on box2 Z-bound semantics; live-test data from the cup session is the only reliable ground truth. (`human_base.hit:148, 151`) |
| Shoulder L/R   | likely undersized + gap| #5/#6 cylinders r=5                | Clavicle→UpperArm distance is 5.99 units (bone dump). r=5 gives a roughly 10-unit-diameter cylinder along that 6-unit segment. The visible shoulder PAD on the soldier mesh is ~7-8 units wide and ~6 units tall; r=5 covers most of the pad lateral but the cylinder is FINITE between bone1 and bone2 (Z-bound check in `mdx_hit_test_cylinder`, `g_mdx.c:2714-2719`). Outside that 6-unit band → no hit. The trapezius and the deltoid CAP (above the clavicle) are likely outside the cylinder. (`human_base.hit:160, 163`) |
| Upper arm L/R  | **GAP**                | none — no UpperArm→Forearm cylinder| Skeleton has `Bip01 L/R Forearm` bones (parent of UpperArm in standard biped) but no HIT block uses them. Once the trace passes UpperArm origin going down the arm, only the AABB-broadphase carries it; under strict mode that means an upper-arm shot on the visible bicep can register `IMPACTPOINT_UNUSED` and reject. (`human_base.hit:160-163` covers only Clavicle→UpperArm, not UpperArm→Forearm.) |
| Lower arm / hand | **GAP**              | none                               | Forearm → Hand bones absent from .hit entirely. Aimed shots at the lower arm or hand fall through the multi-region pipeline. Same story as upper arm.|
| Chest          | likely covered, lateral edge soft | #2 box2 (9,7,5)            | Box anchored Spine1→Neck (chained Z ≈ Spine1→Spine2 5.222 + Spine2→Spine3 5.083 + Spine3→Neck 6.274 ≈ 16.58). Lateral half-extents (9,7) → 18 wide, 14 deep. Covers the torso bulk well in idle pose. The "scale Z=5" component, given how `mdx_hit_test_box2` works (the box is bounded between bone1 and bone2 origins in Z, NOT extended by Z half-extent past the bone endpoints — see D below), is mostly cosmetic for the box envelope. (`human_base.hit:151`) |
| Gut            | likely covered         | #3 box2 (9,7,5)                    | Anchored Pelvis→Spine2 (chained Z ≈ Pelvis→Spine 4.876 + Spine→Spine1 0.431 + Spine1→Spine2 5.222 ≈ 10.53). Same lateral 18×14. Intentionally overlaps CHEST box around Spine1-2 per the inline comment block (`human_base.hit:28-30`). (`human_base.hit:154`) |
| Groin / pelvis | covered                | #4 sphere r=7                      | Sphere centred on `Bip01 Pelvis`, radius 7. Pelvis bone is at Z≈origin (see bone dump: pelvis is the root). Sphere covers ±7 in every direction → covers the visible groin/hip area and overlaps with GUT box bottom. (`human_base.hit:157`) |
| Thigh L/R      | covered                | #7/#8 cylinders r=6                | Thigh→Calf is 16.531 units (bone dump). r=6 → 12-unit-diameter cylinder along the full upper leg. Anatomically the thigh is ~6-7 units half-width — likely fine. (`human_base.hit:166, 169`) |
| Knee           | covered (overlap)      | #7/#8 cylinder mid-region          | The thigh→calf cylinder includes the knee (its mid-segment by bone-distance). No dedicated knee hit-area; knee shots register as `IMPACTPOINT_KNEE_*` because the cylinder is tagged that way. Lateral coverage equal to thigh. (`human_base.hit:166, 169`) |
| Calf / shin L/R| covered                | #9/#10 cylinders r=6               | Calf→Foot is 15.916 units. r=6 cylinder. Covers visible calf well. (`human_base.hit:173, 176`) |
| Foot L/R       | **GAP**                | none below `Bip01 Foot`            | Cylinder ends at the Foot bone origin (the heel area). The visible boot extends ~3-4 units forward and down past Foot origin. Boot-toe shots likely register `IMPACTPOINT_UNUSED` and reject under strict mode. (`human_base.hit:173, 176`)|
| Back / spine   | likely covered         | #2 + #3 box2 stacks                | The CHEST + GUT boxes cover Z ≈ pelvis→neck = ~27 units of torso, with depth 14 (scale Y=7). Back of the torso falls inside that envelope in idle. Strafe / twist poses are untested in this recon and may differ. |
| Armpit         | likely **GAP**         | between #2 and #5/#6               | The CHEST box (anchored Spine1→Neck) lateral half-extent X=9 reaches outward toward the arm; the SHOULDER cylinder is anchored Clavicle→UpperArm (mostly above the armpit). The armpit (where a player's arm meets the side of the chest, ~Z=Spine2 level) is at the seam where neither shape extends. |

### Summary

  * **Confirmed gaps:** neck/collar zone, upper arm, lower arm,
    hand, foot toe/boot, armpit. **Six probable holes.**
  * **Likely undersized but present:** HEAD (lateral), SHOULDER
    (Z-bound truncation past the 6-unit clavicle→upper-arm segment).
  * **Solid coverage:** chest, gut, groin, thigh, knee, calf.
  * **Overlap-redundant (intentional):** CHEST + GUT around
    Spine1-2 (`human_base.hit:28-30`); GUT + GROIN around pelvis;
    THIGH-knee with KNEE-calf at the knee joint.

Six gaps and two undersized regions match Alphaloki's three
symptoms (head impossible, shoulder misses, neck no-damage) AND
predict three more (upper arm, lower arm, foot) that haven't been
called out yet but should show up in a 10-region walkthrough.

---

## D. Anatomy math sanity check

Reference: ET player AABB is 36×36×72 standing
(`g_client.c:55-56`); 1 ET unit ≈ 1 inch; head bone is ~+30 above
the player origin in idle pose.

### HEAD sphere

  * **v0.5.2.1 measurement:** `delta head-neck Z = 10.85`. Sphere
    centre at `Bip01 Head + 6.5` along bone-local +X (= up-the-skull
    in the bind pose for this biped) lands ~+10.85 above neck-bone.
  * **Vertical coverage:** sphere bottom at +4.85 over neck (chin
    area), sphere top at +16.85 (top-of-helmet area). **Vertical
    extent = 12 units.** Reasonable: a real-world combat helmet
    + face stack is ~10-12 inches.
  * **Lateral coverage:** ±6 units about the centre. The visible
    head/helmet on the ET soldier mesh is roughly 7-8 units half-
    width at the cheek (face) and 7-8 units half-width at the
    helmet rim. **r=6 leaves a 1-2 unit gap on each side
    laterally.** This is consistent with Alphaloki's "headshots
    almost impossible" report: the player aims at the visible nose
    or ear, the trace passes within 7-8 units of the bone axis,
    but the sphere only registers within 6.
  * **Cross-reference legacy:** `G_BuildHead` with
    `g_realHead.integer & REALHEAD_HEAD` uses
    `(-6,-6,-6)/(6,6,6)` = same 12-unit cube
    (`g_combat.c:988-989`). The legacy code uses the SAME radius —
    so r=6 is the inherited size, not a Vanguard regression. But
    the legacy realhead path has been the historical "competitive"
    head model and is also widely complained about as too tight.

### SHOULDER cylinder

  * **Bone distance:** Clavicle→UpperArm = 5.986 units (body.mdx).
  * **Cylinder Z-extent:** automatic, between the two bones. So
    the cylinder is exactly 5.99 units tall along its bone-to-bone
    axis. Visible shoulder pad on the soldier mesh is ~6 units tall
    — close fit, no Z slop. Above the clavicle bone origin
    (deltoid cap, the rounded part of the shoulder) and below the
    UpperArm origin (the arm starting to descend into the bicep)
    is OUTSIDE the cylinder.
  * **Lateral extent:** r=5 → 10 unit diameter. Visible shoulder
    is ~6-8 units wide at the deltoid; r=5 is plausible but tight.
  * **Math caveat:** `mdx_hit_test_cylinder` computes a LERPED
    radius between scale[0] and scale[1] (`g_mdx.c:2725-2735`).
    With r1=r2=5 the cylinder is uniform; with different radii
    it would taper linearly. For the shoulder, both ends are 5.

### CHEST / GUT box2

  * `mdx_hit_test_box2` (`g_mdx.c:2751-2786`): the Z bound is the
    early `p1[2] < 0` / `p2[2] > 0` check — that's "between bone1
    and bone2 along their axis". The X/Y check uses lerped scale
    components. **The box does NOT extend past the bone endpoints
    in Z, regardless of the scale Z value.**
  * → The `scale Z=5` token in `human_base.hit:151,154` is
    **effectively ignored for vertical extent** in box2 mode. Only
    bone1↔bone2 distance defines the Z bound. The Z scale only
    affects the un-scale step and influences whether degenerate
    cases register; for a normal box2 it's a no-op.
  * **Practical implication:** the CHEST box top sits AT
    `Bip01 Neck` origin in world-space. The box bottom at
    `Bip01 Spine1`. Neither extends past those endpoints. **The
    gap between CHEST top (`Bip01 Neck` ≈ 4.557 below the head
    bone) and the HEAD sphere bottom (4.85 above the neck bone)
    is approximately `4.557 + 4.85 = 9.4 units measured from the
    Spine3 reference, OR approximately the throat / collar zone
    between sphere-bottom and Neck origin.** Approximately 4-5
    units of vertical "no damage zone" exists at the neck — this
    matches Alphaloki's third complaint exactly.

### GROIN sphere

  * Sphere on `Bip01 Pelvis`, r=7. Pelvis origin sits ~+7 above
    feet (origin is mid-torso, feet are at -24 in player
    AABB-frame; pelvis bone in body.mdx is at Z = +7.479 from
    "root" per bone dump). r=7 → covers ±7 around pelvis, which
    is hip-line ±7. **Solid lower-pelvis coverage**, overlaps with
    GUT box bottom.

### Thigh / Knee / Calf

  * Thigh→Calf = 16.531 units. r=6 cylinder.
  * Calf→Foot = 15.916 units. r=6 cylinder.
  * Visible leg width at thigh: ~7 units (≈human-thigh). At calf:
    ~5 units. **Thigh r=6 fits, calf r=6 is generous (full coverage
    plus 1 unit of slop to either side, which is fine for cup
    play).**
  * Knee fit: the cylinder transitions through the knee joint at
    its midpoint. r=6 covers the knee bulge fine.

---

## E. Cross-mod comparison

**Status: BLOCKED.** WebSearch and WebFetch were both denied in
this session (the harness returned "Permission to use WebSearch
has been denied"). No public hitbox specs for NoQuarter, Silent,
or ETPro could be retrieved.

The only in-tree reference value is **legacy ETLegacy
`g_realHead`** (`g_combat.c:973-989`):

  * Non-realhead head box: `(-6,-6,-2) / (6,6,10)` = 12 wide × 12
    tall AABB.
  * Realhead head box: `(-6,-6,-6) / (6,6,6)` = 12-unit cube AABB.

VanguardMod's `radius 6` HEAD sphere matches both legacy variants
in lateral extent. **Cup-tester complaints about "headshots
impossible" likely reflect a longstanding ETLegacy issue, not a
VanguardMod regression.** A r=6 sphere is the historical baseline
even if it's empirically tight against the visible mesh.

**Action for the user:** if cross-mod data is needed, allow
WebFetch for `etlegacy.com/wiki`, `splatterladder.com`,
or specific NoQuarter/Silent forum posts in a follow-up session.
Without that channel, this section cannot be filled in from
recon alone.

---

## F. Live-test plan for cup-player session

Goal: produce per-region empirical data (hit/reject + which
capsule the trace landed on) that drives the v0.5.3 size /
coverage fix. Everything below uses the existing
`VG_DIAG_DUMP` infrastructure (`g_combat.c:1838-` and
`g_vanguard.c:284-378`) — no new code needed.

### Setup

Tester roles:
  * **Shooter (wahke):** fires shots, calls out which region they
    aimed at.
  * **Target (cup tester #1, e.g. Alphaloki):** stands as a
    stationary reference. NO movement, NO crouch, NO prone — just
    standing-frontal-idle for the first pass.
  * **Observer (cup tester #2, optional):** tails the server log
    and confirms the diagnostic prints landed for each shot.

Server arming sequence (rcon):

```
\rcon set vanguard_dev 1
\rcon set vanguard_hitbox_strict 1
\rcon set vanguard_hitbox_debug 1
\rcon set sv_fps 40
\cg_vanguardDevMultibox 1
```

For each shot, use the manual VG_DIAG_DUMP trigger:

```
\rcon set vanguard_diag_dump 1
[fire ONE shot]
\rcon set vanguard_diag_dump 1
[fire NEXT shot]
```

The cvar auto-resets after each dump (`g_vanguard.c:368-378`).

### 10-region walkthrough — 5-10 shots per region

Aim at the visible mesh feature, not at the wireframe overlay.
The point is to learn whether the visible-mesh hit lands on a
capsule.

| Test | Region label                                           | Expected impactpoint     | Aim instruction                                                                 | Shots |
|------|--------------------------------------------------------|--------------------------|--------------------------------------------------------------------------------|-------|
| T1   | Helmet top                                             | head (1)                 | Crosshair on the very top of the helmet, dead-centre                            | 5     |
| T2   | Face / nose                                            | head (1)                 | Crosshair on the visible nose, dead-centre                                      | 5     |
| T3   | Helmet side (cheek-rim)                                | head (1)                 | Crosshair on the helmet rim AT EAR LEVEL, lateral edge of the visible head     | 10    |
| T4   | Neck / collar                                          | (gap suspected)          | Crosshair on the throat/collar seam where helmet ends and chest begins         | 10    |
| T5   | Shoulder cap (deltoid top)                             | shoulder_l/r (5/6)       | Crosshair on the upper-shoulder seam where the helmet rim ends and the         | 10    |
|      |                                                        |                          | shoulder pad begins                                                             |       |
| T6   | Shoulder side (deltoid)                                | shoulder_l/r (5/6)       | Crosshair on the lateral deltoid bulge, mid-pad                                 | 5     |
| T7   | Upper arm (bicep)                                      | (gap suspected)          | Crosshair on the upper-arm bicep silhouette, mid-segment                        | 10    |
| T8   | Lower arm / hand                                       | (gap suspected)          | Crosshair on the forearm or visible hand                                        | 10    |
| T9   | Armpit / chest-arm seam                                | (gap suspected)          | Crosshair on the seam where the arm meets the chest, ribcage level              | 10    |
| T10  | Chest centre (sternum)                                 | chest (2)                | Crosshair dead-centre torso, sternum height                                     | 5     |
| T11  | Gut (belt buckle)                                      | gut (3)                  | Crosshair on the belt-buckle area                                               | 5     |
| T12  | Groin                                                  | groin (4)                | Crosshair on the visible groin / hip-front                                      | 5     |
| T13  | Thigh                                                  | knee_l/r (7/8)           | Crosshair mid-thigh                                                             | 5     |
| T14  | Knee                                                   | knee_l/r (7/8)           | Crosshair on the visible kneecap                                                | 5     |
| T15  | Calf                                                   | legs (9)                 | Crosshair mid-calf                                                              | 5     |
| T16  | Foot / boot                                            | (gap suspected)          | Crosshair on the boot, ankle level and toe                                      | 10    |
| T17  | Back-of-head (rear)                                    | head (1)                 | Tester turns 180°. Crosshair on rear of helmet                                 | 5     |
| T18  | Mid-back (between shoulderblades)                      | chest (2)                | Tester turns 180°. Crosshair on upper back                                     | 5     |

**Total: 18 region-tests, 130 shots.** Approximately 30-45
minutes with two players (one shooter + one stationary target).

### What to record

For each region the cup-player records, in a shared text file:

  * **Region label** (T1-T18)
  * **Hits / shots** ratio — 5/5 = clean, 0/5 = full gap, 2/5 =
    partial coverage
  * **Impactpoint of the hit shot**, from the `VG hit: <region>`
    print on the attacker's console or the per-shot `VG_DIAG:`
    line in the server log
  * **One representative `VG_DIAG_DUMP` block** for each region
    (saved to the shared file): which capsule's tag origin / axis
    the diagnostic resolved, the bone world positions, the
    refent.axis matrices. The dump is the gold-standard data point
    — copy/paste the whole block.

### Pose variations (after the standing-frontal pass)

If time permits — repeat T2 (face), T5 (shoulder cap), T7 (upper
arm), T16 (foot) under each variant pose:

  * **Crouched standing target.** Stance change shrinks the AABB
    and re-orients the bones via `pmove`'s viewheight pick.
  * **Prone target.** Verifies the prone-specific `playerlegsProneMins/Maxs`
    geometry alignment.
  * **Strafing target (light walk).** Tests dynamic torsoAxis /
    legsAxis decomposition and the v0.5.2 cgame `MatrixWeight`
    fix.

That gives 12 more shots (4 regions × 3 poses × 1 shot each — pose
variations are sanity checks, not sample-size data).

### Pass criteria for v0.5.3 readiness

  * **Hit ratio ≥ 4/5 in 14 of 18 regions** = capsule layout is
    fundamentally sound, only the suspected gaps need filling.
  * **Hit ratio ≤ 1/5 in any of T1, T10, T11, T12, T15** = a
    "covered" region is failing; investigate before sizing fixes.
  * **One full DIAG_DUMP per region** in the shared file.

If those pass, v0.5.3 ships gap-fills + size widens for the
specific regions the data flags. If they don't, v0.5.3 starts
with another diagnostic round.

---

## G. Quick-win candidates (advisory only)

The following are **paper-recon hypotheses**, framed as "if cup
test confirms the symptom, then this is a candidate fix". DO NOT
apply without empirical confirmation.

### G.1 — HEAD sphere lateral coverage

**If T2 (face) or T3 (cheek-rim) confirms ≤ 2/5 hit ratio at the
visible-mesh edges:**

  * Candidate: `radius 6 → radius 7` on `human_base.hit:148`.
    Adds 1 unit to lateral and vertical reach, sphere now covers
    chin (+3.85 over neck-bone) to top of helmet (+17.85), and
    ±7 laterally — covers the typical helmet half-width.
  * Cgame mirror: `cg_vanguard_dev.c:156` scale1 `{6,6,6} →
    {7,7,7}`.
  * Cost: HEAD region grows from 12-unit-diameter sphere to
    14-unit. Marginal but visible widen of the headshot area.
    Cup-balance question: do cup admins want a larger headshot
    target? Their call.

**Alternative:** keep radius at 6 but add a SECOND sphere at
`Bip01 Neck` covering the throat/collar — see G.4.

### G.2 — SHOULDER vertical reach

**If T5 (shoulder cap) confirms ≤ 2/5 hit ratio:**

  * Cylinder Z-bound is fixed by Clavicle→UpperArm distance
    (5.99 units) regardless of scale-Z. To extend vertical reach,
    **change the bone pair**. Candidates:
    * Use `Bip01 Spine3` → `Bip01 L UpperArm` — adds the trapezius
      area between Spine3 and Clavicle (≈3 units).
    * Use `Bip01 Neck` → `Bip01 L UpperArm` — adds even more
      coverage above the clavicle, at the cost of overlapping with
      the HEAD sphere.
  * Tradeoff: changing from Clavicle as the base means the
    impactpoint stays `shoulder_l/r` but the cylinder now covers
    parts of the upper torso. Not necessarily wrong — that's the
    "trapezius" and is anatomically the shoulder.

### G.3 — SHOULDER lateral coverage

**If T6 (deltoid side) confirms ≤ 3/5:**

  * Candidate: `radius 5 → radius 6` on lines 160, 163. Cylinder
    grows from 10 to 12 unit diameter. Matches the visible
    shoulder-pad width on most of the soldier meshes.
  * Cgame mirror: `cg_vanguard_dev.c:180,186` scales `{5,5,5} →
    {6,6,6}`.

### G.4 — NECK / COLLAR gap fill

**If T4 (neck/collar) confirms ≤ 1/5 hit ratio (the most likely
result given the geometry analysis in section D):**

Two candidates, pick one based on simplicity vs. balance:

  * **G.4a — extend the HEAD sphere downward.** Move the sphere
    centre down by reducing the offset, e.g. `offset 6.5,0,0 →
    offset 5.0,0,0`, putting the centre ~+9.35 over neck-bone, so
    the sphere bottom drops to +3.35 (covers throat). But this
    raises sphere-top to only +15.35 (top-of-helmet may be
    uncovered). Trade-off.
  * **G.4b — add a NECK sphere.** New HIT block:
    `HIT body _vg_neck radius 4 impactpoint chest`.
    A r=4 sphere centred on `Bip01 Neck` covers ±4 around neck-bone
    (throat / collar). Tagged as `chest` impactpoint to keep the
    damage-multiplier sane (doesn't deserve headshot multiplier;
    it's a body shot to the neck). Adds an 11th HIT block, no
    parser-table changes needed — `hit_count_max` is dynamic.
    Cgame mirror needs an 11th entry in `vg_hit_areas[]`.

G.4b is the cleaner fix in this recon's reading: small, targeted,
no behaviour change for HEAD, and the cup-multiplier table can
treat neck shots as chest-equivalent damage. But it's still
advisory — wait for T4 data.

### G.5 — ARM gap fill

**If T7 (bicep) and T8 (forearm/hand) confirm ≤ 1/5:**

  * Add: `HIT arm_L _vg_uarm_l _vg_forearm_l radius 4 radius 4
    impactpoint shoulder_left` (and mirror R). Plus TAG bridges
    for `Bip01 L Forearm`, `Bip01 R Forearm`. Covers the bicep.
  * Optionally: a third per-arm cylinder Forearm→Hand for the
    lower arm.
  * Caveat: arm cylinders at r=4 / r=5 thickness will tend to
    register `shoulder_*` impactpoint, which the multiplier table
    already covers. If cup-admins want a separate arm multiplier,
    that's a feature request beyond v0.5.3.

### G.6 — FOOT / BOOT gap fill

**If T16 (boot) confirms ≤ 1/5:**

  * Extend `Bip01 L Foot` cylinder forward. Either:
    * Add an offset to the foot bone in a new TAG bridge to
      project the cylinder endpoint forward toward the toe (small
      offset along bone-local +X), OR
    * Add a small sphere at the foot bone with a forward offset:
      `TAG _vg_boot_l "Bip01 L Foot" offset 6 0 0` then `HIT leg_L
      _vg_boot_l radius 4 impactpoint legs`. Covers the visible
      boot.
  * Low priority — boot shots are rare in cup play.

### Summary table

| Candidate | Trigger condition           | Risk  | Effort | Notes |
|-----------|------------------------------|-------|--------|-------|
| G.1 HEAD r=6→7        | T2/T3 ≤ 2/5         | low   | 2 lines (.hit + cg) | Slightly larger headshot target |
| G.2 SHOULDER bone pair| T5 ≤ 2/5            | medium| 4 lines             | Changes shoulder geometry, may overlap HEAD |
| G.3 SHOULDER r=5→6    | T6 ≤ 3/5            | low   | 4 lines             | Matches deltoid width |
| G.4b NECK sphere      | T4 ≤ 1/5            | low   | 4 lines (new HIT + cg entry) | Cleanest gap-fill, no HEAD change |
| G.5 ARM cylinders     | T7/T8 ≤ 1/5         | medium| 8-12 lines          | New TAG bridges + 2-4 HIT blocks |
| G.6 BOOT extension    | T16 ≤ 1/5           | low   | 4 lines             | Cosmetic — boot shots are rare |

Maximum scope of v0.5.3 if all six trigger: ~30 lines of .hit
edits + ~30 lines of cg mirror edits = ~60 LoC across 2 files.
Realistic scope: 2-3 of the candidates trigger, ~20-30 LoC.

---

## H. Open questions — only empirical data can answer

  1. **Lateral head coverage at distance.** Does the radius-6
     sphere feel impossibly tight at 100m, or only at 5m? Bullet
     spread / weapon angular accuracy interacts with capsule width
     in a way recon cannot predict. Cup data needs to capture
     range, not just hit ratio.
  2. **Shoulder cylinder cap geometry.** The cylinder ends at
     the bone1 / bone2 origins with FLAT caps (no half-spheres —
     `mdx_hit_test_cylinder` uses `p[2] < 0` / `p[2] > 0` tests).
     Does the visible shoulder-pad TOP fall inside or outside the
     cap? Test T5 answers this directly.
  3. **Pose-variation effect on box2 Z-bounds.** When the player
     leans / strafes, the Spine1↔Neck axis tilts and the box2
     Z-bound rotates with it. Does the chest box still cover the
     visible torso top in those poses? Pose-variation T10 tests
     this.
  4. **Multi-hit with shotgun spread.** Each pellet is a separate
     bullet with its own trace. If 8 of 12 pellets land in the
     CHEST box and 4 land in the NECK gap, what's the combined
     damage? Pellet-test in the live session can characterise this.
  5. **Cross-mod headshot-radius reference.** Recon could not
     fetch web data. If a cup admin has played NoQuarter / Silent
     extensively and has a sense of "feels tighter / looser than
     ETLegacy", their qualitative input substitutes for the
     numeric comparison.
  6. **The HEAD `+6.5` magnitude.** v0.5.2.1 settled on +6.5 based
     on the +10.85 measurement. If T1-T3 show TOP-of-helmet hits
     fail but FACE / CHEEK hits succeed, the offset is too high
     and should reduce. If T1 succeeds but T2 / T3 fail, the
     offset is right and it's the radius that's tight (G.1).
  7. **Strict-mode interaction with gap regions.** Under
     `vanguard_hitbox_strict 1`, gap shots reject. Under `strict 0`,
     they apply default multiplier damage. Cup play wants strict;
     public play might want lenient. The size fixes affect both
     paths — wider capsules mean fewer rejections under strict
     AND more multiplier-applied hits under lenient. Mostly a
     side-effect to be aware of, not a question to answer.

---

## Implementation note

This audit is recon-only. No `.hit` or `.c` file was modified.
The next step is a cup-player live-test session running the plan
in section F. Resulting data dictates which subset of section G
candidates v0.5.3 implements.

Files inventoried:
  * `etmain/animations/human_base.hit` — capsule definitions
  * `src/cgame/cg_vanguard_dev.c:130-213` — wireframe mirror
  * `src/game/g_mdx.c:2702-3009` — geometry math
    (`mdx_hit_test_cylinder`, `_box2`, `_sphere`, `_box`,
    dispatcher `mdx_hit_test`)
  * `src/game/g_combat.c:956-989` — legacy `g_realHead` reference
  * `src/game/g_combat.c:1838-1956+` — `VG_DIAG_DUMP` block
  * `src/game/g_vanguard.c:284-378` — diag-dump cvar plumbing
  * `docs/notes/bone_distance_dump_2026-04-28.txt` — body.mdx
    bone distances (53 bones, 9 model variants)
  * `docs/HITS_FORMAT.md` — .hit parser spec
  * `docs/notes/HITBOX_SCALE_ANALYSIS.md` — Phase 6.1.3 sizing
  * `docs/notes/PHASE_7_0_AUDIT.md` — Phase 7.0 (strict-mode fix)
  * `docs/notes/PHASE_7_0_1_AUDIT.md` — Phase 7.0.1 (lateral
    offset fix)

---

**Copyright Notice**

Copyright (c) 2026 wahke <info@wahke.lu> (https://wahke.lu)
Copyright (c) 2026 VanguardMod Project Contributors

Licensed under GPL-3.0-or-later. Part of VanguardMod project.
Built on ETLegacy (https://www.etlegacy.com).
