# VanguardMod

A competitive Wolfenstein: Enemy Territory mod for the **ETLegacy** runtime,
designed from the ground up for ESL/cup play. VanguardMod is **not** a fork of
NoQuarter, NitMod, Jaymod, Silent or ETPub — it is a clean, opinionated build
on top of the ETLegacy Mod SDK with a strong, military-toned identity.

VanguardMod ships in two flavours that share the same client-side code (`cgame.qvm`,
`ui.qvm`) and differ only in the server-side `qagame` module:

| Build       | `qagame` form    | WolfGuard | Distribution         |
|-------------|------------------|-----------|----------------------|
| Community   | QVM              | null stub | public `.pk3`        |
| Protected   | native `.so`/`.dll` | linked  | server-registered    |

The Protected build is gated by server registration on
[vanguardmod.com](https://vanguardmod.com) and ships with **WolfGuard**, an
exclusive, closed-source anti-cheat with a centralized global ban database.

## Status

Early bootstrap — no gameplay code yet. See `BOOTSTRAP.md` for the next steps
to bring in the ETLegacy Mod SDK source tree and produce the first build.

## Layout

    .
    ├── BOOTSTRAP.md              ← read this first
    ├── CMakeLists.txt            ← top-level build entry
    ├── cmake/                    ← CMake helpers (WolfGuard switch, etc.)
    ├── wolfguard/                ← public AC interface + null provider
    │   ├── wolfguard.h
    │   ├── wolfguard_null.c
    │   └── private/              ← (gitignored) closed-source impl, core devs only
    ├── src/                      ← mod source (populated from ETLegacy Mod SDK)
    │   ├── cgame/
    │   ├── game/
    │   ├── ui/
    │   └── qcommon/
    ├── assets/                   ← UI, cfg, scripts that go into the .pk3
    ├── docs/                     ← architecture, build, style, WolfGuard design
    ├── scripts/                  ← helper scripts
    └── tools/                    ← QVM toolchain (lcc/q3asm) if vendored

## Quick start (community build)

    ./scripts/bootstrap.sh                 # pulls in the ETLegacy Mod SDK
    cmake -B build -DVANGUARD_WITH_WOLFGUARD=OFF
    cmake --build build -j

Outputs land in `build/vanguard/`.

## Documentation

- `docs/ARCHITECTURE.md` — two-repo strategy, build flavours, trust model
- `docs/BUILDING.md`     — full build instructions and CMake options
- `docs/WOLFGUARD.md`    — anti-cheat hook surface and detection tiers
- `docs/CODING_STYLE.md` — naming conventions, C standard, formatting

## License

GPL-2.0-or-later — see `LICENSE`. VanguardMod inherits this from the ETLegacy
Mod SDK lineage. WolfGuard's private implementation is **not** GPL and is
distributed only to trusted core developers under separate terms.
