# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

VanguardMod — a competitive Wolfenstein: Enemy Territory mod for the **ETLegacy**
runtime. It is a Vanguard-specific scaffold (`wolfguard/`, `cmake/WolfGuard.cmake`,
`scripts/bootstrap.sh`, `docs/`) overlaid on top of the **full upstream ETLegacy
source tree** (`src/`, `cmake/ETL*.cmake`, `etmain/`, `vendor/`, `misc/`, the
root `CMakeLists.txt`, `COPYING.txt`, `VERSION.txt`, `CHANGELOG.yml`). The
upstream tree was imported by `scripts/bootstrap.sh`, not committed by hand.

The original Vanguard top-level CMake is preserved at
`CMakeLists.scaffold.txt.bak`. The **active** root `CMakeLists.txt` is
upstream ETLegacy's (`project(ETLEGACY C CXX)`). Treat the upstream files as
vendored — modify only with intent, ideally via a re-bootstrap.

VanguardMod ships in two flavours that share `cgame`/`ui` byte-for-byte and
differ only in the server-side `qagame` module: a **Community** build (QVM,
null WolfGuard stub, public `.pk3`) and a **Protected** build (native
`.so`/`.dll`, real WolfGuard linked, server-registered distribution only).

## Build

The repo currently builds **mod-only** via the upstream CMake. The bootstrap
script's exact invocation (this is the canonical configure for this repo —
all engine targets and most optional features are disabled):

    cmake -B build \
        -DCROSS_COMPILE32=OFF \
        -DBUILD_CLIENT=OFF -DBUILD_SERVER=OFF \
        -DBUILD_MOD=ON -DBUILD_MOD_PK3=OFF \
        -DBUNDLED_LIBS=OFF \
        -DFEATURE_LUA=OFF -DFEATURE_OMNIBOT=OFF \
        -DFEATURE_DBMS=OFF -DFEATURE_RATING=OFF -DFEATURE_PRESTIGE=OFF \
        -DINSTALL_EXTRA=OFF
    cmake --build build -j

Outputs land in `build/legacy/` (because upstream's `MODNAME` defaults to
`legacy`, not `vanguard`):

  - `cgame.mp.<arch>.so`
  - `ui.mp.<arch>.so`
  - `qagame.mp.<arch>.so`
  - `tvgame.mp.<arch>.so`

The `docs/BUILDING.md` instructions describe the **intended future** Vanguard
build (`build/vanguard/`, `*.qvm`, `-DVANGUARD_WITH_WOLFGUARD=...`). That
flow is **not** wired into the active root `CMakeLists.txt` yet — it lives
only in the scaffold backup and `cmake/WolfGuard.cmake`. Don't follow
`docs/BUILDING.md` literally for current builds; use the bootstrap flags above.

To rebuild from scratch after pulling upstream changes (re-runs apt and
deletes/rebuilds `build/`):

    ./scripts/bootstrap.sh                 # full: deps + clone + build
    ./scripts/bootstrap.sh --skip-deps     # skip apt
    ./scripts/bootstrap.sh --skip-build    # configure but don't compile

Bootstrap refuses to run if `src/cgame/` already exists — it is a one-shot
import. To re-import upstream, delete `src/` (and probably the other imported
trees) first.

## Important local modification

`src/game/g_xp_saver.c` was renamed to `.disabled` by the bootstrap and
replaced with `src/game/g_xp_saver_stub.c` (no-op stubs for `G_XPSaver_Load`,
`G_XPSaver_Store`, `G_XPSaver_Clear`, `G_XPSaver_Convert`). Reason: upstream
flagged it as needing rework and it depends on an SDK-internal sqlite layer
that's not in the public API. Restore the upstream file or write a real
implementation only when VanguardMod's persistence story (likely server-side
via vanguardmod.com, not local sqlite) is in place.

## WolfGuard layer

`wolfguard/` is the anti-cheat integration boundary. The hook surface lives
in `wolfguard/wolfguard.h` (`WG_Init`, `WG_Shutdown`, `WG_OnClientConnect`,
`WG_OnClientDisconnect`, `WG_OnClientCommand`, `WG_OnFrame`,
`WG_OnSnapshotSend`, `WG_QueryClientStatus`, plus `WG_API_VERSION`).
`wolfguard_null.c` is the community-build no-op. The real implementation
is closed-source and lives in `wolfguard/private/` (gitignored, only on
trusted build hosts).

`cmake/WolfGuard.cmake` exposes `vanguard_link_wolfguard(<target>)` and the
`-DVANGUARD_WITH_WOLFGUARD=ON/OFF` switch. It is **not currently included by
the active root CMakeLists.txt** — wiring `qagame` to call the WG hooks (per
BOOTSTRAP.md Step 3) and including this cmake module is pending work. Adding
or changing a hook is a coordinated change across `wolfguard.h`, the null
provider, the private impl, and the call sites in `src/game/`, with a
`WG_API_VERSION` bump if existing impls would break.

Never include WolfGuard headers from `cgame` or `ui`. WolfGuard is
server-side only.

## Coding style for new code

These rules apply to **Vanguard-specific additions only**. Code inherited
from the upstream ETLegacy tree keeps its original style — do not reformat
it to match.

  - Language: **C89** for anything that ends up in a QVM (`cgame`, `ui`,
    `qagame`). The QVM `lcc` rejects `//` comments, designated initialisers,
    VLAs, etc. Native-only code (protected qagame, WolfGuard private) may
    use C99/C11.
  - Use `/* ... */` comments only.
  - Naming: `vg_` prefix is mandatory on Vanguard additions (`vg_DoTheThing`,
    `vg_thing_t`, `VG_THING`). It keeps diffs against upstream auditable.
    WolfGuard-facing API uses the established `WG_*` / `wg_*`.
  - Tabs for indent in `.c`/`.h` (width 4); spaces for cmake/md/sh/yml.
  - No new globals — attach state to `level` or `g_entities` like the SDK.
  - Header guards: `VANGUARD_<PATH>_H` screaming-snake-case.

See `docs/CODING_STYLE.md` for the full list.

## Repository layout cheatsheet

  - `src/{cgame,game,ui,qcommon,...}` — upstream ETLegacy source (vendored)
  - `etmain/`, `vendor/`, `misc/` — upstream assets/libs/scripts (vendored)
  - `cmake/ETL*.cmake` — upstream build system (vendored)
  - `cmake/WolfGuard.cmake` — Vanguard-only, AC switch
  - `wolfguard/` — Vanguard public AC interface + null provider
  - `assets/` — Vanguard mod assets destined for the `.pk3` (currently empty)
  - `scripts/bootstrap.sh` — re-import upstream and produce a green build
  - `docs/{ARCHITECTURE,BUILDING,CODING_STYLE,WOLFGUARD}.md` — design docs
    (note: `BUILDING.md` describes the planned scaffold build, not the
    current bootstrap-driven build)
  - `CMakeLists.scaffold.txt.bak` — original Vanguard-only top-level CMake,
    preserved for reference when wiring the layered build
