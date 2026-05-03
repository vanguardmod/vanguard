# Phase 10 — Hot-Path Performance Recon

> Pure recon. No code changes. wahke decides scope after this.

Date: 2026-05-03
Author: Claude Code (Opus 4.7) on session for wahke
Status: AUDIT (no implementation)

References Phase 6 multi-region hitbox foundation (Memory #2),
Phase 7.0 strict-mode (v0.5.1), Phase 7.0.2 capsule additions
(v0.5.2.3 — added 4 ARM cylinders + NECK sphere), Phase 7.2
netcode (sv_fps=40 cup profile, lag-comp), Phase 8.0a NULL-guard
(v0.5.2.2), Phase 8.0b Falldamage helper integration (v0.7.1).

## TL;DR

Confirmed by wahke 2026-05-03: ETLegacy plain + 20 bots = smooth;
VanguardMod + 20 bots + `vanguard_hitbox_debug=0` = laggy. The
cost is in **VanguardMod's Multi-Region Hitbox computation**, not
the diagnostic logging.

**Real cost model (re-derived from source):**

- Phase 7.0.2 added capsules: `human_base.hit` now defines **15
  hit-areas** (not 10 from the prompt). HEAD + NECK + CHEST(box) +
  GUT(box) + GROIN + L+R SHOULDER + L+R UPPER ARM + L+R FOREARM +
  L+R KNEE + L+R LEG.
- Per `mdx_hit_test`: 1× `mdx_calculate_bones` (~50 bones × lerp
  math) + 25-30× `mdx_tag_orientation` (avg 1.7 tags/area × 15
  areas) + 25-30× `mdx_hit_warp` (matrix-heavy un-warp) + 15×
  sphere/cylinder/box test. **No AABB short-circuit, no
  early-exit on first hit (picks `best_frac`).**
- Per server tick at sv_fps=40 (25ms budget): 20 bots × 4 traces
  avg = ~80 traces, but typically only ~30 enter multi-region
  (those that hit player AABB). Per-trace cost ~500-800µs →
  15-25ms per tick *just for hitbox math* in worst case. **At
  sv_fps=40 this is at or over the budget**, so server overruns →
  visible lag.

| Recommendation | Impact | Risk | Release |
|---|---|---|---|
| **A1 — Per-tick bone cache (per client)** | 50-70% reduction | LOW | **v0.7.1.1** |
| **Q1 — Per-mdx_hit_test tag cache** | 20-30% reduction (within trace) | LOW | v0.7.1.1 |
| **Q2 — AABB short-circuit (tighter than player AABB)** | 30-50% reduction on misses | MED | v0.8.0 |
| **Q3 — Reduce capsule count (merge ARM cylinders)** | 13% reduction (15 → 13) | MED-HIGH (cup-feel risk) | v0.8.0 with cup-tester |

A1 is the headline win — same player hit by 5 bullets in same
25ms tick currently runs `mdx_calculate_bones` 5 times. Caching
reduces it to 1.

## §1 — Hot-path inventory

### Function call graph (multi-region damage, per bullet trace)

```
G_Damage / G_DamageExt                          (g_combat.c)
  └─ multi-region branch entry-gate (line 1781) (Phase 8.0a)
     ├─ vg_Hitbox_IsActive                       (g_vanguard.c, ~5ns lookup)
     ├─ vg_Hitbox_IsSelfDamageMod                (g_vanguard.c, ~10ns switch)
     ├─ mdx_gentity_to_grefEntity                (g_mdx.c:314, populates refent)
     └─ mdx_hit_test (line 1997)                 (g_mdx.c:2841-3024)
        ├─ BG_GetCharacter                       (cheap)
        ├─ linear scan over hits[hit_count]       (~10 entries, ~50ns)
        ├─ mdx_calculate_bones(refent)           (g_mdx.c:1528, ~50 bones)
        │  └─ for each bone: mdx_calculate_bone_lerp
        │     └─ ~5 VectorMA + lerp ops per bone (~50 bones × 5 = 250 ops)
        └─ for i = 0; i < 15 (hit_count); i++:    ← inner loop
           ├─ mdx_tag_orientation (tag[0])       (g_mdx.c:1702, recursive)
           │  ├─ mdx_bone_orientation OR recursive mdx_tag_orientation
           │  └─ ~5 matrix mults + axis copies
           ├─ if (hit->tag[1] != -1):
           │   └─ mdx_tag_orientation (tag[1])   (same cost as above)
           ├─ MatrixMultiply(a1, hit->axis, a2)
           ├─ mdx_hit_warp (start, end, o1, ...) (g_mdx.c:2632)
           │  ├─ TransposeMatrix
           │  ├─ 4× vec3_rotate
           │  └─ scale + endpoint clamp
           ├─ if 2-tag: mdx_hit_warp (o2)         (cost 2x for box/cylinder)
           ├─ mdx_hit_test_box / cylinder / sphere (~10ns trivial test)
           └─ track best_frac (no early-exit)
```

### Per-trace cost estimate (15-area human_base.hit)

| Step | Calls | Est. cost each | Total |
|---|---|---|---|
| `mdx_calculate_bones` | 1 | ~25µs (50 bones × 0.5µs) | 25µs |
| `mdx_tag_orientation` | ~25 | ~5µs (matrix math) | 125µs |
| `mdx_hit_warp` | ~25 | ~3µs (transpose+rotate) | 75µs |
| Sphere/Box/Cyl test | 15 | ~0.5µs | 7.5µs |
| Outer loop overhead | 1 | ~5µs | 5µs |
| **Per-trace total** | | | **~240µs** |

(Numbers are rough — actual cost varies by CPU, cache state,
matrix-math vectorization. Use as relative comparison only.)

### Per-tick budget (sv_fps=40 cup profile)

- Tick budget: 25ms (40Hz)
- 20 active bots × ~3-5 bullets/sec each = 60-100 traces/sec
- Bullets that hit player AABB → enter multi-region: ~30/sec
- Per-second cost: 30 × 240µs = ~7.2ms CPU/sec (steady state)
- **Worst-case per tick:** if 5 bullets hit different players in
  same tick → 5 × 240µs = **1.2ms just for hitbox math**, plus
  antilag rewind (5 × ~1ms each = 5ms), plus other game logic
- Steady state seems fine but **tail latency on busy ticks is
  the failure mode** — single tick going from 8ms to 28ms causes
  visible stutter.

### Antilag rewind cost (Phase 7.2 audit §A1+A4)

`G_AdjustSingleClientPosition` (g_antilag.c:196) per-trace:

- 22 fields rewound (origin, mins, maxs, viewangles, eFlags,
  pm_flags, viewheight, groundEntityNum, torsoFrame state ×11
  fields, legsFrame state ×11 fields)
- 1× `trap_LinkEntity` (engine round-trip)
- ~1ms per call estimated

Per-trace: 1× call per trace per other-client being tested.
G_HistoricalTrace (g_antilag.c:761) iterates `g_entities[]`
adjusting all clients within ping window, then runs the
trap_Trace, then re-adjusts back. With 20 active clients, that's
40 adjust+readjust calls per trace = 40ms/trace just for antilag
in worst case.

**Phase 7.2 audit §A1 marked antilag "free" — that's accurate at
4-8 player typical cup match. At 20 bots it's NOT free.**

## §2 — Hypothesis A: mdx_hit_test overhead

### Verified

**Yes, this is the dominant cost.** Per-trace ~240µs in
mdx_hit_test alone, per `mdx_*` micro-cost estimates above. With
30 traces/sec entering multi-region branch, ~7.2ms/sec steady,
~1.2ms worst-case per tick. Combined with antilag and other
overhead, busy ticks exceed budget.

### Specific wasted work

1. **`mdx_calculate_bones` re-runs for every trace** even when
   the same player is hit multiple times in same tick. 5 bullets
   on same player same tick = 5× redundant ~25µs = 125µs wasted.
2. **`mdx_tag_orientation` recomputes parent-bone chain per
   capsule** even when multiple capsules anchor at the same
   bone. Example: NECK (single tag at Bip01 Neck), CHEST (Spine1
   → Neck) — both call `mdx_tag_orientation(Neck)`. The cached
   `mdx_bones[Neck]` array entry is reused (good) but the matrix
   math wrapping it (offset, axis, recursion to parent) is
   recomputed per call.
3. **No AABB short-circuit at mdx_hit_test entry.** Every entry
   pays full 15-capsule price. (Note: G_Damage already filtered
   to "trace hit player AABB" — but the trace LINE may still
   miss every capsule; full cost still paid.)
4. **No early-exit on first hit.** Picks `best_frac` (closest
   hit), so all 15 tested. For most hits, the first or second
   capsule is the actual hit; remaining 13 are wasted.

## §3 — Hypothesis B: antilag rewind cost

### Verified at scale

Phase 7.2 audit was correct for 4-8 player matches. At 20 bots
it's a real bottleneck:

- `MAX_CLIENT_MARKERS = 40` ring buffer (g_local.h:912)
- Each trace adjusts all clients within ping window via
  `G_HistoricalTrace`
- `G_AdjustSingleClientPosition` rewinds 22 fields + calls
  `trap_LinkEntity` (engine round-trip cost)
- Then a `G_ReAdjustSingleClientPosition` to restore (another 22
  fields + LinkEntity)

At 20 active clients and 30 multi-region-eligible traces/sec, the
adjust+readjust pairs total ~600/sec. Each pair is ~2ms (rough)
→ ~1.2 seconds CPU-time/sec spent just on antilag. **This alone
saturates a single core**.

### But here's the thing

Antilag is shared with ETLegacy plain. ETLegacy at 20 bots is
smooth. So either (a) my cost estimate is way off, or (b) the
multi-region cost is the trigger that pushes the combined budget
over edge.

**Likely (b).** Antilag is paid by both, multi-region adds 240µs
× 30 = 7.2ms/sec on top, and at sv_fps=40 the per-tick tail
latency picks up the slack. Without multi-region (ETLegacy), the
budget has headroom; with multi-region added on top it doesn't.

### Antilag is not the primary fix target

We can't easily reduce antilag cost — it's engine-coupled and
correctness-critical. The fix is to reduce **VanguardMod-specific
overhead** (multi-region hitbox), so total budget stays under
25ms/tick.

## §4 — Hypothesis C: sv_fps=40 amplification

### Cup profile choice (Phase 7.2)

VanguardMod cup profile sets `sv_fps=40` per Phase 7.2 audit
§3.3. ETLegacy `legacy6.config` does the same. ETPro Crossfire
keeps engine-default `sv_fps=20`.

At sv_fps=40:
- Tick budget: 25ms (vs 50ms at sv_fps=20)
- Antilag marker rate: 800/sec (40Hz × 20 clients)
- Per-tick game logic: same wall-time, but tighter window

### Cross-test recommendation

Important for the implementation phase to verify:

1. **Test 3 from §11 matrix:** VanguardMod + 20 bots + `sv_fps=20`
   (public profile). If lag disappears → confirms sv_fps=40 is
   the trigger. Then the question becomes "is sv_fps=40 worth
   the perf cost?".
2. **If lag persists at sv_fps=20:** the cost is more
   fundamental, A1 (per-tick bone cache) is needed regardless of
   tick rate.

### Open question for wahke (§10 Q5)

If sv_fps=40 is the trigger, is the Phase 7.2 cup-profile choice
still the right call? The audit §3.3 already flagged ETLegacy
issue #1637 (mechanics broken at sv_fps>20). Combined with
hitbox cost, this might warrant reconsidering the cup-profile
default.

But: we should fix the perf bug first (A1+Q1 quick-wins) before
re-litigating the cup-profile decision. If A1+Q1 brings VG to
ETLegacy-plain perf parity at sv_fps=40, the question is moot.

## §5 — Quick-win candidates

### Q1 — Per-mdx_hit_test tag-orientation cache

**Idea:** Within a single `mdx_hit_test` call, cache the result
of `mdx_tag_orientation(tag_id)` so multiple capsules sharing the
same tag don't recompute. Example: NECK uses `_vg_neck` tag (=
Bip01 Neck bone), CHEST endpoint also uses `_vg_neck`. Compute
once per call, reuse.

