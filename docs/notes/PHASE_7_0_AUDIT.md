# Phase 7.0 — Bullet-Trace AABB Tightening — Recon

**Date:** 2026-04-30
**Status:** Recon ✅ → Implemented in v0.5.1 (`fix(server): strict-hitbox
reject AABB-only hits via IMPACTPOINT_UNUSED check`). See section
"Implementation notes" at the bottom of this document for what
actually shipped vs the recon recommendation.
**Scope:** Bullet-trace tightening (Option A) + strict-mode-inconsistency
bonus question (Q D).

---

## TL;DR — recon flips two assumptions

  * **Option A is technically feasible but the actual fix is one
    line, not a custom trace layer.** Every concern Option A
    addresses (AABB-only hits, "shot beside model still kills")
    is structurally caused by a single broken predicate in the
    v0.4.3 strict-mode logic. Fixing the predicate makes the
    existing AABB-broad-phase + capsule-narrow-phase chain
    correct — no new trace-volume work needed.
  * **The Bonus Q (D) is not a separate inconsistency.** It is
    the same bug. `MOD_GARAND` killing on `hit_type=0
    impactpoint=0` despite `vanguard_hitbox_strict 1` is the
    smoking gun: strict-mode never actually rejects AABB-only
    hits today.

The combined fix is small enough that Phase 7.0 and the strict-
mode-bug fix both ship as v0.5.1 in the same commit.

---

## A. Option A feasibility (the headline question)

### Bullet-trace flow today

`g_weapon.c:3596` (inside `Bullet_Fire_Extended`):

```c
G_Trace(source, &tr, start, NULL, NULL, end, source->s.number,
        MASK_SHOT);
```

Point trace (`mins=NULL maxs=NULL`) against the world AND every
linked entity's bounding box. `MASK_SHOT` matches solid world
brushes plus `CONTENTS_BODY` (player AABB). The closest hit wins;
`tr.entityNum` identifies what was struck, `tr.endpos` is the
hit position.

Player AABB at `r.mins / r.maxs` is roughly:

  - Standing: `(-18, -18, -24) / (18, 18, 48)` = 36×36×72 units
  - Crouching: same xy, shorter z (~40 z-extent)
  - Prone: shorter z, longer x/y (~32 z-extent)

Visible mesh is narrower. Phase 6's `human_base.hit` capsules
are tighter still — between the model and the AABB sits ~5–12
units of "tolerance" the trace will catch as a hit.

### Where multi-region runs

After `tr.entityNum` resolves to a player, `Bullet_Fire_Extended`
calls `G_DamageExt` → `G_Damage`. Inside `G_Damage`
(`g_combat.c:1733-1842`):

```c
if (vg_Hitbox_IsActive() && targ->client && targ->health > 0
    && GetMODTableData(mod)->isHeadshot)
{
    mdx_gentity_to_grefEntity(targ, &refent, ...);
    if (mdx_hit_test(muzzleTrace, point, targ, &refent,
                     &mdx_hit_type, &mdx_fraction, &mdx_ip)) {
        // log + apply multiplier + goto skip_legacy
    }
    if (vg_Hitbox_StrictMode()) { return; }
    // else fall through to legacy chain
}
```

`mdx_hit_test` (`g_mdx.c:2754-2953`) walks the ten capsules from
`human_base.hit`, finds the best matching impactpoint at the
trace endpoint, and writes `mdx_hit_type` / `mdx_ip` /
`mdx_fraction` to the caller's locals.

### What "Option A" would need

The user's brief proposed a custom bullet-trace-layer that only
operates against the capsules — replacing or augmenting the
engine's AABB trace. Two reasons this is **not** the right move:

  1. **Performance.** Each player has 10 capsules. A custom
     trace volume that runs all 10 per bullet (vs one AABB trace
     today) would be ~10× the cost on the hot path. With cup
     `sv_fps = 40` and busy fights, this matters.

  2. **Behaviourally redundant.** AABB-broadphase + capsule-
     narrow-phase already produces the desired result *if the
     narrow-phase rejection is wired up correctly*. The bullet
     hits AABB, capsules say "no actually that wasn't a body
     part", the damage is rejected. Visually identical to "the
     bullet missed". No new trace volume needed.

  3. **The actual hit-position visualisation isn't broken.**
     `tr.endpos` lands on the AABB face. The bullet impact
     effect at that position is fine — it's where the bullet
     went. The bug is that *damage* gets applied; the bullet
     itself is in the right place.

