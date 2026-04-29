# Building VanguardMod

## Supported platforms

Tier 1 (regularly built and tested):

  - Linux x86_64 (Debian / Ubuntu)
  - Windows x86_64 (MSVC and MinGW)

Tier 2 (should work, less actively tested):

  - Linux ARM64
  - macOS x86_64 / arm64

The QVM artefacts are platform-independent. Only the protected-build
native `qagame` is platform-specific.

## Prerequisites

  - CMake ≥ 3.16
  - GCC or Clang (Linux/macOS) or MSVC ≥ 2019 (Windows)
  - Git

The QVM toolchain (`lcc` / `q3asm`) is vendored by the ETLegacy Mod SDK
under `tools/` after running `./scripts/bootstrap.sh`. You do not need
to install one separately.

cJSON is vendored under `vendor/cjson/` (v1.7.18, MIT) and compiled
directly into each mod binary, so `libcjson-dev` is **not** a required
host dependency — this matters in particular for the MinGW cross-compile
targets, where no system `libcjson` is available.

## Configuration options

Set with `-D<NAME>=<VALUE>` on the `cmake -B build` line.

| Option                       | Default     | Meaning                                            |
|------------------------------|-------------|----------------------------------------------------|
| `CMAKE_BUILD_TYPE`           | `Release`   | `Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel` |
| `VANGUARD_MOD_NAME`          | `vanguard`  | Output folder name under `build/`                  |
| `VANGUARD_WITH_WOLFGUARD`    | `OFF`       | Link against `wolfguard/private/` impl             |

## Community build

The default. No private dependencies, produces three QVMs.

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j

Outputs in `build/vanguard/`:

  - `cgame.qvm`
  - `ui.qvm`
  - `qagame.qvm`

Pack these into a `.pk3` along with the contents of `assets/` to get a
distributable mod folder.

## Mod distribution `.pk3`

ETLegacy refuses to UDP-download loose `.so`/`.dll` modules to clients on
connect (security feature). To make remote join work without the player
manually installing the mod, every architecture variant plus the matching
ETLegacy mod-asset subset is bundled into one zip-format `.pk3` produced
by upstream's `mod_pk3` target (with Vanguard-specific patches in
`cmake/ETLBuildMod.cmake`).

`bootstrap.sh` builds the three platforms in the right order and emits
`build/vanguard/vanguard_v0.4.2.pk3` as part of the Linux build's `ALL`
target. To repack ad-hoc after touching a single platform's binaries:

    cmake --build build --target mod_pk3

The archive contains 12 module binaries — `cgame`, `qagame`, `tvgame`,
`ui` for Linux x86_64, Windows x86_64 and Windows x86 — plus the open-
source ETLegacy mod assets from `etmain/` (gfx, scripts, sound, ui,
weapons, …). It does **not** contain the genuine Activision paks
(`pak0/1/2.pk3`, `mp_bin.pk3`) even when those are present in your
local `etmain/` for testing — they are filtered explicitly.

The loose binaries stay in `build/vanguard/` next to the `.pk3` so the
dedicated server can `dlopen()` them directly; only remote clients pull
the `.pk3` over the wire (visible as a brief "Awaiting downloads…" screen
on first connect, then cached in their game folder).

Override the version by reconfiguring with `CI_ETL_TAG=v0.4.2
CI_ETL_DESCRIBE=v0.4.2 cmake -B build …` — these env vars are upstream's
`ETLVersion.cmake` overrides and become the `_${VERSION}.pk3` suffix.

## Protected build (core devs only)

Requires the private WolfGuard repo cloned into `wolfguard/private/`.

    git clone <private-url> wolfguard/private
    cmake -B build-protected \
        -DCMAKE_BUILD_TYPE=Release \
        -DVANGUARD_WITH_WOLFGUARD=ON
    cmake --build build-protected -j

Additional output:

  - `qagame_mp_x86_64.so`  (Linux)
  - `qagame_mp_x86_64.dll` (Windows)

These are the server-only, native `qagame` flavours. Distribute via
vanguardmod.com to registered servers — never via the public `.pk3`.

## Debug builds

For development:

    cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
    cmake --build build-debug -j

Debug builds enable assertions, disable optimisations, and (for native
qagame) include symbols. QVM debug builds add range checks; expect
noticeably worse performance.

## Cleaning

    rm -rf build build-protected build-debug

CMake never writes outside the build directory you give it, so cleaning
is always this simple.

## CI

To be set up. Recommended layout: GitHub Actions matrix building both
community and protected flavours on Linux and Windows, with the private
repo cloned via a deploy key on the build host only.
