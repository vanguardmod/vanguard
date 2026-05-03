# Phase 12 — Double-Jump Recon

> Pure recon. No code changes. wahke decides scope after this.

Date: 2026-05-03
Author: Claude Code (Opus 4.7) on session for wahke
Status: AUDIT (no implementation)

References Phase 7.3 (movement physics scope — prone-delay shipped
v0.6.1; jump was out of scope), Memory #8 (vg_fun mode bundle),
v0.7.1 Falldamage (vg_fun helper API validation pattern), v0.7.0
WolfGuard foundation (function-pointer dispatch, no per-frame
velocity hooks today), Phase 6 multi-region hitbox (unaffected —
movement layer is independent).

## TL;DR

Double-jump is a **clean, contained addition** — `PM_CheckJump` at
`bg_pmove.c:788-839` is a self-contained 50-line function with
clear early-returns. State storage fits in **one free bit of
`pm_flags`** (bit 7 = 128, currently unused). WolfGuard imposes
**zero constraints** — current `wg_interface_t` has no per-frame
velocity-check hook (only init/shutdown/client_connect/disconnect/frame).
Cup-orthodox preserved by default `vg_fun=0` master gate.

| Decision | Recommendation |
|---|---|
| Hook-point | **Option A** (modify PM_CheckJump in-place) — cleanest, smallest diff |
| State storage | **`PMF_VG_DOUBLEJUMPED = 128`** in `pm_flags` (bit 7, free) |
| Reset point | At top of `PM_CheckJump` when on ground OR at `PM_GroundTrace` when landing detected |
| Jump velocity | **Cvar-tunable** `vg_fun_doublejump_height`, default `JUMP_VELOCITY` (270) |
| WolfGuard whitelist | **Not needed** — no velocity-check hook in current API |
| Class restrictions | **Bitmask cvar**, default 0 = all classes |
| Stamina cost | **Optional cvar**, default 0 = no cost |
| Velocity direction | **Pure vertical add** — preserve cup-style determinism |
| v0.7.2 scope | Tier 1 only — master + height + classes + stamina cvars |

Effort estimate: **~150-200 LoC** code + ~80 LoC docs. Risk: LOW.

## §1 — PM_CheckJump location + flow

`src/game/bg_pmove.c:788-839` — 50 lines, easy to read, easy to
extend:

```c
static qboolean PM_CheckJump(void)
{
    // 1. Prone block (line 791): EF_PRONE returns false
    // 2. Jump-delay block (line 800): 850ms PM_JUMP_DELAY since last jump
    // 3. Respawned block (line 810): PMF_RESPAWNED returns false
    // 4. Press check (line 815): cmd.upmove < 10 returns false
    // 5. Held check (line 822): PMF_JUMP_HELD returns false (anti-spam)
    // 6. Execute (lines 829-836):
    //    pml.groundPlane = qfalse; pml.walking = qfalse;
    //    pm->ps->pm_flags |= PMF_JUMP_HELD;
    //    pm->ps->groundEntityNum = ENTITYNUM_NONE;
    //    pm->ps->velocity[2] = JUMP_VELOCITY;
    //    PM_PlayJumpAnim();
    return qtrue;
}
```

Called from `bg_pmove.c:1333` inside `PM_AirMove`. JUMP_VELOCITY
constant lives in bg_public.h (~270 units/sec).

PMF_JUMP_HELD anti-spam: cleared at `bg_pmove.c:5206` when
`cmd.upmove < 10` (button released). So engine already enforces
"release-then-press" between consecutive jumps — Double-Jump
benefits from this for free.

Anti-bunnyhop `PM_JUMP_DELAY` (850ms): we'll need to **bypass it
for double-jump** since the second-jump happens within ~500ms of
the first. Recommended: skip the delay check when in-air (the
double-jump is the second jump within the same airtime, which is
exactly the case the delay was designed to allow).

## §2 — Prediction architecture review

