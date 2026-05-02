# Cgame Bone-Render Recon — Phase 6.x

Untracked planning artefact for the multi-region hitbox visualisation
in cgame. Body-region hits work ~95% in v0.3.5; HEAD-sphere hits only
~2% with `impactpoint=0` (UNUSED) about 64% of the time. We need a
visual diagnostic — render the 10 hit-capsules in wireframe in
real-time so we *see* where they are instead of inferring from
hit-rate statistics.

This doc summarises Sub-Phase A findings before code in Sub-Phase B.

---

## Existing infrastructure (key finding)

`src/cgame/cg_vanguard_dev.{c,h}` already exists and renders **legacy
AABB hitboxes** (single body-box + head-box + prone-legs-box) via
`CG_AddLineToScene` (12 edges per box). It uses:

- `cgs.vanguardDev` — server-published configstring gate
  (`vanguard_dev=1`)
- `cg_vanguardDevHitboxes.integer` — client cvar (CVAR_ARCHIVE,
  default 1) — currently 0/1/2 schema (off / boxes / boxes+lines)
- `cg_vanguardDevAlpha.value` — client cvar (CVAR_ARCHIVE, default
  0.4)

Per-frame entry point: `CG_VanguardDev_DrawHitboxes()` called from
`cg_view.c:2712` after `CG_AddPacketEntities` (so `lerpOrigin /
lerpAngles` are final). Iterates `cg_entities[0..maxclients]` with
self-skip for first-person view.

→ **Sub-Phase B+C work extends this file rather than starting fresh.**

---

## A.1 — Render-trap inventory

| Trap | Status | Use |
|---|---|---|
| `trap_R_LerpTag(orientation_t *, refEntity_t *, name, startIdx)` | ✅ available | Resolve tag/bone position in parent-model local frame |
| `trap_R_AddRefEntityToScene` | ✅ used widely | Submit refEntity to render |
| `trap_R_AddPolyToScene` | ✅ available | 4-vert poly draws |
| `trap_R_AddLightToScene` | ✅ available | Dynamic light |
| `CG_AddLineToScene(start, end, rgba)` | ✅ at `cg_draw.c:4503` | **The line primitive used by cg_vanguard_dev for AABB edges** |
| `CG_RailTrail(color, start, end, type, idx)` | ✅ at `cg_weapons.c:798` | Rail-style line, more visual but heavier |

**Decision:** `CG_AddLineToScene` for wireframe primitives. Same as
existing AABB rendering, no event spam, per-frame stateless.

## A.2 — MDX in cgame

```
grep -rn "mdx_\|MDX\|FEATURE_SERVERMDX" src/cgame/
```

**Zero hits** for runtime MDX symbols in cgame. The `g_mdx.c`
collision-test pipeline is **server-side only**. Cgame has access
to MDX models via the engine (refEntity_t::frameModel), but cannot
call `mdx_calculate_bones`, `mdx_hit_test`, etc. directly.

→ Cgame must use the **engine-mediated** bone-position interface.

## A.3 — Bone-position resolution in cgame

Engine-side `trap_R_LerpTag` works for **both** named MDM tags
(`tag_head`, `tag_torso`) and **MDX skeleton bones** (`Bip01 Head`,
`Bip01 Spine2`). Verified by `cg_character.c:252`, which lerps
foot-tags off `anim->mdxFile` directly.

**Canonical world-space transform pattern** (from
`cg_ents.c:CG_PositionEntityOnTag`):

```c
orientation_t lerped;
trap_R_LerpTag(&lerped, parent_refent, "Bip01 Head", 0);

vec3_t world;
VectorCopy(parent_refent->origin, world);
for (int i = 0; i < 3; i++) {
    VectorMA(world, lerped.origin[i], parent_refent->axis[i], world);
}
/* world is now the bone's world-space position */
```

**`parent_refent`** for cgame is the player's `body` refEntity built
in `cg_players.c:CG_Player`. Its `frameModel` field references the
animation .mdx file via `character->animModelInfo->animations[N]->mdxFile`
(see `cg_players.c` reference). Since `body.frameModel` IS the .mdx
that holds our skeleton, `trap_R_LerpTag(&t, &body, "Bip01 Head", 0)`
returns the same bone-position as the server's `mdx_tag_orientation`
(modulo network-frame interpolation).

### Concrete steps for our renderer

1. For each visible player, **rebuild the same `refEntity_t body`**
   that `CG_Player` builds — or simpler: hold a synthetic
   `refEntity_t` whose `frameModel` we set from the player's
   animation state, and `origin/axis` from `cent->lerpOrigin` /
   `cent->lerpAngles`.
2. Call `trap_R_LerpTag` for each TAG-bridge bone (`Bip01 Head`,
   `Bip01 Neck`, `Bip01 Spine`, ...).
