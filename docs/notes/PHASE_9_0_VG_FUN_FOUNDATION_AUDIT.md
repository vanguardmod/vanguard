# Phase 9.0 — vg_fun Foundation Recon

> Pure recon. No code changes. wahke decides scope after this.

Date: 2026-05-03
Author: Claude Code (Opus 4.7) on session for wahke
Status: AUDIT (no implementation)

References Memory #8 (vg_fun architecture spec from Phase 6 era),
Memory #25 (multi-endpoint stats), Phase 8.0b audit (deferred to
v0.7.1 — Falldamage becomes the first vg_fun-controlled feature
once the v0.7.0 foundation lands).

## TL;DR

**vg_fun is platform infrastructure**, not a feature — directly
analogous to the v0.6.0 WolfGuard foundation. v0.7.0 ships the
master switch + helper API + banner extension + `vg_status` command
+ stats mode-tag at log level. No tunable features yet (Falldamage
follows in v0.7.1).

| Decision | Recommendation | Rationale |
|---|---|---|
| Lock-mechanism (§1) | **C — helper function** | runtime mode-switch capable, no CVAR_ROM gymnastics, explicit at every read site |
| File location (§2) | **g_vanguard.c (4th subsystem)** | keeps locality with vg_DevMode / vg_Hitbox / vg_Netcode |
| Cvar registration (§3) | **γ — helper API + internal registry** | features stay self-contained, registry powers `vg_status` introspection |
| Stats tagging (§4) | **NOW (v0.7.0)** | one-line G_LogPrintf format change, future-proofs against ambiguous historic stats |
| Mode-switching (§5) | **vg_fun = CVAR_LATCH; sub-cvars = CVAR_ARCHIVE** | cup-integrity (no mid-match drift) but admins can pre-stage tweaks |
| Naming (§7) | **Flat** (`vg_fun_<feature>_<param>`) for v0.7.0; per-feature toggle added on demand | YAGNI — no v0.7.x feature has surfaced a need for granular toggling yet |
| Banner (§8) | **Extend WG banner with one Mode: line** | minimal, mirrors existing pattern |
| v0.7.0 scope (§13) | **Foundation only — no proof-of-concept feature** | clean separation, lets v0.7.1 Falldamage be the first real validation |

Ships ~150–200 LOC of new code. Foundation-only release; zero
behavioural change at default `vg_fun=0`.

## §1 — Lock-mechanism architecture

### Strategy A — CVAR_LATCH gating

Every `vg_fun_*` cvar is `CVAR_LATCH`. `vg_fun` itself is
`CVAR_LATCH`. Mode change requires `map_restart`. At map_restart,
if `vg_fun = 0`, a hook function `vg_Fun_ApplyCupDefaults()` walks
the registry and `trap_Cvar_Set`s each sub-cvar back to its
cup-default.

**Pro:**
- Engine-supported via standard CVAR_LATCH semantics
- Cup-integrity guaranteed by engine — no "weakened lock" risk
- Stats bucketing per map is unambiguous (mode locked at map start)

**Con:**
- Doesn't actually "lock" sub-cvars from being set — admin can
  still issue `set vg_fun_falldmg_dmg_50 1` and the value persists
  in the cvar; only the *effective* value reverts. This is
  semantically muddy.
- Requires the apply-defaults function to know every sub-cvar's
  cup-default value — registry-coupled
- Doesn't support runtime mode switch (e.g. cup-server hosting
  pub-night without restart)

### Strategy B — Read-only when locked (dynamic CVAR_ROM)

`vg_fun_*` cvars become read-only when `vg_fun=0`. Engine API
doesn't expose dynamic flag toggling from mod side (no
`trap_Cvar_SetFlags(name, flags)`); workaround would be
re-registering the cvar with new flags or intercepting `set`
commands.

**Pro:**
- Strongest enforcement — admin can't even type the wrong value
- Audit trail: server log shows "cvar denied" if admin tries

**Con:**
- Requires custom enforcement (intercept `set` / `setu` /
  `seta` commands in `Svcmd_*` paths)
- Dynamic flag manipulation is fragile across engine versions
- Ugly UX: admin pre-stages sub-cvars in `server.cfg` for the
  next map, then `vg_fun 1` map restart — but if `vg_fun` is 0
  at config-load time, the sub-cvar `set` fails silently.

**Don't ship this.**

### Strategy C — Helper function `vg_Fun_GetInt(name, cup_default)`

Cvars exist with their tuned values, no special flags. Every
read site goes through a helper:

```c
int dmg = vg_Fun_GetInt("vg_fun_falldmg_dmg_50", 50);
```

Helper checks `vg_fun.integer`:
- `0` → returns `cup_default` (50, the engine value)
- `1` → returns the cvar value (admin's tuned value, or
  cup_default if unset)

**Pro:**
- Simplest implementation — one function, no flag gymnastics
- Explicit at every call site — code reviewer sees the protection
- Supports runtime mode switch trivially (next read picks new mode)
- Cup-default always wins under vg_fun=0 — no "did the admin set
  this?" ambiguity
- Testable in isolation

**Con:**
- Discipline-dependent: every read site MUST use the helper.
  A direct `cvar.integer` read bypasses the lock.
- Code review burden — must verify each new read site uses the
  helper

