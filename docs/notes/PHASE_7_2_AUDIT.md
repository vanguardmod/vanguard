# Phase 7.2 — Netcode Tuning Audit (Phase A Recon)

**Date:** 2026-04-30
**Scope:** Audit before any Phase B implementation. No code changes
in this pass — purely understanding the existing infrastructure
across the three sub-goals (lag-comp / sv_fps profile / antiwarp).

---

## TL;DR

| Sub-Goal | Status | Effort to ship |
|---|---|---|
| **1. Lag-comp covers multi-region** | ✅ already correct | 0 — document finding only |
| **2. sv_fps cup-vs-public profile** | 🟡 not implemented, design ready | ~2 h |
| **3. Antiwarp refinement** | 🟠 limited tuning surface | ~1 h (cup-preset bump) or 4–6 h (new cvars + tracking) |

Headline: the user's main worry — that lag-comp would silently
ignore bone state for laggy clients — is **unfounded**. ETLegacy's
`G_StoreClientPosition` / `G_AdjustSingleClientPosition` already
rewind the full per-bone animation state every multi-region trace
needs. The Phase 6 multi-region damage pipeline therefore inherits
correct lag-compensation for free — `mdx_hit_test` reads from
`ent->torsoFrame.*` / `ent->legsFrame.*`, which are exactly the
fields G_AdjustSingleClientPosition restores.

---

## A1 + A4 — Lag-comp ⊃ Multi-region (combined)

### What state gets rewound

`G_StoreClientPosition` (`g_antilag.c:125-188`) is called once per
server tick per client. Each marker captures **everything** a
historical bone-position computation needs:

  * `origin` (`ent->s.pos.trBase` → `clientMarkers[i].origin`)
  * `mins`, `maxs` (bounding box at that frame)
  * `viewangles` (`ent->s.apos.trBase`)
  * `eFlags`, `pm_flags`, `viewheight`, `groundEntityNum`
  * **Torso animation state:** `torsoOldFrameModel`, `torsoFrameModel`,
    `torsoOldFrame`, `torsoFrame`, `torsoOldFrameTime`,
    `torsoFrameTime`, `torsoYawAngle`, `torsoPitchAngle`,
    `torsoYawing`, `torsoPitching`, `torsoAnimationMovetype`
  * **Legs animation state:** same set, prefixed `legs*`

The buffer holds `MAX_CLIENT_MARKERS = 40` entries (`g_local.h:912`)
in a circular ring — at default `sv_fps = 20` (50 ms tick), that's
**2000 ms of rewind history**.

### How it's restored

`G_AdjustSingleClientPosition` (`g_antilag.c:196-486`):

  1. Find a pair `[i, j]` of markers bracketing the requested time.
  2. Lerp `origin / mins / maxs / viewangles` between the two
     markers using `TimeShiftLerp` / `LerpAngle`.
  3. Snap the discrete fields (eFlags, pm_flags, viewheight,
     groundEntityNum, **all torso/legs frame fields**) to the
     closer marker — these can't be lerped meaningfully, so the
     code picks marker `i` or `j` depending on which is closer in
     time to the requested moment.
  4. Set `ent->timeShiftTime = clientMarkers[…].time` so any code
     downstream that needs the historical timestamp (head/leg-
     position helpers in `g_mdx.c:2946+`, etc.) can query it.
  5. `trap_LinkEntity(ent)` to make the engine notice the new
     bbox/origin.

### How the multi-region trace inherits this

Bullet flow (`g_weapon.c:3540-3560`):

```c
G_HistoricalTraceBegin(ent);                 // rolls all OTHER clients back
G_TempTraceIgnoreBodies();
Bullet_Fire_Extended(ent, ent, muzzleTrace, // includes per-bullet
                     end, forward, ...);      //   G_HistoricalTrace + G_Damage
G_ResetTempTraceIgnoreEnts();
G_HistoricalTraceEnd(ent);                   // restores
```

Inside `G_Damage` (`g_combat.c:1715-1804`), the multi-region branch
fires:

```c
mdx_gentity_to_grefEntity(targ, &refent,
    targ->timeShiftTime ? targ->timeShiftTime : level.time);

if (mdx_hit_test(muzzleTrace, point, targ, &refent, ...)) {
    ...
}
```

`mdx_gentity_to_grefEntity` (`g_mdx.c:298-349`) reads:
- `ent->legsFrame.frame`, `oldFrame`, `frameModel`, `oldframeModel`,
  `frameTime`, `oldFrameTime`
