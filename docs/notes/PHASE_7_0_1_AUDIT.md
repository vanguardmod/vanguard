# Phase 7.0.1 — Capsule Lateral Offset Recon

**Date:** 2026-04-30
**Status:** Pure recon. No code changes. User-decision after this report.
**Scope:** Investigate why multi-region capsules sit lateral to the
visible player mesh, exposed by v0.5.1's working strict-mode.

---

## TL;DR

The problem is not Hypothesis A or C cleanly. **Hypothesis B is closest
but needs empirical confirmation before shipping a fix.** The user's
observation that `_vg_chest` (a bone-pair box2 with NO offset) is also
displaced rules out "wrong offset axis" as the sole cause — there's no
offset to be wrong on those. The remaining surface is either:

  (B1) **qagame `mdx_bone_orientation` diverges from the engine
       renderer's `R_CalcBones`** for some bones in animation poses,
       so the capsule and visible mesh share the same .mdx file but
       compute slightly different world positions from it.
  (B2) **The HEAD `+6.5 0 0` offset is in the wrong bone-local
       direction** for the actual `tmpaxis` produced by
       `mdx_bone_orientation` in idle pose. v0.4.2 picked the axis
       empirically when the broken strict-mode hid the discrepancy
       — now exposed.

Recommended path: ship a diagnostic-only v0.5.2-rc1 that dumps the
relevant world positions for one frame, then ship the actual fix as
v0.5.2 (or v0.5.2 final) once data tells us whether (B1) or (B2)
dominates.

---

## A. Where capsules anchor to skeleton bones

### The .hit file (asset side)

`etmain/animations/human_base.hit` defines 13 internal tags wrapping
real `Bip01 *` skeleton bones, plus 10 `HIT` blocks that consume
those tags:

```
TAG _vg_head      "Bip01 Head" offset 6.5 0 0
TAG _vg_neck      "Bip01 Neck"
TAG _vg_spine_lo  "Bip01 Spine"
TAG _vg_spine_mid "Bip01 Spine1"
TAG _vg_spine_up  "Bip01 Spine2"
... (same for clavicle/upper-arm/thigh/calf/foot, L+R)

HIT head _vg_head radius 6 impactpoint head
HIT body _vg_spine_mid _vg_neck scale 9 7 5 scale 9 7 5 impactpoint chest box
HIT body _vg_pelvis _vg_spine_up scale 9 7 5 scale 9 7 5 impactpoint gut box
... (groin sphere, L+R shoulder/knee/leg cylinders)
```

Only `_vg_head` carries an offset. The other twelve interntags wrap
their bone with `offset = (0, 0, 0)`, `axis = identity`, `ishead =
qfalse` — so any displacement they show is **purely bone-position
driven**.

### The bone-position chain (qagame side)

`mdx_hit_test` (`g_mdx.c:2754-2953`) is the entry point. For each
hit-area:

1. `mdx_tag_orientation(refent, tag_idx, o1, a1, ishead, 0)` — looks
   up the interntag, recurses into `mdx_bone_orientation` for the
   actual bone.
2. `mdx_bone_orientation(refent, bone_idx, origin, tmpaxis)`
   (`g_mdx.c:1598-1684`) — reads the per-bone `mdx_bones[bone_idx]`
   model-frame position (populated in advance by `mdx_calculate_bones`),
   computes a `tmpaxis` matrix from the bone's per-frame `anglesF` and
   the `refent->torsoAxis` weighted by `bone->torso_weight`.
3. Back in `mdx_tag_orientation`: `vec3_rotate(tag->offset, tmpaxis,
   offset)` rotates the interntag's offset (e.g. `_vg_head`'s
   `(6.5, 0, 0)`) into model-frame, adds it to `origin`.
4. Final world transform at `mdx_tag_orientation:1748-1764`:
   `world = refent->origin + sum_k(model_local[k] * refent->axis[k])`.

`mdx_bone_orientation` produces the `tmpaxis` matrix as:

```c
AnglesToAxis(angles, tmpaxis);            // bone's intrinsic rotation
TransposeMatrix(tmpaxis, axis1);          // inverse
MatrixWeight(refent->torsoAxis, bone->torso_weight, tmpaxis);
MatrixMultiply(axis1, tmpaxis, axis);     // = transpose(bone) * weighted_torso
```

(`g_mdx.c:1668-1679`) — note the `// FIXME: Why is this transpose
needed?` comment from upstream Zinx. The exact frame-mapping was
never formally documented; v0.4.2 picked `+6.5 along bone-local +X`
empirically by visual eyeball.

### The bone-position chain (cgame side)

`vg_mdx_compute_bone_world_with_offset` (`cg_vanguard_mdx.c:612-`)
mirrors the qagame chain, with one deliberate difference flagged in
the comment block at `cg_vanguard_mdx.c:539-552`:

```c
/* Direct port of mdx_bone_orientation's axis-only path
 * (g_mdx.c:1644-1673), simplified for cgame: the qagame torso-
 * axis mixing via MatrixWeight is omitted because cgame's
 * refent->torsoAxis is set to body->axis (the player's WORLD-
 * frame rotation), not the qagame in-MODEL torsoAxis.
 */
```

That comment was written for v0.4.1 / v0.4.2 when `vg_BuildBodyRefent`
manually constructed the refent (`AxisCopy(body->axis,
body->torsoAxis);`). **In v0.4.3 we switched to using
`cent->pe.bodyRefEnt`** directly, which has `torsoAxis` set by
`CG_PlayerAngles` — the SAME way qagame's `mdx_PlayerAngles` sets it.
The premise of the omission is now stale.

→ **v0.5.1 cgame viz still skips MatrixWeight** even though
torsoAxis is now in the same frame as qagame's. For bones with
`torso_weight != 0` (which includes Bip01 Head and most upper-body
bones), this means cgame and qagame compute slightly different
bone-axis matrices on dynamic poses.

But the user reports the cgame viz AGREES with the server trace. So
either:
  - The discrepancy is small in idle/frontal poses (likely — torsoAxis
    ≈ legs axis ≈ identity-ish in static idle), OR
  - The user happened to test mostly in poses where the discrepancy
    didn't manifest visually.

**This is a latent bug regardless of the lateral-offset fix.** When
the cup live-test moves into dynamic torso-twist poses (strafe,
quick-turn-and-shoot), the cgame viz might disagree with the server
trace in ways the user hasn't tested for yet. Worth fixing in the
same patch.

---

## B. Skeleton-layout sanity check

The skeleton driving both qagame and cgame is the standard ETLegacy
human biped (`etmain/animations/human/base/body.mdx`). Bone-distance
dump from prior recon (`docs/notes/bone_distance_dump_2026-04-28.txt`):

  - `Bip01 Head`     idx=6,  parent=Bip01 Neck, parent_dist=4.557
  - `Bip01 Neck`     idx=5,  parent=Bip01 Spine3
  - `Bip01 Spine1-3` idx=2-4
  - `Bip01 Pelvis`   idx=0
  - L/R Clavicle / UpperArm / Thigh / Calf / Foot — all confirmed
    present and at expected indices

The `.hit` file's TAG bone-name strings (`"Bip01 Head"` etc.) are
case-sensitive `strcmp` lookups via `mdx_bone_lookup` (`g_mdx.c:496-507`).
All 13 bone names verified to match the .mdx skeleton. **Hypothesis C
(skeleton-layout mismatch) is ruled out** — same skeleton, correct
indices, no foreign-mod imports.

---

## C. Yaw / pitch / animation-state composition

`mdx_PlayerAngles` (`g_mdx.c:1956-`) decomposes the player's
`s.apos.trBase` viewangles into three axes via `CG_SwingAngles`-
equivalent logic:

  - `legsAngles` — legs face movement direction, swung
  - `torsoAngles` — torso between legs and head, swung
  - `headAngles` — head looks where the player aims

