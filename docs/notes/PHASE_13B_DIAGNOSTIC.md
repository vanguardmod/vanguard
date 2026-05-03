# Phase 13b — Diagnostic-first hotfix (v0.7.2.2)

## TL;DR

Two separate problems in v0.7.2.1 production:

1. **Registry overflow on map_restart cycles** — fixed outright.
   `vg_Fun_RegisterCvar` is now idempotent.
2. **Second-jump runtime failure** — diagnosed via gated server-side
   log line in `PM_CheckJump`. Live-test data feeds v0.7.2.3 fix.

No behaviour change for production servers (vanguard_dev=0 default).
Phase 8.0a NULL-guard untouched, v0.7.2.1 splash bypass + SERVERINFO
changes untouched, cup-orthodox preserved.

## Why two issues are bundled

Both problems were exposed by the same wahke production test of v0.7.2.1.
The registry overflow is mechanically independent of the second-jump
failure (verified during recon), but they share a fix vehicle and a
release window. Splitting into v0.7.2.2 + v0.7.2.3 was considered and
rejected: registry overflow is small, low-risk, and should not wait.

## Problem 1 — Registry overflow

### Evidence

wahke's server log (post-v0.7.2.1, after several map_restart cycles):

```
VG_Fun: registry full (max=64); cannot register vg_fun_falldmg_dmg_15
VG_Fun: registry full (max=64); cannot register vg_fun_falldmg_dmg_25
VG_Fun: registry full (max=64); cannot register vg_fun_falldmg_dmg_50
VG_Fun: registry full (max=64); cannot register vg_fun_falldmg_gib_health
VG_Fun: registry full (max=64); cannot register vg_fun_doublejump
VG_Fun: registry full (max=64); cannot register vg_fun_doublejump_height
VG_Fun: registry full (max=64); cannot register vg_fun_doublejump_classes
VG_Fun: registry full (max=64); cannot register vg_fun_doublejump_stamina
```

### Mechanism

- `vg_Fun_Init()` is called from `G_InitGame()` at `g_main.c:1929`.
- `G_InitGame()` runs on **every map start AND every map_restart**
  (Q3 engine convention; `restart` arg differentiates but both
  paths call it).
- `s_vg_fun_registry_count` is a `static int` at file scope in
  `g_vanguard.c:690`. Statics persist across `G_InitGame()` calls
  because the .so stays loaded between maps (only a server quit
  drops the module).
- 9 cvars per call (5 falldmg + 4 doublejump) × N map_restarts =
  9N entries.
- Overflow triggers when `s_vg_fun_registry_count >= 64`.
  - 9 × 7 = 63 (still under)
  - call 8 of `vg_Fun_Init` adds `vg_fun_falldmg_dmg_10` at slot
    64, then immediately `s_vg_fun_registry_count >= 64` is true
    and `vg_fun_falldmg_dmg_15` triggers the first reject.
  - All 8 subsequent registrations in that `vg_Fun_Init` call are
    rejected.

The log shows exactly 8 rejects, matching the formula.

### Functional impact of pre-fix overflow

| Layer | Effect |
|---|---|
| Engine cvar pool | **None.** Cvars were registered on map 1 with `CVAR_SERVERINFO`; they persist for the .so lifetime. |
| `trap_Cvar_VariableStringBuffer` reads (any module) | **None.** Engine pool is intact. |
| `vg_pm_cvar_int` (used by `PM_CheckJump`) | **None.** Direct engine read; registry-independent. |
| `vg_Fun_GetInt` (used by `g_active.c` falldamage) | **None.** Direct `trap_Cvar_Register` + `trap_Cvar_Update`; registry-independent. |
| `vg_Fun_PrintStatus` (registry walk) | Inaccurate after map 8; misses the dropped entries. |
| Server log noise | 8+ lines per affected `vg_Fun_Init` call. |

Therefore: the overflow does NOT cause double-jump failure. But the
log spam is toxic for production diagnostic value, and the bug must
be fixed regardless.

### Fix

Idempotent registration. Walk the registry first; if name matches
an existing entry, re-apply `trap_Cvar_Register` (engine merges
flags + leaves live value untouched) and return without bumping
the counter. New cvars take the previous code path.

The `cvar_flags |= CVAR_SERVERINFO` assignment is moved **above**
the lookup loop so re-applied registrations get the same engine
flags as first-time registrations. (Previously the line ran only
on the new-entry path.)

Code:

