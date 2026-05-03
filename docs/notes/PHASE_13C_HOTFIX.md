# Phase 13c — CS_SERVERINFO overflow + airborne diag (v0.7.2.3)

## TL;DR

Two changes:

1. **Fix** — drop the `cvar_flags |= CVAR_SERVERINFO` OR-in from
   `vg_Fun_RegisterCvar`. v0.7.2.1's addition was based on a wrong
   theory of how `CVAR_SERVERINFO` propagates to clients, and in
   production it caused `Info_SetValueForKey: Info string length
   exceeded` warnings via `CS_SERVERINFO` buffer overflow.
2. **Diagnostic** — add a second diag block (`VG_DJ[A]`) in
   `PM_CheckJump` that fires when the player is airborne and
   pressing SPACE, regardless of `PMF_JUMP_HELD`. v0.7.2.2's
   `VG_DJ[S]` block was correctly suppressing on `PMF_JUMP_HELD`
   to avoid spam, but as a side-effect it never captured a true
   mid-air press in production testing.

No production behaviour change at `vanguard_dev=0`. Cup-orthodox
preserved; Phase 8.0a NULL-guard untouched; v0.7.2.1 splash bypass
+ v0.7.2.2 idempotent register untouched.

## Problem 1 — `CS_SERVERINFO` buffer overflow

### Evidence

wahke's server log post-v0.7.2.2:

```
Info_SetValueForKey: Info string length exceeded
```

(repeated)

### Mechanism

