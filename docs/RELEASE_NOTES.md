# VanguardMod release notes

User-visible changes per published version. For build / release
mechanics, see `docs/RELEASE_PROCESS.md`.

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