```c
void vg_Fun_RegisterCvar(const char *name, const char *cup_default, int cvar_flags)
{
    vmCvar_t tmp;
    int      i;

    cvar_flags |= CVAR_SERVERINFO;

    for (i = 0; i < s_vg_fun_registry_count; i++) {
        if (Q_stricmp(s_vg_fun_registry[i].name, name) == 0) {
            s_vg_fun_registry[i].cvar_flags = cvar_flags;
            trap_Cvar_Register(&tmp, name, cup_default, cvar_flags);
            return;
        }
    }

    if (s_vg_fun_registry_count >= VG_FUN_REGISTRY_MAX) {
        G_Printf("VG_Fun: registry full (max=%d); cannot register %s\n",
                 VG_FUN_REGISTRY_MAX, name);
        return;
    }

    Q_strncpyz(...);
    ...
    trap_Cvar_Register(&tmp, name, cup_default, cvar_flags);
}
```

### Regression-safety

- First boot, first map: registry empty, loop does nothing, falls
  through to `>= MAX` check (false), adds entry. Identical to pre-fix.
- map_restart #2..N (pre-overflow): each name found on first match,
  re-applies `trap_Cvar_Register`, returns. Engine merges flags
  on re-register; live admin-set cvar values are NOT clobbered
  (engine semantics — `trap_Cvar_Register` honours existing values).
- The `cvar_flags` field on the existing registry entry is updated
  to reflect the latest as-applied flags (so `vg_Fun_PrintStatus`
  reports current state).

## Problem 2 — Second-jump runtime failure

### Evidence

Live test (post-v0.7.2.1, with `vg_fun=1`, `vg_fun_doublejump=1`,
`vg_fun_doublejump_classes=31`, `vg_fun_doublejump_height=400`,
`vg_fun_doublejump_stamina=0`, `map_restart` confirmed):

> "ingame: 2. SPACE in der Luft macht NICHTS"

### Why v0.7.2.1's CVAR_SERVERINFO change wasn't the fix

CVAR_SERVERINFO causes the engine to publish the cvar value into the
`CS_SERVERINFO` configstring transmitted to clients. The client
engine does NOT auto-create matching local cvars from CS_SERVERINFO.

cgame's `vg_pm_cvar_int` calls `trap_Cvar_VariableStringBuffer`
which reads from the **client process's local cvar pool** — which
never registered the cvar. Read returns empty string → `atoi("") = 0`
→ eligibility fails in client prediction.

This is corroborated by the existing pattern in `cg_servercmds.c:276`,
which reads `vanguard_dev` (a CVAR_SERVERINFO cvar) from cgame via:

```c
cgs.vanguardDev = Q_atoi(Info_ValueForKey(info, "vanguard_dev"));
```

That's the right path for cgame. v0.7.2.1's fix was wrong for cgame.

### Why a server-side fire would still produce visible motion

If only cgame mispredicted (no second-jump in client prediction) but
server-side authoritative pmove fired the second jump, the player
would see at least a server-corrected upward bounce (rubber-banded).

Since the player reports **"absolutely nothing"**, server-side
eligibility must also be failing somewhere. Without instrumentation
we cannot tell which gate.

### Diagnostic — server-side `PM_CheckJump` log

Single Com_Printf in `PM_CheckJump`, server-side only
(`#ifndef CGAMEDLL`), gated by `vanguard_dev=1`. Fires on every
airborne jump-press rising edge:

```c
if ((pm->cmd.upmove >= 10)
    && !(pm->ps->pm_flags & PMF_JUMP_HELD)
    && vg_pm_cvar_int("vanguard_dev", 0))
{
    Com_Printf("VG_DJ[S] press: ground=%d djUsed=%d "
               "fun=%d dj=%d cls=%d staCost=%d hgt=%d "
               "stam=%d class=%d up=%d respawn=%d "
               "delay=%dms candj=%s\n",
               ...);
}
```

#### Output fields

| Field | Meaning | Required for second-jump |
|---|---|---|
| `ground` | `groundEntityNum` | `1023` (ENTITYNUM_NONE) — airborne |
| `djUsed` | `PMF_VG_DOUBLEJUMPED` bit | `0` — second jump unused this airtime |
| `fun` | `vg_fun` cvar (engine read) | `1` — master switch on |
| `dj` | `vg_fun_doublejump` cvar | `1` — feature on |
| `cls` | `vg_fun_doublejump_classes` bitmask | `0` (any class) or `bit << class` set |
| `staCost` | `vg_fun_doublejump_stamina` (×100 internal) | n/a (gate inputs only) |
| `hgt` | `vg_fun_doublejump_height` | n/a (gate inputs only) |
| `stam` | `STAT_SPRINTTIME` (engine 0..20000) | `>= staCost*100` |
| `class` | `STAT_PLAYER_CLASS` (0..4) | n/a (input to cls check) |
| `up` | `cmd.upmove` | `>= 10` (jump press intensity) |
| `respawn` | `PMF_RESPAWNED` bit | `0` |
| `delay` | `cmd.serverTime - pmext->jumpTime` (ms) | bypassed if `candj=yes` |
| `candj` | eligibility result | `yes` for the v0.7.2 second-jump path to fire |

