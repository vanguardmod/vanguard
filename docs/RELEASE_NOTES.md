# VanguardMod release notes

User-visible changes per published version. For build / release
mechanics, see `docs/RELEASE_PROCESS.md`.

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
