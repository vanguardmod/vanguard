# Phase 6.1.3 — G_Damage Insertion Plan

Recon for the multi-box hitbox integration into the combat path.
This document is a planning artefact (untracked) produced during
Phase 6.1.3a; it informs the actual edit in Phase 6.1.3b.

Author: 2026-04-27
References: PHASE_6_PLAN.md, PHASE_6_AUDIT.md, HITS_FORMAT.md.

---

## Sektion 1 — G_Damage / G_DamageExt Anatomie

**Wrapper:** `G_Damage` (g_combat.c:1422) is a thin shim that
forwards to `G_DamageExt` (line 1427). All real logic is in
`G_DamageExt`.

### Signature

```c
void G_DamageExt(
    gentity_t      *targ,
    gentity_t      *inflictor,
    gentity_t      *attacker,
    vec3_t          dir,
    vec3_t          point,
    int             damage,
    int             dflags,
    meansOfDeath_t  mod,
    int            *hitEventOut)
```

### Local variables (g_combat.c:1429-1433)

```c
int         take;
int         knockback;
qboolean    wasAlive, onSameTeam;
hitRegion_t hr           = HR_NUM_HITREGIONS;   /* sentinel = unknown */
int         hitEventType = HIT_NONE;
```

`HR_NUM_HITREGIONS` is the codebase's "no region" value — used as
the initial state until one of the IsHeadShot/IsLegShot/IsArmShot
branches sets a real region.

### Code blocks in execution order

| Lines | Block | Notes |
|---|---|---|
| 1435-1438 | hitEventOut init | early-out scaffold |
| 1440-1443 | takedamage / state checks | non-clients, invisible, under-construction → return |
| 1448-1453 | intermission / warmup gating | early returns |
| 1455-1462 | inflictor/attacker sanitation | substitute world entity |
| 1464-1466 | wasAlive / onSameTeam latches | used later for limbo + teamkill |
| 1468-1482 | combatstate flags | for reward tracking |
| 1484-1487 | flamethrower-in-water immunity | early return |
| 1489-1594 | switch on `targ->s.eType` | ET_MOVER/ET_EXPLOSIVE/ET_MISSILE/ET_CONSTRUCTIBLE special handling |
| 1596-1608 | client-only protection (noclip, PW_INVULNERABLE, FL_GODMODE) | early returns |
| 1610-1617 | dir normalization / DAMAGE_NO_KNOCKBACK gating | |
| 1619-1635 | friendly-fire gate | |
| 1637-1651 | hit counter + ANIM_COND_ENEMY_WEAPON update | sets `hitEventType = HIT_TEAMSHOT`/`HIT_BODYSHOT` provisionally |
| 1653-1658 | `take = MAX(damage, 0)` + DAMAGE_DISTANCEFALLOFF | first damage compute |
| 1660-1679 | knockback compute | |
| 1681-1685 | adrenaline halving | `take *= 0.5f` if PW_ADRENALINE |
| 1687-1694 | engineer flak-jacket | `take -= take*0.5f` for explosive MODs |
| **1696-1790** | **Hit-region detection (legacy)** | **insertion point — see §2** |
| 1792-1797 | antilag re-adjust on eFlags change | needed because EF_HEADSHOT was just toggled |
| 1799-1840 | knockback velocity application | |
| 1842-1847 | g_debugDamage print | |
| 1849-1859 | hit event broadcast (EV_PLAYER_HIT) | |
| 1861-1866 | Lua hook (FEATURE_LUA) | |
| 1868-1901 | per-client damage tracking (damage_blood, damage_from, dmgReceivedSts) | |
| 1903-1914 | lasthurt_* + lastteambleed | |
| 1916+ | apply damage + death/limbo/gib | uses `hr` in `targ->sound1to2 = hr` and `G_AddKillSkillPoints` |

### Existing hit-region branch (the one we extend, g_combat.c:1696-1790)