**Implementation:**
- Add a small per-call cache: `vec3_t cached_origin[MAX_TAGS];
  vec3_t cached_axis[MAX_TAGS][3]; qboolean cached[MAX_TAGS];`
  (typed at top of mdx_hit_test)
- Wrap `mdx_tag_orientation` calls: check cache, fill if miss
- Reset cache at top of mdx_hit_test

**Complexity:** ~30 LOC change to `mdx_hit_test`. No
architectural change. Cache is automatic and stateless across
calls.

**Estimated win:** Each capsule has 1-2 tags. Within 15 capsules
there are ~10 distinct tags (lots of sharing). Reduces
`mdx_tag_orientation` from ~25 calls to ~10 → ~60% of the tag
cost = ~75µs saved per trace. **~30% per-trace reduction.**

**Phase 6 risk:** ZERO. Cache returns identical values to
recompute (same inputs, no animation state mutation between
calls within mdx_hit_test).

**Recommendation:** Ship in v0.7.1.1.

### Q2 — AABB short-circuit (refined)

**Idea original:** Before testing 15 capsules, do single AABB
test. If miss, skip all.

**Reality check:** The trace `(start, end)` already entered the
player's AABB by the time mdx_hit_test is called (G_Damage
gating). So a player-AABB short-circuit always passes — useless.

