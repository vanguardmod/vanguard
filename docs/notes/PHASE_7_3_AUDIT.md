# Phase 7.3 — Movement Physics Recon

> Pure recon. No code changes. wahke decides scope + sequencing
> after reading this doc.

Date: 2026-04-30
Author: Claude Code (Opus 4.7) on session for wahke
Status: AUDIT (no implementation yet)

Embeds Phase 8.0b — Falldamage Redesign — as a co-recon target,
since Phase 8.0a (the v0.5.2.2 SIGSEGV hot-fix) only added a NULL
guard and the actual tuning surface is unbuilt.

---

## TL;DR

Three concrete, *low-risk* findings warrant a v0.5.3 follow-up.
Two more — falldamage redesign and pm_* cvar exposure — are
*medium-to-high* risk and should land in a separate v0.5.4 / v0.6.0.

| Finding | Status | Risk | Effort |
|---|---|---|---|
| `g_pronedelay 0` in `defaultpublic.config` is softer than ETPro orthodoxy | config-only | LOW | trivial |
| Cup profile uses `sv_fps 40` (ETLegacy legacy6) — known broken at >20 | config + audit | LOW-MED | small |
| `pm_*` constants are file-scope floats, not cvars | code expansion | MED-HIGH | medium |
| Falldamage thresholds + damages all hardcoded | code expansion | MED | medium |
| No lag-spike falldamage guard (classic ET bug, unmitigated) | new code | MED-HIGH | medium |
| Strafe-jumping already preserved by upstream `pm_airaccelerate=1` | nothing to do | n/a | n/a |

---

## 1. Code inventory — Movement

### 1.1 Pmove core (execution order)

Entry: `Pmove` (bg_pmove.c:5420) → `PmoveSingle` (bg_pmove.c:5020).

| Function | File:line | Role | Tuning knobs (file:line) |
|---|---|---|---|
| `PM_SetWaterLevel` | bg_pmove.c:1985 | waterlevel 0-3 + flags | `pm_waterSwimScale=0.5` (80), `pm_slagSwimScale=0.30` (82) |
| `PM_CheckProne` | bg_pmove.c:890 | prone FSM (entry/exit) | `g_pronedelay` cvar; PM_JUMP_DELAY=850ms (72); EXTENDEDPRONE_TIME=400ms (71); `pm_proneSpeedScale=0.21` (85) |
| `PM_CheckDuck` | bg_pmove.c:2027 | crouch toggle, viewheight | none cvar-exposed |
| `PM_GroundTrace` | bg_pmove.c:1840 | groundEntityNum + walking flag | — |
| `PM_DeadMove` | bg_pmove.c:1488 | corpse-velocity friction | — |
| `PM_WalkMove` | bg_pmove.c:1308 | ground locomotion | calls PM_Friction + PM_Accelerate |
| `PM_AirMove` | bg_pmove.c:1255 | air locomotion + strafe | `pm_airaccelerate=1` (88) |
| `PM_FlyMove` | bg_pmove.c:1207 | spectator flight | `pm_flyaccelerate=8` (91) |
| `PM_WaterMove` | bg_pmove.c:1130 | swim/wade | `pm_wateraccelerate=4` (89), `pm_slagaccelerate=2` (90) |
| `PM_LadderMove` | bg_pmove.c:4845 | ladder climb | `pm_ladderfriction=14` (97) |
| `PM_NoclipMove` | bg_pmove.c:1514 | noclip | uses `pm_friction × 1.5` |
| `PM_Friction` | bg_pmove.c:497 | velocity decay | `pm_stopspeed=100` (78), `pm_friction=6` (93), `pm_spectatorfriction=5.0` (98) |
| `PM_Accelerate` | bg_pmove.c:595 | Q2-style wish-based accel | `pm_accelerate=10` (87) |

**All `pm_*` constants are static file-scope floats** at bg_pmove.c:78-98 — NOT runtime cvars. No mod (ETPro / NoQuarter / Silent / Jaymod / ETLegacy) exposes them as cvars; they are universally compile-time values inherited from id Software's bg_pmove.c.

