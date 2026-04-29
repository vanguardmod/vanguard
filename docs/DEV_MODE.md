# VanguardMod dev mode

Server-authorised, client-rendered hitbox visualisation, gated by the
`vanguard_dev` cvar. Built for hitbox tuning, match-dispute analysis
and mod development. **Not** for use on public, cup, or live-match
servers.

## At a glance

  - **Server cvar:** `vanguard_dev` (default `0`, `CVAR_SERVERINFO`).
    The master switch — published to clients via the serverinfo
    configstring.
  - **Client cvars:** `cg_vanguardDevHitboxes` (binary, `0`/`1`) and
    `cg_vanguardDevAlpha` (`0.0` – `1.0`). Local render controls.
  - **Sample configs:** `configs/vanguard_dev.cfg` (turn on),
    `configs/vanguard_competitive.cfg` (turn off + lock down).

## What you actually see

When `vanguard_dev 1` is set on the server, every connected client
sees axis-aligned bounding boxes drawn around all visible players,
broken out into three coloured parts:

  - **Red** — head hitbox
  - **Yellow** — torso (the body / damage box)
  - **Green** — legs hitbox (only rendered while prone)

The boxes follow the player through stance changes (standing,
crouching, prone), through damage states, and through animation
movement. The renderer reads the same snapshot data cgame already
has, so the boxes update at full client framerate, not at server
tick rate.

The boxes are drawn with the engine's railgun-trail shader, so their
thickness is fixed by the renderer; the alpha (`cg_vanguardDevAlpha`)
is tunable client-side.

## Architecture

Dev mode is **server-authorised, client-rendered**. The server's only
job is to publish `vanguard_dev` in `CS_SERVERINFO`. The hitbox
geometry itself is rendered locally per frame by `cgame`
(`src/cgame/cg_vanguard_dev.c`) from existing player snapshot data —
`cent->lerpOrigin`, `cent->lerpAngles`, `cent->currentState.eFlags`
plus the local player's predicted state.

This split exists for one reason: **network cost**. The earlier
implementation routed visualisation through the server's
`g_debugPlayerHitboxes` path, which broadcasts ~24 `EV_RAILTRAIL`
events per visible player per frame. With two players in view the
snapshot saturation pushed observed pings from ~30 ms to ~900 ms —
unusable for tuning. Doing the same drawing on the client uses zero
extra bandwidth: snapshots already carry origin/angles/eFlags for
every visible player, and the geometry is reproducible from those
fields plus a handful of `bg_public.h` constants.

Safety properties:

  - A server running `vanguard_dev 0` (the default) cannot leak
    hitboxes — `cgs.vanguardDev` stays `0` and the renderer no-ops.
    The client cvar `cg_vanguardDevHitboxes` is a local filter only;
    flipping it on while the server is in `0` does nothing.
  - Outside dev mode the renderer adds zero work to `CG_DrawActiveFrame`
    (single integer check, early return).
  - The full implementation is in the public repo — anyone auditing
    can verify the gating.

The head- and leg-box origin math mirrors `G_BuildHead` /
`G_BuildLeg`'s no-MDX fallback path in `src/game/g_combat.c`. For the
local player we use `cg.predictedPlayerState` directly so boxes
match the server's antilag exactly. For other players we synthesize
viewheight from `EF_*` flags via `bg_public.h` constants — pmove
sets `ps.viewheight` to exactly those values, so the result matches.
The only approximation is `pmext.proneLegsOffset` for non-local
prone-crawling players (we use 0; stationary prone matches exactly).

## Cvar lifecycle (server)

When `vanguard_dev` flips `0 -> 1`:

  1. Current `sv_cheats` value is remembered, then `sv_cheats` is
     forced to `1` so the engine's CVAR_CHEAT client tooling
     (`noclip`, `cg_thirdperson`, `give`, ...) becomes available
     for inspecting player models from any angle.
  2. A loud red `DEV MODE ACTIVE` banner is printed to the server log.
  3. If the server is publicly heartbeating (`dedicated >= 2` *and*
     at least one `sv_master1`..`sv_master5` slot populated), a
     second "DEV MODE ON A PUBLIC SERVER, this is unsafe" banner
     follows. A LAN dedicated server (`dedicated 1`) is exempt —
     even if `sv_master1` is set to the engine's default, the
     engine never sends heartbeats from `dedicated 1`.

While `vanguard_dev` stays `1`, the banner is re-printed every five
minutes so a server that drifted into dev mode and was forgotten can
be spotted in any log scrape.

When `vanguard_dev` flips `1 -> 0`:

  1. `sv_cheats` is restored to its pre-toggle value.
  2. A green disable line is printed.

`G_ShutdownGame` (map change, server quit) also runs this restore.

The render-side cvars (`g_debugPlayerHitboxes`, `g_debugBullets`)
are **not** touched by dev mode — they belong to upstream's
server-broadcast debug mechanism, which we do not use any more. Set
them by hand if you want the legacy server-side overlay for some
reason.

