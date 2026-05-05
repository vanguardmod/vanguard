# VanguardMod release notes

User-visible changes per published version. For build / release
mechanics, see `docs/RELEASE_PROCESS.md`.

## v0.7.2.3 — CS_SERVERINFO overflow fix + airborne diag (TBD)

> **Phase 13c follow-up to v0.7.2.2.** Two changes: (1) drop the
> erroneous `CVAR_SERVERINFO` OR-in from `vg_Fun_RegisterCvar`
> that was added in v0.7.2.1 — it never solved the cgame
> visibility problem it was meant to fix and caused
> `Info_SetValueForKey: Info string length exceeded` warnings
> in production. (2) add a second airborne diagnostic block
> (`VG_DJ[A]`) to `PM_CheckJump` so we can finally observe what
> happens on a true mid-air press. The v0.7.2.2 ground-press
> diag (`VG_DJ[S]`) is retained for ground-state comparison.
> No production behaviour change at `vanguard_dev=0`. Cup-orthodox
> preserved; Phase 8.0a NULL-guard untouched; v0.7.2.1 splash
> bypass + v0.7.2.2 idempotent-register untouched.

### Bug — CS_SERVERINFO buffer overflow (FIX)

- fix(vg_fun): `vg_Fun_RegisterCvar` no longer auto-applies
  `CVAR_SERVERINFO` to registered cvars. The 9 `vg_fun_*` cvars
  (5 falldmg + 4 doublejump) drop out of the `CS_SERVERINFO`
  configstring entirely.
- **Why it was wrong**: v0.7.2.1's OR-in was added under the
  assumption that `CVAR_SERVERINFO` would let cgame read the
  values via `trap_Cvar_VariableStringBuffer`. It does not —
  cgame's local cvar pool is not auto-populated from
  `CS_SERVERINFO`. The correct cgame-side read pattern is
  `Info_ValueForKey(CS_SERVERINFO_string, "name")`, e.g.
  `cg_servercmds.c:276` for `vanguard_dev`. Until cgame uses
  that path, the values are server-only anyway — and falldmg /
  doublejump are server-authoritative gameplay cvars that cgame
  doesn't strictly need for the gate logic (the server-side
  pmove fires the jump regardless of client mispredict).
- **Why it was also actively harmful**: 9 extra cvars pushed
  the `CS_SERVERINFO` configstring past `MAX_INFO_STRING` (1024
  bytes), producing repeated `Info_SetValueForKey: Info string
  length exceeded` warnings in the server log.
- `vg_fun` master switch keeps `CVAR_SERVERINFO` (registered
  separately via `gameCvarTable` in `g_cvars.c`). UI mode-
  indication still works.

### Diagnostic — Airborne `VG_DJ[A]` block

- diag(pmove): new server-side log line in `PM_CheckJump`
  (`#ifndef CGAMEDLL`, gated by `vanguard_dev=1`). Fires when
  `groundEntityNum == ENTITYNUM_NONE` (1023) AND `cmd.upmove >= 10`,
  regardless of `PMF_JUMP_HELD`. Throttled to 1 per 100ms (server-
  static last-fire timestamp; designed for solo-tester use).
- **Why a second diag**: v0.7.2.2's `VG_DJ[S]` block requires
  `!PMF_JUMP_HELD` (rising edge), which suppresses subsequent
  ticks once the player holds SPACE through the first jump. As
  a result, every captured line in v0.7.2.2 testing showed
  `ground=1022` (`ENTITYNUM_WORLD` — player on map geometry),
  not `ground=1023` (`ENTITYNUM_NONE` — airborne). We never
  observed a true mid-air press. The new `[A]` block fixes that.
- Same field set as `[S]` plus a `held=` field (so we can see
  `PMF_JUMP_HELD` state explicitly). Distinct prefix lets log
  readers `grep VG_DJ\[A\]` for airborne-only observations.
- v0.7.2.2's `VG_DJ[S]` block is retained unchanged — useful
  for ground-state comparison and to keep the existing CI gate
  semantics stable.

### Notes / out-of-scope

- WolfGuard untouched.
- Phase 8.0a NULL-guard at `g_combat.c:1781-1784` untouched.
- v0.7.2.1 splash-bypass helpers untouched.
- v0.7.2.2 idempotent `vg_Fun_RegisterCvar` registration loop
  untouched.
- cgame `vg_pm_cvar_int` reads still go through
  `trap_Cvar_VariableStringBuffer` (will return empty for the
  `vg_fun_*` cvars). The actual cgame cvar-visibility fix —
  swapping to `Info_ValueForKey(CS_SERVERINFO, ...)` — is a
  separate later release once we have airborne diag data on
  the actual server-side eligibility behaviour.
- No tagging convention change: four-digit hotfix per Memory #4.

### CI gates added / inverted

- **Inverted** `Verify Phase 13c vg_Fun_RegisterCvar SERVERINFO
  OR-in REMOVED` (was: must be present; now: must be absent).
  Code-only grep anchored to leading whitespace + identifier
  so the comment-block reference is not matched.
- **Extended** `Verify Phase 13b/c PM_CheckJump diagnostic
  strings in qagame Linux SO` — threshold raised from ≥1 to ≥2,
  pattern broadened to `VG_DJ\[(S|A)\]` so both diag blocks
  are required.
- All other v0.7.2.2 gates retained.

### Verification

- Build: cmake --build green for qagame + cgame + ui + tvgame + mod_pk3.
- SERVERINFO OR-in code-only grep: 0 hits (REMOVED).
- `VG_DJ[A] air:` format string in qagame: present.
- `VG_DJ[S] press:` format string still in qagame: present.
- Both diagnostics ABSENT from cgame (`#ifndef CGAMEDLL`): 0 hits.
- Idempotent loop pattern in source: still present (1 hit).
- Regression: v0.7.2.1 splash bypass helpers (2/2), Phase 8.0a NULL-guard
  reference, vg_Fun_GetInt + vg_Hitbox_IsActive + WG_Active +
  vg_perf_traces_total + mdx_hit_test all alive (5/5).

### Live-test plan (post-release)

1. `\set vanguard_dev 1`, `\set vg_fun 1`, `\set vg_fun_doublejump 1`,
   `\set vg_fun_doublejump_classes 31`, `\set vg_fun_doublejump_height 400`,
   `\map_restart`.
2. Test A — deliberate key-release: jump from ground → release SPACE
   → wait <850ms → press SPACE again mid-air. Repeat across classes.
3. Test B — natural play: just hold/release as feels natural and
   try to double-jump.
4. Grep server log for `VG_DJ\[(S|A)\]` to see ground vs airborne
   observations side by side.
5. Run a few map_restart cycles. Grep `Info_SetValueForKey: Info
   string length exceeded` — expected 0 (or significantly fewer)
   hits compared to v0.7.2.2.

### Decision tree for v0.7.2.4 based on `VG_DJ[A]` data

| Pattern | Diagnosis | v0.7.2.4 fix |
|---|---|---|
| `VG_DJ[A]` lines appear with `candj=yes` and player feels jump | Issue resolved by Phase 13b idempotent register or some upstream side-effect; just remove diag | Diag cleanup only |
| `VG_DJ[A]` lines with `ground=1023 candj=no` and one of the expected gates failing | Targeted gate fix | Gate-specific |
| `VG_DJ[A]` lines with `candj=yes` but player still feels nothing | PMF_JUMP_HELD blocks the actual fire path despite passing the rising-edge gate elsewhere; or velocity gets re-zeroed downstream | Trace post-`PM_CheckJump` flow |
| No `VG_DJ[A]` lines at all even on confirmed airborne presses | Player input not reaching `PM_CheckJump` (cmd.upmove dropped, or pmove not invoked) | cmd-flow audit |

## v0.7.2.2 — Diagnostic-first hotfix (TBD)

> **Phase 13b — diagnostic-first follow-up to v0.7.2.1.** Two
> separate problems addressed: a real registry-overflow bug
> (vg_Fun_RegisterCvar accumulating entries across map_restart
> cycles) is fixed outright; a second-jump runtime failure that
> v0.7.2.1's CVAR_SERVERINFO change did not resolve gets a
> targeted server-side diagnostic so v0.7.2.3 can land a
> data-driven fix instead of a guess. No behaviour changes for
> production servers (vanguard_dev=0 default). Cup-orthodox
> preserved; Phase 8.0a NULL-guard untouched; v0.7.2.1 splash
> bypass + SERVERINFO additions untouched.

### Bug — Registry overflow on map_restart cycles (FIX)

- fix(vg_fun): `vg_Fun_RegisterCvar` is now idempotent — it
  walks `s_vg_fun_registry[]` for an existing entry by name
  before bumping the counter. If the cvar already exists, the
  function re-applies `trap_Cvar_Register` (engine merges flags,
  live value untouched) and returns. New cvars take the previous
  path (slot store + `trap_Cvar_Register` + count bump).
- **Why**: `vg_Fun_Init` runs from `G_InitGame` on every map
  start AND every map_restart (Q3 engine convention). The static
  registry persists across `G_InitGame` calls (the .so stays
  loaded between maps), so the previous implementation
  accumulated 9 entries per cycle and overflowed
  `VG_FUN_REGISTRY_MAX = 64` after ~7 maps. Symptom:
  `VG_Fun: registry full (max=64); cannot register vg_fun_*`
  log spam; `vg_Fun_PrintStatus` accuracy degraded.
- **Functional impact of the pre-fix overflow**: registry
  bookkeeping only — `vg_pm_cvar_int` reads the engine cvar
  pool directly (registry-independent), and the cvars themselves
  were registered on map 1 (so they exist in the engine pool
  with `CVAR_SERVERINFO` from boot). The overflow did NOT cause
  the v0.7.2 second-jump runtime failure.

### Diagnostic — Server-side `PM_CheckJump` instrumentation

- diag(pmove): new server-side log line in `PM_CheckJump` gated
  by `vanguard_dev=1`. Fires on every airborne jump-press rising
  edge (upmove >= 10 and !PMF_JUMP_HELD). Logs all eligibility
  inputs in one line: `groundEntityNum`, `PMF_VG_DOUBLEJUMPED`,
  read values for `vg_fun` / `vg_fun_doublejump` /
  `vg_fun_doublejump_classes` / `vg_fun_doublejump_stamina` /
  `vg_fun_doublejump_height`, `STAT_SPRINTTIME`,
  `STAT_PLAYER_CLASS`, `cmd.upmove`, `PMF_RESPAWNED`,
  `cmd.serverTime - pmext->jumpTime`, and the eligibility result
  `candj=yes/no`.
- **Why server-side only**: cgame's local cvar pool does not
  see `CVAR_SERVERINFO` cvars via `trap_Cvar_VariableStringBuffer`
  (confirmed by the existing `cg_servercmds.c` pattern that uses
  `Info_ValueForKey` on the `CS_SERVERINFO` configstring instead).
  A cgame-side log would always print zeros for the cvar fields,
  which is uninformative noise. v0.7.2.3 swaps the cgame read path
  to configstring + `Info_ValueForKey` and the diagnostic gets
  cleaned up.
- **Why diagnostic instead of speculative fix**: v0.7.2.1's
  CVAR_SERVERINFO change addressed the cgame visibility theory
  but server-side authoritative pmove should fire double-jump
  regardless of client prediction. Player reports "absolutely
  nothing" on second-press, which suggests a server-side gate
  also blocks. Without instrumentation we cannot tell which gate
  fails. v0.7.2.3 will be a chirurgical fix based on this log.

### Notes / out-of-scope

- WolfGuard untouched.
- Phase 8.0a NULL-guard at `g_combat.c:1781-1784` untouched.
- v0.7.2.1 splash-bypass helpers + CVAR_SERVERINFO addition
  untouched (still present, still gated by their own CI checks).
- The CVAR_SERVERINFO assignment line is moved ABOVE the
  idempotent-lookup loop so re-applied registrations get the
  same engine flags as first-time registrations. The assignment
  line is unchanged in semantics — only repositioned.
- No tagging convention change: four-digit hotfix per Memory #4.

### CI gates added

- `Verify Phase 13b idempotent vg_Fun_RegisterCvar` (static-source
  grep for `Q_stricmp(s_vg_fun_registry[i].name, ...)` loop).
- `Verify Phase 13b PM_CheckJump diagnostic in qagame Linux SO`
  (binary-string check for `VG_DJ[S] press:`).
- All v0.7.2.1 gates retained.

### Verification

- Build: cmake --build green for qagame + cgame + ui + tvgame + mod_pk3.
- Diagnostic format string in qagame: present.
- Diagnostic ABSENT from cgame (`#ifndef CGAMEDLL` gate works): 0 hits.
- Idempotent loop pattern in source: present.
- Regression: v0.7.2.1 splash bypass helpers (2/2), Phase 8.0a NULL-guard
  reference, vg_Fun_GetInt + vg_Hitbox_IsActive + WG_Active +
  vg_perf_traces_total + mdx_hit_test all alive (5/5).

## v0.7.2.1 — Production Hotfix (2026-05-03)

> **Two v0.7.2 production-blocker bugs fixed.** Both surfaced
> within 30 minutes of v0.7.2 release via wahke's live-test.
> Single hotfix release; combined diff ~80 LoC across 3 source
> files. Phase 8.0a NULL-guard untouched; cup-orthodox preserved.

### Bug 1 — Splash damage restored (CRITICAL)

- fix(combat): grenades, Panzerfaust, rifle-grenades, dynamite,
  satchel, mortars, airstrike, landmines now apply damage
  correctly. v0.7.2 strict-mode (`vanguard_hitbox_strict 1`)
  rejected splash-damage AABB-only hits because the explosion
  origin sits outside the player capsule volume.
- New helper `vg_Hitbox_IsSplashMod` in `g_vanguard.c` covers
  16 explosion MODs (`MOD_GRENADE`, `_LAUNCHER`, `_PINEAPPLE`,
  `MOD_PANZERFAUST`, `MOD_BAZOOKA`, `MOD_DYNAMITE`,
  `MOD_AIRSTRIKE`, `MOD_EXPLOSIVE`, `MOD_GPG40`, `MOD_M7`,
  `MOD_LANDMINE`, `MOD_SATCHEL`, `MOD_MORTAR`, `MOD_MORTAR2`,
  `MOD_MAPMORTAR`, `MOD_MAPMORTAR_SPLASH`). `MOD_FLAMETHROWER`
  + `MOD_SMOKEGRENADE` deliberately omitted (non-splash).
- New helper `vg_Hitbox_IsBypassMod` combines self-damage
  (Phase 8.0a) + splash (Phase 13). Future MOD-class bypasses
  extend this single predicate.
- Strict-rejection at `g_combat.c:2040` now bypasses for
  `IsBypassMod` MODs — falls through to the multiplier path
  with `dmg_default` (1.0x) for `IMPACTPOINT_UNUSED`, matching
  upstream uniform-splash semantics.

### Bug 2 — Double-jump now triggers (HIGH)

- fix(vg_fun): `vg_Fun_RegisterCvar` now always adds
  `CVAR_SERVERINFO` flag. v0.7.2 double-jump silently failed
  because cgame's local cvar pool returned empty values for
  `vg_fun_doublejump_*` (registered only server-side); shared
  `bg_pmove.c::vg_pm_cvar_int` helper reads via
  `trap_Cvar_VariableStringBuffer` got empty string →
  `atoi("")` = 0 → eligibility check failed.
- `CVAR_SERVERINFO` makes the engine push values to all clients
  via configstring; cgame's cvar pool now sees the
  authoritative server value. Cup-orthodox preserved (flag
  does not change values, only propagation).
- Affected cvars (newly carry CVAR_SERVERINFO):
  - `vg_fun_falldmg_*` (5 cvars, v0.7.1)
  - `vg_fun_doublejump_*` (4 cvars, v0.7.2 — the bug-trigger)
  - All future `vg_fun_*` registered via `vg_Fun_RegisterCvar`

### Phase 6/7/8 regression-safety

- **Phase 8.0a NULL-guard** at `g_combat.c:1781-1784` UNTOUCHED
  (static grep verified)
- **Multi-region direct-hit damage** unchanged (only the
  strict-mode reject branch is bypassed; multiplier path
  identical)
- **Strict-mode** still rejects non-bypass AABB-only hits
  exactly as before
- **Cup-orthodox** preserved at default `vg_fun=0` (master
  gate short-circuits regardless of cgame cvar reads)

### CI

- New gate: splash-bypass helpers presence (`vg_Hitbox_IsSplashMod`
  + `vg_Hitbox_IsBypassMod` symbols in qagame.so, ≥2)
- New gate: `CVAR_SERVERINFO` OR-in in `vg_Fun_RegisterCvar`
  (static-source check)

### Verification (local)

- ✓ Build green for qagame + cgame + ui + tvgame + mod_pk3
- ✓ CI gate sims: 2 splash helpers ✓, SERVERINFO OR-in line ✓
- ✓ Phase 8.0a NULL-guard intact (4-clause guard via grep)
- ✓ `BYPASS` + existing `reject` strings both in binary
  (both diagnostic paths compiled)
- ✓ Regression: vg_Fun_GetInt + vg_Hitbox_IsActive + WG_Active
  + vg_perf_traces_total + mdx_hit_test all alive in qagame.so

### Reference

- Audit: `docs/notes/PHASE_13_PRODUCTION_HOTFIX.md`
- Discovery: 2026-05-03 v0.7.2 live-test (wahke)
- Bundled fixes per Memory #4 four-digit hotfix pattern

## v0.7.2 — Double-Jump (2026-05-03)

