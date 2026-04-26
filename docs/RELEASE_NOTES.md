# VanguardMod release notes

User-visible changes per published version. For build / release
mechanics, see `docs/RELEASE_PROCESS.md`.

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