### What's actually broken

The narrow-phase rejection — the strict-mode check at
`g_combat.c:1830` — never fires for the typical
"shot-beside-model" case. Reason follows in section D below.

**Verdict on Option A:** technically feasible, but the
better-cost-equivalent solution is to fix the narrow-phase
rejection that's already wired up but structurally broken. One
line of code, not a custom trace layer.

---

## B. Implementation sketch (the actual fix)

### The bug surface

`mdx_hit_test` always returns `qtrue` (`g_mdx.c:2952`) regardless
of whether any capsule matched. The function uses sentinel values
to communicate "no match":

  - `*hit_type = MDX_NONE` (= 0)
  - `*impactpoint = IMPACTPOINT_UNUSED` (= 0)
  - `*fraction = 1.0` (clamped from sentinel 2.0 if no match)

Only the startup-error path returns `qfalse` (line 2782, "no hit
file loaded for this character"). For normal play with a loaded
`human_base.hit`, every call returns `qtrue`.

The v0.4.3 strict-mode wrapper assumed the opposite:

```c
if (mdx_hit_test(...)) {
    // hit (success path)
}
if (vg_Hitbox_StrictMode()) {
    return;  // unreachable in practice
}
```

The `return` is **never executed in normal play**. The "miss
path" the comment talks about doesn't exist as `qfalse` — it
exists as `(qtrue, IMPACTPOINT_UNUSED)`.

### The one-line fix

Inside the existing `if (mdx_hit_test(...))` success branch,
before the multiplier application:

```c
if (mdx_hit_test(...))
{
    // VANGUARD: AABB-but-no-capsule rejection. mdx_hit_test
    // always returns qtrue; an "actual miss" comes back as
    // mdx_ip == IMPACTPOINT_UNUSED (no capsule matched). Under
    // strict mode, treat that as a clean miss and return
    // without applying any damage. This is the correct surface
    // for the v0.4.3 strict-mode logic — the prior
    // `if (vg_Hitbox_StrictMode()) return` after the if-branch
    // was structurally unreachable.
    if (mdx_ip == IMPACTPOINT_UNUSED && vg_Hitbox_StrictMode()) {
        if (vg_Hitbox_DebugActive()) {
            G_Printf("VG_DIAG: strict-hitbox reject "
                     "(AABB hit but no capsule) "
                     "attacker=%d target=%d weapon=%d mod=%d\n",
                     (int)(attacker - g_entities),
                     (int)(targ - g_entities),
                     (int)attacker->s.weapon, (int)mod);
        }
        return;
    }

    // ... existing multiplier logic ...
    goto vg_skip_legacy_hit_chain;
}

// the OLD reject block becomes dead code (mdx_hit_test always
// returns qtrue); remove or leave with a comment explaining
// why it's vestigial.
```

The check is INSIDE the `mdx_hit_test == qtrue` branch (which is
always taken), gated on `mdx_ip == IMPACTPOINT_UNUSED`, and only
fires when strict mode is on. Default-1 strict mode now actually
rejects AABB-only hits — which is what v0.4.3 was supposed to do.

### What about non-isHeadshot weapons?

The outer gate at line 1733 also requires `isHeadshot`. Mounted /
mobile MGs (`MOD_MACHINEGUN`, `MOD_BROWNING`, `MOD_MG42`,
`MOD_MOBILE_MG42`, `MOD_MOBILE_BROWNING`) are bullet-firing but
have `isHeadshot = qfalse`, so they bypass the multi-region
branch and run on the legacy AABB path unconditionally.

For "shoot beside model" with a mounted MG: still applies damage
(legacy path). Open question for the user — extend the gate to
all bullet-firing weapons (drop the `isHeadshot` requirement),
or keep mounted MGs on the legacy AABB path and live with the
inconsistency?

  - **Drop isHeadshot requirement:** consistent strict-mode
    semantics across all hitscan. Behavioural change for mounted
    MGs (will start rejecting AABB-only hits). One-line gate
    change.
  - **Keep current gate:** byte-identical mounted-MG behaviour
    vs v0.4.x. Inconsistent but conservative.

Recommendation: **drop the isHeadshot requirement**. The whole
point of strict-mode is that AABB-only hits shouldn't apply
damage; that should be true for every hitscan weapon, not just
rifles/pistols/SMGs.

### Cvar architecture

No new cvar needed. The existing `vanguard_hitbox_strict`
(default 1) is the toggle. It's already documented in
`CUP_VS_PUBLIC.md`, `RELEASE_NOTES.md` v0.4.3, and the inline
comment block in `g_combat.c`. Cup admins who want
byte-identical legacy behaviour set it to 0; that path falls
through to the legacy chain via the `mdx_ip == IMPACTPOINT_UNUSED
&& strict` check failing.

---

## C. Risk-Assessment

### Performance

**Better than v0.4.3.** No new trace work. The change is one
extra `if (impactpoint == 0 && cvar)` check inside the existing
multi-region branch — a single integer comparison plus a cached
cvar read. Not measurable.

### Edge-cases

  * **Through-bullets (ET_EXPLOSIVE):** unaffected. The
    recursive `Bullet_Fire_Extended` call at `g_weapon.c:3682`
    starts a fresh trace from the broken-bmodel position. Each
    recursion goes through `G_Damage` independently, so each
    bullet-segment gets the same strict-mode treatment. No
    behavioural change.
  * **Multi-hit targets (akimbo, shotgun spread):** each
    individual bullet calls `Bullet_Fire_Extended` once. Strict-
    mode applies per-bullet, which is correct.
  * **Splash damage (grenades, panzer, mortar, airstrike,
    landmine, satchel):** all `isExplosive=qtrue, isHeadshot=
    qfalse`, route through `radius_damage`, never enter the
    multi-region branch. **Unaffected by the fix.**
  * **Mounted/mobile MGs:** see "What about non-isHeadshot
    weapons" above. Default isHeadshot gate keeps them on the
    legacy AABB path; recommendation is to drop the gate so they
    benefit from strict-mode too.
  * **Knife:** `MOD_KNIFE` is not bullet-fired, has its own
    melee-range check. Outside Phase 7.0 scope.

### Interaction with existing strict-mode

The current strict-mode check (`g_combat.c:1830`) becomes dead
code — the new check inside the success branch supersedes it.
Either:

  (a) Remove the dead block entirely (cleanest).
  (b) Keep it as a no-op safety net for any future code path
      that makes `mdx_hit_test` actually return qfalse (e.g. a
      character without a hit file loaded).

Recommendation: (a). If a future change makes `mdx_hit_test`
return qfalse for a non-startup-error case, that's a regression
test for whatever new behaviour we want at that point. Better to
delete the dead block now than leave a misleading comment.

### Interaction with multi-region damage multipliers

When the new check fires (AABB-only + strict), no multiplier is
applied — the function returns before the `vg_Hitbox_DamageMultiplierFor`
call. That's correct: the entire damage event is being rejected.

When the check doesn't fire (impactpoint matches a capsule, OR
strict-mode is off), the multiplier logic runs unchanged. v0.4.3
behaviour preserved for the success path.