`bg_pmove.c` runs both server-side (in `g_active.c::ClientThink`)
and client-side (in `cgame::CG_Predict`). For prediction parity:

- **Any state read by PM_CheckJump must be in `playerState_t`**
  (synced via snapshots; client can predict identically).
- `pm_flags` is part of `playerState_t` — synced via
  delta-compression. Adding bit 128 = `PMF_VG_DOUBLEJUMPED`
  costs zero bandwidth (already in the payload).
- No new field needed. No `pmoveExt_t` extension needed.
- **Reset on respawn:** `PMF_RESPAWNED` is set on respawn and
  cleared by PM_CheckJump. `PMF_VG_DOUBLEJUMPED` should likewise
  reset — but it'll naturally reset because respawn lands the
  player on ground (groundEntityNum != ENTITYNUM_NONE → ground
  detection clears the bit).

### Free pm_flags bits (verified)

| Bit | Value | Status |
|---|---|---|
| 0 | 1 | PMF_DUCKED |
| 1 | 2 | PMF_JUMP_HELD |
| 2 | 4 | PMF_LADDER |
| 3 | 8 | PMF_BACKWARDS_JUMP |
| 4 | 16 | PMF_BACKWARDS_RUN |
| 5 | 32 | PMF_TIME_LAND |
| 6 | 64 | PMF_TIME_KNOCKBACK |
| **7** | **128** | **FREE — recommended PMF_VG_DOUBLEJUMPED** |
| 8 | 256 | PMF_TIME_WATERJUMP |
| 9 | 512 | PMF_RESPAWNED |
| 10 | 1024 | FREE (alternative) |
| 11 | 2048 | PMF_FLAILING |
| 12 | 4096 | PMF_FOLLOW |
| 13 | 8192 | FREE |
| 14 | 16384 | PMF_LIMBO |
| 15 | 32768 | PMF_TIME_LOCKPLAYER |

Bit 7 (128) is the natural choice — sits next to existing
PMF_TIME_KNOCKBACK (64), preserves logical bit-progression.

## §3 — Hook-point analysis (A/B/C)

### Option A — Modify PM_CheckJump directly (recommended)

Insert double-jump logic between the existing PMF_JUMP_HELD check
(line 822-827) and the velocity assignment (line 833-834):

```c
// Existing checks 1-5 unchanged...

// Already on ground, but jump-delay still active? → block (existing)
if (pm->ps->groundEntityNum != ENTITYNUM_NONE
    && pm->cmd.serverTime - pm->pmext->jumpTime < PM_JUMP_DELAY) {
    return qfalse;
}

// VanguardMod v0.7.2 — second-jump in air
qboolean isAirborne = (pm->ps->groundEntityNum == ENTITYNUM_NONE);
if (isAirborne) {
    if (!vg_Fun_GetInt("vg_fun_doublejump", 0)) {
        return qfalse;     // upstream behavior preserved when off
    }
    if (pm->ps->pm_flags & PMF_VG_DOUBLEJUMPED) {
        return qfalse;     // already used the second jump
    }
    // Class + stamina checks...
    pm->ps->pm_flags |= PMF_VG_DOUBLEJUMPED;
}
// Ground? Reset bit (paranoia — should already be 0 from landing)
else {
    pm->ps->pm_flags &= ~PMF_VG_DOUBLEJUMPED;
}

// Existing execute path (lines 829-836):
pml.groundPlane = qfalse;
pml.walking = qfalse;
pm->ps->pm_flags |= PMF_JUMP_HELD;
pm->ps->groundEntityNum = ENTITYNUM_NONE;
pm->ps->velocity[2] = isAirborne
    ? vg_Fun_GetInt("vg_fun_doublejump_height", JUMP_VELOCITY)
    : JUMP_VELOCITY;
PM_PlayJumpAnim();
```

