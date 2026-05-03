# Phase 8.0b — Falldamage Redesign Recon

> Pure recon. No code changes. wahke decides scope after this.

Date: 2026-05-03
Author: Claude Code (Opus 4.7) on session for wahke
Status: AUDIT (no implementation)

References Phase 7.3 audit §2 (code inventory) + §3.4 (cross-mod
comparison) + §5 (implementation recommendation). This recon does
not duplicate that material — it builds on top of it to produce a
v0.7.0 implementation spec.

## TL;DR

Phase 8.0b is **profile + cvar exposure on top of the Phase 8.0a
NULL-guard.** Phase 7.3 audit §5 recommended exposing 12 hardcoded
values as cvars; this recon refines that into a **two-tier scope
choice**:

| Tier | Cvars | Prediction risk | Effort | Recommendation |
|---|---|---|---|---|
| **Tier 1 (minimal)** | 6 | none (server-only) | small | **v0.7.0 default** |
| **Tier 2 (full)** | 16 | needs CS sync | medium | v0.7.1 follow-up |

**Tier 1 ships:** 1 profile cvar + 4 damage values + 1 GIB_HEALTH.
All server-side (`G_FallDamage` in `g_active.c`). Zero prediction
impact. Default profile `etlegacy` = zero behaviour change for
upgraders. Phase 8.0a NULL-guard untouched.

**Tier 2 adds (v0.7.1):** 6 delta thresholds + 4 PMF_TIME_KNOCKBACK
durations. These live in `PM_CrashLand` (`bg_pmove.c`) which is
shared between qagame and cgame for prediction. Exposing them
requires a configstring sync mechanism so cgame learns the
authoritative values at map start.

Architecture: standalone `vanguard_falldamage_profile` cvar (Option
A from §1) — mirrors the existing `vanguard_netcode_profile`
pattern, keeps subsystems decoupled.

## §1 — Profile architecture (A/B/C analysis)

### Option A — Standalone `vanguard_falldamage_profile` (recommended)

New top-level cvar, parallel to `vanguard_netcode_profile`:

```
vanguard_netcode_profile     = cup | public | custom
vanguard_falldamage_profile  = etlegacy | cup | public | custom
```

**Pro:**
- Mirrors the existing Phase 7.2 pattern — predictable for admins
- Maximally flexible: a cup-server can run `cup` netcode +
  `etlegacy` falldamage (in fact, that's what cup-orthodox needs
  per Phase 7.3 §3.4)
- Subsystem boundary preserved — falldamage tuning never touches
  netcode code paths
- Each cvar has one responsibility

**Con:**
- 4 profile dimensions (netcode + falldamage + future hitbox /
  movement) to keep consistent in admin's head
- No "give me the full cup posture" one-liner

### Option B — Integrate into `vanguard_netcode_profile`

`vanguard_netcode_profile cup` would auto-set falldamage too.

**Pro:** simple, single profile dimension.

**Con:**
- Conceptually wrong — "netcode" should not own falldamage tuning
- Cup-orthodox per Phase 7.3 §3.4 is `cup` netcode + **engine-default
  falldamage**. Coupling the two means a cup server gets unwanted
  falldamage tweaks just to access cup netcode.
- Future hitbox / movement profiles would all overload one cvar
- Hard to deprecate later

### Option C — Master `vanguard_profile` + sub-overrides

```
vanguard_profile             = cup | public  (master umbrella)
vanguard_netcode_profile     = (inherits or override)
vanguard_falldamage_profile  = (inherits or override)
```

