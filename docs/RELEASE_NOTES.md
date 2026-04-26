# VanguardMod release notes

User-visible changes per published version. For build / release
mechanics, see `docs/RELEASE_PROCESS.md`.

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
