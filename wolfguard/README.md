# WolfGuard layer

This directory is the integration boundary between VanguardMod and the
WolfGuard anti-cheat. Everything here is **public** except `private/`, which
is `.gitignored` and only exists on trusted core developers' machines.

## Files

| File                | Public? | Purpose                                  |
|---------------------|---------|------------------------------------------|
| `wolfguard.h`       | yes     | Public C API every provider must satisfy |
| `wolfguard_null.c`  | yes     | No-op provider (community build)         |
| `private/`          | no      | Closed-source real WolfGuard impl        |

## Build flavours

- **Community build** (default): `wolfguard_null.c` is linked. All hooks
  no-op. The mod runs perfectly fine, just without anti-cheat. This is
  what gets distributed in the public `.pk3`.

- **Protected build**: `wolfguard/private/` is present, CMake is run with
  `-DVANGUARD_WITH_WOLFGUARD=ON`, and the private implementation is linked
  instead. `qagame` is compiled as a native shared library (`.so`/`.dll`),
  not a QVM, so it can use sockets, crypto, and process inspection.

The two builds share `cgame.qvm` and `ui.qvm` byte-for-byte. The only
difference is the server-side `qagame` module.

## Why this split exists

WolfGuard's value rests on its detection logic being hard to study and on
its global ban DB being centralised. Open-sourcing it would erode both.
Keeping the API public and the impl private means:

  - Anyone can read this header and see exactly what WolfGuard *can*
    observe (transparency about the trust boundary).
  - Nobody can read the heuristics, signatures, or detection thresholds.
  - The community build is fully buildable without any private dependency,
    so the public repo stays useful for forks, audits, and contributions.

## Adding a new hook

1. Declare the prototype in `wolfguard.h`, bumping `WG_API_VERSION` if
   existing providers would break.
2. Implement the no-op in `wolfguard_null.c`.
3. Coordinate with the private repo maintainers to update their impl.
4. Wire the call site in `src/game/`.

Never put implementation logic in `wolfguard_null.c` beyond what is needed
to keep the build linkable. Detection belongs in `private/` exclusively.