**Pro:**
- Single function changed
- ~30 LoC addition (pre-class/stamina; +20 with extras)
- Existing early-returns preserved (prone, ladder, water, jump-delay, respawned, jump-held)
- Cup-orthodox path: when `vg_fun_doublejump=0` (default), `isAirborne=true` returns false → **byte-identical to upstream**
- Prediction-safe: state in `pm_flags`, helper reads cvar (server + cgame agree per Strategy C lock)

**Con:**
- Anti-bunnyhop jump-delay needs ground-only gating (otherwise blocks second jump that happens <850ms after first). Trivial to fix.

### Option B — Add post-jump hook

Track jumps_used counter, reset on ground-touch.

**Con:** more bookkeeping, no functional benefit over Option A
unless triple-jump is a future requirement — Memory #8 doesn't
mention triple-jump.

### Option C — Full configurable mid-air-jumps system

`vg_fun_jumps_inair` cvar (0=none, 1=double, 2=triple, …). Loop
in PM_CheckJump.

**Con:** YAGNI. Memory #8 says "double-jump", not "N-jump".
Edge-cases multiply (when to reset counter? velocity per jump?).

### Recommendation

**Option A.** Cleanest, smallest, future-extensible to Option C
if demand surfaces (the bit becomes a counter; cvar becomes
"jumps_inair" with the height cvar applying to each).

## §4 — WolfGuard anti-cheat whitelist

`grep -rn "wg_interface\|WG_VelocityCheck\|WG_FrameCheck" src/`
→ only the v0.6.0 plugin interface, which has these hooks:

```c
typedef struct {
    int  (*init)(void);
    void (*shutdown)(void);
    int  (*client_connect)(int clientNum, const char *userinfo);
    void (*client_disconnect)(int clientNum);
    void (*frame)(int levelTime);
} wg_interface_t;
```

**No per-frame velocity hook. No anomaly-check hook.** The
community stub returns 0 for everything; the protected build
(closed-source, `vanguardmod/wolfguard` private repo) defines
its own dispatch table.

**Conclusion: no whitelist needed for v0.7.2.** Double-jump is
invisible to the current WolfGuard interface.

If a future protected-build provider adds velocity-anomaly
detection, the natural whitelist mechanism would be one of:

- **Read `pm_flags & PMF_VG_DOUBLEJUMPED`** from the server-side
  player state to whitelist mid-air velocity changes.
- **Add a `WG_OnPmoveCheck(int clientNum, int pmFlags)` hook**
  to wg_interface_t (interface bump). Provider's frame-checker
  consults pmFlags before flagging.

Documented for v0.8.x+ if WolfGuard ever grows velocity-checks.
**Not required for v0.7.2.**

## §5 — Edge-cases inventory

Test matrix for the implementation phase:

| Scenario | Expected | Engine handler |
|---|---|---|
| Normal ground jump | velocity = JUMP_VELOCITY | PM_CheckJump existing path |
| Airborne jump (vg_fun=0) | rejected | PM_CheckJump returns false (isAirborne+disabled) |
| Airborne jump (vg_fun=1, first time) | velocity = doublejump_height, sets PMF_VG_DOUBLEJUMPED | new code path |
| Airborne jump (vg_fun=1, already double-jumped) | rejected | PMF_VG_DOUBLEJUMPED check |
| Triple-press in air | 1st = ground jump, 2nd = double jump, 3rd = rejected | PMF_JUMP_HELD between releases + PMF_VG_DOUBLEJUMPED |
| Land then jump | bit reset on landing → normal next jump cycle | PM_GroundTrace detects ground, clears bit (or PM_CheckJump on-ground branch clears) |
| Prone | rejected | EF_PRONE early-return at line 791 |
| On ladder | rejected | engine ladder logic (PMF_LADDER) overrides at PM_AirMove dispatch |
| Swimming (waterlevel >= 2) | water-jump applies; no double-jump | PM_CheckWaterJump path takes precedence |
| Spectator/free-fly | no PM_CheckJump call (PM_NOCLIP/PM_SPECTATOR) | engine pm_type dispatch |
| MG42-mounted | rejected | EF_MOUNTEDTANK + ladder/jump blocks |
| Class-restricted (cvar set) | classMask & (1<<class) check | new code |
| Stamina-cost (cvar set) | sprintTime check + drain | new code reading pmext |
| Velocity-direction | pure vertical add, no XY change | velocity[2] only |
| WolfGuard active (community) | no impact | no velocity hook in stub |
| WolfGuard active (protected, future) | needs whitelist (deferred) | §4 above |

