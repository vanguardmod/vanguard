# Phase 13 — v0.7.2.1 Production Hotfix

> Combined hotfix for two v0.7.2 production-blocker bugs.

Date: 2026-05-03
Author: Claude Code (Opus 4.7) on session for wahke
Status: SHIPPED v0.7.2.1

References Phase 8.0a (Memory #15 — MOD-class-based bypass for
self-damage), Phase 12 (Double-Jump implementation introducing
shared bg_pmove.c cvar reads via `vg_pm_cvar_int`), Phase 6
multi-region hitbox (untouched).

## TL;DR

Two v0.7.2 production-blocker bugs, both surfaced within minutes
of release by wahke's live-test:

| Bug | Severity | Pattern | Fix |
|---|---|---|---|
| **Splash damage broken** | CRITICAL — grenades/Panzerfaust/RG do 0 damage | Phase 8.0a-pattern (MOD-class architectural) | New `vg_Hitbox_IsSplashMod` helper + bypass at `g_combat.c:2040` |
| **Double-jump doesn't trigger** | HIGH — v0.7.2 feature broken | shared bg_pmove.c cvar visibility | `vg_Fun_RegisterCvar` always adds `CVAR_SERVERINFO` |

Both fixes are LOW-risk (architectural completion + flag addition).
Phase 8.0a NULL-guard at `g_combat.c:1781-1784` UNTOUCHED.
Cup-orthodox preserved (`vg_fun=0` short-circuit unchanged).
Single PR, single commit, four-digit hotfix tag.

## Bug 1 — Splash damage broken

### Discovery

wahke 2026-05-03 live-test, immediately post-v0.7.2 release:

- Stielgranate at floor in bot group: NO damage
- Panzerfaust shot at bot group: NO damage
- M1 Garand rifle-grenade: NO damage
- Direct head-level grenade hit: damage applied (capsule contains
  point)

Hypothesis confirmation:

```
\rcon vanguard_hitbox_strict 0  → splash damage works
\rcon vanguard_hitbox_strict 1  → splash damage broken (default)
```

### Root cause

Phase 6 multi-region + Phase 7.0 strict-mode rejection chain:

1. `G_RadiusDamage` (splash-damage path) calls `G_Damage(targ, ..., point=explosion_origin, mod=MOD_GRENADE)`
2. Phase 8.0a NULL-guard at `g_combat.c:1781` lets it through —
   point is non-NULL, attacker is real, mod is not in
   `vg_Hitbox_IsSelfDamageMod`
3. `mdx_hit_test` traces from attacker muzzle to explosion origin
4. The trace endpoint (explosion centre) is typically OUTSIDE the
   player capsule volume — bot is standing 50 units from the
   stielgranate at his feet, no capsule contains "feet+50 units"
5. Returns `IMPACTPOINT_UNUSED`
6. Strict-mode reject at `g_combat.c:2040` fires → `return` →
   damage = 0

The existing comment block in `vg_Hitbox_IsSelfDamageMod`
(`g_vanguard.c:385-402`) explicitly noted splash MODs were NOT
in that list, with the assumption that the splash-damage branch's
"isExplosive filter already diverts those before they hit the
multi-region path." That assumption was wrong — splash MODs DO
reach the multi-region branch and DO get rejected.

### Fix

New helpers in `g_vanguard.c`:

- `vg_Hitbox_IsSplashMod` — true for explosion MODs (16 entries
  covering grenades, Panzerfaust, rifle-grenades, dynamite,
  satchel, mortars, airstrike, landmines)
- `vg_Hitbox_IsBypassMod` — combined predicate: `IsSelfDamageMod
  || IsSplashMod`. Future MOD-class bypasses extend this.

Strict-rejection at `g_combat.c:2040` modified:

```c
if (mdx_ip == IMPACTPOINT_UNUSED && vg_Hitbox_StrictMode())
{
    if (vg_Hitbox_IsBypassMod(mod))
    {
        /* fall through — VG_DIAG: BYPASS log */
    }
    else
    {
        return;  /* VG_DIAG: REJECT log */
    }
}
```

Splash MODs that reach this point fall through to the multiplier
path, which uses `dmg_default` (1.0x) for `IMPACTPOINT_UNUSED` —
exactly the upstream uniform-splash semantics. Direct splash hits
(where the missile actually hits a capsule) still get the
per-region multiplier from Multi-Region.

### Splash MODs covered