### 1.2 Strafe / air control

`PM_AirMove` (bg_pmove.c:1255-1306) calls `PM_Accelerate(wishdir, wishspeed, pm_airaccelerate=1)` at bg_pmove.c:1287. The accelspeed formula at bg_pmove.c:595-631:

```
addspeed   = wishspeed - DotProduct(velocity, wishdir);
accelspeed = pm_airaccelerate × frametime × wishspeed;
if (accelspeed > addspeed) accelspeed = addspeed;
velocity += accelspeed × wishdir;
```

**`pm_airaccelerate=1` and the absence of a ceiling means Q3-style strafe-jumping is preserved.** Rapid alternating ±strafe + mouse aim accumulates velocity unboundedly (within the `addspeed` clamp per frame). This matches every cup-orthodox ET mod. There is no `pm_accelmax` or velocity cap.

### 1.3 Prone state machine

`PM_CheckProne` at bg_pmove.c:890-1106. Three timing gates:

| Gate | Constant | Source | Tunable? |
|---|---|---|---|
| Entry delay (post-exit) | 750ms (default) or 1750ms (PRONEDELAY_TOGGLE) | bg_pmove.c:893-898 | via `g_pronedelay` bit 0 |
| Post-jump entry block | 850ms | bg_pmove.c:72 (`PM_JUMP_DELAY`) | via `g_pronedelay` bit 1 (PRONEDELAY_JUMP) |
| Exit animation extend | 400ms | bg_pmove.c:71 (`EXTENDEDPRONE_TIME`) | NOT tunable |

`g_pronedelay` is registered at g_cvars.c:637 — `"0"`, CVAR_ARCHIVE. It's a *bitfield*, not a duration: bit 0 = TOGGLE (1750ms), bit 1 = JUMP-block (850ms post-jump). Default `0` = 750ms gate, no jump block. ETLegacy's own `legacy6.config` ships `g_pronedelay 3` (both bits set).

### 1.4 Fixed-physics

`g_fixedphysics` (cvarTable g_cvars.c:635, default `"1"` CVAR_ARCHIVE) and `g_fixedphysicsfps` (g_cvars.c:636, default `"125"` CVAR_ARCHIVE).

Macros at bg_pmove.c:54-65 — when the engine sees `g_fixedphysics=1` it calls `Pmove` multiple times per frame at `g_fixedphysicsfps` rate (125 Hz = 8ms per tick), decoupling movement timing from renderer FPS. The actual loop is in the engine, not in cgame/qagame source.

**ETLegacy issue #1379** flags that this implementation generates *excessive* speed vs ETPro's `pmove_fixed`. Some trickjumps become trivial; some become fatal-on-land. Behaviour-impacting, but no mod has fixed it.

### 1.5 Cvar inventory — movement-relevant

| Cvar | Default | Flags | Source | Notes |
|---|---|---|---|---|
| `g_speed` | `320` | (none) | g_cvars.c:431 | base run speed u/s |
| `g_gravity` | `800` | (none) | g_cvars.c:432 | u/s² |
| `g_knockback` | `1000` | (none) | g_cvars.c:433 | damage→velocity multiplier |
| `g_pronedelay` | `0` | CVAR_ARCHIVE | g_cvars.c:637 | bitfield: 1=TOGGLE 1750ms, 2=JUMP-block 850ms |
| `g_fixedphysics` | `1` | CVAR_ARCHIVE | g_cvars.c:635 | 0/1, fixed timestep |
| `g_fixedphysicsfps` | `125` | CVAR_ARCHIVE | g_cvars.c:636 | tick rate Hz |

Not exposed: `pm_*` (all hardcoded), `EXTENDEDPRONE_TIME`, `PM_JUMP_DELAY`, `pm_proneSpeedScale`.

### 1.6 pmoveExt_t fields (movement-relevant)

`pmoveExt_s` at bg_public.h:567-611:

