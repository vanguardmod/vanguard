# Local test server

Reproducible setup for bringing up an `etlded` instance that loads
VanguardMod against an isolated home path, so the WolfGuard layer can be
seen firing in real server logs without touching a production install.

## Prerequisites

  - Original Wolfenstein: Enemy Territory paks at `etmain/pak0.pk3`,
    `etmain/pak1.pk3`, `etmain/pak2.pk3`. These are copyrighted assets,
    are listed in `.gitignore`, and never enter the repo. Copy them in
    from a legitimate WET install.
  - The mod was built into `build/vanguard/` per the bootstrap flags
    (see `BOOTSTRAP.md`). The launcher relies on the `vanguard/`
    symlink pointing at that directory.
  - System library: `libminizip-dev` (engine PK3 handling). Install once:
    `sudo apt install -y libminizip-dev`.

## Build the server binary

A separate build directory keeps the engine artefacts away from the
mod-only `build/`:

    cmake -B build-server \
        -DBUILD_CLIENT=OFF -DBUILD_SERVER=ON -DBUILD_MOD=OFF \
        -DBUNDLED_LIBS=OFF -DCROSS_COMPILE32=OFF \
        -DFEATURE_CURL=OFF -DFEATURE_SSL=OFF -DFEATURE_AUTH=OFF \
        -DFEATURE_AUTOUPDATE=OFF -DFEATURE_IRC_SERVER=OFF \
        -DFEATURE_TRACKER=OFF \
        -DFEATURE_LUA=OFF -DFEATURE_OMNIBOT=OFF -DFEATURE_DBMS=OFF \
        -DFEATURE_RATING=OFF -DFEATURE_PRESTIGE=OFF \
        -DINSTALL_EXTRA=OFF
    cmake --build build-server -j

Most optional features are disabled deliberately so the dependency
surface stays small (no curl, ssl, sqlite, lua etc. needed). Output:
`build-server/etlded.x86_64`.

### Upstream patch carried locally

`src/qcommon/common.c` had `#include "q_unicode.h"` inside
`#ifdef FEATURE_DBMS`, but `Q_UTF8_*` is referenced unconditionally in
the same file. With `FEATURE_DBMS=OFF` this fails compile. The
include has been moved outside the DBMS guard. This is a latent
upstream bug — only manifests for builds that turn DBMS off.

## Run the server

    ./scripts/testserver/run.sh

This:

  1. Verifies `etlded.x86_64` and `etmain/pak0.pk3` are present.
  2. Stages `scripts/testserver/server.cfg` into
     `~/.etlegacy-vanguard-test/vanguard/server.cfg` so the engine's
     filesystem search picks it up via `+exec server.cfg`.
  3. Launches `etlded.x86_64` with `dedicated 1`, `fs_game vanguard`,
     `fs_homepath ~/.etlegacy-vanguard-test`, and `+map oasis`.
  4. Redirects all output to `scripts/testserver/server.log`.

Stop with `Ctrl+C` (sends SIGINT — clean shutdown via the engine's
signal handler). Tail the log live from another shell:

    tail -F scripts/testserver/server.log

## What "working" looks like

The first ~150 log lines should contain, in order:

    execing server.cfg
    ------- Game Initialization -------
    gamename: vanguard
    ...
    VanguardMod: WolfGuard null provider active
    Game Initialization completed in 0.<...> seconds

  - `gamename: vanguard` confirms the `MODNAME` cmake macro propagated
    correctly (i.e. the rename from "legacy" landed).
  - `VanguardMod: WolfGuard null provider active` is the `G_Printf` at
    the end of `G_InitGame` (`src/game/g_main.c`), proving the Stage 2
    hook is wired and the null provider's `WG_GetInfo()` returns the
    expected name.

If `WolfGuard` does not appear, the layer is not actually being called
from qagame; double-check that `build/vanguard/qagame.mp.x86_64.so`
contains the symbols (`nm | grep WG_`) and that the `vanguard/`
symlink resolves.