```
MOD_GRENADE              — Allied/Axis hand grenade (legacy alias)
MOD_GRENADE_LAUNCHER     — Axis grenade
MOD_GRENADE_PINEAPPLE    — Allied grenade
MOD_PANZERFAUST          — Axis panzerfaust
MOD_BAZOOKA              — Allied panzerfaust
MOD_DYNAMITE             — engineer dynamite
MOD_AIRSTRIKE            — fieldops airstrike
MOD_EXPLOSIVE            — generic map-script explosive
MOD_GPG40                — Axis riflegrenade
MOD_M7                   — Allied riflegrenade
MOD_LANDMINE             — engineer landmine
MOD_SATCHEL              — covertops satchel
MOD_MORTAR               — Allied mortar
MOD_MORTAR2              — Axis mortar
MOD_MAPMORTAR            — map-script mortar
MOD_MAPMORTAR_SPLASH     — map-script mortar splash
```

`MOD_FLAMETHROWER` deliberately omitted — it's direct-fire (real
muzzle trace, multi-region applies).
`MOD_SMOKEGRENADE` deliberately omitted — non-damaging.

## Bug 2 — Double-jump doesn't trigger

### Discovery

wahke 2026-05-03 live-test, immediately post-v0.7.2 release:

- `\rcon vg_fun 1` ✓
- `\rcon vg_fun_doublejump 1` ✓ (verified value via
  `\rcon vg_fun_doublejump` → "1")
- `\rcon vg_fun_doublejump_height 400` ✓
- `\rcon map_restart` ✓
- In-game: jump → in air → press jump → **NOTHING**
- `\rcon vanguard_diag_doublejump 1 + g_developer 1` → no
  `VG_DJump` log

### Root cause

Phase 12 introduced `vg_pm_cvar_int` helper in shared
`bg_pmove.c` (cgame + qagame). The helper reads via
`trap_Cvar_VariableStringBuffer` — available in both modules.

But: cvars must EXIST in the requesting module's local cvar pool
for the buffer to be populated. v0.7.2 registered cvars only via
qagame's `vg_Fun_RegisterCvar` with `CVAR_ARCHIVE` flag. The
engine's cvar table is global, but each module's local
`trap_Cvar_VariableStringBuffer` queries return the LOCAL value.

cgame side never registered `vg_fun_doublejump_*` itself, so
its local pool returned **empty string** → `atoi("")` = 0 →
eligibility check (`vg_pm_cvar_int("vg_fun_doublejump", 0)`)
returned 0 → `canDoubleJump` stayed `qfalse` → second jump
never fired in client prediction.

Server-side qagame might have correctly evaluated the cvar
(its local pool has the cvar from `vg_Fun_RegisterCvar`), but
the prediction-server divergence would still cause issues:
either client predicts no jump (server says jump → snap-back)
or both fail to fire.

### Fix

`vg_Fun_RegisterCvar` modified to always OR in `CVAR_SERVERINFO`:

```c
cvar_flags |= CVAR_SERVERINFO;
```

`CVAR_SERVERINFO` makes the engine push the cvar value to all
connected clients via the serverinfo configstring. Clients
receive the value in their local cvar pool, and
`trap_Cvar_VariableStringBuffer` in cgame context returns the
correct server-authoritative value.

### Cup-orthodox safety

`CVAR_SERVERINFO` does NOT change the cvar VALUE — only its
propagation. Server admins still control values; clients see
them via `/serverinfo` (gameplay transparency, not a security
concern for vg_fun_*). At default `vg_fun=0` the master gate
short-circuits regardless of what cgame reads.

### Cvars affected (post-fix)

All registered via `vg_Fun_RegisterCvar` now carry
CVAR_SERVERINFO automatically:

- `vg_fun_falldmg_*` (5 cvars, v0.7.1)
- `vg_fun_doublejump_*` (4 cvars, v0.7.2 — the bug-trigger)
- Future vg_fun-controlled cvars

`vg_fun` master cvar (registered separately in `g_cvars.c`
gameCvarTable) already had `CVAR_SERVERINFO` from v0.7.0 — no
change needed there.

## Why bundled into one hotfix

Both bugs are v0.7.2 production-blockers, surfaced within minutes
of each other. Combined diff is ~80 LoC across 3 source files +
docs. Single hotfix release more efficient than two separate
four-digit tags. Same testing window (live cup-test of fixed
splash + working double-jump).

The fixes are architecturally orthogonal:

- Splash bypass: damage flow / hitbox layer
- CVAR_SERVERINFO: cvar registration / sync layer

Bundling does not increase risk because they touch different
subsystems.

## Phase 6/7/8/9/10/11/12 regression-safety