## When to use it

  - **Hitbox tuning.** Watching the head/torso/leg boxes track an
    animation reveals exactly when the geometry diverges from the
    visible model — the precondition for any Phase-4b hitbox edits.
  - **Match-dispute analysis.** Re-run a demo on a private server in
    dev mode and step through the trace lines to see what the server
    actually saw.
  - **Mod development.** Verify that gameplay changes (stance edits,
    new weapons) don't silently break hitbox alignment.

## When NOT to use it

  - **Cup / league servers** during play. Dev mode = visible hitboxes
    = wallhack-equivalent advantage for everyone. Never toggle it
    while a real match is in progress.
  - **Public servers** that heartbeat to the master list. The
    secondary warning is there because this is the most common
    accident shape.
  - **Streamed scrims** if you don't want hitboxes in the VOD.

## Known limitations

### `sv_cheats` is read-only on Pterodactyl-managed servers

Pterodactyl (the panel many ETLegacy hosts use) marks `sv_cheats` as
read-only at the engine layer. When dev mode tries to flip it on,
the engine logs `sv_cheats is read only` and the cvar stays at `0`.

Consequence: hitbox visualisation **still works** — it is rendered
purely client-side and depends on nothing the server has to flip —
but every CVAR_CHEAT-protected client tool (`noclip`, `cg_thirdperson`,
`give`, `notarget`, `freeze`) refuses to run with "cheats not enabled".
For inspecting bot models from arbitrary angles you then need a
self-hosted dev server (the test-server harness in
`scripts/testserver/` is not Pterodactyl-managed and allows the
toggle).

The dev-mode disable path runs `sv_cheats` restore unconditionally,
so a host that locks the cvar is not corrupted by dev mode being
toggled — the restore is just a no-op the engine ignores.

### Prone-crawling legs box approximation

For non-local prone players the legs-box offset is approximated as
`0` (the server-side `pmext.proneLegsOffset` is not propagated to
other clients). A stationary prone target matches the server
exactly; a crawling one will show legs slightly above or below the
true collision box. The local player always uses the exact pmext
offset.

### Head box is an approximation, not the damage trace

The dev-mode renderer draws the head box from `G_BuildHead`'s
**no-MDX fallback** math (player origin + viewheight + a forward /
up offset table). The server's actual headshot trace, however,
runs through `mdx_head_position` whenever `FEATURE_SERVERMDX=ON`
(our build) and `g_realHead & REALHEAD_HEAD` (default `1`) — that
path bone-tracks the head through the MDX skeleton so the
collision box follows the helmet across every animation frame.

Consequence: in dev mode you may see the red head box sit a few
units away from the visible helmet, especially during run / lean
/ death animations. **That's a visualisation gap, not a hitreg
bug** — sniper headshots land on the helmet because the server
trace queries the bone position, not the box you're seeing. To
visualise the *real* damage box you would need to query MDX bones
client-side, which would require giving cgame access to the
player refent's bone state for non-local clients (a sizeable
refactor, scoped out of the current dev-mode tier).

If precise head-box visualisation matters more than the cgame-side
isolation, the upstream `cg_debugPlayerHitboxes` cvar (separate
from VanguardMod's `cg_vanguardDevHitboxes`) renders boxes from
the live `head.axis` bone tag in `cg_players.c` and matches the
server. Server-broadcast though, so it has the snapshot-saturation
problem we explicitly walked away from in v0.1.1.

## Before / after screenshots

Capturing a clean before/after pair for a hitbox change is the main
reason this mode exists. Recipe:

```
// in-game console, both before and after the change
exec configs/vanguard_dev.cfg     // make sure dev mode is on
cl_pause 1                         // freeze the scene
screenshotJPEG                     // dumped to ~/.etlegacy/<mod>/screenshots/
```

Tips:

  - Use `cg_drawCrosshair 0` and `cg_drawGun 0` for a less cluttered
    capture.
  - Position the test subject at a known map landmark so the after
    shot can be framed identically.
  - For animated stance comparisons, capture stand / crouch / prone
    in the same frame budget — `cl_pause 1` between each
    `screenshotJPEG`.

## Sample sessions

Turn dev mode on for the current map:

```
rcon vanguard_dev 1
```

Or via config:

```
rcon exec configs/vanguard_dev.cfg
```

Reset to a competitive baseline before going public:

```
rcon exec configs/vanguard_competitive.cfg
```

## Related

  - `sv_cheats` is the only cvar the server-side dev mode flips
    (and restores on disable). Upstream's `g_debugPlayerHitboxes`
    and `g_debugBullets` are independent — dev mode does not touch
    them and the client renderer does not need them.
  - The client renderer (`src/cgame/cg_vanguard_dev.c`) ports the
    head/leg origin math from `G_BuildHead` / `G_BuildLeg` in
    `src/game/g_combat.c`, no-MDX fallback path.
  - `cmake/ETLBuildMod.cmake` packs the `configs/` folder above into
    `vanguard_v0.4.1.pk3`, so any client connecting to a dev-mode
    server already has both presets locally as
    `configs/vanguard_dev.cfg` and `configs/vanguard_competitive.cfg`.
