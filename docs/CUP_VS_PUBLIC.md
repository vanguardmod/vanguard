# Cup vs Public — Netcode Profile

VanguardMod ships a single cvar that swaps the server between
"cup-grade" and "public-grade" netcode tuning without touching
`server.cfg`:

```
set vanguard_netcode_profile "cup"     // tournament / scrim
set vanguard_netcode_profile "public"  // pub server (default)
set vanguard_netcode_profile "custom"  // hands-off, admin owns it
```

The cvar is `CVAR_LATCH | CVAR_ARCHIVE | CVAR_SERVERINFO`:

  - **Latched** — locked for the map lifetime. Change requires a
    map restart. By design (no mid-match drift between regimes).
  - **Archived** — persists in `etconfig_server.cfg`, no need to
    re-set after server restart.
  - **Serverinfo** — published to clients on connect; future
    cgame work will surface the profile in HUD / disclaimer copy.

## What each profile does

| Setting | `public` (default) | `cup` | `custom` |
|---|---|---|---|
| `sv_fps` | engine default (20) | 40 | untouched |
| `g_antilag` | engine default (1) | re-assert 1 | untouched |
| `g_antiwarp` | engine default (1) | re-assert 1 | untouched |
| Behaviour vs v0.4.x | byte-identical | competitive tuning | admin-owned |

For the **`cup`** profile, each cvar is set via `trap_Cvar_Set`
and verified by reading back through `trap_Cvar_VariableIntegerValue`.
The verify-after-set pattern catches the Pterodactyl edge case
(see "Pterodactyl gotcha" below).

For **`public`**, nothing changes — same behaviour any v0.4.x
server has today. Default value chosen so existing servers can
upgrade to v0.5.0 with zero config edits.

For **`custom`**, the apply path is also a no-op — VanguardMod
deliberately doesn't touch sv_fps / g_antilag / g_antiwarp. The
admin's `server.cfg` is authoritative, and the profile flag in
serverinfo signals "this server is hand-tuned" to anyone who's
looking.

## Phase 6 lag-comp covers multi-region damage automatically

A frequently asked question coming into Phase 7.2 was whether
ETLegacy's lag-compensation rewinds the per-bone animation state
needed by the multi-region `mdx_hit_test` damage path, or whether
it only rolls back player-origin and leaves the bone calculations
running off current-frame data (which would mis-credit hits on
laggy clients).

**It rewinds the bone state.** `G_StoreClientPosition`
(`g_antilag.c:125-188`) captures, per server tick per client:

  - origin / mins / maxs / viewangles
  - eFlags / pm_flags / viewheight / groundEntityNum
  - **the full torsoFrame state** (frame, oldFrame, frameModel,
    oldFrameModel, frameTime, oldFrameTime, yawAngle,
    pitchAngle, yawing, pitching, animation->movetype)
  - **the full legsFrame state** (same set)

`G_AdjustSingleClientPosition` (`g_antilag.c:196-486`) lerps the
continuous fields and snaps the discrete fields back to either of
the two markers bracketing the requested historical time, then
calls `trap_LinkEntity` to make the engine notice. By the time
`G_Damage`'s multi-region branch reads `ent->torsoFrame.*` /
`ent->legsFrame.*` (via `mdx_gentity_to_grefEntity`,
`g_mdx.c:298-349`), those fields are the historical values,
and `mdx_calculate_bone_lerp` walks the historical pose.

So **no Phase 7.2 work was needed** for that question. Both `cup`
and `public` (and `custom`) inherit the same lag-comp behaviour
the multi-region damage pipeline has been getting correctly since
v0.3.3 — the question only existed because nobody had traced the
chain end-to-end before.

## Caveats and tradeoffs

### Lag-comp history window shrinks at higher sv_fps

