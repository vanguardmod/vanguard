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