Master cvar sets all sub-cvars unless individually overridden.
Memory hint: Phase 9 `vg_fun` master-switch architecture (Memory
#8) follows this pattern.

**Pro:**
- Scales to many sub-systems
- One-liner for "cup posture"

**Con:**
- Premature for v0.7.0 — only 2 profile cvars exist; over-architecting
  for a future need
- Cvar-cascade complexity (when does inherit win vs override?)
- Phase 9 `vg_fun` is a *fun-mode toggle bundle* (xpsave / doublejump
  / noselfdamage), conceptually different from a cup/public-style
  competitive profile umbrella. The two patterns shouldn't share
  cvars.

### Recommendation: Option A

**Standalone `vanguard_falldamage_profile`.** Reasons:

1. Mirrors the working Phase 7.2 pattern — admins already know
   how `vanguard_netcode_profile` works.
2. Cup-orthodox needs decoupling (cup netcode + etlegacy
   falldamage), which Option B prevents.
3. Phase 9 `vg_fun` is a different pattern (feature-toggle bundle),
   not a competitive-profile umbrella; coupling the two via Option
   C would create lock-in for a future refactor.
4. If a `vanguard_profile` master ever lands (v1.0+ maybe), it's a
   clean wrapper on top of independent sub-cvars — the sub-cvars
   don't need to know.

## §2 — Cvar surface inventory

Phase 7.3 audit §2.7 confirmed: zero falldamage cvars exist in any
mod today. v0.7.0 introduces the first cup-tunable surface.

### Tier 1 (v0.7.0 — server-side, no prediction risk)

| Cvar | Default (etlegacy) | Replaces | CVAR flags | File:line |
|---|---|---|---|---|
| `vanguard_falldamage_profile` | `"etlegacy"` | (new) | CVAR_LATCH \| CVAR_ARCHIVE \| CVAR_SERVERINFO | (new) |
| `vanguard_falldmg_dmg_10` | `"10"` | g_active.c:1007 hardcoded | CVAR_ARCHIVE | g_active.c:1005-1007 |
| `vanguard_falldmg_dmg_15` | `"15"` | g_active.c:1003 hardcoded | CVAR_ARCHIVE | g_active.c:1001-1003 |
| `vanguard_falldmg_dmg_25` | `"25"` | g_active.c:999 hardcoded | CVAR_ARCHIVE | g_active.c:997-999 |
| `vanguard_falldmg_dmg_50` | `"50"` | g_active.c:995 hardcoded | CVAR_ARCHIVE | g_active.c:993-995 |
| `vanguard_falldmg_gib_health` | `"-175"` | bg_public.h:72 #define | CVAR_LATCH \| CVAR_ARCHIVE | bg_public.h:72 |

Total: 6 cvars. All read by `G_FallDamage` server-side. No client
prediction touched.

**`vanguard_falldamage_profile` flags rationale:**

- `CVAR_LATCH`: profile change requires map restart. Mirrors
  `vanguard_netcode_profile`. Prevents mid-match drift.
- `CVAR_ARCHIVE`: persists in `etconfig_server.cfg`.
- `CVAR_SERVERINFO`: published to clients on connect. Future cgame
  HUD work can surface "Falldamage: cup" in the same way Phase 7.2
  cgame surfaces netcode profile.

**Damage value cvars CVAR_ARCHIVE only (no LATCH):** these are
read once per `EV_FALL_*` event in `G_FallDamage`. Mid-match
changes apply on the next fall — that's fine, no prediction
inconsistency because cgame doesn't know damage values (only
event types).