### Recommendation: Strategy C, with `vg_fun` itself as CVAR_LATCH

Use the helper for sub-cvars (runtime-switchable, explicit). Make
`vg_fun` itself CVAR_LATCH so the *mode* can't change mid-match
even if the helper supports it. This combination gets:

- Cup-integrity from CVAR_LATCH on the master (engine-enforced)
- Implementation simplicity from the helper (no per-cvar flag
  manipulation)
- Future flexibility: if v1.0 wants to drop the LATCH on
  `vg_fun` for runtime mode switch, only the master cvar flag
  needs to change — all sub-cvar plumbing stays.
- Discipline cost is low because `vg_fun_*` cvars are a closed
  set (only this PR + v0.7.x features add them); a one-line
  CI-grep check catches direct reads.

CI grep to enforce discipline (added in §9):
```
grep -rn 'vg_fun_[a-z_]*\.\(integer\|value\|string\)' src/game/ \
  | grep -v 'g_vanguard.c'   # only the helper itself reads cvars directly
```

Must return zero matches outside `g_vanguard.c`. If this ever
fails, a new feature added a direct read instead of using the
helper — fix at PR review.

## §2 — Existing pattern integration

### Current g_vanguard.c subsystems (Phase 6 / 7.2 / 6.1 era)

| Subsystem | Init line | Shutdown line | Public API |
|---|---|---|---|
| `vg_DevMode` | 182 | 193 | `_OnFrame` |
| `vg_Hitbox` | 295 | 337 | `_IsActive`, `_DebugActive`, `_StrictMode`, `_RegionName`, `_IsSelfDamageMod`, etc. |
| `vg_Netcode` | 559 | 579 | `_ProfileName` |

All three live in `g_vanguard.c` (~620 LOC total). All declare
private static state, register their cvars via direct
`trap_Cvar_Register` (NOT via `gameCvarTable`), expose only
`Init` / `Shutdown` / queryable getters.

### Where vg_Fun fits

**Recommendation: extend g_vanguard.c with a 4th subsystem
`vg_Fun_*`.** Reasons:

1. **Locality** — engineers find all VanguardMod subsystems in
   one file. Spreading them across `g_vanguard.c`, `g_vg_fun.c`,
   `g_vg_falldmg.c`... fragments mental model.
2. **Pattern repetition** — vg_Fun follows the
   Init/Shutdown/Getter shape of the other three. Same file =
   visible parallel structure.
3. **Size** — v0.7.0 vg_Fun is ~80–120 LOC (master cvar + helper
   API + registry walk + banner integration). Adding 100 LOC to
   a 620-LOC file is fine. If it ever hits ~1500 LOC, refactor.
4. **Header discipline** — declarations go in `g_vanguard.h`
   alongside existing `vg_*`, no new header file.

**Anti-recommendation: don't create `src/game/g_vg_fun.c`.** That
implies "fun is a separate subsystem from vanguard" — it's not, it's
*a vanguard subsystem*. The naming convention is `vg_Fun_*` (mixed
case) for functions and `vg_fun_*` (lowercase, underscore) for
cvars; the file is `g_vanguard.c`.

### Where the master cvar lives