> **Second vg_fun-controlled feature.** Players can jump a
> second time while airborne when `vg_fun=1` AND
> `vg_fun_doublejump=1`. Default behaviour matches engine
> ground-jump; admins tune via 4 cvars (height / class-mask /
> stamina cost). At default `vg_fun=0` (cup-orthodox) double-jump
> is disabled entirely — byte-identical to upstream.
>
> Memory #8's vg_fun feature roadmap progresses: Falldamage
> (v0.7.1) and Double-Jump (v0.7.2) both shipped; xp-save,
> fast-reload, class-modifiers remain on the backlog.

### vg_fun feature: Double-Jump

- feat(movement): `vg_fun_doublejump` master toggle + 3 sub-cvars
  enable a second mid-air jump. Cup mode (vg_fun=0) preserves
  engine single-jump behaviour.
- 4 new cvars (all CVAR_ARCHIVE):
  - `vg_fun_doublejump` (default `0`) — master toggle
  - `vg_fun_doublejump_height` (default `270` = engine
    `JUMP_VELOCITY`) — second-jump vertical velocity
  - `vg_fun_doublejump_classes` (default `0` = all classes) —
    bitmask: 1=soldier, 2=medic, 4=engineer, 8=fieldops,
    16=covertops
  - `vg_fun_doublejump_stamina` (default `0` = no cost) —
    sprint-bar drain per double-jump (units of 100 on
    STAT_SPRINTTIME's 0..20000 scale)

### Implementation

- New `PMF_VG_DOUBLEJUMPED = 128` flag in `pm_flags` (bit 7,
  free in upstream layout — sits between `PMF_TIME_KNOCKBACK`
  bit 6 and `PMF_TIME_WATERJUMP` bit 8). Tracks
  "second-jump-used-this-airtime" state.
- `PM_CheckJump` (`bg_pmove.c:822`) modified in-place:
  eligibility check at top, gates the existing 850ms
  `PM_JUMP_DELAY` anti-bunnyhop on `!canDoubleJump`, executes
  the second jump with the configurable height + sets the
  flag + drains stamina.
- `PM_GroundTrace` (`bg_pmove.c:~2078`) clears
  `PMF_VG_DOUBLEJUMPED` at the landing instant
  (`groundEntityNum` NONE → real entity), restoring
  second-jump credit per airtime.
- New helper `vg_pm_cvar_int` in `bg_pmove.c` reads cvars via
  `trap_Cvar_VariableStringBuffer` (available in **both**
  cgame and qagame syscalls — `trap_Cvar_VariableIntegerValue`
  is qagame-only). Ensures cgame prediction agrees with
  qagame authoritative pmove.

### Cup-orthodox preservation

- `vg_fun=0` short-circuits all double-jump logic via the
  master-gate check; eligibility evaluation returns false
  immediately
- Engine's 850ms `PM_JUMP_DELAY` cooldown stays active in the
  standard branch (no bunnyhop regression)
- Phase 6 multi-region hitbox / Phase 7 strict-mode / Phase
  8.0a NULL-guard / Phase 8.0b Falldamage all untouched
  (movement layer is independent of damage / hitbox layers)
- WolfGuard imposes zero constraints — current
  `wg_interface_t` (v0.6.0) has no per-frame velocity-check
  hook (only init/shutdown/client_connect/disconnect/frame).
  Future protected builds with velocity-check hooks can
  whitelist via `pm_flags & PMF_VG_DOUBLEJUMPED`.

### CI

- New gate in `.github/workflows/ci.yml` build-linux job:
  `strings | grep -c "vg_fun_doublejump"` must return ≥ 4
  (catches accidental drop of any cvar registration).

### Verification (local)

- ✓ Build green for qagame + cgame + ui + tvgame + mod_pk3
  (with `-Werror=implicit-function-declaration` plus standard
  warnings — zero warnings emitted)
- ✓ CI gate sim: 4 cvar names found exactly
  (`vg_fun_doublejump`, `_height`, `_classes`, `_stamina`)
- ✓ Phase 8.0a NULL-guard at `g_combat.c:1781-1784` intact
  (static grep)
- ✓ Regression: vg_Fun_GetInt + vg_Hitbox_IsActive +
  WG_Active + vg_perf_traces_total + mdx_hit_test all alive
  in qagame.so

### Out of scope (deferred per audit §11)

- **Triple-jump** — Tier 2; defer indefinitely unless concrete
  demand surfaces
- **Per-class height** (e.g. light-class jumps higher) — Tier 2
- **Jump-pads / map entities** — Phase 13+ territory
- **Diagnostic cvar `vanguard_diag_doublejump`** — server-side
  hook needed in `g_active.c::ClientThink`; deferred to
  v0.7.2.x if cup-tester demand surfaces (cgame can't directly
  log because it's shared bg_pmove code)
- **WolfGuard whitelist flag** — current WG has no
  velocity-check; documented for v0.8.x

### Reference

- Audit: `docs/notes/PHASE_12_DOUBLEJUMP_RECON.md`

## v0.7.1.1 — Performance Hotfix (2026-05-03)

> **Production lag with 20 bots fixed.** Phase 10 perf audit
> identified `mdx_hit_test` as the dominant cost (~240µs/trace).
> This hotfix lands two LOW-risk cache layers + diagnostic
> infrastructure with **estimated 35-45% per-trace reduction**.
> Hit-detection is bit-identical to v0.7.1 — caches are
> state-only, no logic changes; all 15 capsule tests still
> execute (no early-exit).

### Hot-path optimizations
- perf(hitbox): **A1 — per-tick bone cache (per client)**.
  `mdx_calculate_bones` is deterministic for given animation
  state. Cache lives in `gclient_s.vgPerfBoneCache[]`,
  invalidated when `level.time` advances OR animation
  (`torsoFrame`/`legsFrame`) changes. Same client hit by N
  bullets in 1 server tick → bones computed once instead of
  N times. Saves ~25µs per repeat call.
- perf(hitbox): **Q1 — per-call tag cache** (within
  `mdx_hit_test`). Stack-allocated `vg_tag_cache_entry_t` array
  (max 32 entries, linear scan). Multiple capsules sharing the
  same anchor tag (e.g. NECK + CHEST sharing Bip01 Neck) →
  `mdx_tag_orientation` computed once instead of per-capsule.
  Biggest single bucket: ~125µs/trace → ~75µs/trace.

### Diagnostic
- New: **`vanguard_perf_stats` cvar** (CVAR_TEMP, default 0).
  When enabled, emits one summary line per second to the server
  console:
  ```
  VG_Perf: 47 traces in 1s, bone-cache hits/miss 32/15, tag-cache hit-rate 67% (180/270)
  ```
  Counts trace volume + per-cache hit/miss to verify A1+Q1 are
  paying off in real workloads. Rate-limited to 1 emission per
  second — never per-trace (Phase 7.0 lessons-learned: no log
  spam in hot paths).

### Phase 6/7/8 regression-safety
- **Caches are state-only** (no logic changes); hit-detection
  bit-identical to v0.7.1.
- **Phase 8.0a NULL-guard** at `g_combat.c:1781-1784` untouched —
  static grep confirms 4-clause guard intact.
- **Phase 6 multi-region** capsule tests still all execute (no
  early-exit on first hit; `best_frac` semantics preserved).
- **Cache fallback path:** if `mdx_bones_max` exceeds
  `VG_PERF_MAX_BONES` (96, larger than any plausible model),
  cache silently disables → unconditional recompute. No
  functional regression possible.
- **Edge cases handled:** non-PLAYER entities skip per-client
  cache (no `gclient_t`); `level.time == 0` (warmup) skips
  cache.

### CI
- New gate in `.github/workflows/ci.yml` build-linux job:
  `strings | grep -c "vg_perf|vgPerfBoneCache|vanguard_perf_stats|VG_Perf"`
  must return ≥3.

### Out of scope (deferred)
- **Q2 — refined AABB short-circuit** (audit §5 Q2): tighter-
  than-player AABB to reject line traces missing capsule union.
  Needs separate investigation; deferred to v0.8.0.
- **Q3 — capsule count reduction 15→11**: would regress Phase
  7.0.2 cup-tester fix; **NOT pursued**.
- **Profile-guided optimization (PGO)**: separate v0.8.0 work
  if needed.

### Verification (local)
- ✓ Build green for qagame + cgame + ui + tvgame + mod_pk3
- ✓ CI gate sim: 21 hits (≥3 required, PASS)
- ✓ Symbol presence: `vg_tag_orientation_cached`,
  `vg_perf_traces_total`, `vg_perf_bone_cache_hits/misses`,
  `vg_perf_tag_cache_hits/misses`, `vanguard_perf_stats`,
  `vgPerfBoneCache` (gclient_s field)
- ✓ Format string: `VG_Perf: %d traces in 1s, bone-cache
  hits/miss %d/%d, tag-cache hit-rate %d%% (%d/%d)` present
- ✓ Phase 8.0a NULL-guard intact (static grep)
- ✓ Regression: vg_Fun + vg_Hitbox + WG + vg_Netcode +
  `mdx_hit_test` + `mdx_calculate_bones` all alive

### Reference
- Audit: `docs/notes/PHASE_10_PERF_AUDIT.md`

## v0.7.1 — Falldamage Profile (2026-05-03)

> **First vg_fun-controlled feature.** Phase 8.0b Falldamage
> Redesign shipped on top of the v0.7.0 vg_fun foundation. At
> default `vg_fun=0` (cup-orthodox) falldamage is byte-identical
> to v0.7.0.1 — engine defaults preserved via the
> `vg_Fun_GetInt` helper short-circuit. At `vg_fun=1` admins can
> tune 5 cvars for public-server-friendly falls.

### vg_fun first feature
- feat(falldmg): falldamage values become vg_fun-controlled.
  Five sub-cvars expose engine-default damage + gib values:
  - `vg_fun_falldmg_dmg_10` (default `10`)
  - `vg_fun_falldmg_dmg_15` (default `15`)
  - `vg_fun_falldmg_dmg_25` (default `25`)
  - `vg_fun_falldmg_dmg_50` (default `50`)
  - `vg_fun_falldmg_gib_health` (default `-175`)
- feat(falldmg): per-event diagnostic log gated on
  `g_developer 1` AND `vg_fun 1`. Tuning workflow: admin drops
  from known heights, observes the `VG_Falldmg: event=X dmg=Y`
  log lines, adjusts cvar, repeats.

### Validation
- First production use of the `vg_Fun_GetInt` helper API from
  v0.7.0 foundation. Pattern validated end-to-end: cvar
  registration via `vg_Fun_RegisterCvar`, lookup via
  `vg_Fun_GetInt`, `vg_status` shows the 5 registered features.
- Cup-orthodox behaviour preserved — byte-identical falldamage
  at `vg_fun=0`.
- Public-profile recommended values: -20% damage scaling
  (8/12/20/40 vs 10/15/25/50), gib_health=-300 (aggressive
  gib-prevention).

### Hygiene — Triple-Header retrofit on bg_pmove.c
- chore(copyright): VanguardMod attribution added to
  `src/game/bg_pmove.c` (Triple-Header: id Software + ET:Legacy
  + VanguardMod + SPDX). v0.6.2 Copyright-Sweep deferred this
  file because git-log showed zero post-import modifications;
  Phase 8.0b's hook into PM_CrashLand context (the falldamage
  velocity-event chain) makes this the natural moment to retrofit
  per Memory #10 deferral plan. **No code changes** to
  `bg_pmove.c` itself in this release — pure header-only edit.

### Phase 8.0a regression-safety
- NULL-guard in `g_combat.c::G_Damage` (v0.5.2.2 hotfix)
  unchanged — gates on attacker / point / MOD only, doesn't
  read damage values. New cvar values can't reach the crash
  path. Static-grep verified the 4-clause guard is intact.

### CI
- New gate in `.github/workflows/ci.yml` build-linux job:
  `strings build/vanguard/qagame.mp.x86_64.so | grep -c
  "vg_fun_falldmg"` must return ≥5 (one per cvar).

### Out of scope (deferred)
- **Tier 2 cvars** (velocity thresholds — `delta_short`,
  `delta_10/15/25/50/die`, `kb_*` knockback durations). 10 cvars
  in `bg_pmove.c::PM_CrashLand`. Need configstring-sync for
  prediction-correctness; deferred to v0.7.x. Audit §4 covers
  the design.
- **Lag-spike z-velocity clamp** (Phase 7.3 audit §2.5). Classic
  ET resync-instant-fatal-fall bug. Deferred to v0.8.0 with
  netem testing infrastructure.
- **Class-based modifiers** — cup-orthodox forbids per Phase 7.3
  §3.4. Deferred indefinitely.

### Reference
- Audit: `docs/notes/PHASE_8_0B_AUDIT.md` (committed in v0.7.0
  PR; §13 architectural-pivot note records the rename
  `vanguard_falldmg_*` → `vg_fun_falldmg_*`)
- Helper API spec: `docs/VG_FUN_MODE.md` (Falldamage section
  added in this release)
- Cup-vs-public falldamage divergences:
  `docs/CUP_VS_PUBLIC.md` (Falldamage section added in this
  release)

## v0.7.0.1 — UI Cosmetic Updates (2026-05-03)

> Hotfix release. Two UI-cosmetic improvements bundled into a
> single PR — both rebuild the same `ui_mp_*.so/.dll`, both
> low-risk. **No gameplay changes, no server-side changes,
> no regressions to Phase 8.0a / WolfGuard / vg_fun /
> vg_Hitbox.** Falldamage Redesign (Phase 8.0b) stays as
> separate v0.7.1 — different testing window.

### Branding-2 — Mod-list color-codes

- feat(ui): add hardcoded mod-name lookup table for the Mods
  menu. All known ET mods (Jaymod, ETLegacy, NoQuarter, silEnT,
  ETPub, ETPro, CompET, Nitmod, xmod, etmain base game) now
  display in their respective brand colors. VanguardMod itself
  shows as `^8Vanguard^7Mod`.
- New: `src/ui/ui_vg_branding.{c,h}` — lookup table + helper API.
- New: `vanguard_diag_branding` cvar (CVAR_TEMP, default 0) for
  per-mod resolution debugging.
- Hook point: `src/ui/ui_main.c::UI_FeederItemText case
  FEEDER_MODS` — TIER 1 (lookup table) → TIER 2
  (`description.txt`) → TIER 3 (raw dir name) cascade.
- Default behaviour for unknown mods unchanged
  (description.txt → raw dir name).
- New: `docs/VG_BRANDING.md` — full spec, currently branded mods,
  contribution instructions, license attribution (Nitmod
  acknowledged for pattern inspiration; clean-room
  re-implementation).

### Discord-link update

- chore(ui): update VanguardMod Discord invite from
  `umnM8wVrth` to `GNSy7JTHV9` across both UI menu files
  that reference our community server:
  - `etmain/ui/main.menu:147` (main-menu Discord button URL)
  - `etmain/ui/credits_vanguardmod.menu:137` (credits-page
    Discord label)
- ETLegacy attribution Discord (`UBAZFys`) in
  `etmain/ui/etlegacy_discord.menu` is **untouched** — that's
  the upstream ETLegacy team's Discord, linked from their
  credits page as proper attribution.

### CI

- New gate in `.github/workflows/ci.yml` build-linux job:
  `strings build/vanguard/ui.mp.x86_64.so | grep -c
  "vg_mod_brands\|VG_Branding"` must return ≥2. Catches missing
  lookup-table linkage.

### Verification (local)

- ✓ Build green: Linux x86_64 (CI builds Win64 + Linux; Win32
  ships at release time)
- ✓ Symbol presence in `ui.mp.x86_64.so`: `VG_Branding_GetModDisplay`,
  `VG_Branding_PrintTable`, `vg_mod_brands`, `vanguard_diag_branding`
  + 7 string hits for the CI gate (≥2 required, PASS)
- ✓ Color-coded display strings present in binary
  (`^8Vanguard^7Mod`, `^3Jay^7mod`, `^7silEnT`, `^7Comp^1ET`)
- ✓ All 3 TIER log lines present (`VG_Brand: ... (table)`,
  `... (description.txt)`, `... (raw dir name)`)
- ✓ Discord URL `GNSy7JTHV9` present in pk3-staged
  `ui/main.menu`; old `umnM8wVrth` no longer in any Vanguard
  reference (one mention remains in the v0.5.x-v0.7.0 history
  comment at main.menu:147 — intentional for context)
- ✓ Regression: existing UI cvars still alive (`ui_brassTime`,
  `ui_drawCrosshair`, `UI_LoadMods` etc.); WolfGuard banner,
  vg_fun mode-line, vg_Hitbox, Phase 8.0a NULL-guard all
  untouched (UI module doesn't link against qagame).

### Reference

- Audit: `docs/notes/PHASE_BRANDING_2_AUDIT.md` (also committed
  as part of this PR)
- Earlier branding work: `docs/notes/PHASE_BRANDING_AUDIT.md`
  (the v0.5.2.4 description.txt deployment fix — superseded for
  multi-mod consistency by Branding 2)

## v0.7.0 — vg_fun Foundation (2026-05-03)

> Architectural pivot. v0.7.0 was originally scoped for Falldamage
> Redesign (Phase 8.0b). After review, scope changed to **vg_fun
> master-switch foundation first** — Falldamage becomes v0.7.1 as
> the first feature gated by vg_fun. Reasoning: building features
> as `vanguard_falldamage_*` cvars first and renaming to
> `vg_fun_falldmg_*` later would create tech debt.
>
> **v0.7.0 ships infrastructure with zero functional changes to
> gameplay.** Default `vg_fun=0` (cup-orthodox) preserves
> byte-identical behaviour for upgraders.

### Architecture
- feat(vg_fun): add `vg_fun` master-switch cvar (CVAR_LATCH | CVAR_ARCHIVE | CVAR_SERVERINFO, default `0`)
- feat(vg_fun): helper API `vg_Fun_GetInt` / `vg_Fun_GetFloat` enforces the cup-default lock when `vg_fun=0` (Strategy C from Phase 9.0 audit). All future `vg_fun_*` cvar reads must go through these helpers.
- feat(vg_fun): introspection registry powers `vg_status` server console command. v0.7.0 ships with the registry empty (foundation only); v0.7.1 Falldamage registers the first 5 sub-cvars.
- feat(vg_fun): mode-aware stats log-tagging at 5 sites (Kill, ClientConnect, ClientDisconnect, WeaponStats, InitGame) so future leaderboard endpoints can bucket events per mode (Memory #25).

### Banner
- Banner now shows current vg_fun mode line — `^5cup-orthodox` (cyan, vg_fun=0) or `^3fun-public` (yellow, vg_fun=1) — between Backend / Info and Build mode lines. Both community + protected variants updated.

### Server commands
- New: `vg_status` — prints vg_fun mode + registered features + cup-defaults. Mirrors the existing `wg_status` pattern.

### Documentation
- New: `docs/VG_FUN_MODE.md` — full vg_fun specification, mode-switching semantics, helper API contract, feature roadmap.
- Updated: `docs/CUP_VS_PUBLIC.md` — vg_fun master mode section above the existing netcode profile content.
- Committed: `docs/notes/PHASE_9_0_VG_FUN_FOUNDATION_AUDIT.md` (recon doc, was untracked).
- Committed: `docs/notes/PHASE_8_0B_AUDIT.md` (Falldamage recon, was untracked) with new §13 architectural-pivot note recording the Falldamage → v0.7.1 relocation. Tier 1 scope unchanged, just renamed: `vanguard_falldmg_*` → `vg_fun_falldmg_*`.

### Mode-switching semantics
- `vg_fun` is `CVAR_LATCH` — mode changes take effect at the next `map_restart`. Cup-integrity guarantee: a match cannot start cup-mode and silently switch mid-game.
- Sub-cvars (`vg_fun_*`, registered by future features) are `CVAR_ARCHIVE` (no LATCH) — admins can pre-stage tuned values for the next mode switch.

### CI
- New CI gate in `.github/workflows/ci.yml` build-linux job: `strings | grep -c "vg_fun\|VG_Fun"` must return ≥3 (covers cvar name, boot-line, status-command symbols).

### Foundation only — no functional changes
v0.7.0 ships infrastructure with **zero behavioural change** at default `vg_fun=0`. The first vg_fun-controlled feature (Falldamage, Phase 8.0b) ships in v0.7.1.

### Reference
- Audit: `docs/notes/PHASE_9_0_VG_FUN_FOUNDATION_AUDIT.md`
- Spec source: VanguardMod Memory #8 (vg_fun architecture) + #25 (multi-endpoint stats)
- Phase 8.0b cross-reference: `docs/notes/PHASE_8_0B_AUDIT.md` §13

## v0.6.2 — Copyright Attribution Sweep (2026-05-02)

### Hygiene

- chore(copyright): add VanguardMod attribution to **13** modified
  ETLegacy files (Triple-Header: id Software + ET:Legacy + VanguardMod
  + SPDX-License-Identifier). Per Memory #10 / Phase A recon.

### Files updated

- `src/game/`: g_main.c, g_client.c, g_svcmds.c, g_cvars.c
- `src/cgame/`: cg_weapons.c, cg_loadpanel.c, cg_cvars.c, cg_cvars.h,
  cg_servercmds.c, cg_view.c, cg_local.h
- `src/ui/`: ui_main.c
- `src/qcommon/`: common.c

### Pre-existing attributions preserved (not modified by this PR)

Phase A recon initially flagged 18 files (16 Triple + 2 Dual). On
deeper inspection (`head -80` instead of `head -30`), 5 files
already carry valid VanguardMod attribution from the v0.4.3 SPDX
sweep — just in a different format (a separate "Modifications for
VanguardMod" comment block following the GPL boilerplate, with
SPDX-FileCopyrightText / SPDX-License-Identifier lines and
descriptive paragraphs explaining what was modified).

Per wahke review (recon §8 Option α): these are legally correct as-is;
reformatting would not add legal value and would lose the descriptive
paragraphs. **Skipped:**

- `src/game/g_combat.c` — separate VG block at lines 32-46
- `src/game/bg_animgroup.c` — same pattern
- `src/game/bg_public.h` — same pattern
- `src/game/g_mdx.c` — separate VG block (lines 31-44), no ETLegacy
  boilerplate (3rd-party Christopher Lais zlib-license header)
- `src/game/g_mdx.h` — same pattern (lines 29-41)

### Deferred

- `src/game/bg_pmove.c` — currently unmodified post-import per Phase A
  recon (`git log de8ab0a..HEAD -- src/game/bg_pmove.c` returns no
  commits). VanguardMod attribution will be added when Phase 7.3
  falldamage redesign first modifies the file (v0.7.0).

### Reference

- Audit: `docs/notes/PHASE_COPYRIGHT_SWEEP_RECON.md` (§9 execution log)

## v0.6.1 — Phase 7.3 Cup-Movement Foundation (2026-05-02)

### Configuration changes
- chore(config): bump `g_pronedelay` from 0 → 1 in defaultpublic.config
  (ETPro Cup orthodoxy: TOGGLE bit, 1750ms unprone lock). See
  `docs/CUP_VS_PUBLIC.md` for rationale.

### New files
- `docs/CUP_VS_PUBLIC.md` extended with Phase 7.3 sections: ETLegacy
  issue #1637 caveats for `sv_fps 40` (cv-ops disguise / flamer /
  script_movers / pause), and the new Movement / `g_pronedelay`
  divergence section. Existing Phase 7.2 netcode coverage preserved.

### Foundation
- feat(diag): register `vanguard_diag_movement` cvar (no-op stub,
  CVAR_TEMP, default 0). Foundation for v0.7.x movement diagnostics.
  Reads no-op for now; v0.7.x will gate diagnostic emission on this cvar.

### Reference
- Phase 7.3 audit: `docs/notes/PHASE_7_3_AUDIT.md`

## v0.6.0 — WolfGuard Foundation (2026-05-02)

### Features
- feat(wolfguard): add stub interface, startup banner, and dual-mode build support
- feat(wolfguard): add `wg_status` admin command
- feat(ci): add community-build sanity gate (banner string presence check)

### Build
- New CMake option `FEATURE_WOLFGUARD` (default OFF) to switch between community and protected builds
- Community builds compile and run identically — no functional difference yet, foundation only

## v0.5.2.4 — 2026-05-02 — Release-pipeline hotfix: ship description.txt in ZIPs

> **Deployment hotfix, no code changes.** v0.5.2.3 correctly built
> `description.txt` next to the pk3 in `build/vanguard/` (per the
> `cmake/ETLBuildMod.cmake` staging clause), but the GitHub Actions
> release workflow's ZIP-assembly steps only copied the pk3 + omni-bot
> + legal files into the staging directory before zipping. The brand
> string never made it onto end users' disks, forcing wahke to copy
> `description.txt` by hand on the test PC just to see
> `^8Vanguard^7Mod` in the engine's mod selector.

### What changed

  - `.github/workflows/release.yml` — both `Assemble server ZIP` and
    `Assemble client ZIP` steps now `cp build/vanguard/description.txt
    "${STAGING}/vanguard/"` before invoking `zip`. Comment block
    cross-references the cmake staging clause and the
    `FS_GetModList` loose-file constraint (qcommon/files.c:3413,
    documented in v0.5.2.3 release notes) so future maintainers
    don't accidentally drop the file on a re-sync.

### What did NOT change

  - **NECK capsule** — already shipped correctly in v0.5.2.3.
    `etmain/animations/human_base.hit:175` carries
    `HIT body _vg_neck radius 4 impactpoint chest` and
    `src/cgame/cg_vanguard_dev.c:171` carries the matching
    `vg_hit_areas[]` entry. Verified by extracting
    `vanguard-v0.5.2.3-client.zip` from the published GitHub release
    and grepping the bundled `vanguard_v0.5.2.3.pk3` for `_vg_neck` —
    line 125 (TAG bridge) and line 175 (HIT block) are both present.
    If a test PC still misses the NECK sphere visually under
    `g_debugHitboxes 1`, the cause is local pak loading (an older
    `vanguard_v0.5.2.x.pk3` shadowing the new one) — see
    "Verifying the deployed pk3" below. **No source change in
    v0.5.2.4.**
  - **HEAD radius 7** — still `radius 7` on line 162 of the shipped
    `human_base.hit`. Verified the same way.
  - **ARM cylinders** — TAG bridges + 4 HIT blocks all present in the
    shipped pk3.

### Verifying the deployed pk3 on a test PC

If `g_debugHitboxes 1` shows HEAD/CHEST/GUT/SHOULDER but no NECK
sphere — and the running build is supposedly v0.5.2.4+ — the local
pk3 is likely stale or shadowed:

  1. `dir/vanguard/` — confirm only ONE `vanguard_v*.pk3` is present.
     If both `vanguard_v0.5.2.2.pk3` and `vanguard_v0.5.2.4.pk3`
     coexist, the engine loads both and a future filename collision
     (e.g. a hand-rolled `vanguard.pk3`) could shadow the new one.
  2. In-game: `\fs_referencedPakNames` lists every pk3 the renderer
     loaded. The newest `vanguard_v*` should appear; if it does not,
     the file is in the wrong directory.
  3. Server-side: `\sv_referencedPakNames` mirrors what the server
     pushed to the client.
  4. As a one-shot sanity check, `unzip -p vanguard/vanguard_v*.pk3
     animations/human_base.hit | grep _vg_neck` from the OS shell
     should print at least three lines (TAG + HIT + the spine box's
     bone reference).

### Why this is a single-purpose release

Phase 7.0 lesson #3 (`docs/notes/PHASE_7_0_2_AUDIT.md` and
`PHASE_7_3_AUDIT.md` §7): crash-bugs vs. tuning-bugs vs. deployment
bugs each ship in their own release. v0.5.2.4 is *only* the
deployment fix. No source code is touched, no behaviour changes,
no live-test required beyond confirming the staged
`description.txt` actually appears in the published ZIPs.

### Verification (release-time)

Once tagged + pushed, the workflow run for `v0.5.2.4` should produce:

  - `vanguard-v0.5.2.4-client.zip` containing
    `vanguard/description.txt` + `vanguard/vanguard_v0.5.2.4.pk3`.
  - `vanguard-v0.5.2.4-server.zip` containing the same plus all
    .so/.dll modules and the omni-bot runtime.

Quick check after download:
`unzip -l vanguard-v0.5.2.4-client.zip | grep description.txt` →
expects exactly one line. Same on the server zip.

## v0.5.2.3 — 2026-04-30 — Branding + hitbox quick-wins (HEAD r=7, NECK gap, ARM cylinders)

> Bundled release: one branding fix and three hitbox geometry
> tunes from the `PHASE_7_0_2_AUDIT.md` quick-win list. Shipped
> together because the head/shoulder/arm geometry will likely
> get another tuning pass after the next cup-tester session
> anyway — one release now beats three serial micro-releases.
> Acknowledged risk: if cup feedback in 1-2 weeks says "HEAD
> too easy" or flags a coverage gap between the new arm
> cylinders and the existing shoulder cylinder, a v0.5.2.4 /
> v0.5.3 tuning release will follow.

### What changed

  - **Branding (Item A)** — `misc/description.txt` now reads
    `^8Vanguard^7Mod` (16 bytes incl. LF, well under the
    48-byte engine cap). Color codes: `^8` = cyan/turquoise,
    `^7` = white. Replaces the placeholder
    `^1ET^7: LEGACY^1 -^7 legacy mod` carried over from the
    upstream `etmain/description.txt`.
    `cmake/ETLBuildMod.cmake` extended to stage the file via
    `copy_if_different` next to the .pk3 (loose, NOT inside —
    see comment block at line 360-364: `FS_GetModList` reads
    the brand string with `FS_SV_FOpenFileRead`, never opens
    pk3s for it). `scripts/testserver/run.sh` also copies it
    into `$HOMEPATH/vanguard/` as a safety net for local tests
    (fs_homepath wins the search order).
  - **NECK gap fix (Item B, audit §G.4b)** —
    `etmain/animations/human_base.hit` adds
    `HIT body _vg_neck radius 4 impactpoint chest`. Closes the
    ~4-5 unit no-damage band at the throat between the HEAD
    sphere bottom (post-G.1: ~+3.85 above neck-bone) and the
    CHEST box top (anchored AT the neck bone). Tagged as
    `chest` impactpoint — neck shots take chest damage, no
    headshot multiplier for a throat hit.
  - **HEAD radius 6 → 7 (Item C, audit §G.1)** — `human_base.hit`
    HEAD line bumped from `radius 6` to `radius 7`. A cup tester
    reported headshots "almost impossible" on
    v0.5.2.1; the visible helmet half-width on the soldier
    mesh is ~7-8 units, so r=6 left a ~1-2 unit lateral gap
    on each side. r=7 covers the typical helmet width while
    staying anatomically inside the visible head model.
  - **ARM cylinders (Item D, audit §G.5)** — four new HIT
    blocks in `human_base.hit`: cylinders for L+R Upper Arm
    (UpperArm → Forearm) and L+R Forearm + Hand (Forearm →
    Hand). Each radius 4, matching the visible bicep / lower-
    arm half-width. All four reuse `IMPACTPOINT_SHOULDER_LEFT`
    or `_RIGHT` so the existing 0.8x limb-damage multiplier
    applies — entire arm clavicle-to-fingers is one continuous
    region for damage purposes. Without these, upper-arm /
    forearm shots fell through to the AABB broadphase and
    rejected under strict-hitbox mode (v0.5.1+). Adds 4 new
    `TAG _vg_{u,f}arm_{l,r}` and `TAG _vg_hand_{l,r}` bridges
    to satisfy the parser's tag→bone lookup path.
  - **cgame mirror (`src/cgame/cg_vanguard_dev.c`)** — the
    `vg_hit_areas[]` table mirrors all three geometry changes
    (HEAD radius 6 → 7, new NECK sphere, four new ARM
    cylinders) so the `g_debugHitboxes 1` wireframe overlay
    matches the server's hit volumes. Hit-area count grew
    from 10 to 15.

### Verification (local)

  - **Build** — green on all three platforms; multi-platform
    `.pk3` bundles all 7 native modules (Linux x64 .so x3 +
    Win64 .dll x4 + Win32 .dll x4 = 11 binaries) plus the
    updated `animations/human_base.hit` asset (10363 bytes).
  - **Symbol gates** — all Phase 8.0 + multi-region symbols
    present on Linux x64 / Win64 / Win32:
    `vg_Hitbox_IsActive`, `vg_Hitbox_IsSelfDamageMod` (qagame),
    `CG_VanguardDev_DrawHitboxes`, `vg_DrawPlayerMultibox`,
    `vg_hit_areas`, plus the three `cg_vanguardDev*` cvars
    (cgame). Omni-bot symbol count = 12 in qagame — same as
    v0.5.2.2 (no regression from the FEATURE_OMNIBOT build
    flag fix in v0.5.2-rc2).
  - **Branding staging** — `build/vanguard/description.txt`
    contains `^8Vanguard^7Mod`, sits next to
    `vanguard_v0.5.2.3.pk3`, and is correctly NOT inside the
    .pk3 (verified via `unzip -l` — `FS_GetModList` reads it
    loose).
  - **Phase 8.0 regression check (static)** — the multi-region
    branch entry gate at `g_combat.c:1784` still requires
    `attacker && attacker->client && point &&
    !vg_Hitbox_IsSelfDamageMod(mod)`. None of the v0.5.2.3
    changes touched the entry gate, so the SIGSEGV-on-fall
    fix from v0.5.2.2 is preserved.

### Live-test plan (wahke runs after deploy)

  1. **Headshot direct on helmet centre** → impactpoint=1
     (head), hit registers (was reliable on v0.5.2.x; verifies
     no regression from the radius bump).
  2. **Shot at neck/collar** → impactpoint=2 (chest), hit
     registers. Was an AABB-broadphase reject under strict
     mode in v0.5.2.x.
  3. **Shot at upper arm (bicep)** → impactpoint=8
     (shoulder_left/right), hit registers. Same — was
     rejecting before.
  4. **Shot at forearm / hand** → impactpoint=8, hit registers.
  5. **Regression — chest, shoulder, gut, knees, calves** →
     all previously-working hits unchanged.
  6. **Falldamage / suicide / drowning** → no crash, server
     stays alive (Phase 8.0 NULL-guard regression).

### Known risks (acknowledged)

  - **Cup-balance** — the HEAD radius bump (6→7) makes
    headshots ~17% more likely by lateral cross-section
    (π·7² / π·6² ≈ 1.36, but only the lateral edge gap
    closes — the actual hit-rate increase is closer to the
    ~1-2 unit gap on each side, ~5-15% in practice). If cup
    feedback says "too easy", a v0.5.2.4 / v0.5.3 will tune
    back toward 6.5.
  - **ARM coverage seam** — the new UpperArm cylinder starts
    at the UpperArm bone (where the existing Shoulder
    cylinder ends). If the visible mesh has a small gap
    between bone-anchored cylinders at the shoulder/upper-arm
    joint, shots at that exact seam may still reject. Cup
    feedback will tell.

### What's NOT in this release (deferred)

  - 18-region empirical hit-rate test — needs cup-tester
    session; likely v0.5.3.
  - Cross-mod hitbox comparison (NoQuarter / Silent / ETPro)
    — WebSearch blocker; deferred.
  - Falldamage redesign — Phase 7.3 movement work.
  - Phase 7.1 hit-region sounds (groinhit.wav etc.) —
    separate phase.
  - `VG_DIAG_DUMP` label update — cosmetic backlog.

## v0.5.2.2 — 2026-04-30 — Phase 8.0 emergency hot-fix: SIGSEGV on self-damage

> **Critical hot-fix.** Pterodactyl recorded a server crash
> (signal 11 — SIGSEGV) on a `MOD_FALLING` damage event. Root
> cause: the v0.5.1 strict-hitbox fix made the multi-region
> branch in `G_Damage` reachable for ANY damage call (not just
> bullets), and several call sites — `g_active.c:170/174/191/1014`
> for `MOD_SLIME` / `MOD_WATER` / `MOD_LAVA` / `MOD_FALLING` plus
> a dozen `g_props.c` / `g_mover.c` sites for `MOD_CRUSH` —
> pass `point=NULL` to `G_Damage`. The branch handed the NULL
> pointer to `mdx_hit_test → mdx_hit_warp` which dereferenced it.
>
> Defense-in-depth fix: the multi-region branch now requires a
> real `attacker->client`, a non-NULL `point`, and a
> non-self-damage `mod`. Either of the first two would prevent
> the crash; the MOD blacklist documents intent and catches edge
> cases. Self-damage falls through to the legacy chain
> (vanilla-correct, never crashed).

### What changed

  - `src/game/g_combat.c`: multi-region branch entry gate
    extended from `vg_Hitbox_IsActive() && targ->client &&
    targ->health > 0` to add `attacker && attacker->client &&
    point && !vg_Hitbox_IsSelfDamageMod(mod)`. Comment block
    updated with the Phase 8.0 rationale.
  - `src/game/g_vanguard.c` + `g_vanguard.h`: new
    `vg_Hitbox_IsSelfDamageMod(mod)` predicate. Returns qtrue
    for `MOD_WATER`, `MOD_SLIME`, `MOD_LAVA`, `MOD_CRUSH`,
    `MOD_TELEFRAG`, `MOD_FALLING`, `MOD_SUICIDE`,
    `MOD_TRIGGER_HURT`, `MOD_CRUSH_CONSTRUCTION{,DEATH,_NOATTACKER}`.

### Why both NULL-checks AND MOD blacklist?

The NULL-pointer checks are the actual crash prevention — the
branch can't fire without them. The MOD blacklist is correctness:
even if a future call site somehow passed a non-NULL placeholder
`point` for a `MOD_FALLING` event, multi-region capsule semantics
("which body part got hit at this trace endpoint?") have no
meaning for a fall. Falling damage just deducts HP; there's no
trace, no aim, no shooter. The blacklist documents this and
prevents accidental future regressions.

### Verification

Local validation on a dedicated Linux test server:

  - Used the existing `die <name>` rcon command, which calls
    `G_Damage(victim, NULL, NULL, NULL, NULL, health, 0,
    MOD_UNKNOWN)` — exact same NULL-point pattern as the
    `MOD_FALLING` crash.
  - Issued `die BotA1`, `die BotX1`, then `die -1` (kills all
    players in one frame).
  - Result: `<world> killed BotA1 by MOD_UNKNOWN` Kill: events
    in the log; **server stayed alive throughout**; zero SIGSEGV.
  - Plus a separate 180s combat-only run with 8 bots showed
    bullet damage still going through the multi-region branch
    correctly (multiple `MOD_MP40` / `MOD_THOMPSON` / `MOD_KAR98`
    / `MOD_STEN` Kill: events).

### Live-test plan (4 tests)

  1. Falldamage minor (32-64 unit fall) → player takes damage,
     no crash.
  2. Falldamage lethal (>200 unit fall) → player dies, body
     gibs (gibs is fine, separate concern), server stays alive.
  3. Multi-player simultaneous falldamage (2-3 players jump
     off a cliff together) → no crash.
  4. Regression — repeat the v0.5.2.1 6-test path. Multi-region
     hit-detection unchanged for normal bullets.

If 4 of 4 green, tag v0.5.2.2.

### What stays unchanged

  - Stage 3 cgame `MatrixWeight` torso-axis fix (v0.5.2)
    preserved — independently correct.
  - HEAD offset 6.5 (v0.5.2.1 revert) preserved — was always
    correct, only the visualisation was wrong.
  - VG_DIAG_DUMP block + manual `vanguard_diag_dump` cvar +
    bone-axis world-frame output (v0.5.2.1) preserved — off
    by default, no production cost.
  - Strict-hitbox semantics from v0.5.1 unchanged for bullet
    damage.

## v0.5.2.1 — 2026-04-30 — Revert HEAD offset, keep cgame fix, expand diagnostic

> **Hot-fix release.** v0.5.2 was tagged but never deployed: the
> 6-test live-test path failed because the v0.5.2 Stage 2a HEAD
> offset retune (6.5 → 2.0) overshot in the wrong direction.
> Live-test screenshots showed the head sphere sitting at neck/
> shoulder level instead of on the visible helmet. Root cause:
> the v0.5.2 retune assumed the original Phase 7.0.1
> "lateral-versetzt" bug had a vertical-position component;
> in fact it was 100% the cgame `MatrixWeight` skip (Stage 3
> in v0.5.2). With Stage 3 fixed, the original `+6.5` magnitude
> was already correct — the perceived displacement was the
> broken visualisation drifting away from the (correctly-placed)
> server-side capsule. The magnitude reduction wasn't needed.
>
> v0.5.2.1 reverts the HEAD offset back to 6.5 in both
> `human_base.hit` and `cg_vanguard_dev.c`. Stage 3 (cgame
> `MatrixWeight`) and the diagnostic infrastructure stay —
> those were independently correct.

### Changes

  - `etmain/animations/human_base.hit`: `_vg_head` offset
    `2.0 0 0` → `6.5 0 0`. Comment block updated to document
    the v0.4.2-v0.5.1-v0.5.2-v0.5.2.1 magnitude history and
    why the v0.5.2 retune was wrong.
  - `src/cgame/cg_vanguard_dev.c`: `vg_hit_areas[]` HEAD entry
    `{ 2.0f, 0, 0 }` → `{ 6.5f, 0, 0 }`. Same comment update.
  - `src/game/g_combat.c`: `VG_DIAG_DUMP` block extended with
    three new `head_bone.axis[k] (bone-local +X/+Y/+Z)` lines
    showing the head bone's WORLD-frame axis matrix. Makes any
    future offset tuning data-driven: bone-local +X is verified
    to be world-up (~96-97% projection in test data); +Y/+Z
    span the horizontal plane and rotate with the head's yaw —
    so offsets in those axes shift forward/lateral relative to
    the head's facing direction, not the player's. Useful for
    e.g. shifting the capsule centre forward of the bone if a
    future mesh measurement shows skull-centre is forward of
    Bip01 Head.

### Why the v0.5.2 retune was wrong

Phase 7.0.1's original bug report described capsules as
"systematisch LATERAL versetzt" (sideways, not vertical). That
was the cgame `MatrixWeight` skip — `cg_vanguard_mdx.c::vg_mdx_compute_bone_axis_local`
omitted the torso-axis blend that `mdx_bone_orientation` does
on the server. With the skip, the cgame wireframe rendered the
capsule in the wrong frame whenever `torsoAxis ≠ legsAxis`
(strafe-jumps, crouch-moves, head-turns), making it look
laterally displaced even when the actual hit position was
correct.

v0.5.2's Stage 3 fixed that — the wireframe now renders at the
true capsule position. Once Stage 3 was in, Stage 2a's
`6.5 → 2.0` magnitude change was based on an incorrect
inference: that `delta head-neck Z = 10.85` was anatomically
"too high". In fact `+10.85` was the correct face/forehead-zone
position; the radius-6 sphere centred there spans chin (+4.85)
to top of helmet (+16.85), covering the visible head perfectly.
v0.5.2's `+6.35` (with offset 2.0) shifted the sphere centre
down to chin level, leaving the sphere covering upper-neck to
eye-level — missing the upper half of the head, exactly what
the live-test screenshots showed.

### Verification

Local rebuild + diagnostic test on a dedicated Linux server:

  - `human_base.hit` shipped in pk3: `offset 6.5 0 0` ✓
  - 4 of 4 manual `vanguard_diag_dump 1` triggers fired
    correctly across active combat
  - `delta head-neck Z` measurements: 9.40, 9.49, 9.72, 10.78
    (previously ~10.85; small variance from animation pose
    relative to bone-rest pose)
  - `head_bone.axis[0]` (bone-local +X) consistently projects
    ~(small_x, small_y, +0.96-0.97) onto world — confirms the
    +X axis is the up-the-skull / vertical direction across all
    sampled poses, validating the v0.4.2 axis decision.
  - `head_bone.axis[1]` (bone-local +Y) tracks the head's
    looking-forward direction (matches `refent.axis[0]` when
    head and body align; differs by head-yaw when looking
    around).

### Live-test path (unchanged from v0.5.2)

  1. HEAD on visible nose → expect HIT impactpoint=1
  2. HEAD-side displacement (8 units lateral) → expect REJECT
  3. CHEST → expect HIT impactpoint=2
  4. SHOULDER → expect HIT impactpoint=5/6
  5. Pose variation (crouch) → tests 1+3+4 still hit
  6. `vanguard_hitbox_strict 0` → falls through to legacy

If 6/6 green, tag v0.5.2.1. If the head sphere is still off, the
`head_bone.axis[k]` lines in the dump now show exactly which
world direction each bone-local axis maps to — pin down the
required offset combo precisely.

## v0.5.2 — 2026-04-30 — Phase 7.0.1 capsule alignment (UNTAGGED, superseded by v0.5.2.1)

> **Production release.** Phase 7.0.1 capsule-offset bug closed.
> The HEAD-region sphere now sits on the visible face/eye-level
> zone instead of floating above the skull. The cgame wireframe
> visualisation (`g_debugHitboxes 1`) tracks the actual server-
> side hit geometry across all body poses including strafe-jump
> and crouch-move. Strict-hitbox semantics from v0.5.1 unchanged
> — shots that miss every capsule still get rejected.
>
> Carries forward the v0.5.2-rc3 diagnostic infrastructure
> (`VG_DIAG_DUMP`, `vanguard_diag_dump` cvar) so future Phase
> 7.0.x recons can reuse the same toolchain. Both are off by
> default — no production impact.

### Stage 2a — HEAD offset retuned from measured data

The v0.4.2 `_vg_head` offset of `6.5 0 0` (in bone-local +X, the
"up the skull" axis for `Bip01 Head`) placed the radius-6 head
sphere's centre at the top of the cranium instead of face level
— shots aimed at the visible nose missed the sphere entirely.
v0.5.2-rc3's multi-pose `VG_DIAG_DUMP` measurements quantified
the issue: across 5 samples (3 idle + 2 crouch), `delta head-
neck Z = 10.85` was constant regardless of pose, because the
head bone's projection onto world-Z stays vertical across
animations.

With base bone-distance Bip01 Neck → Bip01 Head ≈ 4.35 units
(measured: 10.85 - 6.5 with full Z-projection), the offset to
land the sphere centre at the anatomical face/eye-level target
of `+6` over neck is `6.0 - 4.35 = 1.65`. Rounded to a clean
**`offset 2.0 0 0`** for `~+6.35` over neck — the radius-6
sphere then spans neck-base to top-of-head.

**Live-test verification** on a dedicated Linux test server,
multi-pose:

  - dump #1 (active combat, torsoBacklerp=0.108):
    `delta head-neck Z = 6.45` ✅
  - dump #2 (animation transition, torsoBacklerp=-0.359):
    `delta head-neck Z = 3.35`, `|delta| = 5.80` (pose-
    distorted, expected during fast turn/look)
  - dump #3 (active combat, torsoBacklerp=0.632):
    `delta head-neck Z = 6.21` ✅

Anatomical target (delta_z ∈ [5, 7]) hit on stable poses;
animation-transition edge cases vary but |delta| stays in the
6-unit ballpark.

### Stage 3 — cgame `MatrixWeight` torso-axis fix

The cgame visualisation function `vg_mdx_compute_bone_axis_local`
in `cg_vanguard_mdx.c` was skipping the `MatrixWeight(torsoAxis,
torso_weight)` step that qagame's `mdx_bone_orientation` performs
on every bone with `torso_weight > 0` (spine, neck, head,
clavicles, arms). The skip was correct in v0.4.1 — `refent->
torsoAxis` was set manually to the player's WORLD-frame rotation
back then, so blending it in would have produced a matrix in
the wrong reference frame. Since v0.4.3 cgame reads
`cent->pe.bodyRefEnt` directly, where `torsoAxis` is set by
`CG_PlayerAngles` in the same MODEL-frame qagame uses — the
v0.4.1 rationale stopped applying but the omission stayed.

Visible symptom (live-test screenshots): pose-lag of the
wireframe overlay against the rendered mesh during strafe-jump
and crouch-move — capsules drifted away from the model
whenever `torsoAxis ≠ legsAxis`.

**Fix.** Direct port of `mdx_bone_orientation`'s torso-mix path:
new static helper `vg_mdx_MatrixWeight` (8 lines, verbatim from
`g_mdx.c:178-193`) plus a 5-line update at the end of
`vg_mdx_compute_bone_axis_local` to compose the weighted torso
rotation into the bone basis. Bones with `torso_weight = 0`
(legs/pelvis) are unchanged because `MatrixWeight` with weight=0
is identity.

### Diagnostic infrastructure carried forward

The `VG_DIAG_DUMP` block and `vanguard_diag_dump` cvar from
rc3 stay in qagame for future Phase 7.0.x recons. Cvar-gated
on `vanguard_hitbox_debug 1` (default 0) plus the explicit
manual trigger; off by default, no production cost.

### Files touched

| File | Change |
|------|--------|
| `etmain/animations/human_base.hit` | `_vg_head` offset 6.5 → 2.0; comment block updated |
| `src/cgame/cg_vanguard_dev.c` | `vg_hit_areas[]` HEAD offset 6.5 → 2.0; comment block updated |
| `src/cgame/cg_vanguard_mdx.c` | new `vg_mdx_MatrixWeight` helper; `vg_mdx_compute_bone_axis_local` torso-mix step added |

### Live-test path (6 tests from `docs/notes/PHASE_7_0_1_AUDIT.md` §H)

  1. HEAD on visible nose → expect HIT impactpoint=1
  2. HEAD-side displacement (8 units lateral) → expect REJECT
  3. CHEST → expect HIT impactpoint=2
  4. SHOULDER → expect HIT impactpoint=5/6
  5. Pose variation (crouch) → tests 1+3+4 still hit
  6. `vanguard_hitbox_strict 0` → falls through to legacy

Pass criteria: 5 of 6 produce hits at the visible mesh, test 2
rejects, test 6 falls through.

### Known limitations

  - **Capsule coverage gaps deferred to v0.5.3.** The Phase 6
    `human_base.hit` capsule placement may still leave dead
    zones (Hals/Kragen, knee/groin border) — those measurements
    will use this rc3 diagnostic toolchain in a separate session.
  - **Animation-transition pose distortion.** Dump #2 in the
    validation showed a 3.35 delta_z during an animation
    transition (negative torsoBacklerp). Expected — bone-axis
    interpolation can briefly distort bone-local-X projection
    onto world-Z. Settles back to the 5-7 band on the next
    stable frame.

## v0.5.2-rc3 — 2026-04-30 — Diagnostic bone-lerp fix + Omni-bot all-platforms

> **DIAGNOSTIC RELEASE — not for production cup play.** Third
> release-candidate of v0.5.2 with two fixes on top of rc2:
>
> 1. The VG_DIAG_DUMP block emitted (0,0,0) for every bone position —
>    the `_vg_*` interntag names couldn't be resolved through
>    `trap_R_LerpTag`. Replaced with a new
>    `mdx_diag_resolve_tag_world` helper that uses the same
>    cachetag + `mdx_tag_orientation` path `mdx_hit_test` uses
>    internally for capsule anchoring. Verified locally on a
>    dedicated Linux test server: every bone now resolves to a real
>    world-space coordinate (e.g. `_vg_head=(3084,6022,-642)`,
>    `delta head-neck=10.57` units).
> 2. Omni-bot now built into qagame on **all three platforms**
>    (Linux x86_64, Win64, Win32) instead of Linux only. Required
>    `-DFORCE_OMNIBOT=ON` to override `cmake_dependent_option`'s
>    auto-disable for mingw cross-compiles.
>
> Plus a new manual-trigger cvar for multi-pose diagnostics.

### Bone-lerp fix (CRITICAL — this is what makes the data usable)

**Root cause.** `trap_R_LerpTag(orientation, refent, "_vg_head", 0)`
always returned -1 without writing to the orientation. Why:
`mdm_tag_lookup` returns interntag matches with the `TAG_INTERNAL`
bit (`1 << 30`) set — `tagNum | TAG_INTERNAL` is a positive integer
greater than `model->tag_count`, so `trap_R_LerpTagNumber`'s
`tagNum >= model->tag_count` guard rejects it. The
zero-initialised orientation_t passed straight back through to the
log lines: `(0, 0, 0)` for every bone.

**Fix.** New public function `mdx_diag_resolve_tag_world` in
`g_mdx.c` (~50 LoC, gated on `BONE_HITTESTS`). Looks up the tag
name in the global cachetag table (registers it on the fly if
missing — needed for `_vg_*` interntags only declared but never
referenced inside a HIT block), then calls `mdx_tag_orientation`
with `recursion=0`, which fully transforms model-local
coordinates to world space via `refent->origin` and
`refent->axis`. Mirrors the exact path `mdx_hit_test` uses for
capsule anchors at `hit->tag[0]` (g_mdx.c:2816).

The diagnostic block in `g_combat.c` now uses
`mdx_diag_resolve_tag_world` instead of `trap_R_LerpTag` and
prints a `resolve_status` line so failed lookups (anything
returning -1) are visible in the dump rather than silently
becoming zeros.

### Manual-trigger cvar `vanguard_diag_dump`

New transient cvar parallel to the existing
`vanguard_hitbox_debug 0->1` re-arm path. Use:

  - `rcon set vanguard_diag_dump 1` — fires VG_DIAG_DUMP on the
    next damage event regardless of debug state, then auto-resets
    the cvar to 0. Issue between shots for multi-pose tests in a
    single session (idle / crouch / strafe-mid).

The existing 0->1 re-arm convenience path is unchanged. The dump
now includes a `trigger=` line distinguishing manual from auto
arm, and a `resolve_status` line listing per-tag lookup results
so failed lookups stand out instead of silently becoming
`(0, 0, 0)`.

### Omni-bot on all three platforms

rc2 had `FEATURE_OMNIBOT=ON` on Linux only (per the existing
release.yml convention). rc3 enables it on all three:

  - Linux x86_64 — Omni-bot symbols: 52 (was 52)
  - Win64 — Omni-bot symbols: 46 (was 0)
  - Win32 — Omni-bot symbols: 38 (was 0)

The `cmake_dependent_option` in `CMakeLists.txt:102` silently
auto-disables `FEATURE_OMNIBOT` for mingw cross-compiles unless
`FORCE_OMNIBOT=ON` is also set — that's why the rc2 Windows
`-DFEATURE_OMNIBOT=ON` flag had no effect. Both `release.yml`
and `ci.yml` are updated to pass `FORCE_OMNIBOT=ON` for the
Windows steps.

The release-server ZIP now bundles the **Omni-bot runtime
binaries** (Linux .so + Windows .dll + macOS) under
`vanguard/omni-bot/`, so admins on any supported OS get a
working bot setup out of the box without a separate download.

### CI sanity check (prevents the rc1 build-flag-copy-paste regression)

Both `release.yml` and `ci.yml` now run a `strings | grep -ic
"omni"` symbol check on each qagame artifact and fail the build
if the count is below 5 (real builds carry 30+ symbols). Catches
any future `FEATURE_OMNIBOT` regression at PR-validation time
instead of after a release-candidate ships.

### Known limitations

  - **Still diagnostic-only.** The capsule-offset bug isn't fixed
    in rc3 — only made measurable. v0.5.2 final will use the
    multi-pose data this rc3 collects to drive the empirical
    capsule tuning.
  - **Idle pose is the baseline.** Animation-driven offsets will
    be measured separately via the manual trigger.

## v0.5.2-rc2 — 2026-04-30 — Diagnostic rc1 + Omni-bot build-flag fix

> **DIAGNOSTIC RELEASE — not for production cup play.** Same as
> rc1 with one build-pipeline fix: the local rc1 build had
> `-DFEATURE_OMNIBOT=OFF` for the Linux qagame (copy-paste from
> the Windows-step flags), which compiled out every Omni-bot init
> hook in `g_main.c` (`#ifdef FEATURE_OMNIBOT` at lines 37, 167,
> 173, 213, 1950, 3334). On Pterodactyl: server started cleanly,
> Vanguard mod loaded, but `bot help` / `bot testbot` did nothing
> and zero Omni-bot lines appeared in the log. rc1 was never
> tagged or pushed — the broken artifact was only on the local
> test server.
>
> Fix: rebuild all three platforms with the same flags the
> release.yml CI pipeline already uses — Linux with
> `FEATURE_OMNIBOT=ON` (uses the runtime cached at
> `vendor/omnibot-runtime/extracted/`), Win64 / Win32 with
> `FEATURE_OMNIBOT=OFF` (Windows isn't a supported Omni-bot
> platform). No code change versus rc1; the diagnostic block
> stays as-is. Bumped version to rc2 so the deployed pk3 file
> name is unambiguous.
>
> Verification: `strings qagame.mp.x86_64.so | grep -i omni` —
> rc1 returned 0, rc2 returns >0 (the Omni-bot init / shutdown
> log strings are now in the binary). Same `VG_DIAG_DUMP` block,
> 21 literals across all three qagame binaries.

## v0.5.2-rc1 — 2026-04-29 — Phase 7.0.1 capsule-offset diagnostic (superseded by rc2)

> **DIAGNOSTIC RELEASE — not for production cup play.** This `-rc1`
> exists only to gather one-shot per-session telemetry from a live
> server so the v0.5.2 final fix can be tuned against real numbers.
> Cups should stay on v0.5.1. The only behaviour change versus v0.5.1
> is one additional log block per session when
> `vanguard_hitbox_debug 1` — no gameplay code changed.
>
> **Superseded by rc2** — rc1's local build had
> `FEATURE_OMNIBOT=OFF` and broke `bot` rcon commands. Use rc2
> instead.

### Why this exists

Live-test on v0.5.1 (with strict-hitbox finally working as
advertised) confirmed Phase 6 multi-region capsules sit
**systematically lateral-offset** from the rendered player mesh.
Chest and shoulder capsules drift too — not just the head sphere —
which rules out the v0.4.2 HEAD-only `offset 6.5 0 0` axis as the
sole cause. The leading hypothesis is a divergence between qagame's
`mdx_bone_orientation` (server-side, used to anchor capsules) and
the engine's `R_CalcBones` (client-side, used to render the mesh).
See `docs/notes/PHASE_7_0_1_AUDIT.md` for the full bone-math
diagnosis.

To localise the discrepancy without guessing, v0.5.2-rc1 ships a
one-shot diagnostic dump that fires on the first damage event after
`vanguard_hitbox_debug` flips 0→1, lerps a representative set of
internal tags (`_vg_head` with its 6.5,0,0 offset; `_vg_neck`,
`_vg_spine_mid`, `_vg_pelvis`, `_vg_clav_l`, `_vg_clav_r` with no
offset), and prints the resulting world-space positions plus the
`grefEntity_t` transform inputs that produced them. With those
numbers in hand the v0.5.2 final fix targets the actual offset
instead of more guesswork.

### What changed

  - **`VG_DIAG_DUMP:` block** in `g_combat.c` between
    `mdx_gentity_to_grefEntity` and `mdx_hit_test`. ~120 LoC,
    diagnostic-only, removed in v0.5.2 final.
  - **One-shot per session.** Triggered by the first damage event
    after `vanguard_hitbox_debug` transitions 0→1. Re-arm by
    toggling the cvar 0 then 1 again. The existing per-shot
    `VG_DIAG:` line still fires every shot — the new block is
    `VG_DIAG_DUMP:` so logs grep cleanly.
  - **No gameplay change.** Strict-mode, multi-region capsules,
    damage multipliers, helmet/EF_HEADSHOT logic — all
    byte-identical to v0.5.1.

### How to deploy and report back

  1. Drop `vanguard_v0.5.2-rc1.pk3` into the server's `vanguard/`
     mod directory (replacing v0.5.1's pk3 for the test session).
  2. Set `vanguard_hitbox_debug 0` then `vanguard_hitbox_debug 1`
     to arm the dump. Confirm `vanguard_hitbox_strict 1` is set.
  3. Get a frontal **idle** pose (target standing, facing the
     attacker, no movement, no jumping, no animation in progress).
     This is the empirical baseline — moving / leaning / crouching
     poses come in a later test cycle.
  4. Fire **one shot** at the target. The dump fires on the first
     damage event of that cycle.
  5. `grep "VG_DIAG_DUMP:" server.log` and send the block back.
     Roughly 20 lines per dump.
  6. Repeat for any additional poses you want to characterise by
     toggling the cvar 0→1 between shots.

### Known limitations

  - **Dump is not a fix.** v0.5.2-rc1 still has the lateral-offset
    bug; it just makes the bug measurable.
  - **One-shot per arm cycle only.** The static `s_diag_dump_done`
    is per-process and per-arm cycle. A server restart re-arms; a
    cvar toggle re-arms; in between, only one dump per session.
  - **Idle pose only for the baseline.** Animation-driven poses
    will be characterised in v0.5.3.

## v0.5.1 — 2026-04-30 — Strict-hitbox actually rejects AABB-only hits

### What this fixes

v0.4.3 introduced `vanguard_hitbox_strict` (default 1) with the
intent that shots landing in the engine's broad-phase player AABB
but missing every multi-region capsule should be rejected
entirely. Cup admins added `set vanguard_hitbox_strict 1` to
their server configs and expected "shoot beside the model →
no damage". Live-test on Pterodactyl with `g_debugHitboxes 1`
showed the opposite: `MOD_GARAND` shots logging
`hit_type=0 impactpoint=0` (= no capsule matched) were still
killing bots, identically to non-strict v0.4.x.

Root cause: the v0.4.3 strict-mode predicate was structurally
wrong. `mdx_hit_test` (`g_mdx.c:2754-2953`) **always returns
qtrue** in normal play; "no capsule matched" is communicated via
`*impactpoint = IMPACTPOINT_UNUSED` (= 0), not via the boolean
return. The reject block at `g_combat.c:1830` sat in the dead
`else` of `if (mdx_hit_test(...))` and never fired. Every
AABB-edge hit fell through to the multiplier path, picked up
`vanguard_dmg_default` (= 1.0), and applied normal damage.

The fix is structural and small:

  - **Reject inside the success branch.** New check
    `if (mdx_ip == IMPACTPOINT_UNUSED && vg_Hitbox_StrictMode())`
    at the top of the hit-resolved block. Catches the AABB-but-
    no-capsule case correctly. `VG_DIAG: strict-hitbox reject
    (AABB hit but no capsule)` log line if
    `vanguard_hitbox_debug 1`.
  - **Dead reject block removed.** The unreachable
    `if (vg_Hitbox_StrictMode()) return` after the
    `if (mdx_hit_test...)` is gone. A short comment in its place
    explains the qfalse-only-in-startup-error rationale so
    future readers don't add it back.
  - **`isHeadshot` gate dropped.** Mounted / mobile MGs
    (`MOD_MACHINEGUN`, `MOD_BROWNING`, `MOD_MG42`,
    `MOD_MOBILE_MG42`, `MOD_MOBILE_BROWNING`) now also go through
    the multi-region narrow-phase. v0.4.x bypassed the gate via
    `isHeadshot=qfalse` and applied damage on AABB hit; with
    v0.5.1 they get the same strict-mode treatment as
    rifles/SMGs. Splash-damage MODs (`isExplosive=qtrue`) take a
    completely different code path via `radius_damage` and stay
    unaffected — grenades, panzers, mortars still apply on
    radius regardless of capsule, which is correct.

### Behaviour change for cup admins

Cup servers running with default `vanguard_hitbox_strict 1` will
see fewer "phantom kills" — players hit beside the visible mesh
no longer take damage. This is what cup admins were asking for
in the v0.4.3 strict-mode discussion, finally actually
delivered. Mounted MGs now also benefit from the rejection
(previously bypassed via the `isHeadshot` gate).

For anyone who needs byte-identical legacy behaviour:
`set vanguard_hitbox_strict 0` falls through to the multiplier
path with `dmg_default = 1.0` for AABB-only hits — that's
v0.4.x behaviour preserved as an opt-out. The `isHeadshot`
gate drop is permanent for v0.5.1+; if you specifically need
old mounted-MG semantics, set `dmg_default 0.0` to make
AABB-only hits from any weapon do zero damage even without
strict mode (alternative way to express the same intent).

### Bullet-trace AABB tightening (Phase 7.0)

The fix above IS the AABB tightening for practical purposes.
The original Phase 7.0 plan envisioned a custom bullet-trace
layer that operated only against the multi-region capsules. The
recon (docs/notes/PHASE_7_0_AUDIT.md) found that approach would
be ~10× more expensive on the hot path AND functionally
equivalent to AABB-broad-phase + capsule-narrow-phase rejection
— if the rejection was wired up correctly. v0.5.1 wires up the
rejection. No custom trace layer needed.

### What's NOT in this release

  - Capsule-gap tuning (Phase 7.0.1 candidate). With strict-mode
    actually rejecting now, the live-test may surface "I clearly
    hit the model but no damage" cases where the trace endpoint
    falls in a gap between two adjacent capsules (e.g. between
    Bip01 L Thigh and Bip01 Pelvis). If that happens,
    Phase 7.0.1 = `human_base.hit` capsule overlap tuning,
    separate v0.5.2.
  - Phase 7.3 (movement physics), Phase 7.4 (UI), Phase 7.1
    (sounds) — still scheduled for subsequent v0.5.x releases.

## v0.5.0 — 2026-04-30 — Cup-Mode Foundation

First major release on the Cup-Mode track. Phase 7.2 (Netcode
Tuning) ships the **netcode profile** cvar that lets a server flip
between cup-grade and public-grade netcode tuning without editing
`server.cfg`. Phase 7.3 (movement) and Phase 7.4 (UI) are scheduled
for subsequent v0.5.x point releases.

  - **`vanguard_netcode_profile`** — new cvar
    (`CVAR_LATCH | CVAR_ARCHIVE | CVAR_SERVERINFO`,
    default `"public"`). Three values:
    * `public` (no-op, byte-identical to v0.4.x)
    * `cup` (sv_fps 40, g_antilag 1, g_antiwarp 1)
    * `custom` (admin owns server.cfg, profile flag advertises intent)
  - **Cup preset is verified, not just set.** Each
    `trap_Cvar_Set` call is followed by a
    `trap_Cvar_VariableIntegerValue` read-back; mismatches log a
    yellow `VG_Netcode: WARNING ...` line pointing the admin at
    the documented escape hatch. Catches the Pterodactyl /
    managed-host case where engine cvars are locked at the layer
    above qagame.
  - **`vanguard_competitive.cfg`** gains one line:
    `set vanguard_netcode_profile "cup"`.
  - **`docs/CUP_VS_PUBLIC.md`** documents what each profile does,
    the lag-comp ⊃ multi-region finding from Phase A audit, the
    `MAX_CLIENT_MARKERS=40` history-window math at sv_fps 40, the
    Pterodactyl gotcha, and three ways for admins to verify which
    profile is active.

### Phase 7.2 Sub-Goal 1 — already correct (no code change)

Phase A recon (`docs/notes/PHASE_7_2_AUDIT.md`) found that
ETLegacy's lag-comp infrastructure (`G_StoreClientPosition` /
`G_AdjustSingleClientPosition`) already rewinds the full
torsoFrame and legsFrame state, not just origin/angles. The
multi-region `mdx_hit_test` damage path therefore inherits
correct lag-compensation without any change — `mdx_hit_test`
reads from exactly the fields the antilag layer rewinds. The
recon question was a worry rather than a bug; documented in
`docs/CUP_VS_PUBLIC.md` so future maintainers don't repeat the
trace.

### Phase 7.2 Sub-Goal 3 — antiwarp via cup preset

The cup preset re-asserts `g_antiwarp 1` (already default in
v0.4.x, but a server-config drift could have turned it off).
Per-client warp-incident diagnostic logging is deferred to a
follow-up ticket — wait for a cup operator to actually need it
before adding the infrastructure.

### Release-helper script

New `scripts/release.sh` automates the pre-flight portion of cutting
a release: working-tree-clean check, branch check, repo-name auto-
detect, tag-doesn't-exist-yet check, `VANGUARD_VERSION` write,
`RELEASE_NOTES.md` editing reminder, and printing the exact `git
add / commit / push / tag / push --tags` sequence the admin then
runs by hand. **Does not auto-execute git commands** — the actual
release decision stays a manual step.

### Lag-comp history window — accepted limitation

The cup preset bumps `sv_fps` from 20 to 40, which halves the
lag-comp rewind history `MAX_CLIENT_MARKERS = 40` (`g_local.h:912`)
covers — from 2 seconds at sv_fps 20 down to 1 second at sv_fps 40.

This is **acknowledged and accepted for v0.5.0**, not deferred.
Realistic cup pings (30–80 ms one-way) need at most ~80 ms of
rewind history; even tournament-edge 200 ms pings need ~100 ms.
The 1-second buffer leaves 10–25× headroom over the worst
realistic case. Buffer would only clip at >500 ms ping, where the
player is unplayable on its own merits.

If a future cup live-test surfaces actual clipping (player
reports their hits not registering despite visible aim), the fix
is a one-line bump of `MAX_CLIENT_MARKERS` to 80 (= 2 s history
at sv_fps 40), tracked under a hypothetical Phase 7.2.1 ticket.
Not pre-emptively shipped because the constant is referenced by
modular arithmetic across `g_antilag.c` and a change wants its
own validation pass. v0.5.0 ships with the existing 40-marker
ring.

### No gameplay changes vs v0.4.4

Out of the box, a v0.5.0 server with `vanguard_netcode_profile`
unset (or set to `public`) behaves byte-identically to v0.4.4. The
profile is opt-in.

### What's NOT in this release

Phase 7.3 (movement physics), Phase 7.4 (UI / HUD changes), and
Phase 7.1 (sounds) all deferred to subsequent v0.5.x
releases. Tight-hitbox capsule tuning (Phase 7.0, prep for
v0.4.3's strict-mode) only kicks in if live-test surfaces gaps in
the current `human_base.hit` coverage.

## v0.4.4 — 2026-04-30 — Build Infrastructure & Auto-Versioning

### CI/CD Automation

- **GitHub Actions Release Workflow** — Tag-triggered automatic builds for all 3 platforms (Linux x86_64, Windows x64, Windows x86), produces server + client ZIPs, creates GitHub Releases automatically
- **GitHub Actions CI Workflow** — Build validation on every push/PR, prevents broken commits from landing
- **Documentation** — `docs/CI.md` (workflow + release walkthrough), `docs/INSTALL_SERVER.md`, `docs/INSTALL_CLIENT.md`

### Auto-Versioning

- Version is now derived **from the git tag** via cmake — no more six-spot bump
- Resolution chain: `CI_ETL_TAG` env var → `git describe --tags` → `VANGUARD_VERSION` file fallback
- New `VANGUARD_VERSION` file at repo root for tarball downloads (non-git scenarios)
- `bootstrap.sh` cleanup: hardcoded version strings replaced with placeholders
- `RELEASE_PROCESS.md` rewritten — `git tag vX.Y.Z && git push origin vX.Y.Z` is now the entire release procedure

### No Gameplay Changes

- pk3 contents are **identical** to v0.4.3 — no bytes-on-disk gameplay differences
- All hitbox + strict-mode + cgame fixes from v0.4.3 carry forward unchanged
- This release is purely about build infrastructure and automation

### Internal

- 4 commits since v0.4.3 CI/CD setup (auto-versioning implementation)
- Validated with 4 build-test scenarios:
  - Tagged-exact (v0.4.99 dummy tag → vanguard_v0.4.99.pk3)
  - Tagged-with-commits-since (HEAD past tag → dev-build identifier)
  - No-git fallback (VANGUARD_VERSION file)
  - Explicit override (CI_ETL_TAG=v0.5.0-rc1)

### For Server Admins

- No action required — same gameplay, same configuration as v0.4.3
- Optional: switch to v0.4.4 for first auto-versioned download experience

### For Developers

- New release workflow: edit `RELEASE_NOTES.md`, optionally bump `VANGUARD_VERSION`, then `git tag -a vX.Y.Z -m "..." && git push origin vX.Y.Z`
- See `docs/CI.md` and `docs/RELEASE_PROCESS.md` for details

## v0.4.3 — 2026-04-29 — Strict hitbox + cgame position-lag fix + SPDX

Three changes shipped together:

### Strict-hitbox mode (server-side)

`G_Damage`'s multi-region branch (`g_combat.c`, gated on
`vg_Hitbox_IsActive() && weapon-is-headshot`) used to fall through
to the legacy chain when `mdx_hit_test` couldn't match the trace
endpoint to any of the ten `human_base.hit` capsules. The legacy
chain then credited the shot as an ordinary body hit at full
damage. The engine's broad-phase player-AABB trace can be ~6 units
wider than the visible mesh in some poses, so this turned every
"shot beside the player" into damage — exactly the behaviour
competitive players had been complaining about.

  - **New cvar `vanguard_hitbox_strict`** (`CVAR_ARCHIVE`,
    default `1`). When non-zero, an `mdx_hit_test` miss inside the
    multi-region branch returns immediately from `G_Damage`
    instead of falling through. The trace is treated as a clean
    miss; the broad-phase tolerance no longer leaks into the
    damage path.
  - **Cup-compatibility:** `set vanguard_hitbox_strict 0` restores
    the v0.4.x byte-identical behaviour for organisers who need
    to lock a tournament to the legacy semantics.
  - **Diagnostic note:** under `vanguard_hitbox_debug 1` each
    rejection emits one `VG_DIAG: strict-hitbox reject ...` line so
    admins can audit the rejection rate during a tuning session.
  - Non-headshot weapons (explosives, throwables) are unaffected —
    they don't enter the multi-region branch in the first place.

### Cgame hitbox position-lag fix

The visualisation in v0.4.0–v0.4.2 manually rebuilt its body
refEntity from `cent->lerpOrigin` / `cent->lerpAngles` /
`cent->pe.legs.*`, which produced a visible 5–15 unit drift
during walking and sprinting because `lerpAngles` is the player's
view direction (instant) rather than the smoothed legs direction
the renderer's body model uses. v0.4.3 reuses the renderer's own
cached `cent->pe.bodyRefEnt` instead — the refEntity that
`CG_Player` builds at `cg_players.c:2977` with proper
`CG_PlayerAngles` (yaw smoothing via `CG_SwingAngles`) and
`CG_PlayerAnimation` (per-frame animation state) applied. The
order is safe: `CG_VanguardDev_DrawHitboxes` runs after
`CG_AddPacketEntities` in `CG_DrawActiveFrame`, so `bodyRefEnt`
is fresh by the time we read it. A manual reconstruction is kept
as a fallback for entities that haven't been through `CG_Player`
yet (e.g. first frame after spawn).

The v0.4.2 `VG_DIAG: Bip01 Head bone-axis` one-shot diagnostic
print is removed in this release — the bone-axis convention is
now well understood and documented in
`docs/notes/CGAME_BONE_CALC_RECON.md`.

### SPDX copyright headers + LICENSE / COPYRIGHT / NOTICE

Long-overdue legal hygiene pass.

  - **SPDX-License-Identifier headers** added to all
    VanguardMod-specific source files (cgame `cg_vanguard_*`, game
    `g_vanguard_*`, the WolfGuard public surface, the test-server
    launcher, the Vanguard cmake modules, the bootstrap script).
  - **SPDX modifications block** appended to imported ETLegacy
    files that VanguardMod has touched (`g_mdx.{c,h}`,
    `bg_animgroup.c`, `bg_public.h`, `g_combat.c`,
    `cmake/ETLVersion.cmake`, `cmake/ETLBuildMod.cmake`). The
    original upstream copyright headers are kept verbatim — the
    Vanguard block sits below them and only covers our changes.
  - **`LICENSE`** cleaned up — the placeholder GPL-2.0 paragraph
    is replaced by an actual SPDX-tagged GPL-3.0-or-later notice
    that points at `COPYING.txt` (which already carries the full
    GPL-3.0 text from the upstream import).
  - **New `COPYRIGHT`** root file lists the primary VanguardMod
    copyright holders and points at `git shortlog` for the
    contributor list.
  - **New `NOTICE`** root file acknowledges ETLegacy, Wolfenstein:
    Enemy Territory, Quake III Arena, cJSON, and the Zinx
    Verituse MDX bone math — the third-party stack VanguardMod is
    built on, with each component's license terms.
  - **Markdown docs** under `docs/` get a Copyright Notice footer
    so the same SPDX information is reachable from documentation
    consumers.
  - **Asset header** added to `etmain/animations/human_base.hit`
    — the `.hit` format supports `//` comments at the top so the
    SPDX block is visible to anyone inspecting the asset.

No gameplay logic changes from v0.4.2 outside the strict-hitbox
gate. Same multi-region damage pipeline, same multipliers, same
hit-area geometry, same HEAD anchor offset, same cgame
visualisation algorithm.

## v0.4.2 — 2026-04-29 — HEAD anchor offset axis correction

Patch release on top of v0.4.1 to correct the offset axis used
for the HEAD-sphere anchor. v0.4.1 added `offset 0 0 6.5` to the
`_vg_head` interntag in `human_base.hit` by analogy with the
legacy `mdx_head_position` constant — but that helper applies
its `+6.5` along an MDM tag's world-frame `axis[2]`, which is
NOT the same as a bone-local axis applied via
`mdx_tag_orientation`'s `vec3_rotate(tag->offset, tmpaxis, ...)`
chain. Live-test on Pterodactyl with 13 screenshots in varied
poses (frontal, profile, top-down, crouch, prone, sprint)
confirmed the symptom: HEAD-sphere wandered consistently to the
back of the head — most visibly the top-down view where it
overlapped the medic-pack red cross on the player's back.

Recon (`docs/notes/CGAME_BONE_CALC_RECON.md` follow-up):

  - The offset is rotated by the bone's local-axis matrix from
    `mdx_bone_orientation` (`g_mdx.c:1644-1664`). For a 3DS-Max
    biped bone, local +X is the bone direction (parent → child),
    not local +Z. Confirmed in `mdx_calculate_bone`
    (`g_mdx.c:1402`) where `parent_dist` is placed on `tmp[0]`
    (X) before the per-frame rotation.
  - For `Bip01 Head` whose parent is `Bip01 Neck` and whose
    child direction is "up the skull" in the bind pose, the
    bone-local +X corresponds to world-up for an upright player.
  - v0.4.1's `0 0 6.5` rotated through the bone-local axis
    landed on bone-local +Z, which for `Bip01 Head` is the
    skull-back direction — hence the "sphere on the medic pack"
    symptom.

Fix:

  - **`etmain/animations/human_base.hit`** — `TAG _vg_head`
    offset switched from `0 0 6.5` to `6.5 0 0`. Comment block
    updated to document the correction and contrast with the
    legacy `mdx_head_position` axis convention.
  - **`src/cgame/cg_vanguard_dev.c` `vg_hit_areas[]`** — HEAD
    entry's `offset1` switched from `(0, 0, 6.5)` to
    `(6.5, 0, 0)` to mirror the .hit-side anchor.
  - **`src/cgame/cg_vanguard_mdx.c`** — temporary one-shot
    `VG_DIAG: Bip01 Head bone-axis ...` print added inside
    `vg_mdx_compute_bone_world_with_offset`. Fires once per
    cgame session for the `Bip01 Head` lookup. Logs the bone-
    local axis matrix rows in MODEL frame plus the rotated
    offset vector, so we can verify the axis convention from
    the live-test log even if the visual fix lands wrong (in
    which case v0.4.3 ships with the right axis informed by
    the diagnostic data). Removed in v0.4.3 once the visual
    fix is confirmed.

Server-cgame parity unchanged from v0.4.1: both sides apply the
same `+6.5` along `Bip01 Head` local +X, both rotate via the
same bone-axis math chain. Visualisation continues to track
trace position 1:1.

Expected impact:

  - HEAD-sphere visualisation centers on the skull from all
    viewing angles (top-down: above the helmet, NOT on the
    backpack). Animation tracking via bone-local rotation —
    sphere follows head tilt and rotation.
  - HEAD impactpoint hit-rate climbs as the trace position
    finally lands on the visible skull. Carry-over expectation
    from v0.4.1: ~10–15% of total body shots, varying with
    match style.

If the v0.4.2 visual fix STILL lands wrong (sphere not at skull
centre), the `VG_DIAG: Bip01 Head bone-axis` log lines from a
brief Pterodactyl run-through will let v0.4.3 ship with the
correct axis the same day. The candidate fallbacks per the
v0.4.2 brief are `0 6.5 0`, `-6.5 0 0`, `0 0 -6.5`, `0 -6.5 0`,
or a world-frame offset added post-bone-world-transform that
bypasses bone-axis rotation entirely.

No gameplay logic changes outside the HEAD trace position. Same
hit-detection, multipliers, capsule geometry as v0.4.1.

## v0.4.1 — 2026-04-29 — HEAD anchor fix (+6.5 Z)

Patch release on top of v0.4.0 to fix the HEAD-sphere position
in both the multi-region damage trace and the cgame visualisation.

The v0.4.0 live-test on Pterodactyl confirmed all ten capsules
render and follow animations as designed, but the HEAD sphere
sat visibly too low — at the chin / atlas rather than the skull
centre. Server-side hit-detection produced ~2% HEAD impactpoint
hits in a 1h match log, low enough that the multi-region pipeline
was effectively delivering body-shot damage on what should have
been headshots.

Root cause traced through `src/game/g_mdx.c`:

  - The Phase 6 multi-region `mdx_hit_test` (`g_mdx.c:2754`)
    walks each `_vg_*` interntag from `human_base.hit` and
    traces against the bone position returned by
    `mdx_tag_orientation` (`g_mdx.c:1686`). For `_vg_head`
    the lookup resolves to the raw `Bip01 Head` bone origin —
    which sits at the atlas (skull-base / upper neck) in the
    3DS-Max biped skeleton, not at the visible skull centre.
  - The legacy realhead trace path `mdx_head_position`
    (`g_mdx.c:2946-2971`, used by `g_combat.c:G_BuildHead`
    when `g_realHead & REALHEAD_HEAD`) has compensated for
    this since the original ETLegacy implementation by
    applying `+6.5` units along the head bone's local Z and
    `+0.5` along its local X. Those constants are **not**
    inherited by `mdx_hit_test` — the multi-region path
    silently lost them when v0.3.3 declared `_vg_head` with
    no offset modifier.
  - The cgame visualisation correctly mirrored the trace
    position (chin/atlas), so v0.4.0's sphere visually
    matched where the server actually hit. The mismatch was
    between the server **and the player model**, not between
    server and visualisation.

Fix:

  - **`etmain/animations/human_base.hit`** —
    `TAG _vg_head "Bip01 Head"` → `TAG _vg_head "Bip01 Head"
    offset 0 0 6.5`. The TAG-block parser
    (`g_mdx.c:740` → `hit_parse_tag`) supports an `offset
    X Y Z` modifier on each interntag, applied bone-local
    by `mdx_tag_orientation` via `vec3_rotate(tag->offset,
    tmpaxis, ...)`. With the +6.5 Z restored, the server's
    HEAD sphere recenters on the visible skull. The 0.5
    forward offset that `mdx_head_position` also applies
    is intentionally omitted — for an isotropic radius-6
    sphere it has no effect on the hit volume centre.
  - **`src/cgame/cg_vanguard_dev.c` `vg_hit_areas[]`** —
    HEAD entry gains an `offset1 = (0, 0, 6.5)` field
    matching the .hit-side anchor.
  - **`src/cgame/cg_vanguard_mdx.c`** — new
    `vg_mdx_compute_bone_world_with_offset` function ports
    the bone-local-axis path from qagame's
    `mdx_bone_orientation` (`g_mdx.c:1644-1664`): reads the
    per-frame `anglesF` field that the v0.4.0 parser was
    skipping, lerps current/old via backlerp, builds the
    bone-local axis matrix as
    `transpose(AnglesToAxis(anglesF))`, rotates the offset
    by it, and adds the rotated vector to the bone's
    model-local origin before the world transform. The
    v0.4.0 `vg_mdx_compute_bone_world` becomes a thin
    wrapper that passes a zero offset.
  - **`src/cgame/cg_vanguard_dev.c` `vg_GetBoneOrigin`** —
    accepts the offset and routes to the new helper. The
    fallback path (when `vg_mdx_*` rejects the lookup) also
    applies the offset, in tag-local frame via the engine
    `trap_R_LerpTag` axis — an approximation but the
    closest the engine syscall can produce.

Expected impact: HEAD impactpoint hit-rate climbs from
the ~2% v0.4.0 baseline toward the historically validated
realhead range (~10–15% of all body shots, varying with
match style). Cgame visualisation continues to match the
server trace position 1:1 — both sides now show / hit the
skull centre.

Bonus: `docs/notes/CGAME_BONE_CALC_RECON.md` updated with a
follow-up section documenting that the multi-region
pipeline does **not** inherit `mdx_head_position`'s legacy
offsets. Important for any future region tuning where the
visible mesh region differs from the bone-root location —
the `.hit` file must compensate per-region with a
`TAG offset` modifier.

No gameplay logic changes outside of the HEAD trace
position. Other regions (CHEST, GUT, GROIN, SHOULDER L/R,
KNEE L/R, LEGS) unchanged. Damage multipliers unchanged.

## v0.4.0 — 2026-04-29 — Phase 6 major release

VanguardMod's first feature release: the **Multi-Region Damage
Pipeline** plus a **bone-tracked hitbox visualisation** that
matches it server-side hit-by-hit. This is the milestone that
activates ETPro/RtCW's dormant multi-region damage architecture
in ETLegacy for the first time, and pairs it with a custom
cgame-side MDX skeleton loader so what the client sees is what
the server hits.

### Multi-Region Damage Pipeline (server-side)

Activated in stages from v0.3.3 through v0.3.4 and verified live
in the v0.3.4–v0.3.5 multi-user testing on Pterodactyl. Stable
since v0.3.4; this release packages it as a first-class feature
rather than an internal-testing flag.

  - **Nine fine-grained body regions.** HEAD, CHEST, GUT, GROIN,
    SHOULDER L/R, KNEE L/R, LEGS — each with its own damage
    multiplier (`vanguard_dmg_head` … `vanguard_dmg_legs`,
    `CVAR_ARCHIVE`, mid-match tunable).
  - **Bone-tracked hit detection.** `mdx_hit_test` against
    `etmain/animations/human_base.hit` (Pass 1+2 retune)
    follows the player's MDX skeleton across every animation
    frame — far more accurate than vanilla AABB hitboxes.
  - **Latched mode cvar.** `vanguard_hitbox_mode 1`
    (`CVAR_LATCH | CVAR_ARCHIVE | CVAR_SERVERINFO`) is the
    master switch; `mode 0` falls back byte-identically to the
    vanilla pipeline. Latched so a server cannot drift between
    regimes mid-match.
  - **Verified hit-rate.** v0.3.5 instrumentation logged 0%
    `IMPACTPOINT_UNUSED` across the gestern Multi-User-Test
    (cup tester, 8/8 valid hits). Vanilla AABB had been
    bottlenecking around the 10% mark in equivalent setups.

### Cgame Hitbox Visualisation (client-side, Strategy I)

The visualisation that v0.3.6 promised, finally working. The
v0.3.6–v0.3.8a iterations identified that the engine's
`R_LerpTag` exposes only MDM tags (`tag_head`, `tag_chest`,
…), never the raw skeleton bones our hit-areas are anchored to.
This release ships a self-contained MDX loader in cgame so the
visualisation runs the same bone math the server uses.

  - **Ten wireframe capsules per visible player.** Spheres for
    HEAD and GROIN (radius 6 / 7), `box2` primitives for CHEST
    and GUT (`Spine1↔Neck`, `Pelvis↔Spine2`), cylinders for
    SHOULDER L/R, KNEE L/R, and LEGS (calf↔foot).
  - **Per-region colour palette.** HEAD red, CHEST yellow,
    GUT orange, GROIN pink, SHOULDER blue, KNEE green, LEGS
    cyan. Lets the eye spot misalignment at a glance.
  - **Custom MDX loader** (`src/cgame/cg_vanguard_mdx.c`,
    ~430 lines). Direct port of `mdx_load` +
    `mdx_calculate_bone_lerp` from `g_mdx.c`, origin-only
    (mesh-deformation paths skipped). Loads MDX files via
    `trap_FS_*` syscalls into a private 16-slot registry,
    walks the bone hierarchy with the same per-frame
    `offset_angles` lerp the server uses for hit-detection,
    transforms model-local origins to world space via
    `body->origin + body->axis`. The result is 1:1
    positionally with `mdx_hit_test`'s view of the skeleton.
  - **Path-table bridge** (`src/game/bg_animgroup.c`).
    Engine MDX qhandles are opaque from inside the cgame VM,
    so we record the file path string at registration time
    (in `BG_RAG_ParseAnimFile`) into a parallel
    `vg_mdx_path_table[64]` declared in `bg_public.h`.
    Cgame translates handle → path via `vg_FindMDXPath()`
    when it needs to load a fresh MDX. Capacity is generous
    (16 cgame registry slots vs ~13 MDX files in `human_base`).
  - **Cvar control unchanged from v0.3.6.**
    `cg_vanguardDevMultibox 1` (CVAR_ARCHIVE, default 1)
    toggles the multi-region overlay independently of the
    legacy AABB cvar. Server-side `vanguard_dev 1` is the
    primary gate. `cg_vanguardDevAlpha 0.4` controls
    transparency.
  - **Fallback to `trap_R_LerpTag`** for handles whose path
    failed to register (e.g. stale snapshot during a model
    hot-reload). Tag-name lookups still work for non-skeleton
    anchors if a future hit-area uses one.

### Diagnostic cleanup

  - Removed v0.3.7's per-bone `VG_DIAG: vg_GetBoneOrigin FAIL`
    print and v0.3.8a's MDM tag probe block from
    `cg_vanguard_dev.c`. The `body.hModel = character->mesh`
    fix introduced in v0.3.8a stays — it's a real bug fix,
    not diagnostic.
  - The server-side `VG_DIAG: mdx_hit_test` print
    (`g_combat.c`) is unchanged: still cvar-gated on
    `vanguard_hitbox_debug`, default off.

### Infrastructure fix

  - **`fix(cmake): strip leading zeros from ETL_BUILD_VERSION_INT`**
    (`32da1a0`, originally shipped as part of the v0.3.8a
    release). Latent C-octal-literal bug in the upstream
    `cmake/version_generated.h.in` template that broke the
    build for any version with a digit ≥ 8 in the padded
    form. Fix: drop leading zeros so the literal is parsed as
    decimal. Unblocks every future v0.X.Y where any
    component digit is 8 or 9. Tagged with `# VANGUARD:`
    markers, eligible for upstreaming.

### Phase 6 progression

  - v0.3.3 → BONE_HITTESTS pipeline activation
  - v0.3.4 → `human_base.hit` Pass 1+2 geometry retune
  - v0.3.5 → IMPACTPOINT diagnostic instrumentation
  - v0.3.6 → cgame multi-region visualisation (Bip01 names —
    didn't render because of the MDX/MDM tag mismatch
    discovered in the v0.3.7 live-test)
  - v0.3.7 → bone-resolution diagnostic
  - v0.3.8a → MDM tag list probe + `body.hModel` fix
  - **v0.4.0 → Strategy I full MDX port + Phase 6 release**

No gameplay changes since v0.3.4. Same hit-detection, same
multipliers, same `human_base.hit` geometry. The only new
runtime work is in cgame: MDX file parsing on first sight of
each animation pose (~30 KB per file × ~13 files = ~400 KB
total once the player has been observed in every animation
context), then ~200 multiply-adds per visible player per
frame for bone-position lookup. Negligible.

## v0.3.8a — 2026-04-29 — Tag-list probe + body.hModel fix

Pre-implementation diagnostic for v0.3.8 Strategy II
(tag-anchored hitbox visualization, see
`docs/notes/CGAME_BONE_CALC_RECON.md`). Two changes in
`vg_BuildBodyRefent` / `vg_DrawPlayerMultibox`:

  - **`body.hModel` is now set to `character->mesh`.** v0.3.6
    and v0.3.7 left this unset; the engine's `R_LerpTag`
    dispatches via `refent->hModel` through
    `R_GetModelByHandle`, so an unset hModel hits the
    placeholder model and returns -1 for every tag — independent
    of the tag name. This was a silent second bug stacked on
    top of the bone-name issue (the v0.3.7 VG_DIAG output
    couldn't distinguish "Bip01 Head" missing from "hModel=0"
    failing). With hModel set, the v0.3.8a probe gives a
    truthful answer for each candidate tag. The standard
    player render path in `cg_players.c` already does this
    (`:2969`, `:3348`); the multibox path missed it.

  - **15-tag MDM probe** at first multibox render per client.
    Candidates: `tag_head`, `tag_chest`, `tag_torso`, `tag_back`,
    `tag_armleft`, `tag_armright`, `tag_legleft`, `tag_legright`,
    `tag_footleft`, `tag_footright`, `tag_ubelt`, `tag_weapon`,
    `tag_weapon2`, `tag_mouth`, `tag_bipod`. Each `trap_R_LerpTag`
    return code and tag-local origin is logged. The probe is
    one-shot per client (`vg_diag_probed[MAX_CLIENTS]` static
    flag), so the log isn't spammed at 60 Hz.

This is a diagnostic-only release. The output decides the v0.3.8
work plan: which tags exist drives the `vg_hit_areas[]` remap, and
which are missing tells us where capsule positions need synthetic
interpolation between neighbouring tags.

To diagnose: connect to a v0.3.8a server with at least one other
visible player (the multibox render skips self in first-person —
either spectate, third-person, or have a teammate connected).
After ~30 seconds, grep `server.log` (or the Pterodactyl console
output) for `VG_DIAG: MDM tag probe` lines. Each visible player
slot produces one banner plus 15 tag-result lines.

The v0.3.7 VG_DIAG bone-resolution print is retained — failed
"Bip01 *" lookups are still expected (the render-loop hasn't been
remapped yet) and the print remains throttled to once per second
per bone-name. Both diagnostics will be removed in v0.3.8 once
the remap lands. The hModel set in `vg_BuildBodyRefent` stays
permanently — it's a bug fix, not a diagnostic.

No gameplay changes vs v0.3.7. No hit-detection / multiplier
changes. Same 0-of-10 capsules visible at the moment, same
server-side hit math. Cgame-only diagnostic addition.

## v0.3.7 — 2026-04-29 — Diagnostic build (bone resolution)

Diagnostic-only release for Pterodactyl multi-user testing. Adds a
rate-limited `VG_DIAG` print at the failure path of
`vg_GetBoneOrigin` (`src/cgame/cg_vanguard_dev.c`) so we can identify
which of the ten "Bip01 *" bone-names fail `trap_R_LerpTag` lookup
in the cgame VM.

The v0.3.6 live-test surfaced that the multi-region capsules are not
permanently visible despite a render-loop that draws every area
unconditionally. Inspection ruled out highlight-gating (none exists)
and the alpha cvar default (`cg_vanguardDevAlpha "0.4"`). The
remaining failure mode is `vg_GetBoneOrigin` returning `qfalse` →
`continue;` skipping that area. The strong hypothesis is that the
`.mdm` tag-list does not export the MDX skeleton bones under those
names, so all but one (or zero) of the per-frame `trap_R_LerpTag`
calls fails silently.

This release adds throttled logging at that exact failure path:

  - **Per-bone-name throttle.** Each distinct bone-name prints at
    most once per second per cgame instance. With 10 capsules at 60
    Hz that prevents 600 prints/sec spam if all ten fail.
  - **Refent context.** Each line emits `bone='<name>'`,
    `frameModel=<qhandle>`, `frame=<int>`, `torsoFrame=<int>` so we
    can tell whether the failure is structural ("bone unknown to
    every model") or transient ("frameModel == 0 first frame after
    spawn").
  - **No cvar gate.** The diagnostic always prints in v0.3.7 because
    we want a 30-second live-test session to capture the full set of
    failing bones without requiring an admin to flip a toggle. The
    output is conditional on the failure path itself, so a clean run
    produces zero lines.

No gameplay changes vs v0.3.6. Same hit-detection, same multipliers,
same render-loop logic. Only addition is failure logging.

To diagnose: connect to a v0.3.7 server, walk around visible players
for ~30 seconds, then check `server.log` for `VG_DIAG: vg_GetBoneOrigin
FAIL` lines. The collected bone-name list determines the v0.3.8 fix
path (rename to MDM tag conventions, switch to a different lookup
API, or compute origins from a parent tag + offset).

This print will be removed (or properly cvar-gated) in v0.3.8 once
the fix lands.

## v0.3.6 — 2026-04-29 — Hitbox visualisation + diagnostic cvar-gate

Major addition: client-side rendering of all 10 multi-region
hit-capsules as wireframe primitives in real-time. Designed for
diagnostic use during the ongoing Phase 6 tuning, but also a
nice marketing surface for showing the precision of VanguardMod's
bone-tracked hit detection. v0.3.5's live-test indicated ~64% of
mdx_hit_test traces returned `IMPACTPOINT_UNUSED`, plus HEAD
detection at only ~2%; without visualisation we can only infer
where the capsules are. This release lets you see them.

  - **10 wireframe capsules per visible player.** Spheres for
    HEAD and GROIN (radius 6 / 7), boxes for CHEST and GUT
    (Spine1↔Neck and Pelvis↔Spine2 box2 primitives), cylinders
    for SHOULDER L/R, KNEE L/R, and LEGS (calf↔foot, both
    sides). Capsule positions match
    `etmain/animations/human_base.hit` (Pass 1+2 retune) bone-
    for-bone via `trap_R_LerpTag` against each player's
    animation refent.

  - **Per-region colour palette.** HEAD red, CHEST yellow, GUT
    orange, GROIN pink, SHOULDER blue, KNEE green, LEGS cyan.
    Lets you spot at a glance whether the HEAD-sphere is
    positioned in the right place relative to the player model
    (the v0.3.5 telemetry bug-hunt suggests it is not).

  - **Cvar control.** `cg_vanguardDevMultibox` (CVAR_ARCHIVE,
    default 1) toggles the multi-region overlay independently
    of the legacy `cg_vanguardDevHitboxes` AABB cvar. Server-
    side `vanguard_dev=1` remains the primary gate. Admin can
    show legacy AABB alone, multi-region alone, both, or
    neither.

  - **Diagnostic cvar-gate.** v0.3.5's always-on `VG_DIAG`
    server-log print is now gated on `vanguard_hitbox_debug`
    (CVAR_ARCHIVE, default 0). Enable live during a debugging
    session, leave off otherwise. Removes the log spam during
    normal play without losing the diagnostic capability.

Static visualisation only — hit-highlight pulse on registered
hits is deferred to a later release because the simplest
implementations would break the existing CG_PlayHitSound switch
on HIT_HEADSHOT / HIT_BODYSHOT enum values. A clean public-event
design is warranted but out of scope here.

Known issue carried over: HEAD hit-detection still ~2% rate.
The visualisation should now make it obvious whether the
HEAD-sphere is positioned correctly vs the player model's
actual head bone.

## v0.3.5 — 2026-04-28 — Diagnostic build (Pass 0.5)

Instrumentation release for Pterodactyl multi-user testing.
Adds a `VG_DIAG` server-log print after each `mdx_hit_test`
call to diagnose the ~50% "unknown" hit-rate observed in
v0.3.4 live-test sessions.

No gameplay changes vs v0.3.4. Same `human_base.hit` geometry,
same damage multipliers, same cvars. Only addition is
diagnostic logging.

To analyze: check Pterodactyl `logs/server.log` for `VG_DIAG`
lines after a test session. Each successful damage trace
will produce one line with raw `impactpoint` / `hit_type` /
`fraction` / `mod` values. Reference enum (bg_public.h):
`UNUSED=0 HEAD=1 CHEST=2 GUT=3 GROIN=4 SHOULDER_RIGHT=5
SHOULDER_LEFT=6 KNEE_RIGHT=7 KNEE_LEFT=8 LEGS=9`.

The diagnostic print will be reverted (or absorbed into
the next tune) in v0.3.6.

## v0.3.4 — 2026-04-28 — Internal testing release (Pass 1+2 tune)

Hitbox geometry retuning following v0.3.3 live-test which
showed ~10% real-hit-rate for body shots. Bone-distance
measurement (Pass 0) identified `Bip01 Spine` ↔ `Bip01 Spine1`
distance of 0.431 Quake-units as primary cause — the GUT
capsule was a 2D plane in Z. Secondary cause: limb cylinder
radii too narrow for the visible mesh thickness.

No code changes vs v0.3.3, only the `human_base.hit` asset:

  - **CHEST:** retagged `Bip01 Spine1 ↔ Bip01 Neck` (chained
    Z ≈ 16.58 units) instead of `Spine2 ↔ Spine3` (5.08).
    `scale 9 7 5` for both ends.
  - **GUT:** retagged `Bip01 Pelvis ↔ Bip01 Spine2` (chained
    Z ≈ 10.53 units) instead of `Spine ↔ Spine1` (0.43).
    `scale 9 7 5` for both ends.
  - **GROIN:** sphere radius 5 → 7 on `Bip01 Pelvis`.
  - **SHOULDER cylinders:** radius `3, 4` → `5, 5`.
  - **KNEE cylinders:** radius `3, 3` → `6, 6`.
  - **LEGS cylinders:** radius `3, 2/3` → `6, 6`.
  - **HEAD unchanged:** radius 6 sphere on `Bip01 Head`,
    matches the legacy `REALHEAD_HEAD` size and was the only
    region with reliable detection in v0.3.3.

Expected hit-rate post-tune: >70% real region detection for
body shots vs ~10% in v0.3.3. Live-test verification on
Pterodactyl follows.

## v0.3.3 — 2026-04-28 — Internal testing release

Phase 6 multi-region damage pipeline activation, deployed for
Pterodactyl test-server validation before the v0.4.0 feature
release. **Not for public distribution** — the feature is gated
behind `vanguard_hitbox_mode` (default 1) and falls back
byte-identically to vanilla on `mode 0`, but it has not yet been
validated under real network conditions with distinct clients.

  - **Multi-box damage pipeline live.** `G_Damage` now routes
    through `mdx_hit_test` against `etmain/animations/human_base.hit`
    when `vanguard_hitbox_mode >= 1`, applies per-region damage
    multipliers from the `vanguard_dmg_*` cvars, and maps the 9
    fine-grained `IMPACTPOINT_*` values onto the 4 `HR_*`
    hit-region buckets used by the existing stats array.
    Activation depends on the BONE_HITTESTS pipeline (Phase 6.0,
    `ecaaf27`), the `vg_Hitbox_*` subsystem (`0f5dfe3`), the API
    implementation (`098fa09`), and the G_Damage wiring
    (this release).

  - **11 hitbox cvars.** `vanguard_hitbox_mode` (LATCH +
    SERVERINFO, default 1) plus 9 per-region damage multipliers
    (`vanguard_dmg_head` 2.0, `_chest` 1.3, `_gut` 1.1,
    `_groin` 1.2, `_shoulder_l/r` 0.8, `_knee_l/r` 0.6,
    `_legs` 0.7) plus `vanguard_dmg_default` (1.0, fallback for
    `IMPACTPOINT_UNUSED`). All damage multipliers are ARCHIVE-
    only — admins can tune mid-match without a map restart.

  - **Mode=0 byte-identical to vanilla.** Operators rolling
    back to legacy single-region damage just set
    `vanguard_hitbox_mode 0` + map restart (the cvar is LATCH).
    The wrapper-IF + goto-label pattern in `g_combat.c:1696`
    keeps the legacy `IsHeadShot/IsLegShot/IsArmShot` chain
    untouched on mode=0.

  - **Antilag-safe.** `Bullet_Fire` already wraps the trace +
    damage call chain in `G_HistoricalTraceBegin/End`, so by the
    time `G_Damage` runs, the target is in its rewound pose.
    Multi-box mirrors the existing `IsHeadShot` /
    `mdx_gentity_to_grefEntity` pattern (`targ->timeShiftTime`
    when set, else `level.time`).

  - **Weapon-class gating mirrors `IsHeadShot`.** Multi-box only
    activates for headshot-capable weapons
    (`GetMODTableData(mod)->isHeadshot`). Explosives, grenades,
    flamethrower, knife, MG42 etc. fall through to the legacy
    chain unchanged — preserving the existing stats convention
    that "non-headshot weapons don't log a region."

## v0.3.2 — 2026-04-27

Limbo / connect screen version-string fix, menu title centring,
pause-menu logo polish, plus a multi-platform build-pipeline fix
that was caught during the same release cycle.

  - **Limbo screen showed engine version, not mod version — root
    cause + fix.** `src/qcommon/version.h:49` defines
    `ETLEGACY_VERSION` as `((char *)etlegacy_version)` — a pointer
    to a global symbol whose definition lives in
    `src/qcommon/version.c` and is compiled into every binary that
    consumes the file (engine, cgame.so, qagame.so, ...). When
    `cgame.so` is `dlopen()`ed by the engine, ELF dynamic symbol
    resolution unifies `etlegacy_version` across the loaded image
    and the **main executable's copy wins**. So our cgame.so —
    even though it has `etlegacy_version[] = "v0.3.1"` baked into
    its own `.data` section — would render whatever the engine
    binary has at runtime. On hosts with a non-tagged engine
    build (Pterodactyl, our own `build-server/etlded.x86_64`),
    that engine version is `"2.83-dirty"`, producing the
    confusing `vanguard 2.83-dirty` line on the loading screen.

    Fix in `src/cgame/cg_loadpanel.c:350`: split the rendering
    into two lines and pin the mod-version line to
    `ETL_BUILD_VERSION` — a literal-string `#define` from
    `version_generated.h` that the preprocessor inlines at the
    call site. No symbol lookup, no dynamic-linker interference,
    always shows our compile-time mod version. The engine line
    keeps `ETLEGACY_VERSION` and is honest diagnostic output —
    you can see exactly which ETLegacy build the host is running.
    Result on screen: `VanguardMod v0.3.2` over `Built on
    ETLegacy 2.83-dirty` (or whatever the host engine reports).
  - **Menu titles centred.** All four title-bar itemDefs in
    `etmain/ui/menumacros.h` (WINDOW_FUI, WINDOW_INGAME,
    SUBWINDOW, SUBWINDOWBLACK) defaulted to ITEM_ALIGN_LEFT,
    leaving every menu's heading stuck against the left edge.
    Now centred via `textalign ITEM_ALIGN_CENTER` plus
    `textalignx $evalfloat(.5*(WIDTH-4))` per macro's width
    parameter. Affects the main menu, pause menu, credits and
    every options sub-menu — single-point change, ~30 menus
    benefit.
  - **Pause-menu logo without banner-text.** New asset
    `etmain/ui/assets/vanguardmod/logo_pause.tga` — generated
    from the master by cropping the bottom "VANGUARDMOD" banner
    strip off (top 80%) and pad-centring back to a square so the
    eagle/shield/V/bayonet glyph renders unstretched at 64×64.
    `etmain/ui/assets/vanguardmod/logo_small.tga` is left in
    place; future placements that want the wordmark can use that.
  - **Build-pipeline fix: Windows DLLs now get the bumped
    version too.** `cmake/ETLVersion.cmake` only read
    `CI_ETL_TAG`/`CI_ETL_DESCRIBE` from environment variables,
    so a manual single-platform rebuild in a fresh shell that
    forgot to prefix `CI_ETL_TAG=v0.3.X` silently produced
    binaries with the upstream fallback `MAJOR.MINOR-dirty` from
    `VERSION.txt` baked in. This actually shipped during the
    initial v0.3.2 build cycle: Linux had `v0.3.2`, all eight
    Windows DLLs had `2.83-dirty`. Two-pronged fix —
    `cmake/ETLVersion.cmake` accepts the values as cmake cache
    variables in addition to env, and `scripts/bootstrap.sh`
    passes them as `-DCI_ETL_TAG=...` to all three configures.
    `docs/RELEASE_PROCESS.md` updates the build-sequence
    documentation to use the `-D` form as canonical and adds an
    incident write-up so the same trap doesn't catch the next
    bumper.

  - **Phase 5.6/5.7 v0.3.0/v0.3.1 strings deployment caveat.**
    The previous "v0.3.0" pk3 deployed earlier this week did
    actually contain the right v0.3.1 strings in cgame.so (the
    bump landed inside the f360769 commit even though the commit
    subject says 0.3.0). The user-visible "vanguard 2.83-dirty"
    bug above made it look like nothing was reaching the screen.
    It was reaching, just being shadowed by the engine global.

## v0.3.1 — 2026-04-26

Polish pass on the v0.3.0 main-menu theme.

  - **Hover text was invisible — fixed.** The brand-palette
    rollout in v0.3.0 used a sed substitution to retune all the
    button colours in `etmain/ui/menumacros.h`. The replacement
    string for the hover `forecolor` (the bright-text-on-hover
    state) accidentally dropped the alpha channel — 30
    setitemcolor lines ended up with `forecolor 1 1 1` (3
    components) instead of `forecolor 1 1 1 1` (RGBA). The
    engine's menu parser interprets that as RGB plus alpha=0, so
    the hover text rendered transparent. All 30 sites are fixed
    with a uniform `s/forecolor +1 1 1 ;/forecolor 1 1 1 1 ;/`.
  - **Discord block on the welcome panel — brand-aligned.** The
    `discordTitle` background was still ETLegacy green-grey
    (`.16 .2 .17 .8`) and the `discord_button` background, border
    and hover state were still the upstream neutral grey, because
    those itemDefs use inline `backcolor` / `forecolor` /
    `bordercolor` literals rather than the central
    `menumacros.h` macros. Inline values swapped to the brand
    palette in `main.menu`. The `discord_logo` icon-tint
    manipulations (dim grey -> white on hover) are deliberately
    left intact — those are icon brightness, not panel chrome.
  - **Pause-menu logo and version centred.** Upstream had the
    logo at `x=15..79` (centre 47) but the version label
    starting at `x=60` with `ITEM_ALIGN_LEFT`, so the two looked
    offset rather than a vertical pair. The version label is now
    centred under the logo via `rect 15 WINDOW_HEIGHT+68 64 10`
    + `ITEM_ALIGN_CENTER` with `textalignx=32`. Both `OLD_CLIENT`
    and the active branch are updated for symmetry.

### Known limitations / planned for next release

  - **Spectator HUD overlap with pause menu.** When ESC is
    pressed while spectating, the spectator hint overlay
    ("Press L to open Limbo Menu", "Press MOUSE2 to follow
    previous player", etc.) renders on top of the pause-menu
    buttons. The hint text is drawn from
    `src/cgame/cg_draw.c:3001-3022` via
    `CG_DrawCompMultilineText`, so the fix involves either
    suppressing the spectator HUD while the menu is open or
    repositioning the HUD component — both invasive. Tracked
    for a Phase 5.8 sweep.
  - **Tooltip overlap.** Pause-menu button tooltips (e.g. the
    "Disconnect your connection from current server" bubble)
    render relative to the cursor and overlap neighbouring
    buttons. Tooltip positioning is engine-controlled in
    `src/ui/ui_shared.c`, not driven by the menu file. Tracked
    for the same sweep.
  - **Limbo / connect screen version string mixes mod and
    engine versions.** The lower-right of the limbo briefing
    reads "vanguard 2.83-dirty" — combining `MODNAME` ("vanguard")
    with the upstream ETLegacy engine version ("2.83-dirty"
    from a non-tagged build). This wrongly suggests VanguardMod
    itself is at version 2.83. Planned fix: a Phase 5.8 sweep
    of the version-string call sites to display
    `VanguardMod v0.3.1` and `Built on ETLegacy 2.83` on
    separate lines.

## v0.3.0 — 2026-04-26

VanguardMod main-menu theming pass — three layers, one release.

  - **Branding completion (Schicht 1).** Pause-menu logo (the small
    one shown at the bottom-left when ESC is pressed in-game) now
    uses the VanguardMod brand asset, with a square aspect that
    matches our logo instead of the upstream 2:1 wordmark fit. The
    faint ETLegacy "LEGACY" wordmark watermark that bled through
    every menu's background_1 layer is suppressed (commented out
    in `etmain/ui/global.menu`, kept in source so the slot is
    documented for a future VanguardMod-themed watermark). The
    Discord button URL is redirected to the VanguardMod community
    server (`discord.gg/umnM8wVrth`) at both call sites
    (`main.menu` Welcome panel + `etlegacy_discord.menu` confirm
    dialog). The Welcome-panel header text reads "VANGUARDMOD"
    instead of the generic "WELCOME". The quit-credits screen
    swaps the upstream logo for ours too, with rect re-squared.
  - **Credits (Schicht 2).** New `etmain/ui/credits_vanguardmod.menu`,
    patterned after `credits_etlegacy.menu`. Header carries the
    VanguardMod logo (square 100x100 from `logo_main`), title and
    a one-line tagline; sections list LEAD DEVELOPER (wahke),
    COMMUNITY (wolffiles.eu), PROJECT (vanguardmod.com,
    GitHub repo, Discord) and BUILT ON (ETLegacy, Wolfenstein:
    Enemy Territory, id Tech 3 Engine). Bottom buttons: BACK,
    UPSTREAM CREDITS (leads into the unmodified
    `credits_etlegacy.menu` and from there the full Splash Damage
    / id Software / Activision / contributor chain), and GITHUB
    (opens the repo in the browser). The main-menu Credits button
    is repointed at `credits_vanguardmod` first; the upstream
    chain is reachable in one extra click. No upstream credits
    file is modified or removed — VanguardMod sits on top, the
    rest of the hierarchy is preserved verbatim.
  - **Brand colour palette (Schicht 3).** Central `etmain/ui/menumacros.h`
    macro defaults swapped from upstream's neutral grey + ETLegacy
    green-grey title accent to VanguardMod's black + dark-red
    palette: button rest `.05 .05 .05 .4`, button hover
    `.4 .08 .08 .5`, border `.2 .05 .05 .6`, button text
    `.7 .7 .7 1` rest / `1 1 1 1` hover, title bar background
    `.15 .03 .03 .8`, title bar text `.85 .85 .85 1`. Inline
    `backcolor` / `forecolor` / `bordercolor` in individual .menu
    files are NOT touched in this pass — visible inconsistencies
    will be addressed in a Phase 5.7 polish pass. A revert table
    is documented at the top of `menumacros.h` in case the
    palette needs rolling back.
  - **Minor (not patch) bump.** First version that ships a
    coherent brand identity rather than just a single asset
    swap; user-visible feel changes meaningfully.

## v0.2.1 — 2026-04-26

  - **VanguardMod branding in the main menu.** The welcome screen
    now shows the VanguardMod logo (eagle + shield + V + bayonet
    + banner, military black/grey/red) where ETLegacy's logo used
    to sit. Single asset swap in `etmain/ui/main.menu`; the upstream
    `etl_logo_huge.tga` is kept on disk as a fallback for future
    theming needs and not removed.
  - **Asset architecture introduced.** Master file lives at
    `assets-source/branding/logo_master.png` (2048×2048 PNG, source
    of truth, committed to the repo). Generated TGA variants in
    `etmain/ui/assets/vanguardmod/` (1024 / 256 / 64 px, 32-bit
    RGBA) are auto-packed into the pk3 by the existing
    `etmain/`-recursive glob in `cmake/ETLBuildMod.cmake`. The two
    smaller variants are not yet referenced anywhere — staged for
    future loading-screen and HUD branding.
  - **No game logic changes.** Patch bump rather than minor: pure
    UI/asset substitution. Hitboxes, dev mode, omnibot, all
    unchanged from v0.2.0.

## v0.2.0 — 2026-04-26

  - **Tighter player hitboxes for competitive play.** The standard
    upstream player bounding box was 36×36×72 units (XY ±18,
    Z -24/+48). The XY footprint was visibly more generous than
    any player model in the game and produced "phantom hit"
    feedback — shots landing visibly off-target still registering
    as a body hit. VanguardMod tightens the XY to ±16 (32×32, ~11%
    smaller) while keeping Z untouched so crouch-jump physics and
    view-height relationships are preserved. All stance-derived
    boxes (crouch, prone, dead) inherit the narrower XY since they
    only override the top-Z. Antilag inherits automatically through
    the spawn-time copy of `playerMins/Maxs` into `client->r.mins/maxs`.
  - **Head hitbox unchanged.** It already uses MDX bone-tracking
    (`mdx_head_position`, gated on `FEATURE_SERVERMDX=ON` plus the
    `g_realHead & REALHEAD_HEAD` default), which follows the helmet
    through every animation frame. Some users reported the dev-mode
    head box looking off-helmet — that is a renderer-side
    approximation gap (cg_vanguard_dev.c uses the no-MDX fallback
    math because cgame can't trivially query bones for other
    players); the server's actual damage trace lands on the helmet.
    `docs/DEV_MODE.md` now spells this out under "Known limitations".
  - **Minor bump rationale.** First version that meaningfully
    changes gameplay feel rather than tooling/infra. Player aim
    that was tuned for the wider boxes will have to re-calibrate;
    expected and intended.

## v0.1.2 — 2026-04-26

  - **Disable upstream "UPGRADE NOW" banner.** ETLegacy's UI shows a
    red "SECURITY INFORMATION / You are running old software /
    UPGRADE NOW" block on the main menu (and four spots in the
    in-game menu) whenever its compiled-in version doesn't match
    the engine's. With VanguardMod's own version scheme (v0.1.x)
    this misfires unconditionally — the comparison is between our
    mod version and the ETLegacy engine, which is always a
    mismatch. Both `OLD_CLIENT` defines in `src/ui/ui_main.c` are
    suppressed (with VANGUARD markers explaining why) so the
    banner no longer appears. When VanguardMod ships a real update
    endpoint, replace with a `VANGUARD_UPDATE_AVAILABLE` define
    against vanguardmod.com.

## v0.1.1 — 2026-04-26

  - **Dev mode** (`vanguard_dev` cvar). Server-authorised hitbox
    visualisation for hitbox tuning, match-dispute analysis and mod
    development. Boxes are colour-coded (red head / yellow torso /
    green legs-when-prone) and update at full client framerate.
    Off by default; `vanguard_dev 1` (rcon or
    `configs/vanguard_dev.cfg`) flips it on with a loud red banner.
    Auto-unlocks `sv_cheats` for in-engine inspection tools (noclip,
    cg_thirdperson, give, ...). Banner repeats every 5 minutes; an
    additional warning fires if the server is heartbeating to a
    public master list. See `docs/DEV_MODE.md`.
  - **Client-side hitbox rendering refactor.** The original
    implementation drove visualisation through the server's
    `g_debugPlayerHitboxes` path, which broadcasts ~24 EV_RAILTRAIL
    events per visible player per frame. Real-world test with two
    visible players pushed observed ping from ~30 ms to ~900 ms.
    Visualisation now runs entirely in cgame
    (`src/cgame/cg_vanguard_dev.c`) from existing snapshot data —
    zero added server traffic, identical visuals.
  - **Omni-Bot integration** for the local test server. Bots can be
    added with `bot addbot <team> <skill> [name]` so hitbox tuning
    and gameplay shakeouts no longer need two human players. The
    Omni-Bot runtime (~26 MB) is fetched from `mirror.etlegacy.com`
    by `scripts/bootstrap.sh` on first build with
    `FEATURE_OMNIBOT=ON`, and cached under `vendor/omnibot-runtime/`
    so subsequent bootstraps skip the download. Linux mod build
    only — Windows cross builds remain `FEATURE_OMNIBOT=OFF`. See
    the new "Bots" section in `docs/TESTING.md`.
  - **Known limitation:** `sv_cheats` is read-only on Pterodactyl-
    managed servers. Hitbox visualisation still works
    (it depends on no server-side cvars being flipped), but the
    CVAR_CHEAT-protected client tools (noclip, cg_thirdperson, ...)
    refuse to run with "cheats not enabled" on those hosts. Use a
    self-hosted dev server for inspection-from-arbitrary-angles.
  - **Release process.** New `docs/RELEASE_PROCESS.md` documents the
    six version-bump locations, the multi-platform build sequence,
    the pure-server pk3-only deployment caveat that motivated this
    release, and a sample bump procedure.

## v0.1.0 — initial release

  - VanguardMod scaffold overlaid on top of the full ETLegacy
    upstream source tree (imported via `scripts/bootstrap.sh`).
  - WolfGuard anti-cheat integration boundary
    (`wolfguard/wolfguard.h` + null provider). Server-side only.
  - Mod-only build flow producing `cgame`, `qagame`, `tvgame`, `ui`
    for Linux x86_64 plus Windows x64 / x86 cross builds.
  - Multi-arch `vanguard_v0.1.0.pk3` packaging via upstream's
    `mod_pk3` target with Vanguard-specific patches in
    `cmake/ETLBuildMod.cmake`.
  - Local test-server harness (`scripts/testserver/`).
  - cJSON vendored under `vendor/cjson/` to remove the
    `libcjson-dev` system dependency (relevant for MinGW cross
    builds with no system libcjson).
  - `g_xp_saver.c` neutralised: upstream-flagged as needing rework
    and depends on an SDK-internal sqlite layer not in the public
    API. Replaced with a stub (`g_xp_saver_stub.c`); restore or
    re-implement when VanguardMod's persistence story lands.


---

**Copyright Notice**

Copyright (c) 2026 wahke <info@wahke.lu> (https://wahke.lu)
Copyright (c) 2026 VanguardMod Project Contributors

Licensed under GPL-3.0-or-later. Part of VanguardMod project.
Built on ETLegacy (https://www.etlegacy.com).