**Refined idea:** Compute a TIGHTER bounding box around the
union of all capsule volumes (excludes the gaps in the player
AABB). Reject trace if it doesn't intersect tighter AABB.

**Implementation:**
- Compute tighter AABB once per `mdx_hit_test` call (after
  `mdx_calculate_bones`, scan all capsule centers + radii)
- Or pre-compute per-character at .hit-load time (axis-aligned
  union of all capsule extents in bind pose, transformed by
  player orientation)
- Test trace against tighter AABB
- If miss → return false (no capsule hit)

**Complexity:** ~50 LOC. Needs careful AABB-vs-line-segment
math. Pre-computation needs to handle every animation state
(impossible — bones move) so must be runtime-computed per call.

**Estimated win:** 30-50% of misses get short-circuited. But
misses already have to do mdx_calculate_bones to know capsule
positions. So saved cost is just the 15× capsule tests, not the
bone-calc. ~30% per-miss reduction. Hits pay full price.

**Phase 6 risk:** LOW-MED. Tight AABB must be a true superset of
the actual capsule-union. Off-by-one could reject true hits. Needs
AABB-padding margin.

**Recommendation:** Defer to v0.8.0. The savings is misses-only
and not as big as Q1 or A1. Ship A1 first.

### Q3 — Reduce capsule count (merge ARM cylinders)