`CVAR_SERVERINFO` cvars are pushed into the `CS_SERVERINFO`
configstring (key/value pairs separated by `\`) which is
transmitted to clients. The configstring is bounded by
`MAX_INFO_STRING = 1024` bytes. v0.7.2.1's OR-in caused 9
additional `vg_fun_*` cvars to enter the configstring:

| Cvar | Approx bytes |
|---|---|
| `\vg_fun_falldmg_dmg_10\10` | 24 |
| `\vg_fun_falldmg_dmg_15\15` | 24 |
| `\vg_fun_falldmg_dmg_25\25` | 24 |
| `\vg_fun_falldmg_dmg_50\50` | 24 |
| `\vg_fun_falldmg_gib_health\-175` | 32 |
| `\vg_fun_doublejump\0` | 19 |
| `\vg_fun_doublejump_height\270` | 30 |
| `\vg_fun_doublejump_classes\0` | 28 |
| `\vg_fun_doublejump_stamina\0` | 28 |

≈ 233 bytes added to `CS_SERVERINFO` purely from `vg_fun_*`.
Combined with the existing baseline (server name, hostname, map
name, sv_maxclients, fs_game, gamename, gamedate, protocol,
gamestate, all the timelimit / fraglimit / mode flags, plus
`vanguard_dev`, `vanguard_hitbox_mode`, `g_netcode_profile`,
`vg_fun`), the configstring routinely crosses 1024.

### Why the v0.7.2.1 OR-in was wrong in the first place

The v0.7.2.1 hotfix added `CVAR_SERVERINFO` based on this
theory: cgame's `vg_pm_cvar_int` (which calls
`trap_Cvar_VariableStringBuffer`) was reading empty for
`vg_fun_*` cvars; `CVAR_SERVERINFO` would propagate the values
to clients and fix the read.

In Q3-derived engines (ETLegacy included), `CVAR_SERVERINFO` does
NOT auto-create matching cvars in the client's local cvar pool.
It only populates the `CS_SERVERINFO` configstring. The correct
cgame-side read pattern is:

```c
trap_GetConfigstring(CS_SERVERINFO, info, sizeof info);
value = Info_ValueForKey(info, "varname");
```

This pattern is already in the codebase at `cg_servercmds.c:276`
for `vanguard_dev`:

```c
cgs.vanguardDev = Q_atoi(Info_ValueForKey(info, "vanguard_dev"));
```

v0.7.2.1's OR-in did not change cgame's read path, so it had no
effect on the client side. Server-side reads continued to work
(qagame reads its own engine cvar pool directly, regardless of
`CVAR_SERVERINFO`). Net result: zero functional benefit, plus the
configstring overflow.

### Fix

Remove the `cvar_flags |= CVAR_SERVERINFO;` assignment line from
`vg_Fun_RegisterCvar`. The v0.7.2.2 idempotent-lookup loop is
preserved. The 9 `vg_fun_*` cvars get registered with whatever
flags the caller passes (typically `CVAR_ARCHIVE` only), no
SERVERINFO.

`vg_fun` master switch keeps `CVAR_SERVERINFO` (registered
separately via `gameCvarTable` in `g_cvars.c`):

```c
{ &vg_fun, "vg_fun", "0",
  CVAR_LATCH | CVAR_ARCHIVE | CVAR_SERVERINFO, 0, qfalse, qfalse },
```

This is necessary for UI mode-indication (single cvar, no overflow
risk).

### Regression-safety

| Aspect | Before (v0.7.2.1+v0.7.2.2) | After (v0.7.2.3) |
|---|---|---|
| Server-side `vg_pm_cvar_int` reads | works (engine pool direct) | works (engine pool direct) |
| Server-side `vg_Fun_GetInt` reads | works | works |
| cgame `vg_pm_cvar_int` reads | empty string (CVAR_SERVERINFO didn't help) | empty string (unchanged) |
| Server-side double-jump pmove fire | should fire authoritatively | unchanged |
| Client prediction parity | mismatch (cgame returns 0) | mismatch (unchanged — same as v0.7.2 baseline) |
| `CS_SERVERINFO` size | overflow with 9 extra entries | normal |
| `Info_SetValueForKey` warnings | yes | none expected |

Cgame visibility is a separate (pre-existing) issue. The actual
fix for cgame double-jump prediction parity will swap the cgame
read path to `Info_ValueForKey(CS_SERVERINFO, ...)` in a later
release, scheduled after we have airborne diagnostic data on the
server-side eligibility behaviour.

## Problem 2 — `VG_DJ[S]` diag never captured airborne presses

### Evidence

wahke's v0.7.2.2 live-test data:

> 100% der server-side VG_DJ[S] press lines zeigen candj=no, einziger
> failing Gate ist DELAY:
>   delay=33686ms (1. press, völlig out-of-window)
>   delay=76355ms (2. respawn, sogar mehr)
>   delay=990ms, 1309ms, 1652ms, 887ms, 832ms, 939ms (alle > 850ms threshold)
> Andere Gates alle ok: ground=1022 (ENTITYNUM_NONE = airborne ✓), djUsed=0,
> fun=1, dj=1, cls=31, class=1 in mask, up=127, respawn=0.

### Critical clarification

The user's interpretation `ground=1022 (ENTITYNUM_NONE = airborne ✓)`
was wrong:

```
src/qcommon/q_shared.h:1239: #define GENTITYNUM_BITS     10
src/qcommon/q_shared.h:1240: #define MAX_GENTITIES       (1 << GENTITYNUM_BITS) // = 1024
src/qcommon/q_shared.h:1245: #define ENTITYNUM_NONE      (MAX_GENTITIES - 1)    // = 1023
src/qcommon/q_shared.h:1246: #define ENTITYNUM_WORLD     (MAX_GENTITIES - 2)    // = 1022
```

`ground=1022` is `ENTITYNUM_WORLD` — player on map geometry, NOT
airborne. The eligibility check
`pm->ps->groundEntityNum == ENTITYNUM_NONE` correctly returned
false (1022 != 1023), which is why `candj=no` for every line.

### Why the diag never captured airborne presses

`VG_DJ[S]` gate: `upmove >= 10 && !PMF_JUMP_HELD`.

After a successful first jump from the ground:
- `PM_CheckJump` sets `PMF_JUMP_HELD` (`bg_pmove.c:984`).
- Player keeps SPACE held through the jump motion (instinctive).
- Diag's `!PMF_JUMP_HELD` clause is now false → diag suppressed.
- For the diag to fire on a mid-air second press, the player must
  release SPACE between presses — and the gap must be >0 ticks
  (`upmove<10` triggers the `PMF_JUMP_HELD` clear at
  `bg_pmove.c:5318`).

Most testers don't naturally do this. The user's testing produced
only ground-press observations.

### What the delay numbers actually represent

The post-`PM_CheckJump` block at `bg_pmove.c:1519-1530` updates
`pm->pmext->jumpTime = pm->cmd.serverTime` only when
`serverTime - jumpTime >= 850` (cup-orthodox bunnyhop sprint-drain
gate). For ground-level press chains:

- First press of a session: delay = serverTime - 0 (or - last
  session's `pmext->jumpTime`) → typically large (33686ms /
  76355ms in the data).
- Subsequent presses: delay = time-since-previous-press, capped
  at 850 by the post-update logic. The 990 / 1309 / 1652 /
  887 / 832 / 939 numbers are the natural rhythm of
  press-release-press at human reaction times >850ms.

`pmext->jumpTime` IS being updated correctly. The user's hypothesis
that it was never being set was based on misreading 1022 as airborne.

### Fix — `VG_DJ[A]` block

Add a second diag block in `PM_CheckJump`, alongside (not
replacing) the v0.7.2.2 `VG_DJ[S]` block:

```c
#ifndef CGAMEDLL
{
    static int s_vg_dj_air_last_fire = 0;

    if ((pm->ps->groundEntityNum == ENTITYNUM_NONE)
        && (pm->cmd.upmove >= 10)
        && (pm->cmd.serverTime - s_vg_dj_air_last_fire >= 100)
        && vg_pm_cvar_int("vanguard_dev", 0))
    {
        s_vg_dj_air_last_fire = pm->cmd.serverTime;
        Com_Printf("VG_DJ[A] air:   ... held=%d candj=%s\n", ...);
    }
}
#endif
```

Differences from `VG_DJ[S]`:

| Property | `VG_DJ[S]` | `VG_DJ[A]` |
|---|---|---|
| Trigger | rising edge press (`upmove>=10 && !PMF_JUMP_HELD`) | airborne press (`groundEntityNum==1023 && upmove>=10`) |
| `PMF_JUMP_HELD` requirement | must be clear | don't care (logged as `held=`) |
| Throttle | none (rising edge limits naturally) | 1 per 100ms (server-static last-fire) |
| `held=` field | absent | present |
| Field set | otherwise identical | otherwise identical |
| Use case | observe ground-state at every press | observe true airborne press attempts |
| Prefix | `VG_DJ[S] press:` | `VG_DJ[A] air:  ` |

Throttle scope: server-static (one timestamp shared across all
clients on the server). For the solo-tester scenario this is
acceptable; if multi-tester debug is ever needed, re-shape to a
per-client-indexed array.

### Why retain `VG_DJ[S]`

- Useful for ground-state observations (cup-orthodox baseline).
- Live-test grep can correlate ground vs airborne behaviour
  side-by-side.
- Removing it would break v0.7.2.2's CI gate and require gate
  rewording — wasted churn given we still want both observations.

## Files changed (v0.7.2.3)

| File | Change |
|---|---|
| `src/game/g_vanguard.c` | Replace v0.7.2.1 OR-in code+comment with Phase 13c removal note. Idempotent loop retained. |
| `src/game/bg_pmove.c` | New `VG_DJ[A]` diag block alongside the v0.7.2.2 `VG_DJ[S]` block. Existing block's comment updated to point at `VG_DJ[A]` for airborne data. |
| `.github/workflows/ci.yml` | Inverted SERVERINFO gate (must be absent). Extended diag-string gate (≥2: both `[S]` and `[A]`). |
| `docs/RELEASE_NOTES.md` | v0.7.2.3 entry above v0.7.2.2. |
| `docs/notes/PHASE_13C_HOTFIX.md` | This file (new). |

## Live-test plan

1. Set `\set vanguard_dev 1`, `\set vg_fun 1`, `\set vg_fun_doublejump 1`,
   `\set vg_fun_doublejump_classes 31`, `\set vg_fun_doublejump_height 400`,
   then `\map_restart`.
2. **Test A** (deliberate key-release): jump from ground, release
   SPACE, press again mid-air within ~600ms. Repeat across classes.
3. **Test B** (natural play): just play the game and try to
   double-jump.
4. Grep server log for `VG_DJ\[(S\|A\)]` to see ground vs airborne
   observations.
5. Run a few `map_restart` cycles. Grep for
   `Info_SetValueForKey: Info string length exceeded` —
   expected 0 (or significantly fewer) hits versus v0.7.2.2.

## v0.7.2.4 decision tree (based on `VG_DJ[A]` data)

| Pattern | Diagnosis | Fix |
|---|---|---|
| `VG_DJ[A]` lines appear with `candj=yes` and player feels jump | Issue resolved by Phase 13b idempotent register or some upstream side-effect; just remove diag | Diag cleanup only |
| `VG_DJ[A]` lines with `ground=1023 candj=no` and one of the expected gates failing | Targeted gate fix | Gate-specific |
| `VG_DJ[A]` lines with `candj=yes` but player still feels nothing | `PMF_JUMP_HELD` blocks the actual fire path despite passing the rising-edge gate elsewhere; or velocity gets re-zeroed downstream | Trace post-`PM_CheckJump` flow |
| No `VG_DJ[A]` lines at all even on confirmed airborne presses | Player input not reaching `PM_CheckJump` (cmd.upmove dropped, or pmove not invoked) | cmd-flow audit |

## Lessons

1. **`CVAR_SERVERINFO` is not a client cvar mirror.** It populates
   `CS_SERVERINFO` only. Reading on the client side requires
   `Info_ValueForKey`. v0.7.2.1 should have audited
   `cg_servercmds.c:276` (the existing pattern) before adding the
   OR-in. We did not, and shipped the wrong fix.
2. **`MAX_INFO_STRING = 1024` is small.** Adding many cvars to
   `CS_SERVERINFO` is a real risk. Future Vanguard cvars should
   default to NO `CVAR_SERVERINFO` unless there's a specific
   client read pattern that needs it.
3. **Diagnostic gates need scenario consideration.** `!PMF_JUMP_HELD`
   is a sensible rate-limit for ground-state observation but
   actively suppresses the airborne-press case we care about.
   Always think about what state the user is in when the diag is
   meant to fire, not just when it should be quiet.
4. **`ENTITYNUM_WORLD = 1022, ENTITYNUM_NONE = 1023`.** Off-by-one
   in the user's parse caused a wrong root-cause hypothesis. The
   field comment in the v0.7.2.2 diag block now spells out both
   values explicitly.

## References

- `src/game/bg_pmove.c::PM_CheckJump` (`bg_pmove.c:822`)
- `src/game/g_vanguard.c::vg_Fun_RegisterCvar` (`g_vanguard.c:803`)
- `src/cgame/cg_servercmds.c:276` (existing CVAR_SERVERINFO read pattern)
- `src/qcommon/q_shared.h:1239-1246` (ENTITYNUM_NONE / ENTITYNUM_WORLD)
- `docs/notes/PHASE_13_PRODUCTION_HOTFIX.md` (v0.7.2.1 splash + SERVERINFO)
- `docs/notes/PHASE_13B_DIAGNOSTIC.md` (v0.7.2.2 idempotent register + diag)
