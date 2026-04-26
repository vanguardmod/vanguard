# VanguardMod dev mode

Server-controlled hitbox / bullet visualisation, gated by the
`vanguard_dev` cvar. Built for hitbox tuning, match-dispute analysis
and mod development. **Not** for use on public, cup, or live-match
servers.

## At a glance

  - **Server cvar:** `vanguard_dev` (default `0`, `CVAR_SERVERINFO`).
    The master switch.
  - **Client cvars:** `cg_vanguardDevHitboxes` (`0` / `1` / `2`) and
    `cg_vanguardDevAlpha` (`0.0` – `1.0`). Render filters; passive.
  - **Sample configs:** `configs/vanguard_dev.cfg` (turn on),
    `configs/vanguard_competitive.cfg` (turn off + lock down).

## What you actually see

When `vanguard_dev 1` is set on the server, every connected client
sees axis-aligned bounding boxes drawn around all visible players,
broken out into three coloured parts:

  - **Red** — head hitbox
  - **Yellow** — torso (the body / damage box)
  - **Green** — legs hitbox

The colours follow the player through stance changes (standing,
crouching, prone), through damage states, and through animation
movement. With `cg_vanguardDevHitboxes 2` you additionally see the
red-line bullet trace each shot leaves on the server's authoritative
trace, useful for "did my shot actually hit where I aimed" analysis.

The boxes are rendered with the engine's railgun-trail shader, so
their thickness is fixed by the renderer; only the alpha
(`cg_vanguardDevAlpha`) is tunable from the client side.

## Architecture

Dev mode is server-controlled. A client cannot synthesise hitbox
visuals by itself — the engine only spawns the railtrail entities
cgame renders if the *server* emits the matching `EV_RAILTRAIL`
events, and the server only does that when its
`g_debugPlayerHitboxes` / `g_debugBullets` cvars are non-zero.
Toggling `vanguard_dev` is the only path to those events.

The client cvars are **passive filters**: `cg_vanguardDevHitboxes 0`
suppresses every railtrail drawing call locally, but cannot generate
hitbox data the server did not send. This means a competitive server
running `vanguard_dev 0` (the default) cannot leak hitbox visuals to
clients no matter what cvars they set. Safety from architecture, not
from code-hiding.

This file, the implementation, and the full client- and server-side
state are in the public Vanguard repo. The point is that *anyone*
auditing the code can verify the gating is intact — that is precisely
why publishing it is the safe option, not a risk.

## Cvar lifecycle (server)

When `vanguard_dev` flips `0 -> 1`:

  1. Current values of `g_debugPlayerHitboxes`, `g_debugBullets` and
     `sv_cheats` are remembered.
  2. `g_debugPlayerHitboxes` and `g_debugBullets` are forced to `1`,
     and `sv_cheats` is forced to `1` so the engine's cheat-gated
     client tooling (`noclip`, `cg_thirdperson`, `give`, ...) is
     usable for inspecting player models from any angle — the actual
     point of dev mode for hitbox tuning.
  3. A loud red `DEV MODE ACTIVE` banner is printed to the server log.
  4. If the server is publicly heartbeating (`dedicated >= 2` *and*
     at least one `sv_master1`..`sv_master5` slot populated), a
     second "DEV MODE ON A PUBLIC SERVER, this is unsafe" banner
     follows. A LAN dedicated server (`dedicated 1`) is exempt —
     even if `sv_master1` is set to the engine's default, the
     engine never sends heartbeats from `dedicated 1`.

While `vanguard_dev` stays `1`, the banner is re-printed every five
minutes so a server that drifted into dev mode and was forgotten can
be spotted in any log scrape.

When `vanguard_dev` flips `1 -> 0`:

  1. `g_debugPlayerHitboxes`, `g_debugBullets` and `sv_cheats` are
     restored to the values they had at the `0 -> 1` moment.
  2. A green disable line is printed.

`G_ShutdownGame` (map change, server quit) also runs this restore.

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

Consequence: hitbox visualisation **still works** (it depends only on
`g_debugPlayerHitboxes` / `g_debugBullets`, which dev mode also
forces), but every CVAR_CHEAT-protected client tool — `noclip`,
`cg_thirdperson`, `give`, `notarget`, `freeze` — refuses to run with
"cheats not enabled". For inspecting bot models from arbitrary
angles you then need a self-hosted dev server (the test-server
harness in `scripts/testserver/` is not Pterodactyl-managed and
allows the toggle).

The dev-mode disable path runs `sv_cheats` restore unconditionally,
so a host that locks the cvar is not corrupted by dev mode being
toggled — the restore is just a no-op the engine ignores.

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

  - Upstream cvars `g_debugPlayerHitboxes` (player bounding boxes),
    `g_debugBullets` (shot trace lines) and `sv_cheats` (engine
    cheat-protected client tooling). Vanguard takes ownership of all
    three while `vanguard_dev=1` and restores them on disable.
  - `cmake/ETLBuildMod.cmake` packs the `configs/` folder above into
    `vanguard_v0.1.0.pk3`, so any client connecting to a dev-mode
    server already has both presets locally as
    `configs/vanguard_dev.cfg` and `configs/vanguard_competitive.cfg`.