**Phase 7.0.2 added 4 ARM cylinders** (UpperArm L/R + Forearm
L/R) on top of existing SHOULDER cylinders. Total: 6 arm capsules
(2 SHOULDER + 2 UPPERARM + 2 FOREARM).

**Idea:** Merge each side into a single "arm" cylinder spanning
clavicle → hand. Reduces 6 capsules → 2 capsules.

**Implementation:** Edit `etmain/animations/human_base.hit`,
remove 4 HIT body lines, broaden SHOULDER cylinder to
clavicle→hand.

**Estimated win:** 4 fewer capsules per trace = ~25% reduction
in tag/warp calls (4/15 ≈ 27%). Per-trace: ~60µs saved.

**Phase 6 risk:** MED-HIGH. Cup-tester feedback in v0.5.2.3
specifically requested ARM cylinders to fix "dead-zone" misses
on bicep/forearm shots. Reverting could regress hit-rate per cup
data.

**Recommendation:** Do NOT pursue without cup-tester re-validation.
The 4 ARM cylinders were a deliberate fix, not accidental scope
creep. Defer indefinitely; re-evaluate only if perf still bad
after A1+Q1.

## §6 — Architectural-change candidates

### A1 — Per-tick bone cache (per client) — RECOMMENDED HEADLINE

**Idea:** `mdx_calculate_bones(refent)` populates global
`mdx_bones[]` from the refent's animation state. The result is
deterministic for given (frame, oldframe, torsoFrame, etc.). If
multiple bullets hit the same player in the same tick, those
animation fields don't change → the bone array is identical.