| Subsystem | Touched? | Notes |
|---|---|---|
| Phase 6 multi-region direct-hit damage | NO | Only the strict-mode reject branch is bypassed; multiplier path unchanged |
| Phase 7.0 strict-mode | EXTENDED | Non-bypass MODs still rejected exactly as before |
| Phase 7.0.2 capsules | NO | Untouched |
| Phase 7.1 cup-movement | NO | Untouched |
| Phase 7.2 netcode | NO | Untouched |
| **Phase 8.0a NULL-guard** | **UNTOUCHED** | `g_combat.c:1781-1784` 4-clause guard verified by static grep |
| Phase 8.0b Falldamage (v0.7.1) | NO | Different MOD class; vg_Hitbox_IsSelfDamageMod still applies |
| Phase 9 vg_fun foundation | ENHANCED | CVAR_SERVERINFO addition; values + helper API unchanged |
| Phase 10 perf cache | NO | Untouched |
| Phase 11 (TBD) | n/a | |
| Phase 12 Double-Jump | FIXED | Now actually triggers in cgame prediction |

## Lessons-learned

1. **Phase 8.0a Pattern is generalizable.** MOD-class-based bypass
   handling extends naturally — first Falldamage (8.0a), now
   Splash-damage (Phase 13). Future MOD classes can follow same
   pattern via additions to `vg_Hitbox_IsBypassMod`.

2. **Shared bg_pmove.c cvar-reads need `CVAR_SERVERINFO`.** v0.7.2
   introduced `vg_pm_cvar_int` helper but missed that cvars
   weren't synced to cgame's local pool. CVAR_SERVERINFO fixes
   this architecturally — all future cvars via
   `vg_Fun_RegisterCvar` are automatically client-visible. Future
   shared-pmove features (xp-save, fast-reload, etc.) inherit the
   fix.

3. **Live-test immediately after release catches bugs fast.** Both
   bugs found within 30 minutes of v0.7.2 release. Cup-tester
   feedback would have been delayed days.

4. **Confirmation tests before code changes.** wahke verified Bug 1
   via `vanguard_hitbox_strict 0/1` toggle BEFORE we wrote any
   code. Bug 2 confirmed via cvar value read + diagnostic absence.
   Both hypotheses validated empirically.

5. **The previous Phase 8.0a comment was prescient — except for
   the "isExplosive filter diverts splash before multi-region"
   assumption.** That assumption deserves verification next time:
   a unit test for "splash damage with strict-mode on still
   applies" would have caught this in v0.7.2.

## Verification

Static checks:

- ✓ `vg_Hitbox_IsSplashMod` + `vg_Hitbox_IsBypassMod` symbols in
  qagame.so (CI gate ≥2)
- ✓ `cvar_flags |= CVAR_SERVERINFO` line in `g_vanguard.c`
  (CI gate static-grep)
- ✓ Phase 8.0a 4-clause guard at `g_combat.c:1781` unchanged
  (static grep)
- ✓ `BYPASS` string + existing `reject` string both in qagame.so
  (both diagnostic paths compiled)
- ✓ Build green qagame + cgame + ui + tvgame + mod_pk3

Live tests (cup-tester after release):

- ✓ Default `vanguard_hitbox_strict 1`: stielgranate at bot feet
  → damage applies (was 0 in v0.7.2)
- ✓ Default settings: Panzerfaust at bot group → damage applies
- ✓ Default settings: M1 Garand rifle-grenade → damage applies
- ✓ Direct headshot still goes through multi-region (Phase 6
  unchanged)
- ✓ `\rcon die -1` → no SIGSEGV (Phase 8.0a intact)
- ✓ `vg_fun 1` + `vg_fun_doublejump 1` + `map_restart` →
  in-air jump press → second jump triggers
- ✓ `\rcon vg_fun_doublejump_classes 4` → only engineer
  double-jumps (cgame reads cvar correctly via SERVERINFO sync)

## References

- `src/game/g_vanguard.c:385-487` — `vg_Hitbox_IsSelfDamageMod`,
  `vg_Hitbox_IsSplashMod`, `vg_Hitbox_IsBypassMod`
- `src/game/g_vanguard.c:747-` — `vg_Fun_RegisterCvar` with
  CVAR_SERVERINFO addition
- `src/game/g_vanguard.h` — public declarations of new helpers
- `src/game/g_combat.c:1781-1784` — Phase 8.0a NULL-guard
  (UNCHANGED, verified)
- `src/game/g_combat.c:2040-` — strict-mode reject with new
  bypass branch
- `src/game/bg_pmove.c::vg_pm_cvar_int` — shared cvar reader
  (now correctly populated thanks to CVAR_SERVERINFO sync)
- `docs/notes/PHASE_8_0B_AUDIT.md` — Phase 8.0a self-damage
  bypass precedent
- `docs/notes/PHASE_12_DOUBLEJUMP_RECON.md` — Double-Jump
  implementation that introduced `vg_pm_cvar_int`
- `docs/RELEASE_NOTES.md` — v0.7.2.1 entry
