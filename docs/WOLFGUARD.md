# WolfGuard — anti-cheat design

This document covers the **public** view of WolfGuard: what it can
observe, where it plugs in, and how it escalates. Detection internals
(heuristics, signatures, thresholds) live in the private repo and are
deliberately not described here.

## Hook surface

All hook points are declared in `wolfguard/wolfguard.h`. Provider impls
implement them; the mod calls them at well-defined sites in `qagame`:

| Hook                       | Called from               | Frequency        |
|----------------------------|---------------------------|------------------|
| `WG_Init`                  | `G_InitGame`              | once per map     |
| `WG_Shutdown`              | `G_ShutdownGame`          | once per map     |
| `WG_OnClientConnect`       | `ClientConnect`           | per join         |
| `WG_OnClientDisconnect`    | `ClientDisconnect`        | per leave        |
| `WG_OnClientCommand`       | `ClientCommand`           | per cmd          |
| `WG_OnFrame`               | `G_RunFrame`              | every server frame |
| `WG_OnSnapshotSend`        | snapshot path             | per client per snap |

The hook surface is intentionally narrow. Adding hooks is a coordinated
change between the public API (`wolfguard.h`), the null provider, and
the private impl, with a `WG_API_VERSION` bump if existing impls would
break.

## Ban tiers

WolfGuard escalates through four tiers. Tier semantics are public; what
triggers each tier is not.

| Tier              | Action                                                        |
|-------------------|---------------------------------------------------------------|
| `WG_TIER_OBSERVED`| Logged for review. Player notices nothing.                    |
| `WG_TIER_KICK`    | Player kicked from the current match. Can rejoin.             |
| `WG_TIER_TEMP_BAN`| Time-limited ban on this server's WolfGuard-trusted network.  |
| `WG_TIER_GLOBAL_BAN`| Permanent ban applied across all WolfGuard-protected servers via the central DB. |

Global bans are issued conservatively and reviewed by a small set of
trusted admins before they become permanent. The model is intentionally
similar to Steam VAC: irreversible without out-of-band appeal, applied
only when confidence is high.

## Provider contract

A provider:

  1. Implements every prototype declared in `wolfguard.h`.
  2. Returns a `wg_info_t` from `WG_GetInfo` whose `api_version` equals
     `WG_API_VERSION` at compile time of the mod.
  3. Is reentrant **only** in the sense that the same hook is never
     called twice concurrently. All hooks run on the main game thread.
  4. Must not block. Network calls go through async paths internal to
     the provider.
  5. Must tolerate `NULL` for optional `const char *` parameters as
     documented in the header.

The null provider in `wolfguard_null.c` is the reference for what a
minimal compliant impl looks like.

## Trust boundary

The mod **does not** trust the provider with anything beyond the
arguments it passes. The provider sees:

  - Client number, GUID, IP (only in `WG_OnClientConnect`)
  - Client command strings (only in `WG_OnClientCommand`)
  - Server time / level time (in `WG_OnFrame`)

The provider **cannot**:

  - Mutate game state directly. To kick or ban, it returns the
    appropriate tier from `WG_QueryClientStatus` and the mod takes
    action.
  - Allocate game memory.
  - Spawn threads inside the qagame address space without coordinating
    explicitly with the mod (the protected build runs native, so this
    is technically possible, but it is a contract violation).

This boundary is the reason the API is small and read-mostly. The mod
remains the single source of truth for "what happens to the player";
WolfGuard only supplies signals.