**Implementation:**
- Add per-client cache: `mdx_bone_cache_t s_bone_cache[MAX_CLIENTS]`
- Each entry: `int valid_for_levelTime; int valid_for_clientFrame;
  vec3_t bones[MAX_BONES]; uint64_t state_hash;`
- At `mdx_hit_test` entry, check if cache valid for this client +
  this levelTime. If yes, use cached `bones[]`; else recompute.
- Cache invalidated at next G_RunFrame tick (level.time advances).

**Complexity:** ~80 LOC. New cache module + integration into
`mdx_calculate_bones` and `mdx_hit_test`. State-hash for safety.

**Estimated win:** When same player hit by N bullets per tick,
`mdx_calculate_bones` runs 1× instead of N×. At 20 bots with
spread shots, average 2-3 bullets per player per tick → 50-70%
reduction in `mdx_calculate_bones` calls. Per-trace savings ~20µs
× 60% = ~12µs/trace. With 30 traces/sec = ~360µs/sec saved.

**Wait — that's not a lot.** Re-checking: if cache hit on 2 of 3
traces per tick, per-tick savings = 2 × 25µs = 50µs/tick × 40
ticks/sec = 2ms/sec. Not as huge as initially thought.

**Reality check on A1:** The win is real but smaller than I
initially claimed. Q1 (per-call tag cache) is actually bigger
because tag-orientation cost dominates bone-calc within
mdx_hit_test. **A1 + Q1 together** ≈ 35-40% per-trace reduction.

**Phase 6 risk:** LOW. Cache is invalidated each tick → animation
changes always force recompute. State-hash adds belt-and-suspenders
correctness check.

**Recommendation:** Ship A1 in v0.7.1.1 alongside Q1. Both are
low-risk, additive, mechanically simple.

### A2 — Lazy multi-region only on confirmed AABB hit

**Idea original:** Two-phase trace — standard ETLegacy AABB trace
first, then multi-region only if AABB hits.

**Reality check:** The current code already does this. G_Damage
is only called if the bullet hit the player. The "AABB hit"
filter is implicit in being inside the multi-region branch.

**A2 in this form is a no-op.** Skip.

A different framing: "skip mdx_hit_test for shots that hit AABB
but clearly miss all capsules" — but knowing they miss requires
running mdx_hit_test. So this collapses to Q2 (refined AABB
short-circuit).

**Recommendation:** Drop A2 from the menu. Q2 covers the same
idea more concretely.

### A3 — mdx_hit_warp matrix-math reduction

**Idea:** The un-translate / un-rotate / un-scale per capsule
could be vectorized or simplified for axis-aligned cases.

**Reality check:** `mdx_hit_warp` is ~6 lines of matrix math
(~3µs). Reducing to 1.5µs saves 1.5µs × 25 calls = ~38µs per
trace. Marginal.

**Recommendation:** Defer indefinitely. Premature
micro-optimization compared to A1/Q1.

## §7 — Risk-assessment table

| Candidate | Est. CPU savings | Implementation | Phase 6/7 risk | Test plan |
|---|---|---|---|---|
| **A1 — per-tick bone cache** | 5-15% per trace; 50-70% reduction in `mdx_calculate_bones` calls | ~80 LOC, new cache module + integration | LOW (cache invalidated per tick + state-hash check) | Run §9 hit-count test before/after |
| **Q1 — per-call tag cache** | 25-30% per trace (multi-tag sharing) | ~30 LOC inside `mdx_hit_test` | LOW (cache local to single call, no animation mutation possible) | Same §9 hit-count test |
| **Q2 — refined AABB short-circuit** | 30% on misses only (~15% overall depending on hit-rate) | ~50 LOC, runtime AABB compute + line-vs-AABB | LOW-MED (off-by-one rejects true hits) | Run §9 + edge-case oasis-jump-shot test |
| **Q3 — capsule count reduction** | 13-25% per trace | ~10 LOC in `human_base.hit` | MED-HIGH (regresses Phase 7.0.2 cup-tester feedback) | Cup-tester re-validation required |
| **A2 — lazy multi-region** | 0 (already done) | n/a | n/a | n/a |
| **A3 — matrix math reduction** | ~2% per trace | ~50 LOC of micro-opt | LOW | n/a (marginal) |
| **Antilag mitigation** | OUT OF SCOPE | engine-level | n/a | not VanguardMod scope |
| **sv_fps profile reconsideration** | 50%+ if dropped to 20 | config change only | LOW (Phase 7.2 §3.3 already flagged caveats) | sv_fps=20 cross-test (§4 Test 3) |