### Reset point details

`PMF_VG_DOUBLEJUMPED` clears when player is on ground. Two
candidate reset points:

1. **At top of PM_CheckJump** when `groundEntityNum != ENTITYNUM_NONE`:
   simple, runs once per pmove tick.
2. **In PM_GroundTrace** at the moment ground is detected after
   being airborne: more precise (clear at the exact landing
   instant), but requires an "was airborne previous frame" check.

Recommendation: **Option 1** — clear in PM_CheckJump's on-ground
branch. Simpler, runs whenever PM_CheckJump runs, no extra
state-tracking.

## §6 — Cvar design (vg_fun pattern)

Following the v0.7.1 Falldamage precedent, cvars register in
`g_vanguard.c::vg_Fun_Init`:

```c
vg_Fun_RegisterCvar("vg_fun_doublejump",          "0",   CVAR_ARCHIVE);
vg_Fun_RegisterCvar("vg_fun_doublejump_height",   "270", CVAR_ARCHIVE);
vg_Fun_RegisterCvar("vg_fun_doublejump_classes",  "0",   CVAR_ARCHIVE);
vg_Fun_RegisterCvar("vg_fun_doublejump_stamina",  "0",   CVAR_ARCHIVE);
```

| Cvar | Default (cup-orthodox) | Public recommended | Purpose |
|---|---|---|---|
| `vg_fun_doublejump` | `0` (off) | `1` (on) | Master toggle for double-jump. With vg_fun=0 the helper short-circuits — sub-cvars dormant. |
| `vg_fun_doublejump_height` | `270` (= JUMP_VELOCITY) | `200` (lower than ground-jump for "weaker" feel) | Velocity[2] applied on the second jump only. |
| `vg_fun_doublejump_classes` | `0` (all classes) | `0` or bitmask | Bitmask: 1=soldier, 2=medic, 4=engineer, 8=fieldops, 16=covertops. |
| `vg_fun_doublejump_stamina` | `0` (no cost) | `50` (drains sprint-bar) | Stamina drain on second jump. Reads/writes `pmext->sprintTime`. |

### Behaviour matrix

| `vg_fun` | `vg_fun_doublejump` | Result |
|---|---|---|
| 0 | (any) | Helper short-circuits — upstream behaviour preserved |
| 1 | 0 | Master on but feature off — upstream behaviour |
| 1 | 1 | Double-jump enabled, sub-cvars active |

Cup integrity preserved at the master gate. Public servers opt
in explicitly with `vg_fun 1` (CVAR_LATCH map_restart) +
`vg_fun_doublejump 1` (CVAR_ARCHIVE, takes effect next
PM_CheckJump call after the master applies).

## §7 — Class-restriction design

Bitmask cvar `vg_fun_doublejump_classes`, mirrors ETLegacy
team_max* class-counter pattern:

| Bit | Value | Class |
|---|---|---|
| 0 | 1 | Soldier |
| 1 | 2 | Medic |
| 2 | 4 | Engineer |
| 3 | 8 | Field Ops |
| 4 | 16 | Covert Ops |

Examples:
- `0` (default): all classes can double-jump (no restriction)
- `5` (1+4): soldier + engineer only
- `31` (1+2+4+8+16): all classes (same as 0, explicit)

Implementation in PM_CheckJump:

```c
int classMask = vg_Fun_GetInt("vg_fun_doublejump_classes", 0);
if (classMask != 0) {
    int classBit = 1 << (pm->ps->stats[STAT_PLAYER_CLASS]);
    if (!(classMask & classBit)) return qfalse;
}
```

`STAT_PLAYER_CLASS` is in `playerState_t.stats[]` — synced to
client, prediction-safe.

## §8 — Implementation scope estimate

| Component | LoC |
|---|---|
| `bg_public.h`: `PMF_VG_DOUBLEJUMPED` define | 1 |
| `bg_pmove.c::PM_CheckJump` modification | 30-40 |
| `g_vanguard.c::vg_Fun_Init` cvar registrations | 5-8 |
| Diagnostic log gate (`vanguard_diag_doublejump`) | 15 |
| `docs/VG_FUN_MODE.md` Doublejump section | 50-60 |
| `docs/RELEASE_NOTES.md` v0.7.2 entry | 30 |
| `docs/notes/PHASE_12_DOUBLEJUMP_RECON.md` §X execution log | 30 |
| `.github/workflows/ci.yml` gate | 10 |
| **Total** | **~170-200 LoC code+docs** |

No new files. No new headers. Triple-Header on bg_pmove.c
already retrofitted in v0.7.1 (Phase 8.0b deferral resolved).

## §9 — Risk assessment

| Risk | Severity | Mitigation |
|---|---|---|
| Prediction mismatch (cgame ≠ qagame) | LOW | State in `pm_flags` (synced via snapshot); helper reads cvar identically on both sides |
| Anti-bunnyhop `PM_JUMP_DELAY` blocks second jump | LOW | Gate the delay check with `groundEntityNum != ENTITYNUM_NONE` (ground-only) |
| WolfGuard false-flag | NONE | No velocity hook in current `wg_interface_t`; documented for v0.8.x if added |
| Edge-case break (prone, ladder, swim) | LOW | Engine handles these before PM_CheckJump (EF_PRONE, PMF_LADDER, waterlevel) |
| Cup-orthodox regression | NONE | `vg_fun=0` master gate short-circuits all sub-cvars to cup-defaults |
| Phase 6 hitbox impact | NONE | Movement layer, separate from `g_combat.c` / `mdx_*` |
| Phase 8.0a NULL-guard | NONE | Doesn't touch `g_combat.c::G_Damage` |
| Phase 7.1 falldamage interaction | LOW | Higher jump = harder land; cup-orthodox unaffected; public servers should consider tuning `vg_fun_falldmg_*` together |
| Stamina-cost inconsistency | LOW | Optional cvar (default 0); reads/writes existing `pmext->sprintTime` field |
| PM_PlayJumpAnim wrong animation in air | LOW | Existing function plays standard jump anim; visually fine for double-jump |
| `STAT_PLAYER_CLASS` not always valid | LOW | If stats[] uninitialized, classBit calculation is 0 → masks out → safe (rejects); production case has class set on spawn |

## §10 — Open questions for wahke

1. **Hook-point** — Option A (modify PM_CheckJump in-place,
   recommended), B (post-jump hook with counter), or C (full
   N-jumps configurable)?
2. **Default `vg_fun_doublejump_height`** — same as ground-jump
   (270 = JUMP_VELOCITY, easier feel), or reduced (200, "weaker"
   second jump for arcade flavor)?
3. **Class restrictions** — ship `vg_fun_doublejump_classes` in
   v0.7.2 or defer? Memory #8 mentions class-modifiers as a
   future feature; this would be foundation.
4. **Stamina cost** — ship `vg_fun_doublejump_stamina` in v0.7.2
   or defer? Adds tuning surface; defaults 0 mean no behavioural
   change unless admin opts in.
5. **Velocity direction** — pure vertical add (recommended,
   cup-deterministic), or add cmd input for directional control
   (forward/back/strafe — arcade flavor)?
6. **WolfGuard whitelist** — `PMF_VG_DOUBLEJUMPED` bit alone is
   sufficient (recommended, no interface change), or pre-emptive
   `WG_OnPmoveCheck` interface bump now?