`vg_fun` itself is registered via the established `gameCvarTable`
pattern in `g_cvars.c` (matches v0.6.1's `vanguard_diag_movement`),
NOT inside `vg_Fun_Init`. Reasons:

- gameCvarTable is the canonical place for top-level config
  cvars; `vg_fun` is top-level (peers with `g_pronedelay`,
  `g_speed`, `vanguard_diag_movement`)
- Sub-cvars (registered inside subsystems via direct
  `trap_Cvar_Register`) are subsystem-internal; they don't peer
  with engine cvars
- `vg_Fun_Init` reads the value, doesn't register

Storage in g_cvars.c near `vanguard_diag_movement`:
```c
vmCvar_t vg_fun;   /* v0.7.0 master switch */
```

gameCvarTable entry:
```c
{ &vg_fun, "vg_fun", "0", CVAR_LATCH | CVAR_ARCHIVE | CVAR_SERVERINFO, 0, qfalse, qfalse },
```

`CVAR_SERVERINFO` because cgame should learn the mode (for
HUD / scoreboard work in v0.7.x).

## §3 — Sub-cvar registration architecture

### Pattern α — Central registry table

`g_vanguard.c` declares a static `vg_fun_cvar_registry[]` table.
Each future feature adds entries:

```c
static const vg_fun_cvar_t vg_fun_cvar_registry[] = {
    { "vg_fun_falldmg_dmg_10",     "10",     CVAR_ARCHIVE },
    { "vg_fun_falldmg_dmg_50",     "50",     CVAR_ARCHIVE },
    { "vg_fun_falldmg_gib_health", "-175",   CVAR_LATCH | CVAR_ARCHIVE },
    /* future features add here */
};
```

`vg_Fun_Init` iterates, calls `trap_Cvar_Register` for each.

**Pro:** single source of truth, easy `vg_status` introspection.
**Con:** every feature must touch the registry table; central
file becomes a contention point.

### Pattern β — Self-registration helper

Features call `vg_Fun_RegisterCvar(...)` from their own Init
functions:

```c
/* In some future vg_Falldamage_Init() */
vg_Fun_RegisterCvar(&s_falldmg.dmg_10, "vg_fun_falldmg_dmg_10", "10", CVAR_ARCHIVE);
```

vg_fun module tracks them internally in a dynamic list / static
array sized to MAX_VG_FUN_CVARS.

**Pro:** features stay self-contained; vg_fun module doesn't grow
when features land.
**Con:** registration order matters; vg_Fun_Init must run BEFORE
any feature's Init that calls the helper.

### Pattern γ — Hybrid (helper API + internal registry)

Helper is the public API; an internal registry is the data
structure:

```c
/* In feature code */
vg_Fun_RegisterCvar(&my_cvar, "vg_fun_my_param", "default", CVAR_ARCHIVE);

/* Internal (g_vanguard.c) */
static vg_fun_cvar_entry_t s_registry[MAX_VG_FUN_CVARS];
static int s_registry_count = 0;

void vg_Fun_RegisterCvar(vmCvar_t *cvar, const char *name, const char *def, int flags)
{
    trap_Cvar_Register(cvar, name, def, flags);
    if (s_registry_count < MAX_VG_FUN_CVARS) {
        s_registry[s_registry_count].cvar = cvar;
        s_registry[s_registry_count].name = name;  /* literal pointer is stable */
        s_registry[s_registry_count].cup_default = atoi(def);
        s_registry_count++;
    }
}
```

The helper writes to the registry as a side-effect. `vg_status`
walks the registry to print all known sub-cvars + their effective
values.

**Pro:** features stay self-contained (β advantage), `vg_status`
gets a comprehensive listing (α advantage), single API to remember.
**Con:** slightly more code in vg_fun module.

### Recommendation: Pattern γ

The registry powers `vg_status` introspection (admins want one
command that lists all vg_fun cvars + their effective values). The
helper API keeps features decoupled from a central table.

For v0.7.0, the registry is empty (no features yet). The data
structure + helper API ship now; v0.7.1 Falldamage is the first
caller.

`MAX_VG_FUN_CVARS = 64` is plenty for the foreseeable feature
set (~5 cvars per feature × 10 features ≈ 50).

## §4 — Stats separation infrastructure

### Current ETLegacy stats emission paths

Inventory of the relevant hook points:

| Path | Location | What |
|---|---|---|
| Kill log | `g_combat.c:647` `G_LogPrintf("Kill: %i %i %i: ^7%s^7 killed %s^7 by %s\n", ...)` | Plain text log; no structured tag |
| Kill score | `g_combat.c:89` `AddKillScore(ent, score)` | Internal score field, no external emission |
| Skill points | `g_combat.c:795`, `:804`, etc. `G_AddSkillPoints(attacker, SK_*, score, "tag")` | Internal XP, free-text tag |
| Combat-state XP | `g_active.c:1460-1468` similar | Same pattern |

There is **no external stats endpoint today** — everything stays
in the server log + internal session state. Memory #25's
multi-endpoint stats shipping is v0.7.x or v0.8.0 work.

### Tagging strategy: NOW (v0.7.0)

**Recommendation:** add the mode tag to log lines NOW, even though
no consumer exists yet. Concretely:

```diff
- G_LogPrintf("Kill: %i %i %i: ^7%s^7 killed %s^7 by %s\n",
-             killer, self->s.number, meansOfDeath,
-             killerName, self->client->pers.netname,
-             GetMODTableData(meansOfDeath)->modName);
+ G_LogPrintf("Kill: %i %i %i: ^7%s^7 killed %s^7 by %s mode=%s\n",
+             killer, self->s.number, meansOfDeath,
+             killerName, self->client->pers.netname,
+             GetMODTableData(meansOfDeath)->modName,
+             vg_Fun_GetMode());
```

`vg_Fun_GetMode()` returns `"cup"` (vg_fun=0) or `"fun"` (vg_fun=1).

**Reasons to tag now:**

1. **Future-proofing** — when v0.7.x stats endpoint launches, all
   server logs from v0.7.0+ already carry the tag. Late-tagging
   means months of ambiguous historic stats.
2. **Cost is one line per emission point** — Kill log + 2-3
   skill-point emissions + maybe round-end summary = ~5 sites
   touched in g_combat.c / g_active.c.
3. **Backwards-compatible** — existing log parsers ignore unknown
   trailing `mode=...` tokens (gracefully).
4. **Cup-integrity** — even cup matches log `mode=cup`, which is
   audit-trail value (proves vg_fun was off during the match).

### What's NOT in v0.7.0 stats scope

- Stats endpoint (api.vanguardmod.com) — Memory #25, separate phase
- Multi-endpoint dispatch — separate phase
- SQLite local stats DB — already disabled per CLAUDE.md
- Round-end summary emission — out of v0.7.0 scope
- Per-player aggregation — backend work
- Leaderboard rendering — frontend work

v0.7.0 ships only the **mode tag on existing log lines + helper
function**. Sufficient for retroactive reconstruction by any
future endpoint reader.

### Risk analysis if deferred (tagging-later)

If we ship v0.7.0 without log tagging and add it in v0.7.x:

- All cup-server logs from v0.7.0 → v0.7.x release have NO mode
  tag. A pub server running vg_fun=1 from day 1 of v0.7.0 has
  log lines indistinguishable from cup-server logs.
- Backend launch in v0.7.x must handle: "this server's logs from
  this date range are mode-ambiguous, assume X." Bad data.
- Manual reconstruction would need to cross-reference server
  config snapshots (if recorded) against log timestamps.

The cost of tagging-now is ~5 lines of code; the cost of
tagging-later is permanent stats-bucket ambiguity for the v0.7.x
window. **Tag now.**

## §5 — Mode-switching semantics

### Master cvar `vg_fun`: CVAR_LATCH

Mode change requires `map_restart`. Cup-integrity guarantee: a
mid-match `set vg_fun 1` does NOT take effect until the next map
load. ETLegacy enforces this via the standard CVAR_LATCH
mechanism — no custom enforcement needed.

### Sub-cvars `vg_fun_*`: CVAR_ARCHIVE (no LATCH)

Admins can pre-stage sub-cvar tweaks anytime. They take effect on
the next read (helper Strategy C). Combined with the master being
LATCH:

- Mid-match set `vg_fun_falldmg_dmg_50 5` is accepted (sub-cvar
  is not latched), but `vg_fun` remains 0, so helper still
  returns the cup-default 50. **Effectively a no-op until next
  map_restart with vg_fun=1.**
- Mid-match set `vg_fun 1` is latched. Sub-cvars stay live but
  inactive. Next `map_restart` activates the new mode.

### Mode-switch banner emission

When `vg_Fun_Init` runs (start of `G_InitGame`), it inspects
`vg_fun.integer` and emits one boot line:

```
VG_Fun: mode=cup-orthodox (sub-cvar tweaks ignored, engine defaults active)
```

OR when vg_fun=1:

```
VG_Fun: mode=fun-public (5 sub-cvars active, see vg_status for details)
```

Mirrors `VG_Netcode: profile=cup` and `VG_Hitbox: initialized`
boot-line patterns. Emitted via `G_Printf`.

### Integration with the WolfGuard banner

Add one line to `wg_banner.c::WG_PrintStartupBanner`:

```diff
   G_Printf("^7  WolfGuard:    ^1[ NOT INCLUDED ]\n");
   G_Printf("^7  Protection:   ^1disabled\n");
   G_Printf("^7  Info:         ^5https://vanguardmod.com\n");
+  G_Printf("^7  Mode:         ^%s%s\n",
+           vg_fun.integer ? "3" : "5",
+           vg_fun.integer ? "fun-public" : "cup-orthodox");
   G_Printf("^7  Build mode:   ^3community\n");
```

Color codes: `^5` (cyan) for cup-orthodox (default, conservative),
`^3` (yellow) for fun-public (notable, attention-grabbing).

Same line in the protected-build banner branch.

### `vg_status` server-console command

Mirrors `wg_status`. Prints current vg_fun state + iterates the
registry:

```
========================================================
  VG_FUN STATE
--------------------------------------------------------
  Master:           vg_fun = 1 (fun-public)
  Locked sub-cvars: 5
    vg_fun_falldmg_dmg_10        = 5     (cup-default: 10)
    vg_fun_falldmg_dmg_15        = 15    (cup-default: 15)
    vg_fun_falldmg_dmg_25        = 25    (cup-default: 25)
    vg_fun_falldmg_dmg_50        = 40    (cup-default: 50)
    vg_fun_falldmg_gib_health    = -300  (cup-default: -175)
========================================================
```

For v0.7.0, the registry is empty — `vg_status` prints "0
registered sub-cvars (foundation only)". v0.7.1 Falldamage adds
the first entries.

Register `vg_status` in `g_svcmds.c` consoleCommandTable next to
`wg_status` (line 2640).

## §6 — Phase 8.0b (v0.7.1) prep

Sketch of how Falldamage will use the v0.7.0 foundation. This
example exists ONLY to validate the API design — not implemented
in v0.7.0.

### v0.7.1 falldamage subsystem skeleton

```c
/* New file or extension to g_vanguard.c */
static struct {
    vmCvar_t dmg_10;
    vmCvar_t dmg_15;
    vmCvar_t dmg_25;
    vmCvar_t dmg_50;
    vmCvar_t gib_health;
} s_falldmg;

void vg_Falldamage_Init(void)
{
    vg_Fun_RegisterCvar(&s_falldmg.dmg_10,     "vg_fun_falldmg_dmg_10",     "10",   CVAR_ARCHIVE);
    vg_Fun_RegisterCvar(&s_falldmg.dmg_15,     "vg_fun_falldmg_dmg_15",     "15",   CVAR_ARCHIVE);
    vg_Fun_RegisterCvar(&s_falldmg.dmg_25,     "vg_fun_falldmg_dmg_25",     "25",   CVAR_ARCHIVE);
    vg_Fun_RegisterCvar(&s_falldmg.dmg_50,     "vg_fun_falldmg_dmg_50",     "50",   CVAR_ARCHIVE);
    vg_Fun_RegisterCvar(&s_falldmg.gib_health, "vg_fun_falldmg_gib_health", "-175", CVAR_LATCH | CVAR_ARCHIVE);
}

int vg_Falldamage_GetDmg(int event)
{
    switch (event) {
    case EV_FALL_DMG_10: return vg_Fun_GetInt(&s_falldmg.dmg_10, 10);
    case EV_FALL_DMG_15: return vg_Fun_GetInt(&s_falldmg.dmg_15, 15);
    case EV_FALL_DMG_25: return vg_Fun_GetInt(&s_falldmg.dmg_25, 25);
    case EV_FALL_DMG_50: return vg_Fun_GetInt(&s_falldmg.dmg_50, 50);
    default:             return 5;
    }
}
```

### G_FallDamage rewrite

```diff
 void G_FallDamage(gentity_t *ent, int event)
 {
     int damage;

-    if (event == EV_FALL_DMG_50)      damage = 50;
-    else if (event == EV_FALL_DMG_25) damage = 25;
-    else if (event == EV_FALL_DMG_15) damage = 15;
-    else if (event == EV_FALL_DMG_10) damage = 10;
-    else                              damage = 5;
+    damage = vg_Falldamage_GetDmg(event);

     ent->pain_debounce_time = level.time + 200;
     G_Damage(ent, NULL, NULL, NULL, NULL, damage, 0, MOD_FALLING);
 }
```

Helper signature: `vg_Fun_GetInt(vmCvar_t *cvar, int cup_default)`
— takes the cvar pointer (not name lookup; faster) and the cup
default. The pointer-form lets the helper skip the registry walk
on hot paths like falldamage damage lookup.

A name-form `vg_Fun_GetIntByName(const char *name, int cup_default)`
is also available for ad-hoc lookups (e.g. console commands), but
performance-critical sites use the pointer form.

### Why the helper pattern matters

Code review readability: the line `damage = vg_Falldamage_GetDmg(event);`
makes it obvious that the value is mode-aware. A direct read like
`damage = s_falldmg.dmg_50.integer;` would silently bypass the
lock, and a reviewer needs to know the convention to flag it.

The CI grep in §9 catches forgotten direct reads.

## §7 — Naming convention

### Canonical pattern

- `vg_fun` — master switch (CVAR_LATCH|CVAR_ARCHIVE|CVAR_SERVERINFO,
  default 0)
- `vg_fun_<feature>_<param>` — feature sub-cvar
- (Optional, deferred) `vg_fun_<feature>` — per-feature toggle

Examples for v0.7.x roadmap features (per Memory #8):

```
vg_fun_falldmg_dmg_10
vg_fun_falldmg_dmg_50
vg_fun_falldmg_gib_health
vg_fun_xpsave_persist_across_maps
vg_fun_doublejump_enabled
vg_fun_doublejump_height
vg_fun_fastreload_multiplier
vg_fun_classmod_soldier_falldmg_mult
```

### Two-level vs flat

**Flat (recommended for v0.7.0):**
- `vg_fun=1` unlocks ALL `vg_fun_*` features simultaneously
- Helper checks only the master cvar
- Simpler implementation
- Per Memory #8 spec: *"vg_fun=1 unlocks vg_fun_* features"* —
  matches flat semantics

**Two-level:**
- `vg_fun=1` AND `vg_fun_<feature>=1` both required
- Per-feature toggle = explicit opt-in per feature
- More flexibility (cup-server can host pub-night with only
  doublejump, not falldmg tweaks)
- More complexity (helper checks two cvars)

**Recommendation: flat for v0.7.0.** Two-level is added per-feature
when a real demand surfaces ("admin wants doublejump but not
falldmg"). Backwards-compatible: an `vg_fun_<feature>` cvar can
default to 1, so existing flat semantics persist; admins who care
can flip it to 0.

## §8 — Banner + status integration

### Banner extension (in `wg_banner.c`)

One new line per banner variant. See §5 for the diff. Color
choice: `^5` cyan for cup-orthodox, `^3` yellow for fun-public.

Position: between "Info:" / "Backend:" line and "Build mode:" line.

### `vg_status` command (in `g_svcmds.c`)

Spec output (vg_fun=0, no registered sub-cvars):
```
========================================================
  VG_FUN STATE
--------------------------------------------------------
  Master:           vg_fun = 0 (cup-orthodox)
  Registered:       0 sub-cvars (foundation only — features in v0.7.1+)
========================================================
```

Spec output (vg_fun=1, with future v0.7.1 falldmg cvars registered):
```
========================================================
  VG_FUN STATE
--------------------------------------------------------
  Master:           vg_fun = 1 (fun-public)
  Registered:       5 sub-cvars
    vg_fun_falldmg_dmg_10         =     5    (cup-default: 10)
    vg_fun_falldmg_dmg_15         =    15    (cup-default: 15)
    vg_fun_falldmg_dmg_25         =    25    (cup-default: 25)
    vg_fun_falldmg_dmg_50         =    40    (cup-default: 50)
    vg_fun_falldmg_gib_health     =  -300    (cup-default: -175)
========================================================
```

Format: align cvar name left to ~36 cols, value right to ~5 cols,
cup-default in parens. Diverging values (current ≠ cup-default)
optionally highlighted (e.g. yellow `^3` for the value); this is
polish, not required.

## §9 — CI gates

Add to `.github/workflows/ci.yml` Linux build job (mirror existing
Omni-bot + community-banner pattern):

```yaml
- name: Verify vg_fun foundation in qagame Linux SO
  run: |
    set -euo pipefail
    BIN=$(find build -name 'qagame.mp.*.so' -print -quit)
    test -n "${BIN}"
    MASTER=$(strings -a "${BIN}" | grep -c "^vg_fun$" || true)
    LOG=$(strings -a "${BIN}" | grep -c "VG_Fun: mode=" || true)
    STATUS=$(strings -a "${BIN}" | grep -c "VG_FUN STATE" || true)
    HELPER=$(strings -a "${BIN}" | grep -c "vg_Fun_Get" || true)
    echo "master=${MASTER} log=${LOG} status=${STATUS} helper=${HELPER}"
    if [ "${MASTER}" -lt 1 ] || [ "${LOG}" -lt 1 ] || [ "${STATUS}" -lt 1 ]; then
      echo "::error::v0.7.0 vg_fun foundation surface incomplete"
      exit 1
    fi
```

Thresholds for v0.7.0 (foundation only):
- `MASTER ≥ 1` (cvar name string in binary)
- `LOG ≥ 1` (boot-line `VG_Fun: mode=...`)
- `STATUS ≥ 1` (banner string `VG_FUN STATE`)
- `HELPER` not gated (foundation has no callers yet)

For v0.7.1 (Falldamage lands), bump:
- Add `FALLDMG=$(strings | grep -c "vg_fun_falldmg_")` ≥ 5
- Bump `HELPER ≥ 5` (5 read sites in G_FallDamage)

### Discipline-grep (recommended addition)

In CI, run a static-source grep that catches direct reads of
`vg_fun_*` cvars outside the helper:

```yaml
- name: Verify no direct reads of vg_fun_* cvars (helper discipline)
  run: |
    set -euo pipefail
    DIRECT=$(grep -rn 'vg_fun_[a-z_]*\.\(integer\|value\|string\)' src/game/ \
             | grep -v 'src/game/g_vanguard.c' \
             | grep -v 'extern vmCvar_t vg_fun' || true)
    if [ -n "${DIRECT}" ]; then
      echo "::error::Direct reads of vg_fun_* cvars found outside helper:"
      echo "${DIRECT}"
      echo "::error::Use vg_Fun_GetInt() helper instead — Strategy C lock-mechanism."
      exit 1
    fi
```

This enforces the Strategy C discipline at PR review time. v0.7.0
ship state: zero matches (no features yet).

## §10 — Documentation touchpoints

| File | Change | Scope |
|---|---|---|
| **NEW** `docs/VG_FUN_MODE.md` | Full spec — what vg_fun is, lock-mechanism, helper API, naming convention, roadmap of features (xpsave/doublejump/etc per Memory #8). ~150–200 LOC. | v0.7.0 PR |
| `docs/CUP_VS_PUBLIC.md` | New top-level section "vg_fun mode" parallel to existing sv_fps + g_pronedelay sections. Cross-link to VG_FUN_MODE.md. | v0.7.0 PR |
| `docs/RELEASE_NOTES.md` | New v0.7.0 entry above v0.6.2 — covers vg_fun foundation, banner extension, vg_status, log mode-tag | v0.7.0 PR |
| `docs/notes/PHASE_8_0B_AUDIT.md` | Add §13 "Architectural pivot to v0.7.1, foundation in v0.7.0" — link forward to this audit + the v0.7.0 PR | v0.7.0 PR |
| `docs/notes/PHASE_9_0_VG_FUN_FOUNDATION_AUDIT.md` (this file) | Add §15 "Execution log" after v0.7.0 ships (mirror Phase Copyright-Sweep §9) | v0.7.0 PR |

## §11 — Open decisions for wahke

1. **Lock-mechanism (§1)** — Strategy A (CVAR_LATCH apply-defaults),
   B (read-only), or C (helper)? **Recommendation: C with vg_fun
   itself LATCH.**
2. **File location (§2)** — Extend `g_vanguard.c` or create
   `g_vg_fun.c`? **Recommendation: extend g_vanguard.c.**
3. **Cvar registration (§3)** — Pattern α (central registry),
   β (self-register helper), or γ (hybrid)? **Recommendation: γ.**
4. **Stats tagging (§4)** — Now (v0.7.0) or later (v0.7.x)?
   **Recommendation: now** (1 line per emission, future-proofs).
5. **Mode-switching (§5)** — `vg_fun` CVAR_LATCH (recommended)
   or live-switchable?
6. **Naming (§7)** — Flat or two-level lock? **Recommendation:
   flat for v0.7.0** (per-feature toggle added on demand later).
7. **Banner (§8)** — Include vg_fun mode line (recommended) or
   keep banner unchanged?
8. **v0.7.0 scope (§13)** — Foundation only (recommended) or
   include 1 trivial proof-of-concept feature like
   `vg_fun_announce_mode 0/1`?

## §12 — Risk map per sub-topic

| Sub-topic | Risk | Why |
|---|---|---|
| `vg_fun` master cvar registration | LOW | Mirrors v0.6.1 `vanguard_diag_movement` exactly |
| `vg_Fun_Init` / `Shutdown` subsystem | LOW | Mirrors `vg_Netcode_Init` exactly (~30 LOC) |
| Helper API `vg_Fun_GetInt` | LOW | One function, ~10 LOC, pure logic |
| Internal registry data structure | LOW | Static array, MAX_VG_FUN_CVARS=64 |
| `vg_status` console command | LOW | Mirrors `wg_status` exactly (~20 LOC) |
| Banner extension | LOW | One new line in wg_banner.c |
| Stats log mode-tag (5 sites) | LOW | Pure G_LogPrintf format string change |
| `VG_Fun: mode=...` boot-line | LOW | One G_Printf in vg_Fun_Init |
| **Strategy C discipline (every read uses helper)** | MED | Code-review burden; CI grep mitigates |
| **Future per-feature toggle backwards compat** | LOW-MED | Flat default safe for v0.7.0; per-feature toggles default to 1 if added later |
| **CVAR_SERVERINFO weight** | LOW | One bool string, ~10 bytes in serverinfo |
| **Cup-integrity (mid-match `vg_fun 1`)** | LOW | CVAR_LATCH on master engine-enforced |
| **Sub-cvar persistence across map_restart** | LOW | CVAR_ARCHIVE; admin sets once, persists |
| **Stats endpoint (deferred)** | OUT-OF-SCOPE | Memory #25, separate phase |
| **Per-feature implementation (deferred)** | OUT-OF-SCOPE | v0.7.1+ (Falldamage first) |

## §13 — Implementation-order recommendation

### v0.7.0 — Foundation (recommended single PR)

1. `g_cvars.c`: `vmCvar_t vg_fun;` storage + gameCvarTable entry
   (CVAR_LATCH|CVAR_ARCHIVE|CVAR_SERVERINFO, default `"0"`)
2. `g_vanguard.h`: declarations for `vg_Fun_Init`, `vg_Fun_Shutdown`,
   `vg_Fun_GetMode`, `vg_Fun_GetInt`, `vg_Fun_GetIntByName`,
   `vg_Fun_RegisterCvar`, `vg_Fun_PrintStatus`
3. `g_vanguard.c`: `vg_Fun_*` subsystem (4th subsystem in the file).
   ~120 LOC: registry array + register-helper + getters + status
   printer + boot-line emission
4. `g_main.c::G_InitGame`: call `vg_Fun_Init()` after `vg_Netcode_Init()`
5. `g_main.c::G_ShutdownGame`: call `vg_Fun_Shutdown()` next to other vg shutdowns
6. `wolfguard/wg_banner.c`: extend banner with Mode: line (both
   community + protected variants)
7. `g_svcmds.c`: add `vg_status` to consoleCommandTable + define
   `Svcmd_VG_Status_f` calling `vg_Fun_PrintStatus()`
8. `g_combat.c::G_LogPrintf` Kill line: add `mode=%s` token + arg
9. `g_combat.c::G_AddSkillPoints` 4–5 sites: add mode tag to the
   tracking call (or just to the log emission — TBD per how
   detailed we want to be in v0.7.0)
10. CI gate: `.github/workflows/ci.yml` Linux job adds the §9
    yaml block + the discipline grep
11. Docs: `docs/VG_FUN_MODE.md` (new), `docs/CUP_VS_PUBLIC.md`
    (new section), `docs/RELEASE_NOTES.md` (v0.7.0 entry),
    `docs/notes/PHASE_8_0B_AUDIT.md` (§13 cross-link), this audit
    (§15 execution log)

Estimated effort: 1 PR, 5–7 commits (subsystem + banner +
status-cmd + log-tag + CI + docs). ~250 LOC code + ~400 LOC docs.
Mirror v0.6.0 WolfGuard foundation PR effort.

### v0.7.1 — Falldamage (first vg_fun-controlled feature)

1. Add `vg_Falldamage_*` subsystem to `g_vanguard.c`
2. Register the 5 sub-cvars via `vg_Fun_RegisterCvar`
3. Add `vg_Falldamage_GetDmg(event)` getter using
   `vg_Fun_GetInt(&s_falldmg.dmg_X, X)`
4. Modify `G_FallDamage` to call the getter
5. Update `vg_status` test data (registry now has 5 entries)
6. Bump CI gate thresholds (§9 v0.7.1 section)
7. Docs: `FALLDAMAGE_PROFILE_REFERENCE.md` (per Phase 8.0b §7),
   `RELEASE_NOTES.md` v0.7.1 entry, this audit §15 update,
   Phase 8.0b §13 update

Phase 8.0b audit's recommendations are preserved — Tier 1 (6 cvars)
is what v0.7.1 implements, just renamed to `vg_fun_falldmg_*`
prefix.

### v0.7.2+ — Future features per Memory #8

- `vg_fun_xpsave_*` — XP persistence across map changes
- `vg_fun_doublejump_*` — Q3-style double-jump
- `vg_fun_fastreload_*` — reload-time multiplier
- `vg_fun_classmod_*_*` — class-specific modifiers (e.g. soldier
  falldmg multiplier)

Each future feature follows the v0.7.1 pattern: subsystem in
`g_vanguard.c`, sub-cvars registered via helper, read sites use
`vg_Fun_GetInt`. CI grep enforces discipline.

### Why not include a proof-of-concept feature in v0.7.0

Including e.g. `vg_fun_announce_mode 0/1` (server prints "[FUN
MODE]" in chat every 5 minutes) as POC could validate the helper
pattern. But:

- Foundation-only PRs are easier to review (one mental model:
  infrastructure)
- v0.7.1 Falldamage is a real validation, not a toy
- Toy feature would need its own removal in v0.7.x cleanup
  (vs Falldamage being permanent)

**Recommendation: foundation only in v0.7.0.** v0.7.1 Falldamage
is the proof-of-concept and the production feature in one shot.

### Defer indefinitely

- Stats endpoint / multi-endpoint dispatch (Memory #25,
  separate phase)
- Cup-vs-fun leaderboard frontend (vanguardmod.com work)
- Backend api.vanguardmod.com schema for fun-mode tagged stats
- `vg_fun_<feature>` per-feature toggles unless feature explicitly
  needs that granularity
- Live mode-switching (drop CVAR_LATCH from `vg_fun`) — only if
  cup community asks; cup-integrity argument outweighs flexibility

## §14 — References

### Code

- `src/game/g_cvars.c:296` — `g_pronedelay` storage example
  (cvarTable pattern for the master cvar)
- `src/game/g_cvars.c:310` — `vanguard_diag_movement` (v0.6.1
  cvarTable pattern, exact mirror for `vg_fun` storage)
- `src/game/g_cvars.c:654` — `vanguard_diag_movement` cvarTable
  entry
- `src/game/g_main.c:65` — `extern vmCvar_t vanguard_diag_movement`
  (extern pattern for the helper)
- `src/game/g_main.c:1922-1932` — boot-line gate pattern
  (template for VG_Fun: mode= log)
- `src/game/g_vanguard.c:182` — `vg_DevMode_Init` (subsystem template)
- `src/game/g_vanguard.c:295` — `vg_Hitbox_Init` (subsystem with
  multiple cvars, helper-style getters)
- `src/game/g_vanguard.c:559` — `vg_Netcode_Init` (subsystem with
  apply pattern + serverinfo cvar — closest analog to vg_Fun)
- `src/game/g_vanguard.c:579` — `vg_Netcode_Shutdown`
- `src/game/g_vanguard.c:582` — comment: shutdown hook chronology
- `src/game/g_combat.c:89` — `AddKillScore` (internal scoring)
- `src/game/g_combat.c:647` — `G_LogPrintf("Kill: ...")` (the line
  to add `mode=%s` to)
- `src/game/g_combat.c:795,804` — `G_AddSkillPoints` examples
  (other sites that may want mode tagging)
- `src/game/g_active.c:1460-1468` — combat-state XP emission
- `src/game/g_svcmds.c:2640` — `wg_status` registration in
  consoleCommandTable (pattern for `vg_status`)
- `src/game/g_svcmds.c:2556` — `Svcmd_WG_Status_f` definition
  (pattern for `Svcmd_VG_Status_f`)
- `src/game/wolfguard/wg_banner.c:30-55` — `WG_PrintStartupBanner`
  (banner to extend with Mode: line)

### Memory references

- **Memory #8** — vg_fun architecture spec from Phase 6 era. Per
  this prompt: *"vg_fun (default 0=Cup-orthodox, 1=Fun-Public).
  vg_fun=0 locks sub-cvars to cup-defaults; vg_fun=1 unlocks
  vg_fun_* features. WolfGuard active on BOTH modes (universal).
  Stats tracking on BOTH but SEPARATED leaderboards."*
- **Memory #25** — multi-endpoint stats spec. *"Stats events
  tagged with mode=cup/fun, vanguardmod.com renders both
  leaderboards + combined profile."*
- **Memory #10** — naming convention `vg_*` for new VanguardMod
  files; mixed-case for functions, lowercase for cvars
- **Memory #11** — v0.4.3 SPDX sweep precedent; new files get
  SPDX-only headers

### Audit cross-refs

- `docs/notes/PHASE_8_0B_AUDIT.md` — Falldamage redesign audit;
  Tier 1 (6 cvars) becomes v0.7.1 implementation. v0.7.0 vg_fun
  foundation is the prerequisite. Phase 8.0b §13 should be added
  in the v0.7.0 PR with a forward-link to this audit.
- `docs/notes/PHASE_7_3_AUDIT.md` — movement physics audit;
  references Phase 8.0b which now becomes v0.7.1
- `docs/notes/PHASE_7_2_AUDIT.md` — netcode profile precedent;
  `vanguard_netcode_profile` is the pattern to mirror for vg_fun
- `docs/CUP_VS_PUBLIC.md` — the public-facing companion doc; gets
  a new section in v0.7.0

### Phase 7.0 lessons-learned applied

1. **Diagnostic infrastructure** — `VG_Fun: mode=cup-orthodox`
   boot-line + `vg_status` command land in v0.7.0 alongside the
   foundation, not retrofitted later
2. **Build-flag mismatches** — CI gate (§9 yaml) extends the
   existing `strings | grep` pattern; discipline grep adds a
   second layer (catches direct reads bypassing the helper)
3. **Crash-bugs vs tuning-bugs split** — v0.7.0 is pure
   architecture-foundation, no value tuning. Tuning happens in
   v0.7.1 (Falldamage). Pattern parallels v0.6.0 WolfGuard
   foundation → v0.6.1 cup-movement (config + cvar tuning on
   top of v0.6.0 plumbing).