Then in `mdx_gentity_to_grefEntity`:

```c
AnglesToAxis(legsAngles,  refent->axis);
AnglesToAxis(torsoAngles, refent->torsoAxis);
AnglesToAxis(headAngles,  refent->headAxis);
```

So `refent->axis` is legs-frame, `torsoAxis` is torso-frame,
`headAxis` is head-frame. These compose into the bone-axis chain via
`MatrixWeight` in `mdx_bone_orientation`.

For the LATERAL displacement question: in **frontal idle** (the test
scenario the user used), all three angles agree (player faces target,
not strafing). `torsoAxis` and `legsAxis` are nearly equal,
`headAxis` follows view direction. This collapses the bone-axis math
to nearly `transpose(bind_pose_bone_rotation) * legs_axis`.

What's `bind_pose_bone_rotation` for Bip01 Head? Without sampling the
.mdx binary directly, my best guess is "identity in T-pose, but
ETLegacy's idle animation has a small head adjustment". The
discrepancy from "true T-pose" times 6.5 units of offset is a
plausible source of a few-units lateral displacement.

---

## D. Which hypothesis fits — diagnosis

### What the user observed
  - Crosshair on visible nose → 11/11 strict-rejects (capsule far
    from where mesh nose is rendered)
  - Crosshair on red HEAD-sphere (cgame viz) → headshot
  - **Chest, shoulder also displaced** — these have NO offset, just
    bone-pair-defined cylinders/box2

### Hypothesis A (Bone-Anchor Offset Wrong)
Affects `_vg_head` (and only that — only HEAD has an offset). Cannot
explain chest/shoulder displacement on its own.

**Verdict for HEAD specifically:** likely a contributor.
**Verdict for chest/shoulder:** ruled out (no offset to be wrong).

### Hypothesis B (Yaw / bone-axis composition)
Has two distinct sub-flavours:

**B1 — qagame mdx vs renderer R_CalcBones divergence.** The engine
renderer's `R_CalcBones` (`renderer/tr_animation_mdm.c`,
`R_MDM_GetBoneTag` consumer at `tr_animation_mdm.c:2183-2233`) is a
DIFFERENT bone-position pipeline from qagame's
`mdx_calculate_bone_lerp` + `mdx_bone_orientation`. Both read the
same .mdx file; sign conventions, matrix transpose ordering, or
torso-axis composition might differ. If they do, the visible mesh
sits at "renderer's bone positions" while our capsules sit at
"qagame's bone positions" — close but not identical.

This would explain BOTH the head displacement AND the chest/shoulder
displacement consistently.

**B2 — HEAD `+6.5 0 0` offset axis is wrong.** Specific to HEAD.
v0.4.2 picked the axis by visual eyeball when the broken strict-mode
hid the discrepancy — now exposed. Could be a stand-alone bug for
HEAD in addition to (B1) for non-HEAD capsules.

### Hypothesis C (Skeleton Layout Mismatch)
Ruled out per section B above. Same skeleton, correct indices.

### Most likely diagnosis

**B1 + B2 in combination.** The chest/shoulder evidence demands B1.
The HEAD-specific behaviour might be B1 alone, or B1+B2 — can't tell
without empirical data.

**Latent bug also present:** cgame's `vg_mdx_compute_bone_axis_local`
omits `MatrixWeight`, which since v0.4.3 is no longer the right call.
Would manifest as cgame-viz vs server-trace mismatch in dynamic
torso-twist poses (not the static idle the user tested). Worth
fixing in the same patch even though it's not the headline bug.

---

## E. Implementation sketch

### Stage 1 — empirical diagnostic (ship as part of v0.5.2 fix or as
separate v0.5.2-rc1)

Add a one-shot `VG_DIAG` print in `g_combat.c` (or a new
`g_diag` helper) that captures, for one bullet trace per session
when `vanguard_hitbox_debug 1`:

```
VG_DIAG: capsule-positions client=N (frontal-idle test)
  Bip01 Head bone world  = (X, Y, Z)
  Bip01 Head capsule wld = (X, Y, Z)   ; with +6.5 0 0 offset
  tag_head world (engine) = (X, Y, Z)  ; via trap_R_LerpTag
  Bip01 Spine1 bone wld  = (X, Y, Z)
  Bip01 Neck bone wld    = (X, Y, Z)
  refent.origin           = (X, Y, Z)
  refent.axis row0/1/2    = ...
  refent.torsoAxis row0/1/2 = ...
```

Three comparisons fall out of this dump:

  1. **Bip01 Head bone world** vs **tag_head world (engine)** — if
     they diverge, that's the smoking gun for B1 (qagame mdx code
     produces different bone positions than the engine renderer).
     If they agree, the bone math is consistent across both paths.
  2. **Bip01 Head capsule wld** vs **Bip01 Head bone world** —
     should differ by exactly 6.5 along some direction. Whether
     that direction is up-the-skull, forward, or sideways tells us
     directly which bone-local axis the offset projects to.
  3. **refent axes** — sanity check on the player-orientation
     composition.

### Stage 2 — fix based on Stage 1 data

Branch tree depending on findings:

  - **B1 confirmed (bone-positions differ):** the fundamental fix is
    invasive (rewriting qagame's mdx_calculate_bone_lerp to match
    the engine's R_CalcBones, or routing the trace through the
    engine via a new trap). For v0.5.2: empirically tune the .hit
    capsule positions (offsets, scales) to compensate for the
    discrepancy in idle/standing pose. Won't be perfect across all
    poses but should bring frontal-idle into alignment with the
    visible mesh.
  - **B1 ruled out, B2 confirmed (HEAD offset axis wrong):** swap
    `offset 6.5 0 0` for the correct direction (`0 6.5 0`,
    `0 0 6.5`, or a negative variant). The diagnostic dump's
    `tag_head world` vs `Bip01 Head bone world` direction tells
    us which.
  - **Both apply:** Stage 2 = HEAD axis fix (B2) + .hit empirical
    tuning for non-HEAD capsules (B1).

### Stage 3 — fix the latent cgame `MatrixWeight` omission

Independent of the lateral-offset fix, update `cg_vanguard_mdx.c`
`vg_mdx_compute_bone_axis_local` to include the MatrixWeight step
that qagame uses. The comment block claiming the omission is correct
was written for v0.4.1 when refent->torsoAxis was set differently;
since v0.4.3 the premise is stale.

```diff
-    AnglesToAxis(angles, pre);
-    TransposeMatrix(pre, outAxis);
+    vec3_t intrinsic[3];
+    vec3_t weighted[3];
+    AnglesToAxis(angles, intrinsic);
+    TransposeMatrix(intrinsic, outAxis);
+    /* Match qagame's MatrixMultiply(axis1, weightedTorso, axis).
+     * Since v0.4.3 refent->torsoAxis is set by CG_PlayerAngles,
+     * the same way qagame's mdx_PlayerAngles sets it — the v0.4.1
+     * "wrong frame" rationale no longer applies. */
+    vg_mdx_MatrixWeight(body->torsoAxis, legsModel->bones[i].torso_weight, weighted);
+    MatrixMultiply(outAxis, weighted, intrinsic);
+    AxisCopy(intrinsic, outAxis);
```

Plus the small `vg_mdx_MatrixWeight` helper port from `g_mdx.c:165-`
(8 lines).

---

## F. Risk-assessment