7. **PMF flag bit number** — 128 (bit 7, recommended,
   smallest-free), 1024 (bit 10), or 8192 (bit 13)? All free,
   bit 7 sits naturally next to PMF_TIME_KNOCKBACK.
8. **v0.7.2 scope** — full Tier 1 (master + height + classes +
   stamina) or minimal (master + height only, defer classes/stamina
   to v0.7.x)?

## §11 — Implementation phasing

### v0.7.2 — Tier 1 (recommended)

1. `bg_public.h`: add `#define PMF_VG_DOUBLEJUMPED 128`
2. `bg_pmove.c::PM_CheckJump`: insert double-jump branch between
   PMF_JUMP_HELD check and velocity assignment (Option A)
3. `g_vanguard.c::vg_Fun_Init`: register 4 cvars
4. `g_active.c` or `g_main.c`: optional diagnostic log
   (`vanguard_diag_doublejump` cvar gate)
5. CI gate: `strings | grep -c "vg_fun_doublejump"` >= 4
6. Docs: `VG_FUN_MODE.md` Doublejump section, `RELEASE_NOTES.md`
   v0.7.2 entry, this audit §X execution log

Estimated: ~150-200 LoC. Single PR. Standard 7-step release.

### v0.7.x — Optional follow-ups

Only if cup-tester / public-server feedback demands:

- Triple-jump (Option C upgrade, change bit to counter)
- Per-class height (`vg_fun_doublejump_height_<class>`)
- Cooldown between jumps (separate from PMF_JUMP_HELD)
- Jump-pads (level-entity, Phase 13 territory)

Defer until concrete demand surfaces.

### Out of scope for any v0.7.x

- Strafe-jumping (Phase 7.3 explicitly punted)
- Air control (Phase 7.3 explicitly punted)
- Custom physics constants (`pm_airaccelerate` etc.)
- WolfGuard anti-cheat hooks expansion (v0.8.x+)

## §12 — Phase 6/7/8 regression-safety

Double-jump is **purely additive in the movement layer** and
touches no other VanguardMod subsystem:

| Phase / subsystem | Touched? |
|---|---|
| Phase 6 multi-region hitbox (`g_combat.c`, `g_mdx.c`) | NO |
| Phase 7.0 strict-mode (`g_combat.c`) | NO |
| Phase 7.0.2 capsule additions (`human_base.hit`) | NO |
| Phase 7.1 cup-movement foundation (`g_pronedelay`, `vanguard_diag_movement`) | NO |
| Phase 7.2 netcode profile (`vg_Netcode_*`) | NO |
| Phase 8.0a NULL-guard (`g_combat.c:1781-1784`) | NO |
| Phase 8.0b Falldamage (`g_active.c::G_FallDamage`, `vg_fun_falldmg_*`) | NO (but interaction noted: higher jumps = harder lands) |
| Phase 9 vg_fun foundation (`vg_Fun_GetInt`, `vg_Fun_RegisterCvar`) | YES — uses helper API (intended) |
| Phase 10 perf hot-path (`mdx_hit_test`, `vg_perf_*`) | NO |
| Phase 11 (TBD) | NO |
| WolfGuard interface (`wg_interface.h`) | NO |
| Branding-2 (`ui_vg_branding`, mod-list) | NO |

**Cup-orthodox regression test:**

1. Boot server with default settings (`vg_fun 0`).
2. Verify Mods menu still shows ETLegacy/Vanguard branding.
3. Run die-command crash test (`die -1`) — Phase 8.0a guard.
4. Drop tests at known oasis heights — falldamage values
   unchanged from v0.7.1.1.
5. Try jump-spam (hold jump, never release) — engine
   PMF_JUMP_HELD blocks as before.
6. Try jump-press in air with default `vg_fun_doublejump 0` —
   rejected (upstream behaviour).