### Behavioural change for cup admins

Cup servers running with strict-mode default will see fewer
"phantom kills" — players hit beside the visible mesh no longer
take damage. This is what cup admins have been asking for, but
it IS a behavioural change vs v0.4.3-actually-shipped. Worth a
clear release-notes line so cup organisers know what to expect.

For public servers running with strict-mode on (the v0.5.0
default), same change. Public-pub players might initially
notice "I missed but used to count as a hit" — likely positive
reception once they understand it.

For anyone who sets `vanguard_hitbox_strict 0`: byte-identical
to v0.4.3-actually-shipped. Escape hatch preserved.

---

## D. Bonus Q — strict-mode inconsistency root cause

**Yes, it's a latent bug. Yes, fast fixable. Yes, same patch.**

### The user's symptom

`hit_type=0 impactpoint=0` with `MOD_GARAND` (mod=11) killed a
bot despite `vanguard_hitbox_strict 1`. Other MODs sometimes
rejected, sometimes applied. Inconsistent.

### Root cause

1. `mdx_hit_test` (`g_mdx.c:2754-2953`) always returns `qtrue`
   in normal play. "No capsule matched" is communicated via
   `*impactpoint = IMPACTPOINT_UNUSED` (= 0), not via the return
   value.

2. The v0.4.3 strict-mode reject at `g_combat.c:1830` is in the
   `else` of `if (mdx_hit_test(...))`. Since the if always
   succeeds, the strict-mode reject NEVER FIRES in normal play.