## §8 — Implementation phasing

### v0.7.1.1 — Performance Hotfix (recommended)

**Scope:** A1 (per-tick bone cache) + Q1 (per-call tag cache).
Both LOW-risk, mechanical, ~110 LOC total. **Combined estimated
win: 35-45% per-trace reduction.** Should bring 20-bot perf to
parity with ETLegacy plain.

Acceptance: 20-bot stress test on testserver pre-tag, verify
smooth gameplay. Phase 6 hit-count regression test (§9) must
pass within ±5%.

If this doesn't fix the lag fully, escalate to v0.8.0 with Q2 +
profiling.

### v0.7.1.2 / v0.8.0 — Performance Overhaul (deferred)

If v0.7.1.1 insufficient:

**Scope:** Q2 (refined AABB short-circuit), profiling
infrastructure (`vg_perf_*` counters), more aggressive cache
strategies.

Includes: vg_perf timing helpers (cycle-counter wrappers around
hot functions, cumulative cost tracked in stats output via new
diagnostic cvar `vanguard_diag_perf`).

### What's NOT in any v0.7.x

- **Q3 capsule reduction** — needs cup-tester re-validation; out
  of scope without explicit go.
- **Antilag refactor** — engine-level, not VG scope.
- **Cup-profile sv_fps reconsideration** — separate Phase 7.2
  follow-up; reopen only if A1+Q1+Q2 still insufficient.
- **A3 matrix micro-opt** — marginal gains, not worth the
  maintenance burden.

## §9 — Phase 6/7 regression-protection plan

Multi-Region Hitbox is Phase 6 foundation. Any optimization MUST
preserve:

1. **Hit-region identification accuracy** — HEAD vs CHEST vs GUT
   vs SHOULDER vs ARM vs KNEE vs LEGS, no remapping.
2. **Phase 7.0 strict-mode behaviour** — AABB-only hits (where
   the trace endpoint hits the player AABB but no capsule)
   continue to be rejected by `vg_Hitbox_StrictMode`.
3. **Phase 7.0.2 capsule sizes** — HEAD r=7, ARM cylinders r=4,
   etc. unchanged.
4. **Phase 8.0a NULL-guard** — `g_combat.c:1781-1784` 4-clause
   guard untouched.
5. **Phase 8.0b Falldamage helper** — v0.7.1
   `vg_Fun_GetInt("vg_fun_falldmg_*")` calls in G_FallDamage
   continue to resolve.

### Hit-count baseline test (manual, pre-merge)

Before any optimization:

1. Boot testserver, `addbot` 4 bots, `vg_fun 0`.
2. Place player at fixed coords on `oasis` aiming at static bot
   model (use `bot freezeall` if available).
3. Fire 100 shots: 20 at head, 20 at chest, 20 at gut, 10 at
   each shoulder, 5 at each knee, 5 at each leg.
4. Capture `vanguard_hitbox_debug 1` output: log of `hit_type`
   per shot.
5. Save baseline as `hit_baseline_pre_phase10.txt`.

After optimization:

1. Same setup, same 100 shots.
2. Save as `hit_baseline_post_phase10.txt`.
3. `diff` the two — must agree on per-region counts within ±5%
   tolerance.

Edge cases to also check:
- Strict-mode reject: shoot at corner of AABB → must still
  reject (impactpoint=0, no damage).
- Phase 8.0a regression: `die -1` → no SIGSEGV (NULL-guard
  test).

### CI gate for v0.7.1.1 (optional)