## Files & paths

| Where                                 | What                                                |
|---------------------------------------|-----------------------------------------------------|
| `build-server/etlded.x86_64`          | The dedicated server binary (gitignored)            |
| `vanguard/` (symlink → `build/vanguard/`) | Mod modules, found by the engine via fs_game   |
| `scripts/testserver/server.cfg`       | Server configuration source of truth                |
| `scripts/testserver/run.sh`           | Launcher                                            |
| `scripts/testserver/server.log`       | Run log (gitignored, overwritten each run)          |
| `~/.etlegacy-vanguard-test/`          | Isolated fs_homepath, outside the repo              |

`build-server/` and `scripts/testserver/server.log` are listed in
`.gitignore` and must stay there. `~/.etlegacy-vanguard-test/` lives
outside the repo and needs no entry.

## Bots (Omni-Bot)

The test server runs Omni-Bot so we can populate it with AI players for
hitbox visualisation and gameplay-feature shakeouts. The qagame mod is
built with `FEATURE_OMNIBOT=ON` (Linux only — Windows cross builds stay
off) and dlopen()s `omnibot_et.x86_64.so` from `vanguard/omni-bot/` at
map load.

### Where the runtime comes from

The Omni-Bot runtime (~26MB tarball) is not in any apt repo. The
bootstrap fetches it from `https://mirror.etlegacy.com/omnibot/` on
first run and caches it under `vendor/omnibot-runtime/`. The cache
survives `rm -rf build`, so subsequent bootstraps skip the download.

After the Linux mod build, `bootstrap.sh` copies the cached tree into
`build/vanguard/omni-bot/`, which is what the engine searches via
`omnibot_path "omni-bot"` (relative to `fs_game/`).

If `mirror.etlegacy.com` is unreachable, place the tarball manually at
`vendor/omnibot-runtime/omnibot-linux-latest.tar.gz` and re-run
`scripts/bootstrap.sh` — it will pick up the file from the cache
without trying to fetch.

### Server cvars

Already set in `scripts/testserver/server.cfg`:

  - `bot_enable 1`     — engine-side master switch (CVAR_LATCH; must
                         be set before `+map`)
  - `omnibot_enable 1` — qagame-side switch
  - `omnibot_path "omni-bot"` — search path under `fs_game/`

If any of these is missing or set late, qagame logs `Omni-bot library
not loaded` and `bot addbot` will fail silently.

### What "working" looks like

In the server log shortly after `Game Initialization completed`:

    Omni-bot Loaded: <version>
    Loaded mapscript: <map>.gm

If you instead see `Failed to load Omni-bot` or `omnibot_et.so: cannot
open shared object file`, check that `build/vanguard/omni-bot/omnibot_et.x86_64.so`
exists and that `omnibot_path` matches the directory name.

### Bot commands (rcon / server console)

  - `bot addbot <team> <skill> [name] [classnum]`
    - team: `axis` or `allies`
    - skill: `1` (easy) … `5` (very hard)
    - example: `bot addbot allies 3 BotJim`
  - `bot kickbot <name|number>` — remove one
  - `bot kickbots` — clear all
  - `bot maxbots <n>` — cap auto-spawned bots (some mapscripts auto-fill teams)
  - `bot debug 1` — verbose Omni-Bot logging
  - `bot goals` / `bot waypoints` — debug overlays (need cheats and a
    spectating client; useful with `vanguard_dev 1`)

For hitbox screenshots: spawn 1-2 bots, free-cam to them with `noclip`
+ `cg_thirdperson 1` (both unlocked by `sv_cheats 1` which dev mode
sets automatically), and snap with `screenshotJPEG`.

### Updating the runtime

Delete the cache and re-bootstrap to pull a newer Omni-Bot:

    rm -rf vendor/omnibot-runtime/
    ./scripts/bootstrap.sh