3. Inside the success branch, `vg_Hitbox_DamageMultiplierFor(0)`
   returns `dmg_default.value` (= 1.0). Damage is multiplied by
   1.0, applied, and `goto vg_skip_legacy_hit_chain` skips the
   legacy chain. The hit goes through.

The "inconsistent" behaviour the user observed is actually
**100% consistent**: every hit goes through with `dmg_default`
multiplier, the strict cvar is essentially ignored. The
`hit_type=0 impactpoint=0` log line is the smoking gun — that's
exactly what `mdx_hit_test` writes when no capsule matched, and
it shouldn't have been logged at all if the strict-mode logic
worked (the function should have early-returned before the log).

### Why some MODs felt rejected

If the user observed `MOD_GRENADE` "rejected" — that's not
strict-mode rejecting, that's `isHeadshot=qfalse` causing the
grenade to bypass the multi-region branch entirely and hit
legacy-AABB damage with normal grenade falloff. From the
outside it looks like rejection but it's a different code path.

If the user observed `MOD_MACHINEGUN` (mounted MG) "rejected" —
same thing. `isHeadshot=qfalse` bypasses the gate.

For genuine `isHeadshot=qtrue` weapons, **strict-mode never
rejects today**. That's the whole bug.

### The same one-line fix solves both

The fix in section B catches `IMPACTPOINT_UNUSED` correctly and
emits the proper `VG_DIAG: strict-hitbox reject` log line. Cup
admins suddenly start seeing those reject lines on every shot
that's beside the model, exactly what they want. The
"inconsistency" disappears because there was never an
inconsistency — only a bug that meant strict-mode wasn't doing
anything.

### Phase 7.0 scope decision

**Recommendation: ship one combined commit.** The strict-mode
fix (Q D) and the AABB-tightening goal (Q A/B/C) are the same
fix from two angles. Splitting them would commit a misleading
narrative (Q D as a "bonus" implies it's separable, but it's
not).

Single commit message: `fix(server): strict-hitbox reject
AABB-only hits via IMPACTPOINT_UNUSED check`. Covers both
concerns; release notes for v0.5.1 cite both the user-visible
problem ("shots beside model no longer damage") and the
underlying cause ("v0.4.3's strict-mode predicate was
structurally wrong").

---

## Open questions for the user before Phase 7.1

1. **Drop the `isHeadshot` gate?** Default behaviour change for
   mounted MGs (Browning, MG42 emplacement, mobile MG42, etc.).
   Pro: consistent strict-mode semantics. Con: cup admins who
   relied on legacy MG behaviour will notice. **Recommendation:
   drop it.** Cup play rarely uses mounted MGs against players
   anyway (mostly area denial / dynamite covering).

2. **Remove or keep the dead strict-mode block at line 1830?**
   Recommendation: remove (cleanest).

3. **Diagnostic-print rate-limit.** With strict-mode actually
   firing, the `VG_DIAG` print could spam under
   `vanguard_hitbox_debug 1` during a busy fight (every shot
   beside the model, every aim-jitter that misses). Keep
   un-throttled (matches the existing `mdx_hit_test` debug
   line cadence) or per-attacker rate-limit? Recommendation:
   keep un-throttled — admins enable debug for tuning sessions,
   not normal play.

4. **Versioning.** Phase 7.0 lands as v0.5.1 (per the user-
   provided plan). Single commit, single release. RELEASE_NOTES
   section explains the structural bug. **Recommendation: ship
   it.** No reason to defer.

5. **Behaviour-test expectations** for the live test on
   Pterodactyl with Alphaloki:
   - Shot directly on visible model → hit, region-correct
     damage (multiplier applied) — UNCHANGED from v0.5.0
   - Shot ~6 units beside model (in AABB tolerance) → no
     damage, server log `VG_DIAG: strict-hitbox reject (AABB
     hit but no capsule)` if `vanguard_hitbox_debug 1`
   - Shot at gap between two adjacent capsules (e.g. between
     thigh and groin) → no damage, same reject log line
   - Shot with `vanguard_hitbox_strict 0` → byte-identical to
     v0.4.3-actually-shipped (legacy chain takes over)
   - Mounted MG shot beside model → IF gate dropped: rejected;
     ELSE applied via legacy

The "gap between two adjacent capsules" case is interesting —
it's where the user might see "I clearly hit the model but no
damage". If that becomes a real complaint, Phase 7.0.1 = tune
capsule overlap in `human_base.hit`. Out of scope for Phase 7.0
itself but worth flagging for the live-test feedback loop.

---

## Files that would change

  * `src/game/g_combat.c` — multi-region branch in `G_Damage`,
    ~10 lines added/removed (new check inside success branch,
    optionally remove the dead reject block).
  * `docs/RELEASE_NOTES.md` — v0.5.1 section explaining the
    structural fix and the user-visible behavioural change.
  * `VANGUARD_VERSION` — bump to v0.5.1.
  * `docs/CUP_VS_PUBLIC.md` — possibly update the "strict
    mode" paragraph to clarify what it actually does (since
    v0.4.3 was structurally wrong, the doc is now describing
    the v0.5.1 behaviour).