- `jumpTime` (571) — last-jump timestamp; gates re-jump and post-jump prone
- `proneTime` (580) — signed: positive=enter-prone, negative=exit-prone
- `extendProneTime` (581) — exit-anim duration (400ms)
- `proneLegsOffset` (583) — legs bbox offset for prone collision
- `airTime` (608) — frame count in air; suppresses scoped-rifle unscoping on brief airtime
- `sprintTime` (574) — stamina mirror of `STAT_SPRINTTIME`

### 1.7 Vanguard touchpoints in movement code

**Zero direct edits in `bg_pmove.c` or `bg_local.h`.** The Phase 7.2 netcode profile (`vg_Netcode_Init` at g_vanguard.h:221) tunes `sv_fps`, antilag, antiwarp from outside pmove. No `vg_*` symbols in pmove core. This is good — every Vanguard movement change so far has been *config-driven*, not source-edits.

---

## 2. Code inventory — Falldamage

### 2.1 Velocity → damage chain

```
PM_CrashLand            (bg_pmove.c:1594-1742)
   ↓ delta = velocity² × 0.0001
   ↓ water/SURF_NODAMAGE adjustments
   ↓ PM_AddEventExt(EV_FALL_*)
ClientEvents            (g_active.c:1024-1050)
   ↓ switch (event)
G_FallDamage            (g_active.c:972-1015)
   ↓ event → damage value
G_Damage(targ, NULL, NULL, NULL, NULL, dmg, 0, MOD_FALLING)
                        (g_active.c:1014)
   ↓ Phase 8.0a entry gate
   ↓ attacker=NULL → multi-region branch SKIPPED
   ↓ legacy chain handles damage
meansOfDeath table       (bg_misc.c:289)
   {MOD_FALLING, WP_NONE, ..., 0.f multiplier, ...}
```

### 2.2 EV_FALL_* events + thresholds

`EV_FALL_*` enum at bg_public.h:1408-1415. Only 6 of the 8 are actually raised:

| Event | bg_public.h | delta > | ≈ velocity (u/s) | Damage | Knockback |
|---|---|---|---|---|---|
| `EV_FALL_SHORT` | 1408 | 7 | -265 | 0 (sound only) | none |
| `EV_FALL_DMG_10` | 1415 | 38.75 | -623 | 10 | 1000ms PMF_TIME_KNOCKBACK |
| `EV_FALL_DMG_15` | 1414 | 48 | -693 | 15 | 1000ms |
| `EV_FALL_DMG_25` | 1413 | 58 | -762 | 25 | 250ms |
| `EV_FALL_DMG_50` | 1412 | 67 | -820 | 50 | 1000ms |
| `EV_FALL_NDIE` | 1411 | 77 | -877 | `GIB_DAMAGE(health) = h+176` | — |
| `EV_FALL_MEDIUM` | 1409 | (unused) | — | — | — |
| `EV_FALL_FAR` | 1410 | (unused) | — | — | — |

Raise sites in PM_CrashLand: bg_pmove.c:1676, 1685, 1698, 1711, 1724, 1732. Damage assignment: g_active.c:993-1007.

### 2.3 Gib threshold