| Concern | Status |
|---|---|
| Damage-multiplier-logic | ✓ unaffected. Multi-region branch reads `mdx_ip` from the same code; only the world position the trace endpoint compares against shifts. |
| Strict-mode reject | ✓ unaffected. Same `IMPACTPOINT_UNUSED` predicate. |
| Mounted MGs | ✓ unaffected. The v0.5.1 isHeadshot-gate-drop is independent of capsule position. |
| Splash damage | ✓ unaffected. Different code path entirely. |
| Cup-admin UX | ✓ improvement. Capsules will sit on the visible mesh; "shoot at the model = damage" semantics finally align with the rendered visualisation. |
| Live-test regression risk | medium. A .hit file empirical tune (if Stage 2 needs it) might over-correct for one pose and under-correct for another. Reduce by tuning ONLY in idle/standing reference pose first. |
| Phase 7.0.1 → Phase 7.0.2 dependency | low. Once lateral offset is correct, the capsule-coverage-gap test (user's planned 10-region walkthrough) measures the actual overlap geometry, not displacement artefacts. Cleaner data. |

---

## G. Code-change estimate

| Stage | LoC | Files |
|---|---:|---|
| 1 — diagnostic print | ~25 | `g_combat.c` (or new `g_diag.c`) |
| 2a — HEAD offset axis swap | 1 | `etmain/animations/human_base.hit` |
| 2b — .hit empirical tuning | 5–20 | `human_base.hit` (per-region offset/scale tweaks) |
| 3 — cgame MatrixWeight fix | ~20 | `cg_vanguard_mdx.c` |
| Release notes + audit doc update | ~50 | `RELEASE_NOTES.md` + this file's "Implementation notes" footer |

**Most likely actual diff** (assuming B1 + B2 from data):
  - 2 files in the actual fix (`human_base.hit`, `cg_vanguard_mdx.c`)
  - ~30 LoC code + asset
  - 1 commit `fix(hitbox): align multi-region capsules with visible
    mesh` + 1 commit `chore: prep v0.5.2 release`

Roughly half a day of Claude-time, plus the live-test roundtrip on
Pterodactyl for the empirical tuning loop.

---

## H. Live-test path after fix

1. Deploy v0.5.2 pk3 to Pterodactyl host.
2. Frontal-idle bot setup (same as v0.5.1 live-test):
   ```
   \rcon set vanguard_hitbox_strict 1
   \rcon set vanguard_hitbox_debug  1
   \rcon set vanguard_dev           1
   \cg_vanguardDevMultibox 1
   \rcon bot addbot wolfdude /skill 0 axis
   ```
3. **Test 1: HEAD on visible mesh.** Aim crosshair at the bot's
   visible nose. Fire one rifle shot.
   - Pass: `VG_DIAG: mdx_hit_test -> impactpoint=1 (head)` log line,
     `VG hit: head (mult=2.00)` print, bot HP drops by ~50.
   - Fail: `impactpoint=0` + `strict-hitbox reject` line. Reach for
     the diagnostic log to see how far off the capsule is.
4. **Test 2: HEAD-side displacement.** Aim 8 units to the LEFT of
     the visible nose (clearly off the model). Fire.
   - Pass: `strict-hitbox reject` line, no HP change.
   - Fail (still applying damage): means strict-mode is broken —
     should have been caught by v0.5.1 testing, escalate.
5. **Test 3: CHEST on visible torso.** Aim center-mass on the bot.
   - Pass: `impactpoint=2 (chest)`, HP drops with chest multiplier.
   - Fail: indicates chest capsule still displaced, .hit tuning
     incomplete.
6. **Test 4: SHOULDER on visible shoulder.** Aim at the bot's left
   or right shoulder.
   - Pass: `impactpoint=5 or 6 (shoulder_l/r)`, HP drops with
     shoulder multiplier.
7. **Test 5 — pose variation.** Make the bot crouch (or shoot a
   live player who crouches), then test 1 + 3 + 4 again. Confirms
   the fix works across stance changes, not just standing.
8. **Test 6 — strict-mode regression.** Set `vanguard_hitbox_strict
     0`, repeat Test 2. Should hit the bot via legacy fallback.

Pass criteria: tests 1–5 produce hits at the visible mesh, test 2
rejects, test 6 falls through to legacy.

If tests 1, 3, 4 all hit but with the **wrong region multiplier**
(e.g. shooting visible chest registers as `impactpoint=3 (gut)`),
that's still a fix — the capsule covers the right area, just the
mapping of capsule-to-impactpoint needs adjustment. Phase 7.0.2
territory.

---

## I. Bonus question — combine with capsule-coverage-gaps?

User's plan: shoot 10 body regions (Helm, Brust, Bauch, Oberschenkel,
Schulter, Achsel, Innenschritt, Arme, Hände, Füße) and document gaps.