- `ent->torsoFrame.*` (same fields)

These are the **exact** fields `G_AdjustSingleClientPosition` rewinds.
So when `mdx_hit_test` walks `vg_mdx_calculate_bone_lerp` against the
resulting `grefEntity_t`, the bone math runs against the historical
animation pose, not the current one. Sub-Goal 1 is structurally
satisfied without a code change.

### Caveats / known sharp edges

1. **`MAX_CLIENT_MARKERS = 40` shrinks at higher `sv_fps`.** At
   `sv_fps = 40` (the cup profile we'd ship in Sub-Goal 2), the
   ring covers only **1000 ms** of history. For typical cup pings
   (40–80 ms each way → ≤160 ms `serverTime` deficit), 1 s is more
   than enough. But the constant is per-`g_local.h:912`, so if a
   future cup ever runs at `sv_fps = 60` and admits 200+ ms pings,
   the buffer would clip and the antilag would fall back to current-
   time hits for the worst-case players. Recommendation:
   leave the constant alone, document the floor in `docs/CUP_VS_PUBLIC.md`.

2. **Animation movetype is restored conditionally.** Lines 339,
   355, 383, 399 of `g_antilag.c` only restore
   `torsoFrame.animation->movetype` if the marker captured a
   non-zero movetype. If a client connected mid-frame the
   first marker can hit this conditional and miss the animation
   movetype — a visible irregularity for the very first historical
   trace after spawn. Pre-existing; not a Phase 7.2 regression.

3. **Bots are exempted.** `G_HistoricalTrace*` and `G_AdjustSingleClientPosition`
   bail early on `SVF_BOT`. Bots play at server-time; no rewind
   needed (their input is generated on the server). This means
   any test scenario with bots-only doesn't exercise lag-comp at
   all — keep that in mind during Phase B testing.

### A1+A4 conclusion

**No implementation work needed.** Document the finding in
`docs/CUP_VS_PUBLIC.md` so future maintainers don't re-investigate
the same hypothesis from scratch.

---

## A2 — sv_fps profile

### Current state

  - **Default:** `DEFAULT_SV_FPS = 20` (`q_shared.h:524-527`),
    50 ms tick. Server-engine cvar `sv_fps` (registered upstream
    in `sv_main.c`).
  - **Game-side cvar reference:** `sv_fps` declared `extern vmCvar_t`
    in `g_cvars.h:293`. Read at runtime in:
    * `g_etbot_interface.cpp:6017` — bot AI tick rate
    * `g_mdx.c:1905,1919` — MDX-driven move computations
    * `g_script_actions.c:1655,1690,1712` — script wait timing
      (with explicit "match wait time to sv_fps 20" code that
      compensates above 20)
    * `bg_pmove.c:5458` — animation timing comment
      (`// in reality, this should be split according to sv_fps`)
    * `g_teammapdata.c:1209` — landmine spotted-counter (comment
      annotation only)
  - **No vanguardmod-side override** today. `vanguard_competitive.cfg`
    leaves `sv_fps` at the engine default.

### Implications of bumping to `sv_fps = 40` (cup)

  - **Snapshot cadence doubles.** Each client receives ~40 snapshots
    per second instead of 20. Bandwidth per client roughly doubles
    for the player-state portion (snapshots are mostly delta-
    compressed, so the actual bytes/sec increase is ~1.5–1.8×).
  - **Hit-detection latency floor halves.** Rough rule of thumb:
    one tick of jitter on either end. At `sv_fps = 20` that's
    50 ms × 2 ends = 100 ms worst-case detection delay; at 40
    it's 25 ms × 2 = 50 ms. Cup play wants this.
  - **Lag-comp history window halves** (1 s instead of 2 s — see
    A1 caveat 1). Still adequate for typical cup pings.
  - **Animation timing.** `bg_pmove.c:5458` notes a TODO: animation
    timing should split per sv_fps but doesn't. Most ETLegacy
    cup servers run at 40 in production with no observable
    animation issues, so this is likely a comment with no real
    bug behind it. Worth a paragraph in the cup-vs-public doc
    so admins know about the latent caveat.
  - **Server CPU.** ~2× as much physics + mdx_hit_test work per
    second. On modern hardware negligible; on a constrained VM
    measurable. No documented budget on Vanguard's existing
    Pterodactyl host — assume comfortable headroom.

### Implementation surface

A new latched cvar `vanguard_netcode_profile` in `g_vanguard.{c,h}`
mirroring the `vanguard_hitbox_*` lifecycle:

```c
vmCvar_t profile;
trap_Cvar_Register(&s_netcode.profile, "vanguard_netcode_profile",
                   "public", CVAR_LATCH | CVAR_ARCHIVE | CVAR_SERVERINFO);
```

At `vg_Netcode_Init` (called from `G_InitGame`):

```c
if (!Q_stricmp(s_netcode.profile.string, "cup")) {
    trap_Cvar_Set("sv_fps",     "40");
    trap_Cvar_Set("g_antilag",  "1");
    trap_Cvar_Set("g_antiwarp", "1");
} else if (!Q_stricmp(s_netcode.profile.string, "public")) {
    /* leave engine defaults */
} else if (!Q_stricmp(s_netcode.profile.string, "custom")) {
    /* admin owns it */
}
```

`CVAR_LATCH` means the value is locked for the map session — no
mid-match drift. `CVAR_SERVERINFO` lets a client UI tell the player
which profile is active. `CVAR_ARCHIVE` persists in `etconfig_server.cfg`.

`vanguard_competitive.cfg` then reduces to one line:

```
set vanguard_netcode_profile "cup"
```

### A2 effort estimate

~2 hours: cvar registration + apply-at-init + cup config edit +
`docs/CUP_VS_PUBLIC.md` + a release-notes bullet. No risk of breaking
the `"public"` default (no-op).

---

## A3 — Antiwarp

### Current state

  - **Default:** `g_antiwarp 1` (`g_cvars.c:577`), enabled out of
    the box.
  - **Implementation:** `src/game/et-antiwarp.c` (350 lines),
    described in the file header as "Antiwarp code from etpro thanks
    to zinx". Two key entry points:
    * `G_DoAntiwarp(ent)` (line 16) — guard called from the
      command-processing path. Returns false if antiwarp disabled,
      gamestate is intermission, the client is spectator/limbo/bot,
      or the client connected <5 s ago.
    * `etpro_AddUsercmd(clientNum, cmd)` (line 55) — appends a
      pending command to the per-client `cmds[]` ring of size
      `LAG_MAX_COMMANDS` (defined in `g_local.h`).
  - **`g_antiwarp` semantics are bitfield-shaped:**
    * bit 0 (= 1) — basic enable
    * bit 4 (= 16) — `ETLEGACY_DEBUG`-only debug visualization
    * bit 5 (= 32) — bandwidth-eating ping + queued-packets
      print-feedback to clients
  - **No tunable thresholds.** The actual queue-then-throttle logic
    is hardcoded — every command runs `ClientThink_cmd` once, the
    queue blows past `LAG_MAX_COMMANDS` triggers a head-bump, and
    that's the entire knob.

### What "refinement" could mean

Given the hardcoded thresholds, three honest options:

  **Option α — Cup-preset bumps the existing knob.**
  Already implied by the `vanguard_netcode_profile = cup` work in
  Sub-Goal 2 (we'd `Cvar_Set("g_antiwarp", "1")` in the profile
  apply). Zero new code. Enabled-by-default already, so this is
  mostly making the guarantee explicit and documented.

  **Option β — Add diagnostic logging.**
  Track per-client warp incidents (commands dropped,
  queue-overflow events) and either log them under a new
  `vanguard_antiwarp_debug` cvar or expose them via `!stats` /
  console command. Lets cup admins see who's warping during a
  match without needing demos. ~3 h.

  **Option γ — Engine-level changes.**
  Out of scope — etpro's core logic lives in `et-antiwarp.c` but
  the underlying packet-processing cadence is in `src/server/`.
  Touching that risks upstream-resync conflicts and is rarely
  what ETLegacy operators want anyway (the current behaviour is
  what every cup uses).

### A3 conclusion

Recommend **Option α** (free, ships as part of Sub-Goal 2 cup
preset). Document Option β as a follow-up ticket if cup admins
report needing per-incident diagnostics; not warranted today.

---

## Recommendations for Phase B

### Scope split

  * **In v0.5.0:** Sub-Goal 2 (`vanguard_netcode_profile` cvar +
    cup config + docs). Sub-Goal 1 finding documented in the same
    PR. Sub-Goal 3 ships as part of the cup-preset apply (Option α).
  * **Backlog:** Sub-Goal 3 Option β (antiwarp incident logging) —
    open a separate ticket if cup operators ask. Don't ship without
    a real ask.

### Versioning

User asked between progressive (`v0.4.5` → `v0.4.6` → `v0.4.7`)
and a single `v0.5.0`. Given the actual scope is one cvar + a
config edit, **single `v0.5.0-rc1`** is cleaner. Progressive
incrementing would create three releases for what amounts to one
deliverable.

`v0.5.0-rc1` makes the "release candidate" status explicit during
the live-test phase on Pterodactyl; promote to `v0.5.0` after the
cup preset has been validated in a real scrim.

### Risk Assessment

| Risk | Mitigation |
|---|---|
| `sv_fps = 40` exposes bg_pmove animation-timing drift | Document caveat in `CUP_VS_PUBLIC.md`. Live-test verifies. |
| MAX_CLIENT_MARKERS=40 → 1 s history at sv_fps=40 | Document. Cup pings ≤ 200 ms total still safe. |
| Cup preset latches at map start; admin can't toggle mid-match | By design (CVAR_LATCH). Document. |
| `Cvar_Set("sv_fps", ...)` from inside qagame may fail on some Pterodactyl images that lock engine cvars | Verify on the actual host before promoting rc1 → v0.5.0. Fallback: instruct admins to set sv_fps in `server.cfg` directly and use `vanguard_netcode_profile "custom"` to mark the choice. |
| Custom profile lets admin diverge from declared mode | Intentional escape hatch. CVAR_SERVERINFO publishes which profile is active so clients/spectators can tell. |

### Estimated effort

  * Audit (this document): done.
  * Phase B implementation: **3–4 hours** end-to-end.
    - cvar + lifecycle: 1 h
    - cup config + docs: 1 h
    - multi-platform build + commit + status report: 0.5 h
    - response to live-test feedback: 0.5–1.5 h
  * Live-test on Pterodactyl: out-of-band (not Claude-time).

---

## Open questions for user before Phase B

1. **Sub-Goal 1 — accept the "already correct" finding?** No code
   to ship here, just a docs sentence in `CUP_VS_PUBLIC.md`.

2. **Profile values.** `cup` / `public` / `custom` as proposed,
   or rename? Default `"public"` is the safe pick (no-op for
   existing servers). Alternative defaults — leave unset and treat
   missing as `"custom"` — is more explicit but requires admins to
   set the cvar to opt in. Recommendation: default `"public"` so
   the cvar always has a defined meaning.

3. **`sv_fps` mid-match.** With `CVAR_LATCH` an admin can't change
   the profile during a running map. Acceptable for cup play;
   public servers might want hot-toggle. Recommendation: keep
   `CVAR_LATCH` — public servers staying at `"public"` doesn't need
   to flip mid-match.

4. **Antiwarp diagnostic logging (Option β).** Wait for cup
   demand, or pre-emptively ship in v0.5.0? Recommendation: defer
   — the existing `g_debugBullets`-style knobs cover most diagnosis
   needs and shipping unused infrastructure is feature-creep.

5. **`docs/CUP_VS_PUBLIC.md`** — accept this filename or prefer
   a different location (e.g. extend `DEV_MODE.md`, or put under
   `docs/notes/` to keep the user-facing tree clean)? The file
   would document: profile values, what they change, the latched-
   cvar caveat, the MAX_CLIENT_MARKERS history-window math, and
   the bg_pmove animation-timing note.

6. **Versioning.** `v0.5.0-rc1` then promote to `v0.5.0` after
   live-test, or skip the rc and tag `v0.5.0` directly? rc1 lets
   you quietly pull the build if a regression surfaces; direct
   `v0.5.0` is cleaner if you're confident.

---

## Phase A finding: nothing about the v0.4.x → v0.5.0 jump is risky

The biggest concern coming into the audit was Sub-Goal 1 (lag-comp
covering bone state). That turned out to already be solved by the
existing infrastructure. Sub-Goal 2 is one cvar with well-understood
implications. Sub-Goal 3 is largely a config-level decision with a
single-line apply. The cup release is **mostly mechanical from here**.

---

**Copyright Notice**

Copyright (c) 2026 wahke <info@wahke.lu> (https://wahke.lu)  
Copyright (c) 2026 VanguardMod Project Contributors

Licensed under GPL-3.0-or-later. Part of VanguardMod project.
Built on ETLegacy (https://www.etlegacy.com).