- `GIB_HEALTH = -175` (#define at bg_public.h:72)
- `GIB_DAMAGE(h) = h - GIB_HEALTH + 1 = h + 176` (bg_public.h:73)

NDIE always gibs (damage forced ≥ 176 over current health). **Hardcoded #define, not cvar-tunable.**

### 2.4 Phase 8.0a entry-gate (verified)

g_combat.c:1781-1784 (the v0.5.2.2 hot-fix):

```c
if (vg_Hitbox_IsActive() && targ->client && targ->health > 0
    && attacker && attacker->client
    && point
    && !vg_Hitbox_IsSelfDamageMod(mod))
{
    /* multi-region branch */
}
```

`vg_Hitbox_IsSelfDamageMod` at g_vanguard.c:385-420 covers 11 MODs:
`MOD_WATER`, `MOD_SLIME`, `MOD_LAVA`, `MOD_CRUSH`, `MOD_TELEFRAG`,
**`MOD_FALLING`**, `MOD_SUICIDE`, `MOD_TRIGGER_HURT`,
`MOD_CRUSH_CONSTRUCTION`, `MOD_CRUSH_CONSTRUCTIONDEATH`,
`MOD_CRUSH_CONSTRUCTIONDEATH_NOATTACKER`.

The dynamic `die -1` test today (8 kills, 0 SIGSEGV) confirmed the
gate works for `MOD_UNKNOWN`; the same NULL-pattern applies to
`MOD_FALLING`. **No change needed to 8.0a infrastructure.**

### 2.5 Lag-spike protection

**None present.** PM_CrashLand reads `pml.previous_velocity[2]`
without sanity clamp. A net-hitch resync producing `velocity[2]=-1000`
delivers instant `EV_FALL_NDIE`. Water reduction (0.25× / 0.5× /
0×) and SURF_NODAMAGE flag are the only guards. Comment at
bg_pmove.c:1660-1663 acknowledges prediction-correctness but does
not address lag-induced spikes.

### 2.6 Class / armor modifiers

**None.** `G_FallDamage(ent, event)` takes no class param.
No lookup on `STAT_PLAYER_CLASS` or armor. ETLegacy + every
listed cup mod treats all classes identically for falldamage.
Cup-orthodox: do *not* add class scaling.

### 2.7 Cvar surface

**Zero falldamage cvars exist** in any mod. Only `g_gravity`
(default 800) indirectly affects PM_CrashLand's kinematic solve.
Every threshold, multiplier, and the gib limit is a #define or
file-scope literal.

### 2.8 Notable comments

- bg_pmove.c:1660-1663 — *velocity must be cleared in pmove, not g_active, otherwise prediction breaks* (design constraint for any redesign)
- bg_pmove.c:1639 — *reduce falling damage if there is standing water* (intentional, preserve this in any redesign)
- bg_pmove.c:1656 — *SURF_NODAMAGE for bounce pads* (preserve)

---

## 3. Cross-mod comparison — what's "cup-orthodox"

### 3.1 Strafe-jumping / air-control

| Mod | Q3-style preserved? | `pm_airaccelerate` | Toggle Q3↔CPMA? | Antibhop? |
|---|---|---|---|---|
| ETPro | ✓ (de-facto reference) | hardcoded 1 | no | no |
| NoQuarter | ✓ | hardcoded 1 | no | no |
| Silent | ✓ | hardcoded 1 | no | unconfirmed |
| Jaymod | ✓ | hardcoded 1 | no | unconfirmed |
| ETLegacy | ✓ | hardcoded 1 (bg_pmove.c:88) | no | no |

**Cup-orthodox: leave it alone.** No cup mod has ever exposed a
Q3↔CPMA selector or capped air-accel. VanguardMod is already
correct here.

### 3.2 Prone-delay

| Mod | Cvar | Cup default | Semantics |
|---|---|---|---|
| ETPro | `b_pronedelay` | `1` (Crossfire/EuroCup global1+global6) | 1 = max-spread 1s after prone, 1750ms unprone lock |
| NoQuarter | none dedicated | n/a | `g_realism` bit 1 affects animation timing only |
| Silent | `g_proneDelay` | unconfirmed | ETPro-style ms gate |
| Jaymod | `g_proneDelay` | unconfirmed | ETPro-port |
| **ETLegacy** | **`g_pronedelay`** | **`0` in defaultpublic.config; `3` in legacy6.config (cup ruleset)** | bitfield: 1=TOGGLE 1750ms, 2=JUMP-block 850ms |
| **VanguardMod** | inherits ETLegacy | **`0` in defaultpublic.config** | matches ETLegacy *public* default; *softer* than cup orthodoxy |

**Cup-orthodox: `b_pronedelay 1` (ETPro Crossfire/EuroCup).** ETLegacy's
numeric mapping is `g_pronedelay 1` = TOGGLE bit, which yields
1750ms instead of 750ms — semantically closest to ETPro's
"unprone lock". `legacy6.config` uses `3` (both bits) for stricter
cup play. **VanguardMod's `0` is the only listed value softer than
the cup standard.**

### 3.3 Fixed-physics + tickrate

| Mod | `b/g_fixedphysics` | `b/g_fixedphysicsfps` | `pmove_fixed` | `pmove_msec` | `sv_fps` cup default |
|---|---|---|---|---|---|
| ETPro Crossfire | `1` | `125` | `0` | unset (8) | unset (engine 20) |
| Jaymod | `1` (default 125) | `125` | `0` | 8 | 20 |
| ETLegacy `legacy6.config` | `1` | `125` | `0` | `8` | **`40`** |
| **VanguardMod cup profile** | `1` | `125` | `0` | `8` | **`40`** (per Phase 7.2 memory) |
| **VanguardMod public profile** | `1` | `125` | inherits | inherits | `20` |

**Cup-orthodox: `sv_fps 20`** (ETPro Crossfire / EuroCup). VanguardMod's
cup profile follows ETLegacy `legacy6.config`'s `sv_fps 40` — which
**ETLegacy issue #1637** flags as breaking cv-ops disguise theft,
flamer range, script_movers, pause timer (mechanics hardcoded against
the 50ms tick). **This is a divergence wahke should explicitly
decide on**: match ETPro orthodoxy (sv_fps 20, lower latency-feel
but standard) or follow ETLegacy's stance (sv_fps 40 with known
broken mechanics).