7. Toggle `vg_fun 1` + `vg_fun_doublejump 1` + map_restart — now
   double-jump works.

All 7 must pass in v0.7.2 build before tag.

## §13 — References

### Code

- `src/game/bg_pmove.c:788-839` — `PM_CheckJump` (the hook
  point)
- `src/game/bg_pmove.c:1333` — `PM_CheckJump` call site (in
  `PM_AirMove`)
- `src/game/bg_pmove.c:5203-5206` — `PMF_JUMP_HELD` clear
  on button release
- `src/game/bg_public.h:544-560` — `PMF_*` flag definitions
- `src/game/bg_public.h:JUMP_VELOCITY` — jump-velocity constant
- `src/game/bg_public.h::stats[STAT_PLAYER_CLASS]` — class read
- `src/game/g_vanguard.c::vg_Fun_RegisterCvar` — cvar
  registration (v0.7.0 foundation)
- `src/game/g_vanguard.c::vg_Fun_GetInt` — cvar lookup with
  cup-default short-circuit
- `src/game/wolfguard/wg_interface.h` — current dispatch
  surface (no velocity hook)

### Audit cross-refs

- `docs/notes/PHASE_7_3_AUDIT.md` — movement physics audit
  (prone-delay, air-control, strafe-jumping; double-jump
  out-of-scope at the time, this recon picks it up)
- `docs/notes/PHASE_8_0B_AUDIT.md` — Falldamage redesign
  (Tier 1 in v0.7.1; double-jump may interact with falldamage
  at high heights — public-profile tuning consideration)
- `docs/VG_FUN_MODE.md` — vg_fun pattern + cvar naming
  convention
- `docs/CUP_VS_PUBLIC.md` — divergence documentation pattern

### Memory cross-refs

- **Memory #8** — vg_fun mode bundle spec; double-jump is
  named feature in the roadmap
- **Memory #6** — WolfGuard subsystem context
- **Memory #20** — cup-server config-vs-code distinction
  (this is code-feature, not config)

### Phase 7.0 + Phase 11 lessons-learned applied

1. **Diagnostic infrastructure** — `vanguard_diag_doublejump`
   cvar (CVAR_TEMP, default 0) for per-event log gate. Mirrors
   v0.6.1 `vanguard_diag_movement` pattern. Helps debug
   prediction-mismatches in public-server reports.
2. **Build-flag mismatches** — CI gate `strings | grep -c
   "vg_fun_doublejump"` ≥ 4 (one per cvar) catches accidental
   drop of registration.
3. **Crash-bugs vs tuning-bugs** — double-jump is feature
   addition, not bug-fix. v0.7.2 stays scope-clean.
4. **Server-config check first** (Phase 11 lesson) — n/a here;
   double-jump is genuinely missing, no upstream cvar exists.
5. **Phase 11 data-driven approach** — recon based on actual
   PM_CheckJump source (verified line-by-line), not assumptions.

## §14 — v0.7.2 implementation log

Date: 2026-05-03
Branch: `feat/v0.7.2-doublejump` → PR
Commit (single): `<hash, post-merge>`

### Implemented per audit decisions (all "alle wie empfohlen")

- **PMF_VG_DOUBLEJUMPED = 128** added to `bg_public.h:551` next
  to `PMF_TIME_KNOCKBACK` (verified bit 7 free in upstream
  layout).
- **`vg_pm_cvar_int` helper** added at `bg_pmove.c:79` —
  reads cvars via `trap_Cvar_VariableStringBuffer` + atoi.
  Forward-declares the trap under `#ifndef CGAMEDLL` since the
  qagame-side `g_local.h` isn't included by bg_pmove.c (only
  q_shared.h + bg_public.h).
- **PM_CheckJump rewrite** — eligibility check at top
  computes `canDoubleJump`, `dj_height_velocity`,
  `dj_stamina_drain`. Gates the 850ms `PM_JUMP_DELAY` on
  `!canDoubleJump`. Branch the velocity assignment + flag
  setting + stamina drain at the bottom. C90 declarations
  moved to block-top (compiler warning fixed).