```c
if (IsHeadShot(targ, dir, point, mod, &refent, qtrue)) {
    take = MAX(50, take * 2);                 /* hard floor + 2x */
    if (dflags & DAMAGE_DISTANCEFALLOFF) take *= G_DamageFalloff(...);
    /* helmet pop on first headshot, with .8x reduction unless scoped */
    if (!(targ->client->ps.eFlags & EF_HEADSHOT)) {
        G_AddEvent(targ, EV_LOSE_HAT, DirToByte(dir));
        if (mod != MOD_K43_SCOPE && mod != MOD_GARAND_SCOPE) take *= 0.8f;
    }
    targ->client->ps.eFlags |= EF_HEADSHOT;
    /* stats */
    if (targ->client && attacker && attacker->client && cross-team) {
        G_addStatsHeadShot(attacker, mod);
        if (hitEventType != HIT_TEAMSHOT) hitEventType = HIT_HEADSHOT;
    }
    G_LogRegionHit(attacker, HR_HEAD);
    hr = HR_HEAD;
}
else {
    if (IsLegShot(targ, dir, point, mod, &refent, qfalse)) {
        G_LogRegionHit(attacker, HR_LEGS);
        hr = HR_LEGS;
    }
    else if (IsArmShot(targ, attacker, point, mod)) {
        G_LogRegionHit(attacker, HR_ARMS);
        hr = HR_ARMS;
    }
    else if (targ->client && targ->health > 0) {
        if (GetMODTableData(mod)->isHeadshot) {
            G_LogRegionHit(attacker, HR_BODY);
            hr = HR_BODY;
        }
        /* no else — non-headshot weapons (explosives etc.)
         * leave hr = HR_NUM_HITREGIONS */
    }
}
```

**Notes on legacy branch:**
- `IsHeadShot`/`IsLegShot` use a static `grefEntity_t refent`
  (g_combat.c:1395) — `newRefent=qtrue` on first call rebuilds
  the bone state, `qfalse` on subsequent reuses the cache.
- `IsArmShot` does NOT use refent — it computes from a fixed
  shoulder-offset model (no MDX involvement).
- The legacy chain is order-dependent: HEAD wins over LEG wins
  over ARM wins over BODY. Multi-box should produce equivalent
  precedence by relying on `mdx_hit_test`'s "smallest fraction
  wins" semantics.

---

## Sektion 2 — Insertion Point

**Exact location:** `src/game/g_combat.c:1696`, immediately
before `if (IsHeadShot(...))`.

**Pre-context (5 lines, lines 1691-1695):**

```c
    if (GetMODTableData(mod)->isExplosive) {
        take -= take * .5f;     /* engineer flak jacket */
    }
}
                              /* <- blank line 1695 */
                              /* <- INSERT HERE, line 1696 */
if (IsHeadShot(targ, dir, point, mod, &refent, qtrue))
```

**Post-context (5 lines, lines 1791-1797):**

```c
        }                                  /* end legacy else */
}                                          /* end legacy chain */

if (g_antilag.integer) {
    /* re-adjust now because we are changing eFlags and pm_flags
     * and doing it later would overwrite them */
    G_ReAdjustSingleClientPosition(targ);
}
```

### Variables in scope at insertion-point

| Var | State |
|---|---|
| `targ` | non-NULL (early-outs already handled) — but may be a non-client (mover etc.) — guard required |
| `attacker` | non-NULL (substituted with world if needed) |
| `dir`, `point` | non-NULL after line 1610 |
| `mod` | weapon `meansOfDeath_t` |
| `take` | already computed: max(damage,0), with falloff/adrenaline/flak applied |
| `hr` | `HR_NUM_HITREGIONS` (init value, unmodified) |
| `hitEventType` | `HIT_TEAMSHOT` / `HIT_BODYSHOT` / `HIT_NONE` from earlier block |
| `refent` | static, garbage from previous call — must be rebuilt before mdx_hit_test |
| `dflags` | for DAMAGE_DISTANCEFALLOFF check |
| `muzzleTrace` | global from g_weapon.c, the trace start point |

### Variables that must NOT be overwritten by our branch