**`vanguard_falldmg_gib_health` CVAR_LATCH:** the `GIB_HEALTH`
#define is also referenced from non-falldamage code paths
(G_Damage's gib threshold check, limbo respawn logic). Mid-match
change would create cross-system drift. LATCH locks it for map
lifetime.

### Tier 2 (v0.7.1 — needs configstring sync)

| Cvar | Default | Replaces | CVAR flags | File:line |
|---|---|---|---|---|
| `vanguard_falldmg_delta_short` | `"7"` | bg_pmove.c:1730 | CVAR_LATCH \| CVAR_ARCHIVE | bg_pmove.c:1730 |
| `vanguard_falldmg_delta_10` | `"38.75"` | bg_pmove.c:1717 | CVAR_LATCH \| CVAR_ARCHIVE | bg_pmove.c:1664, 1717 |
| `vanguard_falldmg_delta_15` | `"48"` | bg_pmove.c:1704 | CVAR_LATCH \| CVAR_ARCHIVE | bg_pmove.c:1704 |
| `vanguard_falldmg_delta_25` | `"58"` | bg_pmove.c:1691 | CVAR_LATCH \| CVAR_ARCHIVE | bg_pmove.c:1691 |
| `vanguard_falldmg_delta_50` | `"67"` | bg_pmove.c:1678 | CVAR_LATCH \| CVAR_ARCHIVE | bg_pmove.c:1678 |
| `vanguard_falldmg_delta_die` | `"77"` | bg_pmove.c:1674 | CVAR_LATCH \| CVAR_ARCHIVE | bg_pmove.c:1674 |
| `vanguard_falldmg_kb_10` | `"1000"` | bg_pmove.c:1722 | CVAR_LATCH \| CVAR_ARCHIVE | bg_pmove.c:1722 |
| `vanguard_falldmg_kb_15` | `"1000"` | bg_pmove.c:1709 | CVAR_LATCH \| CVAR_ARCHIVE | bg_pmove.c:1709 |
| `vanguard_falldmg_kb_25` | `"250"` | bg_pmove.c:1696 | CVAR_LATCH \| CVAR_ARCHIVE | bg_pmove.c:1696 |
| `vanguard_falldmg_kb_50` | `"1000"` | bg_pmove.c:1683 | CVAR_LATCH \| CVAR_ARCHIVE | bg_pmove.c:1683 |

Total: 10 additional cvars. All in `PM_CrashLand` which runs
client-side (prediction) AND server-side (authoritative). Need
configstring sync — see §4.

### Out of scope (deferred indefinitely)

| Hardcoded value | Location | Why not exposed |
|---|---|---|
| Animation trigger `-220` | bg_pmove.c:1605 | Cosmetic threshold; below user-perceptible damage |
| Velocity-clear threshold `38.75` | bg_pmove.c:1664 | Tightly coupled to delta_10; same value by design |
| Water reductions `0.25` / `0.5` | bg_pmove.c:1642, 1646 | No cup-mod tunes water mitigation; defer until requested |
| `SURF_NODAMAGE` exemption | bg_pmove.c:1658 | Map-script flag, not a runtime cvar |

## §3 — Profile definitions

### Profile `etlegacy` (default)

Zero behaviour change vs upstream. Every cvar matches the current
hardcoded value:

| Cvar | Value |
|---|---|
| `vanguard_falldmg_dmg_10` | `10` |
| `vanguard_falldmg_dmg_15` | `15` |
| `vanguard_falldmg_dmg_25` | `25` |
| `vanguard_falldmg_dmg_50` | `50` |
| `vanguard_falldmg_gib_health` | `-175` |

A v0.6.2 server upgrading to v0.7.0 with no config edits sees
identical falldamage behaviour.

### Profile `cup`

Phase 7.3 audit §3.4 confirmed: every cup-mod (ETPro, NoQuarter,
Silent, Jaymod) uses **engine-default** falldamage. There is no
"cup-orthodox tuning" to apply.

**Recommendation:** `cup` profile is **identical to `etlegacy`**.
Either:
- (a) Make `cup` an alias — server log shows "profile=cup
  (etlegacy values)"
- (b) Drop `cup` from the enum entirely — only `etlegacy` /
  `public` / `custom` ship in v0.7.0

**My recommendation: (a) keep the alias.** Reasons:
1. Future-proofs against ETPro / cup-community tuning emerging
   later — we already have the slot
2. Operationally clean: `vanguard_netcode_profile cup` +
   `vanguard_falldamage_profile cup` reads symmetrical even
   though the latter is a no-op today
3. Costs ~5 LOC to alias

### Profile `public`

Server-friendly tweaks. Plausible defaults (each up for wahke
review):

| Cvar | Public value | etlegacy | Rationale |
|---|---|---|---|
| `vanguard_falldmg_dmg_10` | `5` | 10 | Halve the lightest fall damage; pubbers do small jumps constantly |
| `vanguard_falldmg_dmg_15` | `15` | 15 | Unchanged |
| `vanguard_falldmg_dmg_25` | `25` | 25 | Unchanged |
| `vanguard_falldmg_dmg_50` | `40` | 50 | Slightly softer big-fall damage |
| `vanguard_falldmg_gib_health` | `-300` | -175 | **Prevents gib-on-fall** — pubbers hate exploding from a single cliff drop. -300 means a player at 100 HP needs `100 + 301 = 401` damage to gib. Falldamage caps at 50, so falls never gib. Other gib paths (panzer, dynamite) still gib at 50+ damage past 0 HP because they pass the GIB threshold legitimately. |

This is one suggestion — wahke approves or counter-proposes per
public-server feedback.

### Profile `custom`

No profile-driven overrides. Each individual cvar is read
literally. If admin sets nothing, defaults match `etlegacy`. If
admin sets some, only those override.

Implementation: when `vanguard_falldamage_profile = "custom"`,
the `vg_Falldamage_Apply` function is a no-op — it doesn't touch
the individual cvars. Server log shows
"VG_Falldamage: profile=custom (admin owns it)".

## §4 — CVAR_LATCH + prediction-correctness strategy

The big risk for Tier 2 (delta_* + kb_* cvars in `PM_CrashLand`):
`bg_pmove.c` runs in BOTH qagame.so (server, authoritative) AND
cgame.dll (client, prediction). If qagame reads cvar value but
cgame uses hardcoded constant, they disagree on `EV_FALL_*` event
firing → client predicts wrong event → snapshot correction emits
visible animation/sound mismatch.

Phase 7.3 audit §2.8 already flagged the prediction constraint:
`bg_pmove.c:1660-1663` — *"velocity must be cleared in pmove,
not g_active!  (prediction will be wrong, otherwise.)"*

### Tier 1 (v0.7.0): no prediction risk

Tier 1 cvars are **only** read by `G_FallDamage` (g_active.c) on
the server side. cgame doesn't run G_FallDamage. The event
classification in PM_CrashLand stays hardcoded. Damage values
don't enter prediction. **Safe to ship as plain CVAR_ARCHIVE.**

`GIB_HEALTH` is the borderline case — also referenced from gib
checks elsewhere. CVAR_LATCH avoids mid-match drift across
systems. Tier 1 still safe.

### Tier 2 (v0.7.1): four options

**Option A — Configstring sync at map start.**
- Server computes the 10 delta+kb values from profile/cvars at
  G_InitGame
- Broadcast as a single configstring (e.g.
  `CS_VANGUARD_FALLDMG = "7 38.75 48 58 67 77 1000 1000 250 1000"`)
- cgame parses on receipt + caches in static state
- PM_CrashLand reads from cgame cache (client) or cvar (server)
- Both sides agree per map

**Pro:** clean, one CS slot, atomic per map.
**Con:** requires new CS slot allocation (check
`CS_*` enum in bg_public.h for free index), parsing code in
cgame, cache invalidation on map change.

**Option B — CVAR_SYSTEMINFO.**
- `CVAR_SYSTEMINFO` flag tells engine to push the cvar to all
  clients automatically
- cgame can `Cvar_Get` the value directly

**Pro:** zero protocol code, engine handles sync.
**Con:** SYSTEMINFO pushes via systeminfo configstring (CS_SYSTEMINFO),
which has a ~1024 byte limit. 10 new cvars consume ~150 bytes —
should fit, but adds permanent protocol weight. Also: clients
that never call `Cvar_Get(...)` for these never sync (engine is
lazy here).

**Pro/Con for SYSTEMINFO:** unsure if the engine accepts mod-defined
CVAR_SYSTEMINFO cvars. ETLegacy historically restricted SYSTEMINFO
to engine cvars (`sv_*`, `cl_*`, `g_synchronousClients`, etc.).
**Needs source verification before committing to this path.**

**Option C — Hardcode for cgame, cvar for qagame.**
- cgame stays at hardcoded constants
- qagame reads cvars
- Tolerate prediction mismatches

**Pro:** zero new protocol.
**Con:** every cup tester sees animation glitches on every fall
that crosses a tweaked threshold. Violates the prediction
constraint comment at bg_pmove.c:1660-1663. **Don't ship this.**

**Option D — Don't expose Tier 2 in v0.7.x at all.**
- Stay at server-side cvars only (Tier 1)
- Tier 2 deferred until a real cup-tester demand surfaces

**Recommendation:** **Option A (configstring sync) for v0.7.1**,
with Tier 1 shipping in v0.7.0. CS slot allocation requires
checking the CS_* enum range — a small recon item for the v0.7.1
prep, not v0.7.0.

If a cup-server wants to tune Tier 2 in v0.7.0 timeframe, the
escape hatch is `vanguard_falldamage_profile custom` + manually
setting `vanguard_falldmg_dmg_*` (works) — Tier 2 deltas stay
hardcoded but damage values cover most useful tuning surface.

## §5 — Phase 8.0a regression-safety

The 8.0a NULL-guard at `g_combat.c:1781-1784` doesn't read
falldamage values. Test matrix for v0.7.0:

| Test | Expected | Verifies |
|---|---|---|
| `die -1` rcon (the v0.5.2.2 test, `MOD_UNKNOWN`) | 0 SIGSEGV, all clients killed | 8.0a NULL-guard intact |
| Fall lethal on `oasis` cliff with profile `etlegacy` | HP loss matches engine table (10/15/25/50/gib at correct delta) | Tier 1 default-path correct |
| Same fall with profile `public` | HP loss matches public table (5/15/25/40, no gib) | Tier 1 profile flip works |
| Fall lethal with profile `custom` + `vanguard_falldmg_dmg_50 9999` | Player gibs immediately (over-damage) | Custom override works, no crash |
| Suicide (`/kill`) with each profile | Clean death, no SIGSEGV | 8.0a guard for MOD_SUICIDE intact |
| Drowning in water on `radar` with each profile | HP drains via MOD_WATER, no SIGSEGV | 8.0a guard for environmental MOD intact |

Static check before live test: `grep -A8 "vg_Hitbox_IsActive"
src/game/g_combat.c` must show the 4-clause guard unchanged. If a
v0.7.0 PR accidentally moves or weakens any of the four clauses,
the static grep catches it.

### Why the 8.0a guard is structurally protected from v0.7.0 changes

Phase 8.0b only modifies:
- `g_active.c::G_FallDamage` (reads new cvars instead of literals)
- `bg_public.h` (replaces `GIB_HEALTH` macro with cvar lookup —
  IF gib-cvar exposed; safer alternative is to keep #define and
  only override it inside `G_FallDamage`)
- New file: `g_vanguard.c::vg_Falldamage_*` subsystem

None of these touch `g_combat.c::G_Damage` where the guard lives.
Cross-file regression risk is low. CI build green confirms compile
integrity.

## §6 — CI gates strategy

Mirror the existing Omni-bot + community-banner gate pattern from
`.github/workflows/ci.yml`. Add to the Linux build job:

```yaml
- name: Verify falldamage profile cvar in qagame Linux SO
  run: |
    set -euo pipefail
    BIN=$(find build -name 'qagame.mp.*.so' -print -quit)
    test -n "${BIN}"
    PROFILE=$(strings -a "${BIN}" | grep -c "vanguard_falldamage_profile")
    DMGCV=$(strings -a "${BIN}" | grep -c "vanguard_falldmg_")
    LOGLINE=$(strings -a "${BIN}" | grep -c "VG_Falldamage:")
    echo "profile=${PROFILE} dmg_cvars=${DMGCV} log=${LOGLINE}"
    if [ "${PROFILE}" -lt 1 ] || [ "${DMGCV}" -lt 5 ] || [ "${LOGLINE}" -lt 1 ]; then
      echo "::error::v0.7.0 falldamage cvar surface incomplete"
      exit 1
    fi
```

Tier 1 thresholds: profile≥1, dmg_cvars≥5 (4 dmg + 1 gib), log≥1.

If Tier 2 ships in v0.7.1, bump `dmg_cvars` threshold to 15.

### Diagnostic infrastructure (Phase 7.0 lessons-learned #1)

Boot-line log analogous to `VG_Netcode: profile=cup`:

```
VG_Falldamage: profile=etlegacy (defaults active)
  - dmg_10=10  dmg_15=15  dmg_25=25  dmg_50=50  gib_health=-175
```

When profile is `custom`, log shows current cvar values (so admin
can verify their server.cfg was picked up).

When profile is `public`, log shows the public override values
applied (so admin knows the difference from etlegacy).

The log also satisfies the smoke-test requirement — CI's
`VG_Falldamage:` grep proves the boot path executed.

## §7 — Documentation touchpoints

| File | Change | When |
|---|---|---|
| `docs/CUP_VS_PUBLIC.md` | New top-level section "Falldamage" parallel to existing sv_fps + g_pronedelay sections. Same table format. | v0.7.0 PR |
| `docs/notes/PHASE_7_3_AUDIT.md` | Add §10 "Phase 8.0b implementation notes" — link forward to this audit + the v0.7.0 PR | v0.7.0 PR |
| `docs/RELEASE_NOTES.md` | New v0.7.0 entry above v0.6.2 — covers Tier 1 cvars, profile defaults, public vs etlegacy diff | v0.7.0 PR |
| `docs/FALLDAMAGE_PROFILE_REFERENCE.md` | **New file** — per-cvar reference (default, valid range, semantics, profile values). Length ≈ 100 LoC for Tier 1; ~250 LoC if Tier 2 lands. | v0.7.0 if Tier 1 has 6 cvars (yes) |
| `docs/notes/PHASE_8_0B_AUDIT.md` | Add §13 "Execution log" after v0.7.0 ships (mirror Phase Copyright-Sweep §9 pattern) | v0.7.0 PR |

## §8 — Roll-out strategy

### Default = etlegacy (mandatory)

`vanguard_falldamage_profile` defaults to `"etlegacy"`. A v0.6.2 →
v0.7.0 upgrade with zero config changes preserves byte-identical
falldamage behaviour. **This is the load-bearing safety
guarantee** — any divergence from this is a regression.

### Live-test plan (post-build, pre-tag)

Reuse the v0.5.2.2 die-command crash test as the regression
harness. Spin up testserver per the existing `scripts/testserver/run.sh`
flow.

**Test 1 — etlegacy regression (mandatory pass before tag):**

1. Boot testserver with default profile (etlegacy)
2. Verify boot log: `VG_Falldamage: profile=etlegacy (defaults active)`
3. Drop tests on `oasis` from the canonical heights (radar tower
   roof, balcony to flag, ramp). Record HP loss per drop.
4. Compare against the engine-default table from Phase 7.3 §2.2:
   - delta > 38.75 → 10 dmg
   - delta > 48 → 15 dmg
   - delta > 58 → 25 dmg
   - delta > 67 → 50 dmg
   - delta > 77 → gib
5. **Pass criterion:** zero deviation from the table.

**Test 2 — die-command crash regression (Phase 8.0a):**

1. Issue `die -1` 4 times across a 60-second test (per v0.5.2.2
   pattern, 8 kills total)
2. **Pass criterion:** 0 SIGSEGV, all kills register, server alive.

**Test 3 — public profile diff:**

1. Restart with `vanguard_falldamage_profile public`
2. Verify boot log shows public override values
3. Same drop matrix as Test 1; record HP loss
4. Compare against the public table from §3
5. **Pass criterion:** values match the public table; no falls
   produce gibs (gib_health=-300 means falls cap at 50 dmg, no
   gib path).

**Test 4 — custom profile sanity:**

1. Restart with `vanguard_falldamage_profile custom`
2. Set individual cvars in server.cfg to extreme values (e.g.
   `vanguard_falldmg_dmg_50 200`)
3. Drop from a 50-dmg height
4. **Pass criterion:** player loses 200 HP (instant death),
   server alive.

If all 4 tests pass, v0.7.0 is ready for tag. If Test 1 fails,
the `etlegacy` profile has drift — block release.

## §9 — Open decisions for wahke

1. **Profile architecture** — Option A (standalone cvar,
   recommended) vs B (integrated with netcode) vs C (master
   umbrella)?
2. **Scope tier** — Tier 1 (6 cvars, server-side only,
   recommended for v0.7.0) vs full Tier 1+2 (16 cvars, needs CS
   sync, push to v0.7.1)?
3. **Tier 2 prediction strategy (if v0.7.0 includes Tier 2)** —
   Option A (configstring sync, recommended) vs B (CVAR_SYSTEMINFO,
   needs engine verification) vs D (defer Tier 2 entirely)?
4. **`cup` profile** — alias for `etlegacy` (recommended) or omit
   from enum entirely? (audit §3.4 says cup-orthodox = engine
   defaults)
5. **`public` profile baseline values** — accept the §3 table
   (dmg_10=5, dmg_50=40, gib_health=-300) or counter-propose?
6. **`vanguard_falldmg_gib_health` exposure** — include in v0.7.0
   (recommended; LATCH-protected) or skip until §3 public profile
   demand is verified by cup testers?
7. **CI gate threshold** — strict (`dmg_cvars≥5` exact) vs
   lenient (`≥1`)? Strict catches accidental drops; lenient is
   forgiving during incremental landings.

## §10 — Risk map per sub-topic

| Sub-topic | Risk | Why |
|---|---|---|
| `vanguard_falldamage_profile` cvar registration | LOW | Mirrors Phase 7.2 `vanguard_netcode_profile` pattern; well-trodden |
| `vanguard_falldmg_dmg_*` cvars (4) | LOW | Server-side only, replaces 4 hardcoded `damage = N;` lines |
| `vanguard_falldmg_gib_health` cvar | MED | `GIB_HEALTH` is referenced from non-falldamage paths (G_Damage gib check, limbo logic). Need to confirm only `G_FallDamage` reads the cvar; other paths still use the #define for backward-compat |
| Profile `public` baseline values | LOW-MED | Default values are conservative; public-server feedback may demand re-tuning in v0.7.0.x |
| Boot-log diagnostic | LOW | Pure G_Printf addition; copy-paste of VG_Netcode pattern |
| CI gate addition | LOW | Same shape as existing Omni-bot + banner gates |
| **Tier 2 `delta_*` cvar exposure** | MED-HIGH | Prediction-correctness constraint (§4); needs CS sync infrastructure |
| **Tier 2 `kb_*` (PMF_TIME_KNOCKBACK) exposure** | MED | Same prediction risk as delta_*; cup-mods don't tune these |
| Lag-spike z-velocity clamp (deferred to v0.8.0) | MED-HIGH | Phase 7.3 §2.5 already documented; deliberately out of v0.7.0 |
| Class-based modifiers (deferred indefinitely) | DO-NOT | Cup-orthodox forbids per Phase 7.3 §3.4 |

## §11 — Implementation-order recommendation

### v0.7.0 — Tier 1 (recommended)

1. Add `vanguard_falldamage_profile` cvar
   (CVAR_LATCH|CVAR_ARCHIVE|CVAR_SERVERINFO, default `"etlegacy"`)
2. Add `vanguard_falldmg_dmg_{10,15,25,50}` cvars (CVAR_ARCHIVE)
3. Add `vanguard_falldmg_gib_health` cvar (CVAR_LATCH|CVAR_ARCHIVE)
4. Subsystem in `g_vanguard.c`:
   `vg_Falldamage_Init / Apply / Shutdown / IsCustom`
5. Modify `g_active.c::G_FallDamage` to read cvars via
   `vg_Falldamage_GetDmg(event)` getter
6. Optional: also gate `GIB_DAMAGE` macro callsite in
   `G_FallDamage` to use `vg_Falldamage_GetGibHealth()`
7. Boot log line in `G_InitGame` after `vg_Falldamage_Init()`
8. CI gate (§6 yaml snippet)
9. Docs: CUP_VS_PUBLIC.md + RELEASE_NOTES.md +
   FALLDAMAGE_PROFILE_REFERENCE.md + Phase 8.0b audit §13
10. Live-test (§8 plan) before tag

Estimated effort: 1 PR, 5–7 commits (subsystem + getter +
g_active.c integration + cvar registration + CI gate + docs +
audit-doc update). ~250 LOC code + ~400 LOC docs.

### v0.7.1 — Tier 2 (follow-up if cup-tester demand)

1. Allocate new `CS_VANGUARD_FALLDMG` configstring slot
   (verify free index in bg_public.h CS_* enum)
2. Add `vanguard_falldmg_delta_*` (6) and `vanguard_falldmg_kb_*`
   (4) cvars (CVAR_LATCH|CVAR_ARCHIVE)
3. Server: `vg_Falldamage_PublishCS()` writes the 10-value CS at
   map start
4. Cgame: parse CS on receipt, cache in static state
5. Modify `bg_pmove.c::PM_CrashLand` to read from cache (cgame)
   or cvar (qagame)
6. Bump CI gate threshold to `dmg_cvars≥15`
7. Docs update (FALLDAMAGE_PROFILE_REFERENCE.md grows ~150 LOC)
8. Live-test: prediction-correctness specifically — record-replay
   compare client + server fall events, must agree

Defer indefinitely if no cup-tester reports lethal-fall threshold
issues with Tier 1 alone.

### v0.8.0 — Lag-spike z-velocity clamp (separate phase)

Per Phase 7.3 §5 v0.6.0 plan: optional `vanguard_falldamage_lagspike_clamp`
cvar gating a sanity-clamp on `pml.previous_velocity[2]` in
PM_CrashLand. Needs network-hitch integration test (netem). Out
of v0.7.x scope.

### Deferred indefinitely

- pm_* cvar exposure (Phase 7.3 §5: only revisit if cup community
  asks)
- Class-based falldamage modifier (cup-orthodox forbids)
- Per-map falldamage overrides (no demand surfaced)

## §12 — References

### Code

- `src/game/bg_public.h:65-73` — `GIB_HEALTH`, `GIB_DAMAGE` macros
- `src/game/bg_public.h:1408-1415` — `EV_FALL_*` event enum
- `src/game/bg_pmove.c:1594-1742` — `PM_CrashLand` (delta math, threshold cascade)
- `src/game/bg_pmove.c:1660-1663` — prediction-correctness constraint comment
- `src/game/g_active.c:972-1015` — `G_FallDamage` (event → damage value)
- `src/game/g_active.c:1024-1050` — `ClientEvents` (EV_FALL_* dispatch)
- `src/game/g_combat.c:1781-1784` — Phase 8.0a NULL-guard
- `src/game/g_vanguard.c:385-420` — `vg_Hitbox_IsSelfDamageMod` (covers MOD_FALLING)
- `src/game/g_vanguard.c:518-595` — Phase 7.2 `vg_Netcode_*` subsystem (pattern to mirror)
- `src/game/g_cvars.c:296` — `g_pronedelay` storage example (cvarTable pattern)

### Audit cross-refs (do not duplicate)

- Phase 7.3 audit `docs/notes/PHASE_7_3_AUDIT.md`:
  - §2 — Falldamage code inventory (12 hardcoded values mapped)
  - §3.4 — Cross-mod falldamage table (ETPro / NoQuarter / Silent / Jaymod / ETLegacy all use engine defaults)
  - §5 — Implementation order recommendation (v0.6.0/v0.6.1 covered; v0.7.0 = Phase 8.0b)

### ETLegacy upstream

- Issue tracker filter: <https://github.com/etlegacy/etlegacy/issues?q=falldamage+OR+%22fall+damage%22+OR+EV_FALL>
  (no open issue currently blocks Phase 8.0b at the time of recon)
- `meansOfDeath` table for `MOD_FALLING`: `bg_misc.c:289` —
  weapon `WP_NONE`, multiplier `0.f`, weapon stat `WS_MAX`
  (uncounted)

### ETPro / Cup config sources

- `msh100/ETPro-configs` global1.config / global6.config — confirmed
  no `mp_falldamage` / `b_falldamage` cvars (engine defaults
  inherited)
- Crossfire / EuroCup distributed config packs — no falldamage
  overrides
- VanguardMod's `etmain/configs/legacy6.config` — same (no
  falldamage overrides)

