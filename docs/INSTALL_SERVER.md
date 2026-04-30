# Installing VanguardMod on a Server

This guide covers installing the VanguardMod server-side files on
an existing ETLegacy server (Linux or Windows). Both platforms use
the same archive layout — pick the binaries that match your host's
architecture.

## Prerequisites

- An ETLegacy server (the dedicated build) installed and working
  with vanilla / `etmain` / `legacy`. VanguardMod is a mod, not a
  full game install; it does not replace the engine.
- Pure-server enabled (`sv_pure 1`), which is the default for
  competitive setups. Pure-server is what makes clients download
  the multi-arch `.pk3` from the server on connect.
- The unpacked archive on hand: `vanguard-vX.Y.Z-server.zip`.

## Quick install (Linux)

```bash
# 1. Unpack the server archive next to your ETLegacy install
cd /path/to/etlegacy-server
unzip /tmp/vanguard-vX.Y.Z-server.zip

# 2. The unzip places a vanguard/ directory containing 12 module
#    binaries plus the multi-arch vanguard_vX.Y.Z.pk3 — verify:
ls vanguard/
#   cgame.mp.x86_64.so   qagame.mp.x86_64.so   tvgame.mp.x86_64.so
#   ui.mp.x86_64.so      cgame_mp_x64.dll      qagame_mp_x64.dll
#   tvgame_mp_x64.dll    ui_mp_x64.dll         cgame_mp_x86.dll
#   qagame_mp_x86.dll    tvgame_mp_x86.dll     ui_mp_x86.dll
#   vanguard_vX.Y.Z.pk3

# 3. Launch the server with +set fs_game vanguard
./etlded.x86_64 +set fs_game vanguard +exec server.cfg
```

## Quick install (Windows)

Same archive, same `vanguard/` directory. Drop it next to your
`etlded.exe` install:

```
etlegacy-server\
├── etlded.exe
├── etmain\
└── vanguard\
    ├── (12 module binaries)
    └── vanguard_vX.Y.Z.pk3
```

Launch with `etlded.exe +set fs_game vanguard +exec server.cfg`.

The server will load `qagame_mp_x64.dll` (or `qagame_mp_x86.dll`
on 32-bit builds) directly via `dlopen`/`LoadLibrary`. The
`.pk3` is what gets shipped to connecting clients on a pure server.

## What's in the archive

| File | Purpose |
|------|---------|
| `vanguard/*.so`, `vanguard/*.dll` | Loose module binaries — the server `dlopen`s these |
| `vanguard/vanguard_vX.Y.Z.pk3` | Multi-arch redistributable — clients download this on connect |
| `LICENSE` | GPL-3.0-or-later license summary |
| `COPYRIGHT` | Copyright holders |
| `NOTICE` | Third-party attributions (ETLegacy, Wolfenstein:ET, Quake III, cJSON, MDX bone math) |
| `INSTALL.md` | This file |

## Pure-server requirement

ETLegacy refuses to UDP-download loose `.so`/`.dll` modules to
clients on connect (security feature). On a pure server the client
must get the modules out of a hash-checked `.pk3`. The
`vanguard_vX.Y.Z.pk3` in this archive bundles all 12 module
binaries plus the matching ETLegacy mod assets, so a client
joining your pure server can connect even with no prior knowledge
of VanguardMod.

If you run a non-pure server (`sv_pure 0`), connected clients still
need the loose binaries on their end. Most admins should leave
`sv_pure 1` as-is.

## Cup / competitive configuration

VanguardMod ships dev-mode hitbox visualisation, multi-region
damage, and a strict-hitbox toggle. For tournament play:

```
// in your server.cfg, at the bottom

// Lock down dev-mode visualisation (default off, force here).
set vanguard_dev 0

// Multi-region damage pipeline. mode 1 (default) routes through
// the Phase 6 mdx_hit_test. mode 0 falls back to vanilla AABB.
set vanguard_hitbox_mode 1

// Strict hitbox: reject AABB-only hits that don't match a
// human_base.hit capsule. Default 1 — flip to 0 for byte-
// identical legacy behaviour.
set vanguard_hitbox_strict 1
```

`vanguard_hitbox_mode` is `CVAR_LATCH` — it cannot drift mid-match.
The other two are `CVAR_ARCHIVE` — set them in `server.cfg` and
they persist across map changes.

## Upgrading from a previous VanguardMod version

```bash
# 1. Stop the server.
# 2. Replace the entire vanguard/ directory contents.
rm -rf vanguard/*
unzip /tmp/vanguard-v0.4.4-server.zip
# 3. Restart.
```

Connected clients will pull the new `.pk3` automatically on next
connect (visible as a brief "Awaiting downloads…" prompt). Old
client caches are addressed by hash, so the new pk3 with its new
filename is downloaded fresh.

## Troubleshooting

**Server logs `failed to load qagame.mp.<arch>.so`:**
You unpacked into the wrong directory or the binaries don't match
the server architecture. Verify with `file vanguard/qagame.mp.x86_64.so`.

**Connecting client logs `Couldn't load default.cfg`:**
The client is connecting before the pk3 finishes downloading. Try
again — if persistent, check that `vanguard_vX.Y.Z.pk3` is readable
on the server.

**Client crashes on connect:**
Likely a version mismatch — make sure both your server's
`vanguard_vX.Y.Z.pk3` and the client's local cache are the same
version. Force a re-download by deleting the client's
`~/.etlegacy/vanguard/vanguard_v*.pk3`.

For deeper issues, see the project's GitHub issues or the
maintainer contact in `COPYRIGHT`.

---

**Copyright Notice**

Copyright (c) 2026 wahke <info@wahke.lu> (https://wahke.lu)  
Copyright (c) 2026 VanguardMod Project Contributors

Licensed under GPL-3.0-or-later. Part of VanguardMod project.
Built on ETLegacy (https://www.etlegacy.com).