### 3.4 Falldamage

| Mod | Velocity thresholds | Cup-disabled? | Class modifier | Lag-spike guard |
|---|---|---|---|---|
| ETPro | engine defaults (38.75/48/58/67/77) | no | no | no |
| NoQuarter | engine defaults | unconfirmed | no | no |
| Silent | engine defaults | unconfirmed | no | unconfirmed |
| Jaymod | engine defaults | unconfirmed | no | unconfirmed |
| ETLegacy | engine defaults | no (legacy6 doesn't disable) | no | no |
| **VanguardMod** | inherits engine | no | no | no |

**Cup-orthodox: falldamage on, base thresholds untouched, no class
modifier.** VanguardMod is already cup-aligned. Phase 8.0b's
ambition (cup vs public profiles, class modifiers, gib prevention,
lag-spike guard) goes *beyond* cup orthodoxy. wahke decision:
implement profile cvars *anyway* because public servers benefit,
or stay engine-default and skip 8.0b entirely?

### 3.5 ETLegacy-specific issues to track

- **#1379** "g_fixedphysics generates excessive speed" — VanguardMod
  inherits this. If cup-feedback says "feels off" vs ETPro, this is
  the suspect.
- **#1637** "sv_fps>20 breaks cv-ops disguise / flamer / script_movers /
  pause" — directly impacts the v0.5.0 Phase 7.2 cup profile choice.

---

## 4. Risk map per sub-topic

| Sub-topic | Risk | Why |
|---|---|---|
| Strafe-jumping (no-op) | NONE | already preserved by upstream |
| `g_pronedelay` config bump 0→1 | LOW | cvar-only change, no recompile, well-understood gate |
| `sv_fps 40 vs 20` cup-profile audit | LOW (audit) → MED (decision) | requires call: orthodoxy vs ETLegacy stance; mechanics review for #1637 |
| Falldamage cvar-exposure (thresholds + dmg values, defaults unchanged) | MED | wide cvar surface (~12 new cvars), but defaults preserve current behaviour; needs CVAR_LATCH on values that affect prediction (PM_CrashLand) |
| Falldamage profile system (`vanguard_falldamage_profile cup/public`) | MED | analogous to `vanguard_netcode_profile` from Phase 7.2; well-trodden pattern |
| Class-based falldamage modifier | MED-HIGH | not cup-orthodox; cup community will object; only do if public-profile feedback demands |
| Lag-spike z-velocity clamp | MED-HIGH | new code in PM_CrashLand; prediction-correctness comment at :1660-1663 warns about exactly this region; needs careful integration test |
| `pm_*` cvar exposure (airaccelerate / accelerate / friction / stopspeed) | HIGH | touches every player frame; muscle-memory hazard for cup players; no precedent in any cup mod |
| Strafe-cap / antibhop | DO-NOT | actively against cup orthodoxy |

---

## 5. Implementation-order recommendation

### v0.5.3 — small, sauber (config-only)

1. **Decide & set `g_pronedelay`** in `defaultpublic.config` and any
   cup config. wahke choice: `1` (ETPro orthodox) or `3` (ETLegacy
   cup ruleset — stricter). My recommendation: `1` — match ETPro
   orthodoxy; cup players already know the timing.
2. **Decide & document `sv_fps`** in cup profile. Three options:
   - (a) Stay at 40 (current), document #1637 caveat in `docs/CUP_VS_PUBLIC.md`.
   - (b) Drop to 20 (orthodox), document the change in release notes.
   - (c) Add a 3rd profile `cup-strict` (sv_fps 20) alongside `cup` (sv_fps 40).
3. Add a small **diagnostic skeleton** `vanguard_diag_movement` cvar
   following the Phase 7.0 `VG_DIAG_DUMP` pattern from Memory #19's
   "diagnostic infrastructure must accompany change" lesson. No code
   yet — just the cvar, register, log a stub. Foundation for v0.5.4+.
4. Ship as a config-tightening + doc release. No new behaviour.

### v0.5.4 — Phase 8.0b foundation (medium)

5. Add `vanguard_falldamage_profile` cvar (values: `etlegacy` /
   `cup` / `public` / `custom`), default `etlegacy` so existing
   behaviour preserved.
6. Cvar-expose the 6 thresholds + 5 damage values + GIB_HEALTH.
   Profile flips them, nothing else. CVAR_LATCH.
7. Add CI sanity gate for the new cvars (Memory #19 lesson 2).
8. **Live-test plan**: drop tests at known heights on `oasis` /
   `radar` for each profile; record HP loss; regression-test
   v0.5.2.2 `die -1` still no-crash.

### v0.6.0 — Movement Physics Foundation (large)

9. **Optional:** lag-spike z-velocity clamp in PM_CrashLand,
   gated by `vanguard_falldamage_lagspike_clamp` cvar (off by default).
   Needs network-hitch integration test (netem, possibly).
10. **Optional:** prone timing fine-tuning cvars
    (`vanguard_prone_extend_time_ms`, `vanguard_prone_jumplock_ms`)
    if cup feedback demands.
11. **Defer indefinitely:** `pm_airaccelerate` / `pm_accelerate` /
    `pm_friction` cvar exposure. Only revisit if cup community
    explicitly asks.

### Defer to separate phases (per user note)

- Phase 9 `vg_fun` mode bundle — *after* 7.x cup-foundation.
- Phase 7.4 UI redesign — after 7.3.
- Phase 7.1 hit-region sounds — last.
- Phase 7.0.x capsule tuning — patch on top of v0.5.2.3 if cup
  feedback flags the HEAD r=7 / NECK / ARM choices.

---

## 6. Test-plan sketches per sub-topic

### Prone-delay
- **Setup:** `g_pronedelay` test matrix `0,1,2,3` on `oasis` w/ recording.
- **Procedure:** stopwatch (or scripted timestamp diff) from
  +prone keypress → `EF_PRONE` set → first-fire allowed.
- **Expected:** matches the 750/1750ms entry gate × 850ms post-jump
  block table at bg_pmove.c:893-898.
- **Pass:** all 4 values produce predicted timings ±50ms.

### sv_fps (issue #1637 audit)
- **Setup:** cup profile, sv_fps 40 vs 20.
- **Procedure:** test cv-ops disguise theft, flamer max range,
  any script_mover map, pause timer at sv_fps 40 vs 20.
- **Expected:** at 40 some break per #1637; at 20 all work.
- **Pass:** decision gate, not an automatable test.

### Falldamage cvar-exposure (defaults preserved)
- **Setup:** `vanguard_falldamage_profile etlegacy` (default).
- **Procedure:** drop from precise heights on `oasis` (known fall
  spots: balcony to flag = ~580 u/s impact, ramp = ~700, tower roof = ~840).
- **Expected:** HP loss matches engine table (10/15/25/50/gib).
- **Pass:** zero deviation from upstream behaviour at default profile.

### Lag-spike clamp (if implemented)
- **Setup:** `tc qdisc` netem rule injecting 500ms hitch via WSL2.
- **Procedure:** walk-jump-walk pattern; measure HP loss with/without
  clamp.
- **Expected:** without clamp, occasional unexpected damage during
  hitches; with clamp, no damage spike.
- **Pass:** hitch-induced damage events drop to zero.

### Strafe-jumping regression (any movement work)
- **Setup:** speedometer overlay (cgame), record max velocity on
  `radar` long-strafe run (well-known route).
- **Procedure:** run pre/post any movement-touching change, compare
  max velocity histogram (10 trials each).
- **Expected:** identical to within ±5% (frame-quantization noise).
- **Pass:** no statistically significant velocity shift.

---

## 7. Lessons-learned application (from Phase 7.0 Memory #19)

1. **Diagnostic infrastructure** — every Phase 7.3 sub-topic that
   touches PM_* needs a `vanguard_diag_movement` flag analogous to
   `vanguard_diag_dump`. v0.5.3 should ship the flag (even no-op)
   so v0.5.4+ can build on it.
2. **Build-flag mismatches** — falldamage cvar-exposure adds 12+
   new symbols. CI symbol-gate (analogous to today's
   `vg_Hitbox_IsSelfDamageMod` / `CG_VanguardDev_DrawHitboxes`
   checks) must be extended in v0.5.4.
3. **Crash-bugs vs tuning-bugs** — Phase 8.0a was crash, Phase
   8.0b is tuning. **Keep them in separate releases.** v0.5.2.2
   was 8.0a; v0.5.4 will be 8.0b. Never mix.

---

## 8. Open decisions for wahke

1. **`g_pronedelay`** — set to `1` (ETPro orthodox) or `3` (ETLegacy
   strict)? Or audit cup-tester feedback first?
2. **`sv_fps`** — keep cup at 40 (current, follows ETLegacy legacy6,
   has #1637 caveats), drop to 20 (ETPro orthodox), or split into
   two cup profiles?
3. **Phase 8.0b scope** — full redesign (12+ cvars, profiles,
   lag-spike clamp, possibly class modifiers), or minimal
   (just `vanguard_falldamage_profile etlegacy/cup/public` flag,
   profile only flips a single multiplier)?
4. **v0.5.3 scope** — config-only (items 1-3 in §5 above) or include
   the diagnostic skeleton already?
5. **Class modifiers** — out of scope (cup-orthodox), or in scope
   for the public profile only (helmet/armour-style)?

---

## 9. References

- Code: `bg_pmove.c`, `bg_local.h`, `g_active.c`, `g_combat.c`,
  `g_cvars.c`, `g_vanguard.c`, `bg_public.h`, `bg_misc.c`.
- Configs: `etmain/configs/defaultpublic.config`,
  `etmain/configs/legacy6.config`.
- Cross-mod: ETPro Crossfire/EuroCup configs (msh100/ETPro-configs),
  Jaymod cvar XML tree, silEnT 0.8.1 changelog, NoQuarter wiki.
- Issues: ETLegacy #1379 (fixedphysics speed), #1637 (sv_fps>20).
- Vanguard prior phases: `PHASE_7_0_AUDIT.md`, `PHASE_7_0_2_AUDIT.md`,
  `PHASE_7_2_AUDIT.md`, `RELEASE_NOTES.md` v0.5.2.x.