### Phase 7.0 lessons-learned applied

1. **Diagnostic infrastructure** — `VG_Falldamage:` boot log line
   (§6) lands with v0.7.0, not retrofitted later
2. **Build-flag mismatches** — CI gate (§6 yaml) extends the
   existing `strings | grep` pattern from Omni-bot + community-banner
3. **Crash-bugs vs tuning-bugs split** — Phase 8.0a (crash) shipped
   in v0.5.2.2; Phase 8.0b (tuning) is v0.7.0. Recon §5 explicitly
   documents that the crash-fix is preserved

## §13 — Architectural pivot (post-recon, 2026-05-03)

After this recon completed, wahke decided to ship the **vg_fun
master-switch foundation first** (v0.7.0) and move Falldamage to
**v0.7.1** as the first feature gated by vg_fun. Reasoning: building
Falldamage as `vanguard_falldamage_*` cvars first and renaming to
`vg_fun_falldmg_*` later would create tech debt.

The Tier 1 scope from §2 (6 cvars: profile + 4 damage values + 1
GIB_HEALTH) is **unchanged in scope, just renamed**:

| Recon name | v0.7.1 name |
|---|---|
| `vanguard_falldamage_profile` | (dropped — replaced by `vg_fun` master) |
| `vanguard_falldmg_dmg_10` | `vg_fun_falldmg_dmg_10` |
| `vanguard_falldmg_dmg_15` | `vg_fun_falldmg_dmg_15` |
| `vanguard_falldmg_dmg_25` | `vg_fun_falldmg_dmg_25` |
| `vanguard_falldmg_dmg_50` | `vg_fun_falldmg_dmg_50` |
| `vanguard_falldmg_gib_health` | `vg_fun_falldmg_gib_health` |

The standalone `vanguard_falldamage_profile` cvar (§1 Option A) is
no longer needed: vg_fun is the umbrella mode switch. `vg_fun=0`
locks all sub-cvars to cup-defaults via the `vg_Fun_GetInt`
helper; `vg_fun=1` unlocks them.

The CVAR_LATCH discussion in §4 is **superseded** for these cvars —
under the vg_fun foundation, sub-cvars are `CVAR_ARCHIVE` (no
LATCH). The master `vg_fun` is `CVAR_LATCH`, which provides
cup-integrity at the mode level. Sub-cvars take effect on the next
helper read once the master is `1` after a map_restart, so
prediction-correctness is preserved without needing a configstring
sync.

This pivot is orthogonal to §5 (Phase 8.0a regression-safety) —
the NULL-guard at `g_combat.c:1781-1784` is unchanged; the v0.7.1
implementation modifies only `g_active.c::G_FallDamage` and
`bg_public.h::GIB_DAMAGE` macro callsite.

**Reference:** `docs/notes/PHASE_9_0_VG_FUN_FOUNDATION_AUDIT.md`
documents the vg_fun foundation architecture in detail.