Three commits feels right:

  1. `fix(server): strict-hitbox reject AABB-only hits via
     IMPACTPOINT_UNUSED check`
  2. `docs: update CUP_VS_PUBLIC.md strict-mode description for
     v0.5.1` (only if the existing doc misrepresents v0.4.3
     behaviour — quick read says the doc describes the INTENT
     correctly even though the impl was wrong, so this commit
     might collapse to nothing)
  3. `chore: bump to v0.5.1`

---

## Estimated effort

  * Implementation: 30 minutes
  * Doc updates: 30 minutes
  * Multi-platform build: 10 minutes
  * Live-test on Pterodactyl with Alphaloki: out-of-band

Total Claude-time: ~1.5 hours.

---

## Implementation notes (v0.5.1)

Implemented in commit on top of `000a34b` (v0.5.0 release helper).
Touched files:

  * `src/game/g_combat.c` — multi-region branch in `G_Damage`.
    Net diff: ~25 insertions, ~13 deletions.
    - `isHeadshot` removed from outer gate (Q1: yes per user).
    - `if (mdx_ip == IMPACTPOINT_UNUSED && vg_Hitbox_StrictMode())`
      reject added at top of success branch.
    - Old strict-mode block at line ~1830 deleted (Q2: yes).
    - Diagnostic print un-rate-limited (Q3: confirmed).
    - Updated comment block above the gate explaining the new
      semantics (mounted MGs now go through, splash damage
      unaffected because they take a different code path).
  * `VANGUARD_VERSION` — bumped to `v0.5.1`.
  * `docs/RELEASE_NOTES.md` — v0.5.1 section prepended with
    root-cause writeup, behaviour-change explanation, and the
    pointer to this audit doc for full context.

`docs/CUP_VS_PUBLIC.md` left unchanged — its existing strict-mode
paragraph describes the INTENT correctly even though the v0.4.3
implementation didn't match. v0.5.1 makes the impl match the
intent; the doc was already accurate.

### Build verification

Multi-platform clean:

  * `build/vanguard/vanguard_v0.5.1.pk3` — 24,4 MB
  * Linux qagame.mp.x86_64.so contains the new diagnostic string
    `VG_DIAG: strict-hitbox reject (AABB hit but no capsule)`
  * Win64 qagame_mp_x64.dll same string present (1 match)
  * Win32 qagame_mp_x86.dll same string present (1 match)
  * Configure log: `Version: 0.5.1.0 and int version: 5010000`

### Live-test path on Pterodactyl (the v0.4.3 lesson — verify in
practice, not on paper)

This is the step-by-step path the operator runs **before** tagging
v0.5.1, to confirm the fix actually fires in production.

#### Setup

1. Deploy `vanguard_v0.5.1.pk3` to the Pterodactyl host's
   `vanguard/` directory (replace v0.5.0 pk3, keep loose binaries
   in sync via the same archive).
2. Start the server.
3. Connect with a test client. Add a stationary bot:
   ```
   \rcon bot addbot wolfdude /skill 0 axis
   ```
   (Name + skill + team adjustable to taste; the key is a
   stationary target for repeatable shots.)
4. Set the diagnostic + strict cvars:
   ```
   \rcon set vanguard_hitbox_strict 1
   \rcon set vanguard_hitbox_debug  1
   ```
5. Optional: enable cgame visualisation to see capsule positions:
   ```
   \rcon set vanguard_dev 1
   \cg_vanguardDevMultibox 1
   ```
6. Tail the server log:
   ```
   tail -f ~/.etlegacy-vanguard-test/vanguard/server.log
   # or whatever path your Pterodactyl exposes
   ```

#### Test 1 — direct shot on visible model (regression check)

Aim center-mass on the bot, fire one rifle shot.