### Why server-side only

- cgame would print zeros for the cvar fields (CVAR_SERVERINFO
  visibility issue) — uninformative noise.
- Log volume on the server is bounded: rising-edge gate (`upmove>=10
  AND !PMF_JUMP_HELD`) fires once per actual press.
- Server logs are persisted (`logfile` cvar), client logs are not.

### Live test plan

1. wahke sets `\set vanguard_dev 1` on the server.
2. Joins, gets airborne (regular jump from floor or a ledge drop).
3. Mid-air, presses SPACE again.
4. Repeats 2-3 a handful of times across different classes.
5. Greps the server log for `VG_DJ[S] press:`.
6. Pastes the output back; v0.7.2.3 makes the targeted fix.

### v0.7.2.3 expected outcomes

| Diag pattern | Likely root cause | v0.7.2.3 fix |
|---|---|---|
| `fun=0` and/or `dj=0` even after `\set` | Server-side cvar read broken (somehow) | Replace `vg_pm_cvar_int` server side too with `vmCvar_t` + `trap_Cvar_Update` |
| `fun=1 dj=1 candj=no` and one of `ground != 1023`, `djUsed=1`, `cls` mismatch, `stam` insufficient | Eligibility logic blocks unexpectedly | Targeted gate fix |
| `fun=1 dj=1 candj=yes` but no jump fires | Logic past eligibility (PM_JUMP_DELAY, PMF_RESPAWNED, upmove, PMF_JUMP_HELD) blocks | Trace the PM_CheckJump tail; likely PMF_JUMP_HELD is set wrong |
| No log lines at all on second SPACE | Press not reaching `PM_CheckJump` (input dropped, wrong upmove) | cmd-flow audit |

After fix, the diagnostic block + `#ifndef CGAMEDLL` block + this
note get cleaned up. CI gate for the diag string also gets removed.

## Files changed (v0.7.2.2)

| File | Change |
|---|---|
| `src/game/g_vanguard.c` | `vg_Fun_RegisterCvar` idempotent (lookup loop + SERVERINFO moved up). |
| `src/game/bg_pmove.c` | Server-side diagnostic block in `PM_CheckJump`. |
| `.github/workflows/ci.yml` | 2 new gates (idempotent loop source-grep + diag binary string). |
| `docs/RELEASE_NOTES.md` | v0.7.2.2 entry above v0.7.2.1. |
| `docs/notes/PHASE_13B_DIAGNOSTIC.md` | This file (new). |

## Lessons

1. **CVAR_SERVERINFO ≠ client-side cvar pool population.** A SERVERINFO
   flag publishes the value to the CS_SERVERINFO configstring, but
   client modules must explicitly read via `trap_GetConfigstring` +
   `Info_ValueForKey`. The existing `cg_servercmds.c:276` pattern was
   in front of us the whole time; v0.7.2.1 didn't audit it.
2. **Statics in mod .so persist across G_InitGame.** The .so stays
   loaded between maps; only `G_ShutdownGame` + `G_InitGame` cycle
   runs, and statics keep their values. Any per-map idempotency must
   be coded explicitly. (Future audit candidate: are there other
   statics in `g_vanguard.c` / WolfGuard with the same shape?)
3. **Diagnostic-first beats speculative fix when symptoms don't
   match the leading hypothesis.** Two production hotfixes back-
   to-back (v0.7.2.1 + a hypothetical v0.7.2.2 guess) burns goodwill;
   one diagnostic + one targeted fix is the same calendar time but
   higher confidence.

## References

- `src/game/bg_pmove.c::PM_CheckJump` (`bg_pmove.c:822`)
- `src/game/g_vanguard.c::vg_Fun_RegisterCvar` (`g_vanguard.c:803`)
- `src/cgame/cg_servercmds.c:276` (existing CVAR_SERVERINFO read pattern)
- `docs/notes/PHASE_12_DOUBLEJUMP_RECON.md` (Phase 12 design)
- `docs/notes/PHASE_13_PRODUCTION_HOTFIX.md` (v0.7.2.1 splash + SERVERINFO)
