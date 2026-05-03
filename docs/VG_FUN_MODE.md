# VanguardMod vg_fun Mode

VanguardMod operates in two distinct modes via the `vg_fun` master
cvar — cup-orthodox or fun-public. This document explains how the
mode system works and what features are gated by it.

## Modes

| `vg_fun` | Mode | Behaviour |
|---|---|---|
| `0` (default) | **cup-orthodox** | All `vg_fun_*` sub-cvars locked to cup-defaults. ETPro / cup-mod-aligned values. Stats tagged as `mode=cup`. |
| `1` | **fun-public** | Sub-cvars unlocked; server admin can tune. Public-server-friendly values. Stats tagged as `mode=fun`. |

## Mode switching

`vg_fun` is `CVAR_LATCH` — changes take effect at the **next
`map_restart`**, not immediately. This protects cup-integrity: a
match cannot start in cup-mode and silently switch mid-game.

```
\rcon vg_fun 1
\rcon map_restart
```

Sub-cvars (`vg_fun_*`) are `CVAR_ARCHIVE` (no LATCH) — admins can
pre-stage tweaks anytime; they take effect on the next read once
the master is `1` after a map restart.

## Stats separation

Both modes tag stats events with `mode=cup` or `mode=fun`. The
vanguardmod.com backend renders separate leaderboards per mode
(planned; v0.7.x stats endpoint work — Memory #25).

This means cup-stats and fun-stats are **never mixed** —
cup-integrity of the leaderboard is preserved regardless of how
many fun-mode servers exist.

Tagged log lines (v0.7.0):

- `Kill: ... mode=<cup|fun>` — primary stats event
- `ClientConnect: ... mode=<cup|fun>` — session bookend
- `ClientDisconnect: ... mode=<cup|fun>` — session end
- `WeaponStats: ... mode=<cup|fun>` — per-player weapon dump
- `InitGame: ... mode=<cup|fun>` — server session bookend

A future stats endpoint can retroactively reconstruct per-mode
totals from any log file produced by v0.7.0+.

## WolfGuard universality

WolfGuard anti-cheat is active on **both** modes. Fun-mode is for
relaxed gameplay tunings, **not** for relaxed anti-cheat
enforcement.

## Server console commands

- **`vg_status`** — prints current vg_fun mode + registered
  features + cup-defaults

  Example output (cup mode, foundation only):

  ```
  ========================================================
    VG_FUN STATE
  --------------------------------------------------------
    Master:           vg_fun = 0 (cup)
    Registered:       0 sub-cvars
      (none — v0.7.0 foundation only; v0.7.1 adds first feature)
  ========================================================
  ```

  Example output (fun mode with v0.7.1 Falldamage registered):

  ```
  ========================================================
    VG_FUN STATE
  --------------------------------------------------------
    Master:           vg_fun = 1 (fun)
    Registered:       5 sub-cvars
      vg_fun_falldmg_dmg_10                (cup-default: 10)
      vg_fun_falldmg_dmg_15                (cup-default: 15)
      vg_fun_falldmg_dmg_25                (cup-default: 25)
      vg_fun_falldmg_dmg_50                (cup-default: 50)
      vg_fun_falldmg_gib_health            (cup-default: -175)
  ========================================================
  ```

## Feature roadmap

vg_fun is **infrastructure only** in v0.7.0 — no features yet gate
on it. The first vg_fun-controlled feature ships in v0.7.1:

| Version | Feature | Status |
|---|---|---|
| v0.7.0 | Foundation (master cvar, helper API, status command, banner integration, stats tagging) | shipped |
| v0.7.1 | Falldamage profile (Phase 8.0b): `vg_fun_falldmg_dmg_10/15/25/50`, `vg_fun_falldmg_gib_health` | planned |
| v0.7.x | XP-save (`vg_fun_xpsave_persist_across_maps`) | backlog |
| v0.7.x | Double-jump (`vg_fun_doublejump_*`) | backlog |
| v0.7.x | Fast-reload (`vg_fun_fastreload_multiplier`) | backlog |
| v0.7.x | Class modifiers (`vg_fun_classmod_*`) | backlog |

## Falldamage (v0.7.1+) — first vg_fun-controlled feature

When `vg_fun 1`, the following cvars become tunable. Defaults
match cup-orthodox (engine values); recommended public-profile
values listed:

| Cvar | Cup default | Public recommended | Description |
|---|---|---|---|
| `vg_fun_falldmg_dmg_10` | 10 | 8 | Damage at "moderate fall" velocity (`EV_FALL_DMG_10`) |
| `vg_fun_falldmg_dmg_15` | 15 | 12 | Damage at "high fall" velocity (`EV_FALL_DMG_15`) |
| `vg_fun_falldmg_dmg_25` | 25 | 20 | Damage at "severe fall" velocity (`EV_FALL_DMG_25`) |
| `vg_fun_falldmg_dmg_50` | 50 | 40 | Damage at "near-death fall" velocity (`EV_FALL_DMG_50`) |
| `vg_fun_falldmg_gib_health` | -175 | -300 | Health threshold for fall-NDIE gib formula (engine: -175 = always gib on NDIE; -300 = aggressive gib-prevention) |

Public profile recommends symmetrical -20% damage scaling plus
aggressive gib-prevention. Server admins tune to taste.

### Activation

```
\rcon vg_fun 1
\rcon map_restart
\rcon vg_fun_falldmg_dmg_10 8
\rcon vg_fun_falldmg_dmg_15 12
\rcon vg_fun_falldmg_dmg_25 20
\rcon vg_fun_falldmg_dmg_50 40
\rcon vg_fun_falldmg_gib_health -300
```

Cup servers leave `vg_fun 0` and inherit engine defaults
unconditionally. The helper API at `vg_Fun_GetInt` short-circuits
to the cup_default when vg_fun=0, so any admin-set sub-cvar
values are silently ignored.

### Diagnostic

Set both `vg_fun 1` AND `g_developer 1` to see per-event damage
log lines:

```
VG_Falldmg: event=EV_FALL_DMG_10 dmg=8 (vg_fun=1, helper-resolved)
VG_Falldmg: event=EV_FALL_NDIE dmg=176 (vg_fun=1, helper-resolved)
```

Tuning workflow: drop from known oasis heights, observe damage
output, adjust cvar, repeat.

### Phase 8.0a regression-safety

Phase 8.0a's NULL-guard (`g_combat.c::G_Damage` v0.5.2.2 hotfix)
prevents SIGSEGV on `MOD_FALLING` with NULL attacker / point.
v0.7.1 adds tuning on top — the guard reads attacker / point /
mod, not the damage value, so new cvar values can't reach the
crash. Verified via `die -1` rcon test: 0 SIGSEGV before and
after, with vg_fun=0 and vg_fun=1.

### What's NOT in v0.7.1 (deferred)

- **Velocity threshold cvars** (`delta_short`, `delta_10`, etc. —
  6 cvars in `bg_pmove.c::PM_CrashLand`). Tier 2 in Phase 8.0b
  audit; deferred to v0.7.x because cgame + qagame share
  `bg_pmove.c` and prediction-correctness needs configstring sync.
- **PMF_TIME_KNOCKBACK durations** (4 cvars). Same prediction
  problem; deferred to v0.7.x with thresholds.
- **Lag-spike z-velocity clamp** (Phase 7.3 audit §2.5). The
  classic ET resync-spike-instant-fatal-fall bug. Deferred to
  v0.8.0 with netem testing infrastructure.
- **Class-based modifiers** (e.g. soldier-takes-less-falldamage).
  Cup-orthodox forbids — Phase 7.3 audit §3.4 confirmed every
  cup-mod treats classes identically. Deferred indefinitely.

## Double-Jump (v0.7.2+) — second vg_fun-controlled feature

When `vg_fun 1` AND `vg_fun_doublejump 1`, players can jump a
second time while airborne. Default behaviour matches engine
ground-jump (full height, no cost, all classes); admins tune
via 4 cvars.

| Cvar | Cup default | Public recommended | Description |
|---|---|---|---|
| `vg_fun_doublejump` | 0 | 1 | Master toggle for the feature |
| `vg_fun_doublejump_height` | 270 | 270 (or 200 for "lower second jump") | Vertical velocity for the second jump (engine `JUMP_VELOCITY` = 270) |
| `vg_fun_doublejump_classes` | 0 (all) | 0 or bitmask | Restrict to specific classes |
| `vg_fun_doublejump_stamina` | 0 (no cost) | 50 (drains sprint) | Sprint-bar cost per second jump (units of 100 on STAT_SPRINTTIME's 0..20000 scale) |

### Class bitmask

```
0  = all classes (default)
1  = soldier
2  = medic
4  = engineer
8  = field-ops
16 = covert-ops
```

Combine bits: `5` = soldier + engineer only.

### Activation

```
\rcon vg_fun 1
\rcon map_restart
\rcon vg_fun_doublejump 1
```

Cup servers leave `vg_fun 0` and inherit engine single-jump
behaviour unconditionally — `vg_pm_cvar_int` reads the master
gate; cup-orthodox path returns 0 and skips the entire
double-jump branch in `PM_CheckJump`.

### How the second jump works

1. Player jumps from ground (engine standard, `velocity[2] = JUMP_VELOCITY`)
2. While airborne, player can press jump once more (after release —
   the existing `PMF_JUMP_HELD` anti-spam still applies)
3. Engine's 850ms `PM_JUMP_DELAY` anti-bunnyhop cooldown is
   bypassed for the second jump only; ground-jumps still pay the
   delay (cup-orthodox bunnyhop prevention preserved)
4. `PMF_VG_DOUBLEJUMPED` flag set on the player; further jump
   presses while airborne are rejected
5. Flag cleared at the landing instant in `PM_GroundTrace`
   (transition `groundEntityNum` NONE → real entity), restoring
   the second-jump credit for next airtime

### Falldamage interaction

Higher jumps mean harder lands. Public-server admins running
`vg_fun_doublejump 1` should consider tuning
`vg_fun_falldmg_*` together for a balanced feel:

```
\rcon vg_fun_falldmg_dmg_50 35       # was 50 (cup), 40 (public-recommended)
\rcon vg_fun_falldmg_gib_health -300 # no-gib-on-fall
```

### Cup-orthodox preservation

`vg_fun=0` short-circuits double-jump entirely:

- `vg_pm_cvar_int("vg_fun", 0)` returns 0 → eligibility check
  fails immediately, byte-identical to upstream behaviour
- The 850ms `PM_JUMP_DELAY` gate stays active in the standard
  branch
- `PMF_VG_DOUBLEJUMPED` bit is never set; engine sees normal
  `pm_flags` layout
- Phase 6 multi-region hitbox + Phase 7 strict-mode + Phase 8.0a
  NULL-guard untouched (movement layer is independent)

### Edge cases handled by upstream

- **Prone:** PM_CheckJump early-returns on `EF_PRONE` (line 825),
  so prone players don't double-jump
- **Ladder:** `PM_LadderMove` dispatches before `PM_AirMove`, so
  PM_CheckJump never fires on ladders
- **Swimming:** `PM_CheckWaterJump` runs before, water-jump path
  takes precedence
- **Spectator/free-fly:** different pm_type dispatches; PM_CheckJump
  doesn't fire
- **MG42-mounted, vehicles:** `EF_MOUNTEDTANK` blocks all jumps
  upstream

## Implementation notes

All `vg_fun_*` cvar reads MUST use the helper API
(`vg_Fun_GetInt`, `vg_Fun_GetFloat`). Direct `.integer` / `.value`
access on `vg_fun_*` cvars bypasses the cup-default lock mechanism
and is a **CI failure** (the discipline grep in
`.github/workflows/ci.yml` enforces this).

Helper signature:

```c
int   vg_Fun_GetInt(const char *cvar_name, int   cup_default);
float vg_Fun_GetFloat(const char *cvar_name, float cup_default);
```

Behaviour:
- `vg_fun=0` → returns `cup_default` (the engine value, untouched)
- `vg_fun=1` → returns the cvar value (admin's tuned value, or
  `cup_default` if the admin hasn't set it)

See `src/game/g_vanguard.c::vg_Fun_*` for the helper implementation
and the introspection registry that powers `vg_status`.

## Reference

- Architectural recon: `docs/notes/PHASE_9_0_VG_FUN_FOUNDATION_AUDIT.md`
- Spec source: VanguardMod Memory #8 (Phase 6 era architectural plan)
- Stats spec: VanguardMod Memory #25 (multi-endpoint stats)
- Cup-vs-public divergences: `docs/CUP_VS_PUBLIC.md`
- Phase 8.0b Falldamage audit (v0.7.1 first feature):
  `docs/notes/PHASE_8_0B_AUDIT.md`
