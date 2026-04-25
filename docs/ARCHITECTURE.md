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