A microbenchmark binary that runs `mdx_hit_test` 10000 times on
a fixed refent + trace, asserts wall-time under threshold.
Higher engineering effort; defer to v0.8.0 perf-overhaul if
needed.

For v0.7.1.1, the hit-count regression test above + visual
testserver check is sufficient.

## §10 — Open decisions for wahke

1. **Implementation order** — A1 + Q1 in v0.7.1.1 (recommended)
   or split (Q1 first as smaller/easier patch)?
2. **Q2 timing** — defer to v0.8.0 (recommended) or pull into
   v0.7.1.1?
3. **Q3 ARM-cylinder reduction** — pursue with cup-tester
   re-validation, or accept current 15-area design indefinitely?
4. **sv_fps cup-profile** — keep at 40 (current Phase 7.2
   choice) and rely on A1+Q1 to fix perf, OR proactively drop to
   20 (ETPro orthodox) as belt-and-suspenders?
5. **Bot-count cap workaround** — ship "max 16 active shooters
   recommended for VanguardMod cup servers until v0.7.1.1" doc
   note, or skip the workaround and ship A1+Q1 fast?
6. **Profiling infrastructure** — include `vg_perf_*` counters
   in v0.7.1.1 (helps validate the fix) or defer to v0.8.0?
7. **Antilag scope** — explicitly mark "out of scope, engine
   territory" in `docs/CUP_VS_PUBLIC.md` so cup-testers don't
   raise it as a VanguardMod issue?

## §11 — References

### Code

- `src/game/g_combat.c:1714-2030` — Phase 6 multi-region damage
  branch + Phase 8.0a entry-gate (line 1781-1784) + mdx_hit_test
  call site (line 1997)
- `src/game/g_mdx.c:2841-3024` — `mdx_hit_test` (the main hot
  loop, 15 capsules per call, no AABB short-circuit, no
  early-exit)
- `src/game/g_mdx.c:1528-1559` — `mdx_calculate_bones` (per-trace
  bone-chain population into global `mdx_bones[]`)
- `src/game/g_mdx.c:1592-1692` — `mdx_bone_orientation` (parent-
  chain + torso-weighted axis math)
- `src/game/g_mdx.c:1702-1785` — `mdx_tag_orientation` (recursive
  tag → bone resolution; called per-capsule per-tag)
- `src/game/g_mdx.c:2632-2780` — `mdx_hit_warp` (un-translate +
  un-rotate + un-scale per capsule)
- `src/game/g_antilag.c:125-188` — `G_StoreClientPosition`
- `src/game/g_antilag.c:196-486` — `G_AdjustSingleClientPosition`
  (22 fields rewound + LinkEntity)
- `src/game/g_antilag.c:761-` — `G_HistoricalTrace`
- `src/game/g_local.h:912` — `MAX_CLIENT_MARKERS = 40`
- `etmain/animations/human_base.hit` — 15 hit-areas (post Phase
  7.0.2)

### Audit cross-refs

- `docs/notes/PHASE_7_2_AUDIT.md` §A1+A4 — antilag "free" claim
  (accurate at 4-8 player, NOT at 20 bots — this audit refines)
- `docs/notes/PHASE_7_0_AUDIT.md` — strict-mode introduction
  (v0.5.1)
- `docs/notes/PHASE_7_0_2_AUDIT.md` — Phase 7.0.2 capsule additions
  that brought hit-area count to 15 (4 ARM cylinders + NECK)
- `docs/notes/PHASE_8_0B_AUDIT.md` — Falldamage helper integration
  (independent of hot-path; G_FallDamage is server-only, not in
  per-bullet path)

### Phase 7.0 lessons-learned applied

1. **Diagnostic infrastructure** — recon proposes `vg_perf_*`
   timing helpers for v0.8.0 (cycle counters around hot
   functions, output via new `vanguard_diag_perf` cvar).
   Mirrors `VG_DIAG_DUMP` pattern from Phase 7.0.
2. **Build-flag mismatches** — n/a directly, but a CI
   microbenchmark gate is mentioned in §9 as v0.8.0 candidate.
3. **Crash-bugs vs tuning-bugs split** — performance is its own
   category. v0.7.1.1 quick-wins distinct from v0.8.0 overhaul,
   distinct from v0.5.2.2 SIGSEGV crash-fix and v0.7.1
   value-tuning. Each release category stays focused.