**Expected log:**
```
VG_DIAG: mdx_hit_test -> hit_type=2 impactpoint=2 fraction=0.xxx mod=11
VG hit: chest (mult=1.30)
```

(impactpoint = IMPACTPOINT_CHEST = 2; mult depends on
`vanguard_dmg_chest`, default 1.3)

**Expected console (attacker):** `VG hit: chest (mult=1.30)` print
from `g_debugBullets`-driven trap_SendServerCommand. Bot HP
decreases. **NO** strict-hitbox reject line.

If this test fails: the multi-region pipeline is broken — fall
back to v0.5.0 immediately and investigate.

#### Test 2 — shot in AABB edge beside capsule (the actual bug fix)

Aim ~6 units to the side of the bot — the bullet should strike
the AABB but miss every capsule. With `vanguard_dev 1` +
`cg_vanguardDevMultibox 1` you can see exactly where the capsules
end and the AABB extends; aim just outside a capsule.

**Expected log:**
```
VG_DIAG: mdx_hit_test -> hit_type=0 impactpoint=0 fraction=1.000 mod=11
VG_DIAG: strict-hitbox reject (AABB hit but no capsule) attacker=N target=M weapon=W mod=11
```

**Expected behaviour:** Bot HP **unchanged**. NO `Hurt by ...`
event. NO blood / hit-marker.

**This is the v0.4.3 vs v0.5.1 contrast.** On v0.4.3 / v0.5.0
the same shot would have logged the first line, then applied
`dmg_default × bullet_damage` to the bot, and the second line
would never appear. With v0.5.1 the second line appears and the
bot stays at full HP.

#### Test 3 — escape hatch (legacy fallback)

Toggle strict-mode off:
```
\rcon set vanguard_hitbox_strict 0
```

Repeat Test 2 (shot in AABB edge).

**Expected log:**
```
VG_DIAG: mdx_hit_test -> hit_type=0 impactpoint=0 fraction=1.000 mod=11
VG hit: unknown (mult=1.00)
```

**Expected behaviour:** Bot HP decreases (legacy v0.4.x
behaviour preserved). NO `strict-hitbox reject` line because
strict mode is off.

This confirms the cup-admin escape hatch is intact for anyone
who explicitly wants byte-identical legacy fallback.

#### Test 4 — mounted MG (gate drop, isHeadshot=qfalse weapon)

If a map with a mounted MG is loadable, mount the MG and fire
into the AABB edge of the bot.

**Expected log on v0.5.1 (gate dropped):**
```
VG_DIAG: mdx_hit_test -> hit_type=0 impactpoint=0 fraction=1.000 mod=15
VG_DIAG: strict-hitbox reject (AABB hit but no capsule) attacker=N target=M weapon=W mod=15
```
(mod=15 = MOD_MACHINEGUN.)

**Expected on v0.5.0 / v0.4.3 (gate intact):** the multi-region
branch was bypassed — no `mdx_hit_test` log line, the legacy
chain applies damage directly.

This confirms Q1 (drop `isHeadshot` gate) actually changed the
behaviour for mounted MGs.

#### Test 5 — direct shot stays correct (final regression)

Re-enable strict mode:
```
\rcon set vanguard_hitbox_strict 1
```

Repeat Test 1 to confirm direct hits still apply damage with
the correct multiplier and full diagnostic chain. If anything
changed in Test 1 between the start and end of the test
sequence, something is wrong.

#### Pass criteria

  * Test 1: HP decrease + `VG hit: <region>` line (regression
    check passes).
  * Test 2: HP unchanged + `strict-hitbox reject` line (the
    actual fix works).
  * Test 3: HP decrease + no reject line (escape hatch works).
  * Test 4: HP unchanged + reject line (gate drop works).
  * Test 5: same as Test 1 (no drift).

If all five pass, tag v0.5.1 with confidence. If Test 2 fails
specifically (bot HP still drops on AABB-edge shot despite the
log line), there's a deeper issue — DON'T tag, debug.

This is the verification step that v0.4.3's strict-mode logic
should have had. Code that "worked on paper" needs a real
Pterodactyl roundtrip before promotion.

---

**Copyright Notice**

Copyright (c) 2026 wahke <info@wahke.lu> (https://wahke.lu)  
Copyright (c) 2026 VanguardMod Project Contributors

Licensed under GPL-3.0-or-later. Part of VanguardMod project.
Built on ETLegacy (https://www.etlegacy.com).