3. Apply the world-space transform pattern to get world coordinates.

**Open question (NON-BLOCKING):** does `body.frame /
body.torsoFrame` need to be set for `trap_R_LerpTag` to interpolate
correctly? `cg_character.c:248-250` sets all four `frame /
oldframe / torsoFrame / oldTorsoFrame` plus their model handles
before calling `LerpTag`. We should mirror that to get
animation-correct positions. **Test in Sub-Phase B and confirm.**

## A.4 — `.hit` file loading in cgame

`trap_FS_FOpenFile` + `trap_FS_Read` are **available** in cgame —
verified by `cg_character.c:125` (reads `characters/temperate/...char`)
and `cg_hud_io.c:740` (reads HUD configs). So a cgame-side .hit
parser is technically feasible.

**However:** the existing `cg_vanguard_dev.c` style is to hardcode
constants (`vg_BodyMins`, `vg_HeadMins`, etc.). The 10 hit-areas
are tightly coupled to the qagame's runtime expectations — if the
client `.hit` data drifted from the server's, the visualisation
would be incorrect.

**Decision:** **hardcode the 10 hit-areas as a compile-time
constant array** in `cg_vanguard_dev.c`, mirroring the `.hit` file's
TAG-bridge list and HIT block parameters. This:
- Matches the existing dev-renderer style.
- Eliminates a parser duplicate (qagame already has `hit_load`).
- Forces version-locked client/server visualisation.
- Trade-off: any `.hit` retune requires a client rebuild + cgame
  redistribution — already the case for v0.3.4 onward.

Concrete schema:

```c
typedef enum { VG_HIT_SPHERE, VG_HIT_CYLINDER, VG_HIT_BOX2 } vg_hit_kind_t;

typedef struct {
    vg_hit_kind_t  kind;
    const char    *bone1;
    const char    *bone2;     /* NULL for SPHERE */
    vec3_t         scale1;
    vec3_t         scale2;    /* (0,0,0) for SPHERE */
    int            impactpoint; /* IMPACTPOINT_* for colour-coding */
} vg_hit_area_t;

static const vg_hit_area_t vg_hit_areas[] = {
    /* HEAD sphere */
    { VG_HIT_SPHERE,   "Bip01 Head",     NULL,           {6,6,6}, {0,0,0}, IMPACTPOINT_HEAD     },
    /* CHEST box2 Spine1 -> Neck */
    { VG_HIT_BOX2,     "Bip01 Spine1",   "Bip01 Neck",   {9,7,5}, {9,7,5}, IMPACTPOINT_CHEST    },
    /* GUT box2 Pelvis -> Spine2 */
    { VG_HIT_BOX2,     "Bip01 Pelvis",   "Bip01 Spine2", {9,7,5}, {9,7,5}, IMPACTPOINT_GUT      },
    /* ... 7 more ... */
};
```

## A.5 — Wireframe primitives

`CG_AddLineToScene(start, end, rgba)` is the building block. We need
three composite primitives:

| Primitive | Lines | Notes |
|---|---|---|
| Wire-box (axis-aligned)   | 12 | Already implemented in cg_vanguard_dev.c (`vg_DrawWireBox`) |
| Wire-sphere (icosahedron-style)  | ~24 | 3 great circles × 8 segments each = 24 line-segs |
| Wire-cylinder (between two tags) | ~16 | 2 ring of 8 segments at each cap + 2 vertical edges = 18 lines (or simplify to 8 vertical + 2 caps × 4 segs = 16) |
| Wire-box2 (rotated between two tags) | ~12 | Same edge topology as AABB box but with both endpoint frames |

Implementation effort: ~50 LOC each. Wire-sphere and wire-cylinder
need the tag's `axis[3]` for orientation — which `trap_R_LerpTag`
already returns in `orientation_t::axis`.

## A.6 — Cvar wiring in cgame

Existing Vanguard-namespace cvars in `cg_cvars.c:266-267`:
- `cg_vanguardDevHitboxes` (CVAR_ARCHIVE, default 1) — currently 0/1/2 for AABB visualisation
- `cg_vanguardDevAlpha` (CVAR_ARCHIVE, default 0.4)

For multi-region overlay we add **one new cvar**:

```c
{ &cg_vanguardDevMultibox, "cg_vanguardDevMultibox", "1", CVAR_ARCHIVE, 0 },
```

→ `0` off, `1` show 10 hit-capsules with per-region color-coding.

Both gates (`vanguard_dev=1` server-side AND `cg_vanguardDevMultibox=1`
client-side) must pass before render. AABB cvar
`cg_vanguardDevHitboxes` stays orthogonal — admin can show legacy
boxes alone, multi-region alone, both, or neither.