`MAX_CLIENT_MARKERS = 40` (`g_local.h:912`) — a fixed-size circular
buffer of historical client states. The buffer covers
`MAX_CLIENT_MARKERS / sv_fps` seconds of rewind history:

  - At `sv_fps = 20` (public default): **2 seconds** of history.
  - At `sv_fps = 40` (cup): **1 second** of history.

For typical cup pings (30–80 ms one-way → 60–160 ms round-trip,
i.e. ≤80 ms server-time deficit), 1 second of history is 12–33×
the rewind distance the antilag actually requests. Even tournament-
edge 200 ms pings (rare on cup servers, but accepted) need ~100 ms
of history — still 10× margin. The buffer would only **clip** at
roughly 500 ms ping, where the player is unplayable on networking
merits regardless of antilag.

This is an **explicitly accepted limitation in v0.5.0**, not a
deferred concern. No realistic cup-play scenario exercises the
clip. If a future live-test ever surfaces clipping symptoms (a
player reports hits visibly on-target but not registering), the
fix is a one-line bump of `MAX_CLIENT_MARKERS` to 80 (= 2 s
history at sv_fps 40):

```diff
-#define MAX_CLIENT_MARKERS 40
+#define MAX_CLIENT_MARKERS 80
```

The constant feeds modular arithmetic across `g_antilag.c`
(circular ring traversal in `G_AdjustSingleClientPosition`,
`G_StoreClientPosition`'s `topMarker` advance), so the change
wants its own validation pass — would land under a Phase 7.2.1
follow-up ticket if needed, not pre-emptively in v0.5.0.

### Animation timing comment in bg_pmove

`bg_pmove.c:5458` carries a TODO from upstream:

```c
// in reality, this should be split according to sv_fps,
```

The comment is about animation-frame timing math that was
historically only validated at `sv_fps = 20`. Most ETLegacy cup
servers run at `sv_fps = 40` in production with no observable
animation issues, so the comment is likely a latent issue with no
real bug behind it. Flagged here in case a future cup live-test
turns up an animation glitch that traces to it.

### ETLegacy issue #1637 — sv_fps 40 mechanics caveats

The ET engine has mechanics hardcoded for the 50ms tick (`sv_fps 20`).
At `sv_fps 40` (25ms tick) the following are known to misbehave per
[etlegacy/etlegacy#1637](https://github.com/etlegacy/etlegacy/issues/1637):

  - **Cv-ops disguise theft** — disguise grab range and timing window
    shift; some grabs that work at sv_fps 20 fail at sv_fps 40.
  - **Flamer max range** — flame-tick scaling means the visual flame
    reaches further than the damage hitbox at sv_fps 40.
  - **Script_movers** — map-script-driven movers (doors, lifts on some
    custom maps) timed against 50ms ticks may de-sync.
  - **Pause timer** — match-pause countdown skews against wall-clock
    time at sv_fps 40.

**Mitigation in cup play:** match-rules typically forbid disguise theft
in critical situations and use referee-managed pauses, sidestepping
the worst issues.

**If your cup ruleset requires strict sv_fps 20:** the canonical
escape hatch is the `custom` profile (see "Pterodactyl gotcha" below
for the same `set sv_fps … + profile custom` pattern). A future
`cup-strict` profile (sv_fps 20 baseline) may ship in v0.7.x if
cup-tester feedback demands it.

### Pterodactyl gotcha

Some managed-hosting providers (most prominently Pterodactyl) lock
certain engine cvars at the engine layer and silently drop
`trap_Cvar_Set` calls on them. If the cup-preset apply hits this,
the server log will show:

```
VG_Netcode: WARNING sv_fps set to 40 but engine reports 20 —
host may lock the cvar. Set in server.cfg and switch profile to
"custom" to avoid this warning.
```

The remedy on a locked-cvar host is the documented escape hatch:

1. Set `sv_fps 40` (and `g_antilag 1`, `g_antiwarp 1`) directly in
   `server.cfg` — these the engine accepts as part of normal cvar
   loading.
2. Set `vanguard_netcode_profile "custom"` — VanguardMod stops
   trying to set the cvars itself, the WARNING goes away, and the
   profile's published serverinfo value tells observers "this
   server is hand-tuned".

`vanguard_netcode_profile = "custom"` is also the right choice for
admins who want a non-standard combination (e.g. `sv_fps 40` for
hit-detection precision but `g_antiwarp 0` because they want to
gate it via a different layer — unusual, but the profile shouldn't
override that).

### Cup profile is latched

`vanguard_netcode_profile` is `CVAR_LATCH`, so changing the value
mid-match has no effect until the next map load. This is by
design: cup organisers don't want the regime drifting between
maps in a series, let alone between rounds.

To apply a profile change, restart the map (or end the match —
the next map's `G_InitGame` re-evaluates the cvar):

```
\map oasis    // or whatever map the server is running
```

## Movement: `g_pronedelay`

| Setting | VanguardMod (v0.6.1+) | ETPro `b_pronedelay` | ETLegacy legacy6 |
|---|---|---|---|
| `g_pronedelay` | **1** (TOGGLE bit, 1750ms unprone lock) | 1 (1750ms unprone lock) | 3 (TOGGLE + JUMP-block) |

**As of v0.6.1:** `defaultpublic.config` sets `g_pronedelay 1` to
match ETPro Cup orthodoxy. Previously (v0.6.0 and earlier) the
default was `0` (750ms gate, no jump block) which was softer than
every cup-mod listed in the Phase 7.3 audit.

`g_pronedelay` is a bitfield, not a duration:

  - bit 0 (`+1`) — `PRONEDELAY_TOGGLE`: 1750ms unprone lock instead
    of the default 750ms gate. ETPro convention.
  - bit 1 (`+2`) — `PRONEDELAY_JUMP`: blocks prone for 850ms after a
    jump, preventing prone-spam exploits.

If your server prefers the stricter ETLegacy `legacy6` ruleset,
override in your `server.cfg`:

```
set g_pronedelay 3
```

This activates both bits (TOGGLE + JUMP-block). Phase 7.3 audit §3.2
covers the cross-mod comparison.

## Verifying the active profile

Three places to check:

1. Server console at map start:

   ```
   VG_Netcode: profile=cup
   VG_Netcode: applying "cup" preset (sv_fps 40, g_antilag 1,
   g_antiwarp 1)
   VG_Netcode: applied sv_fps=40
   VG_Netcode: applied g_antilag=1
   VG_Netcode: applied g_antiwarp=1
   ```

2. From rcon:

   ```
   \rcon vanguard_netcode_profile
   "vanguard_netcode_profile" is:"cup" default:"public"
   ```

3. From any connected client (via `serverinfo`):

   ```
   \serverinfo
   ... vanguard_netcode_profile : cup ...
   ```

## Recommended cup configuration

`etmain/configs/vanguard_competitive.cfg` already locks down the
dev-mode visualisation and forces `sv_pure 1`. With v0.5.0 it
reduces to one additional line:

```
set vanguard_netcode_profile "cup"
```

Running `\exec configs/vanguard_competitive.cfg` followed by a
map restart applies the full cup posture.

## Reference

- Phase 7.2 audit: `docs/notes/PHASE_7_2_AUDIT.md`
- Phase 7.3 audit: `docs/notes/PHASE_7_3_AUDIT.md`
- ETLegacy issue #1637: <https://github.com/etlegacy/etlegacy/issues/1637>
- ETPro Crossfire/EuroCup configs: `b_pronedelay 1` in `global1.config` + `global6.config`

---

**Copyright Notice**

Copyright (c) 2026 wahke <info@wahke.lu> (https://wahke.lu)  
Copyright (c) 2026 VanguardMod Project Contributors

Licensed under GPL-3.0-or-later. Part of VanguardMod project.
Built on ETLegacy (https://www.etlegacy.com).