- **PM_GroundTrace landing-reset** at `bg_pmove.c:~2078` —
  clears `PMF_VG_DOUBLEJUMPED` inside the existing "just hit
  the ground" branch (transition `groundEntityNum` NONE →
  real entity).
- **4 cvars registered** in `g_vanguard.c::vg_Fun_Init` after
  the v0.7.1 falldamage block.
- **CI gate** in `.github/workflows/ci.yml` build-linux job:
  `strings | grep -c "vg_fun_doublejump"` must return ≥ 4.

### Critical implementation discovery

The audit §3's pseudocode used `vg_Fun_GetInt` (qagame-only,
defined in `g_vanguard.c`). cgame can't link against this
function — bg_pmove.c is **shared between cgame and qagame**.
Investigating cgame syscalls revealed:

- `trap_Cvar_VariableIntegerValue` is qagame-only
  (g_syscalls.c:206); cgame doesn't expose `CG_CVAR_VARIABLEINTEGERVALUE`
- `trap_Cvar_VariableStringBuffer` IS in both modules
  (g_syscalls.c, cg_syscalls.c:120); cgame exposes
  `CG_CVAR_VARIABLESTRINGBUFFER`

Resolution: new `vg_pm_cvar_int` helper at `bg_pmove.c:79`
uses `trap_Cvar_VariableStringBuffer` + `atoi`. Both cgame
and qagame read the same cvar values, prediction parity
preserved without needing a CGAMEDLL/GAMEDLL macro mirror
or cgame-side cvar registration.

### Deferred (per audit §11)

- Diagnostic cvar `vanguard_diag_doublejump` — would need a
  server-side detection hook in `g_active.c::ClientThink`
  watching for `PMF_VG_DOUBLEJUMPED` transitions (bg_pmove.c
  itself can't easily emit logs). Skipped for v0.7.2 hotfix
  scope; v0.7.2.x candidate if cup-tester reports issues.
- Triple-jump (Option C upgrade) — Tier 2.
- Per-class height — Tier 2.
- Jump-pads — Phase 13+ territory.
- WolfGuard whitelist flag — current WG interface has no
  velocity-check hook; documented for v0.8.x.

### Phase 6/7/8 regression-safety verified

- ✓ Phase 8.0a NULL-guard at `g_combat.c:1781-1784` untouched
  (static grep)
- ✓ vg_Fun_GetInt + vg_Hitbox_IsActive + WG_Active +
  vg_perf_traces_total + mdx_hit_test all present in
  qagame.so (no symbol regressions)
- ✓ Build green for qagame + cgame + ui + tvgame +
  mod_pk3 (zero warnings after C90 declaration cleanup)
- ✓ Cup-orthodox path: `vg_fun=0` short-circuits in the very
  first eligibility check (`vg_pm_cvar_int("vg_fun", 0)`
  returns 0); `canDoubleJump` stays qfalse;
  `pm->ps->velocity[2] = JUMP_VELOCITY` runs in the else
  branch — byte-identical to upstream

### Measured impact (to be filled post cup-tester validation)

- Default `vg_fun 0`: byte-identical to v0.7.1.1 (cup-orthodox
  preserved)
- `vg_fun 1 + vg_fun_doublejump 1`: second mid-air jump enabled
  with default 270 velocity (same as ground jump)
- `vg_fun 1 + vg_fun_doublejump 1 + vg_fun_doublejump_stamina 50`:
  costs 5000 STAT_SPRINTTIME (= 25% of 20000 max sprint bar)
  per double-jump
- `vg_fun 1 + vg_fun_doublejump 1 + vg_fun_doublejump_classes 4`:
  engineer only

Cup-tester instructions: leave at default for cup matches
(`vg_fun 0`, no behavioural change). Fun servers enable with
the standard activation sequence in `docs/VG_FUN_MODE.md`.