- `wasAlive`, `onSameTeam` — used in death/limbo logic later
- `knockback` — already computed
- `take` — we MAY scale it (that's the point), but we must preserve
  the falloff/adrenaline/flak compounding done before us
- `hitEventType` — we may upgrade to HIT_HEADSHOT, but must not
  downgrade HIT_TEAMSHOT

---

## Sektion 3 — Branch-Skizze

```c
if (s_hitbox.mode.integer >= 1 && targ->client) {
    /* === Multi-box path (Phase 6.1.3) === */
    int                       mdx_hit_type;
    vec_t                     mdx_fraction;
    animScriptImpactPoint_t   mdx_ip;
    qboolean                  hit;

    /* Build refent once. Antilag-aware: timeShiftTime is non-zero
     * during a G_HistoricalTrace-wrapped damage call (see §4). */
    mdx_gentity_to_grefEntity(targ, &refent,
        targ->timeShiftTime ? targ->timeShiftTime : level.time);

    hit = mdx_hit_test(muzzleTrace, point, targ, &refent,
                       &mdx_hit_type, &mdx_fraction, &mdx_ip);

    if (hit) {
        float       mult;
        hitRegion_t mapped_hr;

        mult      = vg_Hitbox_DamageMultiplierFor(mdx_ip);
        mapped_hr = vg_Hitbox_RegionFor(mdx_ip);
        take      = (int)(take * mult);

        /* Mirror legacy HEAD-special: helmet pop, EF_HEADSHOT,
         * stats, hitEventType upgrade. ONLY when impactpoint
         * is HEAD — multi-box treats other regions as plain
         * region-multiplier hits, no helmet, no headshot stat. */
        if (mdx_ip == IMPACTPOINT_HEAD) {
            if (!(targ->client->ps.eFlags & EF_HEADSHOT)) {
                G_AddEvent(targ, EV_LOSE_HAT, DirToByte(dir));
                if (mod != MOD_K43_SCOPE && mod != MOD_GARAND_SCOPE) {
                    take = (int)(take * 0.8f);  /* helmet absorb */
                }
            }
            targ->client->ps.eFlags |= EF_HEADSHOT;

            if (attacker && attacker->client
#ifndef DEBUG_STATS
                && attacker->client->sess.sessionTeam != targ->client->sess.sessionTeam
#endif
                ) {
                G_addStatsHeadShot(attacker, mod);
                if (hitEventType != HIT_TEAMSHOT) {
                    hitEventType = HIT_HEADSHOT;
                }
            }
        }

        if (mapped_hr != HR_NUM_HITREGIONS) {
            G_LogRegionHit(attacker, mapped_hr);
            hr = mapped_hr;
        }

        if (g_debugBullets.integer) {
            trap_SendServerCommand(attacker - g_entities,
                va("print \"VG hit: %s (mult=%.2f)\n\"",
                   vg_Hitbox_RegionName(mdx_ip), mult));
        }
    } else {
        /* mdx_hit_test failed — file not loaded for this character,
         * or trace landed outside all hit-areas. Fall back to legacy
         * chain so we never silently miss a damage event. */
        goto legacy_hit_chain;
    }
} else {
legacy_hit_chain:
    /* === Legacy path (mode=0 or non-client target) === */
    /* unchanged — the existing 1696-1790 block goes here verbatim */
    if (IsHeadShot(targ, dir, point, mod, &refent, qtrue)) { ... }
    else { ... }
}
```

**Goto rationale:** A boolean fallback flag would also work, but
`goto legacy_hit_chain` is forward-only and keeps the legacy block
verbatim untouched (zero-diff in mode=0 / fallback paths). C89
explicitly allows it. A reviewer can read the multi-box branch
top-to-bottom and the legacy block byte-identical.

### What we deliberately do NOT replicate from legacy

- `take = MAX(50, take * 2)` hard 50-floor on headshots — multi-box
  uses `vanguard_dmg_head` (default 2.0) as the multiplier and
  trusts the cvar setting. A 50-floor would defeat the point of
  per-region tuning. Server admins who want the floor can set
  `vanguard_dmg_head` to a value that achieves it given their
  base damage.
- DAMAGE_DISTANCEFALLOFF is applied to `take` BEFORE our branch
  (line 1655-1658) so the multiplier compounds with falloff
  correctly, matching legacy.
- The `else if (GetMODTableData(mod)->isHeadshot)` body branch
  is a legacy quirk — it only logs HR_BODY for headshot-class
  weapons (rifles/SMGs), not for explosives. Multi-box has no
  such weapon-class gating; every hit gets a region.

---

## Sektion 4 — Antilag-Kompatibilität

**Verified safe.** The damage-path is wrapped at the call site,
not inside G_Damage:

```c
/* g_weapon.c:3548-3559 (Bullet_Fire, the parent of all bullet damage) */
G_HistoricalTraceBegin(ent);                  /* sets timeShiftTime
                                                  on every other client */
{
    Bullet_Fire_Extended(ent, ent, ...);     /* does the trace + G_Damage */
}
G_HistoricalTraceEnd(ent);                   /* resets timeShiftTime to 0 */
```

So inside `G_Damage`, `targ->timeShiftTime` is **already set** to
the rewound timestamp. Mirror IsHeadShot's pattern verbatim:

```c
mdx_gentity_to_grefEntity(targ, &refent,
    targ->timeShiftTime ? targ->timeShiftTime : level.time);
```

This matches g_combat.c:964 (G_BuildHead) and g_combat.c:1087
(G_BuildLeg) — both proven-correct call sites.

`G_ReAdjustSingleClientPosition(targ)` at line 1796 (post-branch)
re-applies the historical pose AFTER our branch overwrites
`eFlags` (specifically `EF_HEADSHOT`) — this is engine bookkeeping
to keep the pose consistent through the rest of the frame. We
inherit this for free; no extra antilag code on our side.

**Bot exception:** `G_HistoricalTrace` skips bots
(`ent->r.svFlags & SVF_BOT`) — so when a bot fires,
timeShiftTime stays 0, mdx_gentity_to_grefEntity uses level.time,
which is correct. When a bot is *targeted*, the rewind applies to
the targets, not the bot — so bot-attacker tests still work
through the multi-box path.

---

## Sektion 5 — Failure-Modes + Mitigation

### F1: `mdx_hit_test` returns `qfalse` (line 2782 path)

**Cause:** No `hits[i]` entry matches `targ->client->...->animModelInfo`.
Either the .hit file failed to load, or this character has no
.hit file (e.g., a future custom model without animations/X.hit).

**Mitigation:** Fall through to legacy path via `goto
legacy_hit_chain`. The damage event is preserved; the player
gets a single-region hit with no multiplier. Operator visibility
is via the LOUD-warning at startup (deferred to a later phase per
6.1.2 TODO) plus the `g_debugBullets` print.

### F2: `mdx_hit_test` returns `qtrue` but `impactpoint == IMPACTPOINT_UNUSED`

**Cause:** A hit-area was registered without an `impactpoint`
keyword (parser default = `NUM_ANIM_COND_IMPACTPOINT`, code maps
this to `IMPACTPOINT_UNUSED` for default cases).

**Mitigation:** `vg_Hitbox_DamageMultiplierFor` returns
`vg_dmg_default.value` (1.0 by default → no scaling), and
`vg_Hitbox_RegionFor` returns `HR_NUM_HITREGIONS` so we **don't**
log a region hit (consistent with legacy "no region detected"
behaviour for non-headshot-class explosive weapons).

### F3: `vanguard_hitbox_mode == 0` (admin-disabled)

**Cause:** Admin set `vanguard_hitbox_mode 0`. Latched, so requires
map restart.

**Mitigation:** First gate of the if. Branch evaluates to false,
goto target lands directly in legacy path. **Bit-identical**
behaviour to pre-Phase-6 code; no side effects.

### F4: `targ` is not a client (mover, explosive, constructible)

**Cause:** The code path before us already filters most non-client
targets (eType-switch at 1489-1594), but mdx_hit_test only makes
sense for animated player skeletons.

**Mitigation:** First gate `targ->client` ensures we never call
mdx_hit_test on non-clients. Movers etc. take the legacy path,
where IsHeadShot's `if (!targ->client) return qfalse;` (line
1156) early-exits to the body branch.