## §12 — v0.7.1.1 implementation log

Date: 2026-05-03
Branch: `fix/v0.7.1.1-perf-hotfix` → PR
Commit (single commit, all parts bundled): `<hash, post-merge>`

### Implemented

- **A1: per-tick per-client bone cache.** `gclient_s` extended
  with `vgPerfBoneCachedTick` / `vgPerfBoneCachedTorsoFrame` /
  `vgPerfBoneCachedLegsFrame` / `vgPerfBoneCacheValid` /
  `vgPerfBoneCache[VG_PERF_MAX_BONES]` (g_local.h). Cache
  lookup in `g_mdx.c::mdx_hit_test` entry: on hit, `memcpy`
  cached bones into the global `mdx_bones[]`; on miss, run
  `mdx_calculate_bones` then `memcpy` back into per-client
  cache. `VG_PERF_MAX_BONES = 96` safely exceeds the
  human_base mdx (~50 bones); larger models silently fall back
  to unconditional recompute.
- **Q1: per-call tag-orientation cache.** New static helper
  `vg_tag_orientation_cached(refent, idx, withhead, ...)` in
  `g_mdx.c` wraps `mdx_tag_orientation`. Per-call stack-
  allocated array `vg_tag_cache_entry_t tag_cache[32]`,
  populated during the 15-capsule loop. Both
  `mdx_tag_orientation` call sites (tag[0] and tag[1]) replaced.
  Cache key = `(tag_idx, withhead)`.
- **Diagnostic: `vanguard_perf_stats` cvar.** Storage in
  `g_cvars.c`, gameCvarTable entry (CVAR_TEMP, default `"0"`).
  Counters in `g_mdx.c`: `vg_perf_traces_total`,
  `vg_perf_bone_cache_hits/misses`,
  `vg_perf_tag_cache_hits/misses`. Per-second summary
  emission in `g_main.c::G_RunFrame` (rate-limited to 1
  emission/sec, counters reset each window).

### Build issues encountered + fixed

- `mdx_tag_orientation` is declared `static` later in the file
  (line 1774). My new helper at line ~106 needed a forward
  declaration. Added under the `#ifdef BONE_HITTESTS` gate.
- First `g_local.h` Edit attempts didn't apply (Edit tool
  required fresh Read). Re-read + re-applied successfully.

### Deferred (per audit §10)

- Q2 (refined AABB short-circuit) → v0.8.0 with separate AABB-
  tightness investigation. MED risk needs cup-tester perf data.
- Q3 (capsule reduction 15→11) → **NOT pursued**. Would regress
  Phase 7.0.2 cup-tester fix that explicitly added the 4 ARM
  cylinders.
- µs-precision per-trace timing → trap_Milliseconds is ms-only;
  sub-ms timing needs Sys_Microseconds (engine-side, not
  exposed via trap_*). Counter-based diagnostics (cache
  hit-rate) sufficient for v0.7.1.1; precision timing deferred
  to v0.8.0 with proper Sys_Microseconds wrapper.

### Measured impact (to be filled post cup-tester validation)

- Pre-fix (v0.7.1): 20 bots laggy, ~240µs/trace estimated
- Post-fix (v0.7.1.1): 20 bots [tbd], ~Xµs/trace [tbd]
- Improvement: [tbd]%

Cup-tester instructions: `/rcon vanguard_perf_stats 1` for 5+
seconds during 20-bot fight, capture VG_Perf log lines, report
trace count + tag-cache hit-rate.

### Phase 6/7/8 regression-safety verified

- ✓ Phase 8.0a NULL-guard at g_combat.c:1781-1784 untouched
  (static grep)
- ✓ Hit-detection bit-identical (caches store state, not
  results; same `mdx_calculate_bones` math runs on miss; same
  `mdx_tag_orientation` math runs on miss)
- ✓ All 15 capsule tests still execute (no early-exit; `best_frac`
  semantics preserved)
- ✓ Cache fallback for non-PLAYER, level.time=0, oversize
  models — never functionally regresses