## A.7 — Hit-highlight events (Sub-Phase C optional)

Existing event schema:
- `EV_PLAYER_HIT` (bg_public.h:1510, value 131) carries
  `HIT_TEAMSHOT/HIT_BODYSHOT/HIT_HEADSHOT` in
  `event[ENT_EVENT_NUMBERPARAMS]`. Cgame handles in
  `cg_event.c:2966`.
- Resolution: 3 buckets — too coarse for our 10-region overlay.

**For Sub-Phase C hit-highlight** we need the actual `impactpoint`
delivered to cgame. Options:

1. **Encode impactpoint in EV_PLAYER_HIT's `effect2Time` or
   `dmgFlags` field** (existing event entitystate slots). 4 bits
   sufficient for the 10 enum values. No new event type, minimal
   protocol change. 1-line server edit + 1-line cgame decode.

2. **Add a new EV_VG_REGION_HIT event** with explicit
   `entityNum + impactpoint + timestamp` payload. Cleaner but more
   bytes per hit.

**Decision (deferred to Sub-Phase C):** start with Option 1 —
encode impactpoint in `effect2Time` of the existing event. Cheaper
to ship; can swap to a dedicated event later if we want more
fields.

For Sub-Phase B (static render) we **skip hit-highlight entirely**
— just draw all 10 capsules in their region colours. Hit-pulse
glow is C polish.

---

## Sub-Phase B+C+D file list

| File | Sub-Phase | Change |
|---|---|---|
| `src/cgame/cg_vanguard_dev.c` | B + C | Add `vg_hit_areas[]` table + `vg_DrawWireSphere` / `vg_DrawWireCylinder` / `vg_DrawWireBox2` helpers + extend `CG_VanguardDev_DrawHitboxes` to render multi-region capsules |
| `src/cgame/cg_vanguard_dev.h` | B | (no change unless we expose a new entry point) |
| `src/cgame/cg_cvars.c` | B | Register `cg_vanguardDevMultibox` (1 line in defs + 1 line in table) |
| `src/cgame/cg_cvars.h` | B | Extern declaration (1 line) |
| `src/game/g_combat.c` | C | Encode impactpoint in EV_PLAYER_HIT (1-line edit at the existing G_AddEvent call site) |
| `src/cgame/cg_event.c` | C | Decode impactpoint, set per-player per-region last-hit timestamp |

No new files needed. All changes are additive into existing modules.

---

## Time estimates

| Sub-Phase | Estimate | What |
|---|---|---|
| B (core wireframe render) | 2.5h | Hit-area table + 3 primitives + LerpTag-based bone resolver + per-frame loop. End: capsules visible, all-white. |
| C (color-coding + highlight) | 1.5h | Per-region palette + EV_PLAYER_HIT impactpoint encoding + glow timer |
| D (commit + version + multi-platform build) | 1h | 3 commits + six-spot bump + Win64+Win32+Linux build |
| **Total** | **5h** | from current state to v0.3.6 deployable |

Risk: A.3 open question (frame/torsoFrame setup for trap_R_LerpTag)
might add ~30 min of trial-and-error in B if my mirror of
`CG_Player` body-refent setup is incomplete.

---

## Recommended next-step decisions

1. **Bone-resolution approach: `trap_R_LerpTag` against a synthetic
   `refEntity_t` we build per-player.** Use `cent->lerpOrigin /
   lerpAngles` for transform, `character->animModelInfo->animations[0]->mdxFile`
   for `frameModel` — same setup as the existing `body` refent in
   `CG_Player`.

2. **Hit-area data: hardcoded `vg_hit_areas[10]` table** in
   `cg_vanguard_dev.c`, kept in sync with `etmain/animations/human_base.hit`
   manually on each retune. (Long-term cleanup: codegen from .hit
   at build time — out of scope for Phase 6.x.)

3. **Wireframe drawing: `CG_AddLineToScene`** for all primitives —
   matches existing style, no new render-trap dependencies.

4. **Cvar gating: NEW cvar `cg_vanguardDevMultibox`** orthogonal to
   `cg_vanguardDevHitboxes`. Server `vanguard_dev=1` gates both.

5. **Hit-highlight in C: encode impactpoint in `EV_PLAYER_HIT`'s
   spare event slot** (effect2Time or similar). 1-line server edit,
   1-line client decode. Skip entirely in B.

Confidence to proceed with B as scoped: **9/10**. Open question
in A.3 is the only residual unknown; mitigation is to test the
synthetic-refent approach in the first 30 min of B and pivot to
mirroring `CG_Player`'s exact setup if `LerpTag` returns junk.

---

*End. Ready for User decision: proceed to Sub-Phase B?*
