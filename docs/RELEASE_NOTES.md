# VanguardMod release notes

User-visible changes per published version. For build / release
mechanics, see `docs/RELEASE_PROCESS.md`.

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
