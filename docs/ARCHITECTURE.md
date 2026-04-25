# Architecture

This document describes how the VanguardMod project is structured across
repositories, build flavours, and trust boundaries.

## Two-repo strategy

VanguardMod lives in two separate Git repositories:

### Public repo: `vanguardmod`

  - Full mod source (forked/derived from the ETLegacy Mod SDK)
  - `cgame`, `ui`, `qagame` — all gameplay, networking, UI code
  - WolfGuard **public interface** (`wolfguard/wolfguard.h`)
  - WolfGuard **null provider** (`wolfguard/wolfguard_null.c`)
  - Build system, docs, conventions

  License: GPL-2.0-or-later. Forkable, auditable, contributable.

### Private repo: `wolfguard-impl` (name TBD)

  - Closed-source WolfGuard implementation
  - Detection heuristics, signatures, network code talking to the
    vanguardmod.com backend
  - Drops into `wolfguard/private/` of the public repo at build time

  License: proprietary, not redistributable. Access limited to vetted
  core developers.

## Build flavours

| Flavour    | qagame form          | WolfGuard | How produced                                        |
|------------|----------------------|-----------|-----------------------------------------------------|
| Community  | QVM                  | null stub | `cmake -B build -DVANGUARD_WITH_WOLFGUARD=OFF`      |
| Protected  | native `.so` / `.dll`| linked    | private repo cloned + `-DVANGUARD_WITH_WOLFGUARD=ON`|

Client modules (`cgame.qvm`, `ui.qvm`) are **identical** between flavours
and live in the same `.pk3`. Only the server-side `qagame` differs:

  - Community servers run `qagame.qvm` from inside the `.pk3`. Anyone
    can build it from the public repo. No anti-cheat.
  - Protected servers register on vanguardmod.com, receive a
    server-bound native `qagame_*.so`/`.dll`, and load that instead of
    the QVM. WolfGuard is active.

## Why qagame goes native for the protected build

QVM bytecode runs in the engine's sandbox. It has no direct memory
access, no sockets of its own, no crypto, no file hashing beyond the
engine's vfs. That sandbox is fine for gameplay logic, but it is far
too restrictive for serious anti-cheat work. WolfGuard needs:

  - Outbound TCP/HTTPS to the central ban DB
  - Process and module inspection on the server host
  - Cryptographic verification of signatures
  - Optional native libraries (e.g. for hashing, TLS)

A native `qagame` gets all of that. Because `cgame` and `ui` are
unchanged, players join a protected server with the same `.pk3` they
use everywhere else.

## Trust model

| Actor                       | Sees public repo | Sees private repo | Can ship Protected |
|-----------------------------|------------------|-------------------|--------------------|
| Anyone on the internet      | yes              | no                | no                 |
| Public-repo contributors    | yes              | no                | no                 |
| Core developers (vetted)    | yes              | yes               | yes                |
| Server admins (registered)  | binaries         | no                | yes (binary only)  |

Server admins who want to run a protected server **never** build the
protected qagame themselves — they download a signed binary from
vanguardmod.com after registering their server. This keeps the private
code on a small set of build hosts and out of every server admin's
filesystem.

## Update flow

1. Public mod change → public repo PR → review → merge → tag.
2. Tagged build runs in CI on a build host that has the private repo
   cloned in. Both community and protected artefacts are produced.
3. Community `.pk3` is published openly. Protected qagame binaries are
   pushed to vanguardmod.com, signed, and offered to registered servers
   on next check-in.

## Distribution strategy: self-contained `.pk3`

ETLegacy refuses to UDP-download loose `.so` / `.dll` modules from a
server (security feature — a hostile server could otherwise push native
code to every connecting client). Mods therefore have to bundle their
modules inside a `.pk3`, and the engine fetches that `.pk3` over UDP
when the client lacks it locally.

Two strategies were considered:

  - **Lean.** Pack only `cgame` and `ui` (the client-side modules) for
    the host platform actually being built. This is what upstream
    ETLegacy does for its own `legacy_*.pk3`. Smallest archive, but a
    Windows player connecting to a Linux-built mod gets nothing
    runnable; you'd have to publish per-platform `.pk3`s and rely on
    the player picking the right one.

  - **Self-contained (chosen).** Pack every architecture variant of
    every module — `cgame`, `qagame`, `tvgame`, `ui` × Linux x86_64,
    Windows x86_64, Windows x86 — into one multi-arch `.pk3`. Larger
    (~23 MiB plus mod assets), but a single archive serves every
    client and every listen-server / TV-spectator scenario. The engine
    picks the right binary by filename at load time.

The trade-off: ~10 MiB of extra download per first-time connect in
exchange for one canonical artefact and zero per-platform packaging
churn. For a competitive mod with cup spectators (`tvgame`) and
self-hosted scrim servers (listen-server `qagame`), the self-contained
flavour is the only one that "just works" across the user base.

The `.pk3` is produced by upstream's `BUILD_MOD_PK3=ON` target with
Vanguard-specific patches in `cmake/ETLBuildMod.cmake`:

  - Activision pak0/1/2 and `mp_bin.pk3` are filtered out of the
    bundled etmain set, so the genuine WET paydata can sit in your
    local `etmain/` for testing without leaking into a redistributable.
  - Cross-built Windows DLLs from `build-windows/` and
    `build-windows-32/` are staged into the working dir before tar.
  - The tar list is extended with `qagame` and `tvgame` (upstream
    packs only client-side `cgame` + `ui`) and the Windows DLL
    basenames.

See `docs/BUILDING.md` for the build commands.

## Why not put WolfGuard into a QVM too?

Because the moment you ship the bytecode in a `.pk3`, you've shipped the
detection logic to anyone who can run a disassembler. The whole point of
WolfGuard's exclusivity is that the heuristics aren't public. Native +
server-only distribution is the cheapest path to that property without
inventing new infrastructure.

## Why not use ETLegacy itself as the host (fork the engine)?

Considered and rejected. Forking the engine means:

  - Players need a custom client to play. Fragments the user base.
  - Every ETLegacy security/portability fix becomes a manual merge.
  - Maintenance cost grows linearly with engine drift.

Staying as a QVM-distributed mod (with a server-side native `qagame` for
the protected flavour) gives us the anti-cheat depth we want without
dragging the entire engine along for the ride.