### F5: Multiplier produces zero/negative damage

**Cause:** Admin sets `vanguard_dmg_legs 0` (intentional disable
of leg damage). `take * 0 = 0`, no damage applied.

**Mitigation:** `if (take)` guard at g_combat.c:1917 already
gates the actual damage application — zero damage just doesn't
hurt the target. Region hit is still logged (intentional —
admins can see leg shots in stats even if they don't bleed).
Negative: clamped via `int` truncation at multiplier site, but
multipliers should be in `[0, ∞)` by design.

### F6: Pose mismatch between mdx_hit_test and traceEnt determination

**Cause:** The original trap_Trace that determined `traceEnt ==
targ` ran in `G_HistoricalTrace` with whatever pose was active.
mdx_hit_test re-resolves the bone state from `timeShiftTime`.
If something between trace and damage modifies poses, results
could disagree.

**Mitigation:** Audited the path (Bullet_Fire_Extended →
G_DamageExt). No pose mutation between the two. `timeShiftTime`
is the canonical source for "the pose at trace time."

---

## Sektion 6 — Stats-Tracking-Plan

**Convention:** `G_LogRegionHit(attacker, hr)` (g_stats.c:87)
increments `attacker->client->pers.playerStats.hitRegions[hr]`.
4-bucket histogram (HEAD/ARMS/BODY/LEGS).

**Plan:**

1. Compute `mapped_hr = vg_Hitbox_RegionFor(mdx_ip)` after
   successful mdx_hit_test.
2. If `mapped_hr != HR_NUM_HITREGIONS`, call `G_LogRegionHit(attacker,
   mapped_hr)` and set local `hr = mapped_hr`.
3. The `hr` local propagates downstream to:
   - `targ->sound1to2 = hr;` (g_combat.c:1969) — used by EV_OBITUARY
     to render the death message HUD-side ("headshot kill").
   - `G_AddKillSkillPoints(attacker, mod, hr, ...)` (g_combat.c:1977)
     — XP awards for skill-based gameplay.

**No 9-bucket fine-grained tracking yet.** The
`vg_Hitbox_RegionName`-based stats output (Phase 6.1.4) would
need a parallel 10-element histogram added to playerStats — that
requires an SDK-struct touch and is deferred. The 4-bucket roll-up
is sufficient for now and matches the existing
`#stats <client>` accuracy report (g_cmds.c:4797-4800).

---

## Sektion 7 — Bot-Test-Plan

**Setup:** scripts/testserver/run.sh + add bots via console
(`bot_minPlayers 4` or similar), oasis or radar map for stable
indoor encounters.

**Test 1 — Mode=0 baseline (legacy guarantee):**
- Set `vanguard_hitbox_mode 0`, restart map.
- Sniper-rifle (K43_SCOPE) headshot a stationary bot.
- ✅ Expect: 1-shot kill, "headshot kill" obituary,
  hitRegions[HR_HEAD]++.
- This MUST be byte-identical to pre-Phase-6 behaviour.

**Test 2 — Mode=1 head:**
- Set `vanguard_hitbox_mode 1`, restart map.
- Same sniper headshot scenario.
- ✅ Expect: 1-shot kill (mult=2.0 × base scoped damage).
  Helmet popped on first hit.
- Verify `hitRegions[HR_HEAD]` increments via `#stats` command.

**Test 3 — Mode=1 body shots:**
- Stationary bot, MP40 burst to chest.
- ✅ Expect: TTK roughly equivalent to vanilla (~6-8 shots).
  `vanguard_dmg_chest 1.3` ≈ vanilla body damage scaling.
- Verify `hitRegions[HR_BODY]` increments.

**Test 4 — Mode=1 region tuning:**
- Set `vanguard_dmg_knee_l 0.3`, no map restart needed
  (CVAR_ARCHIVE not LATCH).
- Aim at bot's left knee with rifle.
- ✅ Expect: ~3x more shots needed than baseline knee shots.
- Verify `hitRegions[HR_LEGS]` increments.

**Test 5 — Antilag stability:**
- Bot vs bot match, 5-10 minutes, both teams full.
- ✅ Expect: no crashes, no `MDX WARNING` spam in log,
  reasonable kill rates (no infinite-HP or one-shot weirdness).
- Sample 3-5 obituary lines from log to confirm they look
  semantically correct.

**Test 6 — Fallback-Pfad:**
- Edit `etmain/animations/human_base.hit` to break parsing
  (introduce a stray `INVALID` token).
- Reload server.
- ✅ Expect: `MDX WARNING: ...` at startup, mode=1 falls back
  to legacy path silently per F1, kills still work, no crash.
- Restore the file before continuing.

**Failure criteria (any of these blocks the merge):**
- Bot match crashes or hangs.
- Mode=0 behaviour deviates from pre-Phase-6 (Test 1).
- HR_*-stats stop incrementing.
- Damage multipliers don't visibly affect TTK.

---

## Sektion 8 — Open Questions

### Q1 — `IMPACTPOINT_UNUSED` on register vs hit (CLOSED)

**Q:** Does `mdx_hit_test` return `IMPACTPOINT_UNUSED` only for
hit-areas without an `impactpoint` keyword, or also for "trace
missed all hit-areas"?

**A:** Looking at `mdx_hit_test` (g_mdx.c:2790-2791): the local
`best_impactpoint` initializes to `IMPACTPOINT_UNUSED` and is
overwritten when a hit-area is intersected. If no hit-area is
hit, the function returns `qfalse` (not qtrue with UNUSED) —
verified at g_mdx.c:2853-2860 where the return reflects whether
`best_type != MDX_NONE`. So `IMPACTPOINT_UNUSED` on `qtrue` only
means "registered without impactpoint keyword."

### Q2 — Helmet pop order with multi-box (CLOSED)

**Q:** Should the helmet-pop event fire before or after the
multiplier is applied?

**A:** Legacy applies the helmet 0.8x AFTER the 2x headshot mult
(`take = MAX(50, take * 2); ... take *= 0.8f`). Matching that
order in multi-box: apply `vg_dmg_head` first, then helmet 0.8x
on first hit only. This produces the same compounding for the
default `vg_dmg_head 2.0`.

### Q3 — Static refent reuse (CLOSED)

**Q:** The static `refent` is shared across IsHeadShot/IsLegShot/
IsArmShot. If we call `mdx_gentity_to_grefEntity` ourselves and
fall back to legacy, will IsHeadShot's `newRefent=qtrue` be a
no-op (same data) or a refresh?

**A:** It will refresh. `mdx_gentity_to_grefEntity` is
idempotent given the same `(ent, timeShiftTime)` input — the
refent ends up in the same state. Cheap. No correctness issue.

### Q4 (OPEN, NON-BLOCKING) — Phase 6.1.4 fine-grained stats

The 4-bucket roll-up loses information (chest vs gut vs groin all
collapse to HR_BODY). For competitive analysis a 9-bucket
histogram would be useful. Requires:
- Adding `int hitRegions9[9]` (or named field) to
  `playerStats_t` (bg_public.h or g_local.h).
- New stats command output.
- cgame-side display.

This is independent of 6.1.3 and can land anytime after.

### Q5 (OPEN, NON-BLOCKING) — Headshot-class weapon gating

Legacy gates HR_BODY logging on `GetMODTableData(mod)->isHeadshot`
(line 1774) — i.e., body shots from explosives don't increment
hitRegions[HR_BODY]. Multi-box currently logs every region hit.
This is a deliberate divergence (multi-box treats explosions as
"if they hit a region, record it") but should be confirmed with
the user. Could be flag-gated under a future
`vanguard_hitbox_explosive_regions` cvar if needed.

### Q6 (OPEN, NON-BLOCKING) — `g_realHead.integer & REALHEAD_HEAD` interaction

`G_BuildHead` (g_combat.c:947) has a `g_realHead` cvar that
already enables MDX-based head detection. When mode=0 +
g_realHead is on, the legacy path uses MDX for HEAD only.
Multi-box mode=1 supersedes this — IsHeadShot's MDX path
becomes dead code in mode=1. Should `g_realHead` be locked or
have its scope clarified when `vanguard_hitbox_mode >= 1`? Leave
as-is for now (no harm), document in commit message.

### Q7 (OPEN, BLOCKING for 6.1.4) — pers.playerStats.hitRegions audit-trail

Need to confirm:
- That `playerStats_t::hitRegions[HR_NUM_HITREGIONS]` is written
  ONLY through `G_LogRegionHit`.
- That cgame reads from a field that survives our integration.

If `hitRegions` is mirrored to cgame via configstrings or some
other channel, we may need to bump that.

**Status:** non-blocking for 6.1.3b; checking before 6.1.4 stats
work.

---

*End of plan. Confidence to proceed with 6.1.3b implementation:
high (8/10) — the antilag-already-handled finding is the key
de-risking. Main residual unknown is the bot-test bandwidth
(Tests 3-5 may need more iterations than planned).*