**Recommendation: keep v0.5.2 (lateral offset) and v0.5.3 (coverage
gaps) separate.**

  - The 10-region test measures **actual capsule coverage** geometry.
    If lateral offset is uncorrected, the test results conflate
    "capsule is in the wrong place" with "capsule has the wrong
    size/shape" — same underlying symptom, different fix.
  - With lateral offset corrected first, the 10-region walkthrough
    measures the geometry of capsules that ARE in the right place.
    Any "miss" then is genuinely a coverage gap (e.g. armpit not
    covered by shoulder cylinder, hand not covered by anything),
    and the .hit tuning is straightforward (extend cylinder caps,
    add new sphere for hand if needed).
  - **Don't mix them in one release.** The lateral offset fix is
    risk-bounded (.hit file edits for known-correct anchors). The
    coverage-gap fix is broader (could need new TAG entries, new
    HIT entries, capsule shape changes). Mixing them would entangle
    the live-test feedback signal.

**Proposed sequence:**

  - **v0.5.2** = lateral offset fix (this audit's recommendations).
    Goal: capsules sit ON the visible mesh in idle pose.
  - **wahke runs the 10-region test** on v0.5.2 with strict-mode +
    debug on. Reports gaps.
  - **v0.5.3** = coverage-gap fix based on user's data. Goal: every
    visible body region has a corresponding capsule.

Two releases, two concerns, two clean live-test signals.

---

## Open questions for the user before Phase 7.0.1 implementation

1. **Stage 1 diagnostic only as v0.5.2-rc1, or stage 1 + stage 2
   together as v0.5.2?** Going rc1 first is safer (data first, fix
   second) but doubles the deploy roundtrips. Direct combined release
   is faster but fixes might be wrong if data points elsewhere than
   B1+B2.
   - Recommendation: **rc1 first**. The recon analysis here is
     fact-bounded but the actual axis-direction question is
     empirical. One rc1 round-trip is cheap, and the fix lands
     correctly the first time vs ping-pong on guesses.

2. **Stage 3 (cgame MatrixWeight) — same release as Stage 2 or
   separate?**
   - Recommendation: **same release**. The fix is small (~20 LoC),
     orthogonal to the lateral-offset issue (different code path),
     and pre-emptively patches a latent bug that would surface in
     dynamic poses anyway. Bundling avoids a separate v0.5.2.1.

3. **Empirical tuning bound** — should the .hit empirical tuning
   target "frontal idle pose" alone, or attempt a multi-pose
   compromise?
   - Recommendation: **frontal idle alone first**. Multi-pose is the
     next refinement once the baseline-pose works. Don't optimize
     for cases the user hasn't tested yet.

4. **Versioning** — v0.5.2-rc1 → v0.5.2, or just v0.5.2?
   - Recommendation: **rc1 → v0.5.2** per the established Cup-Mode
     pattern (v0.5.0-rc1 was demoted in favour of direct release
     because the verify-after-set helper covered the risk; here the
     RECON has known unknowns, so rc1 is the right safety net).

5. **Combine with capsule-coverage-gaps (bonus)?**
   - Recommendation: **NO**, ship sequentially. v0.5.2 = lateral
     offset; v0.5.3 = coverage gaps after wahke's 10-region test.
     Detailed reasoning in section I above.

---

**Copyright Notice**

Copyright (c) 2026 wahke <info@wahke.lu> (https://wahke.lu)  
Copyright (c) 2026 VanguardMod Project Contributors

Licensed under GPL-3.0-or-later. Part of VanguardMod project.
Built on ETLegacy (https://www.etlegacy.com).
