# VanguardMod Branding

This document explains VanguardMod's mod-list branding system
(Phase Branding 2, v0.7.0.1).

## What it does

When the player opens the Mods menu, VanguardMod renders a
color-coded list of all installed mods. Each known ET mod
(Jaymod, ETLegacy, NoQuarter, silEnT, ETPub, etc.) is displayed
in its respective brand colors, with VanguardMod itself
prominently shown as `^8Vanguard^7Mod`.

## How it works

The UI module (`src/ui/ui_main.c::UI_FeederItemText case
FEEDER_MODS`, line ~8083) queries a 3-tier cascade:

1. **TIER 1 — VanguardMod hardcoded lookup table**
   (`src/ui/ui_vg_branding.{c,h}`): returns a color-coded string
   for known mods. **Overrides** description.txt for consistency.
2. **TIER 2 — `description.txt`** (existing ETLegacy behaviour):
   if the directory is not in the lookup table, reads
   `description.txt` from the mod's directory.
3. **TIER 3 — Raw directory name** (existing fallback): if
   neither above match, displays the directory name plain.

This means:

- Known mods always display correctly, even if their
  `description.txt` is missing or fails to deploy.
- Community mods that ship `description.txt` continue to work
  via TIER 2.
- Truly unknown mods display the directory name (no broken UI).

## Currently branded mods

| Directory | Display |
|---|---|
| `vanguard` | `^8Vanguard^7Mod` |
| `legacy` | `^1ET^7:Legacy` |
| `jaymod` | `^3Jay^7mod` |
| `nitmod` | `^7N^1!^7tmod` |
| `noquarter` | `^1No Quarter` |
| `etpro` | `^7ETPro` |
| `silent` | `^7silEnT` |
| `etpub` | `^7ETPub` |
| `compet` | `^7Comp^1ET` |
| `xmod` | `^7xmod` |
| `etmain` | `^7Wolfenstein: ET` |

## Adding a mod

If you maintain an ET mod and want VanguardMod to display your
brand colors in our Mods menu, please open a PR adding an entry
to `src/ui/ui_vg_branding.c::vg_mod_brands[]`. We accept
community contributions for mod-author-specified branding strings.

The lookup is **case-insensitive** (`Q_stricmp`), so the
directory-name key in the table is canonical lowercase regardless
of how an admin named the directory on disk.

## Diagnostics

Set `vanguard_diag_branding 1` to log per-mod resolution outcomes
to console. Useful when debugging "why doesn't my mod show
branded".

Example output (with three known mods + one unknown installed):

```
VG_Brand: vanguard     -> ^8Vanguard^7Mod (table)
VG_Brand: legacy       -> ^1ET^7:Legacy (table)
VG_Brand: jaymod       -> ^3Jay^7mod (table)
VG_Brand: mymod_test   -> Some Custom Mod 1.2 (description.txt)
VG_Brand: othermod     -> (no table, no description; raw dir name)
```

The cvar is `CVAR_TEMP` — re-arm per session, not persisted.

## License attribution

The mod-list color-coding pattern was inspired by **Nitmod** (by
jaquboss). VanguardMod's implementation is a clean-room
re-implementation:

- Concept observed via `strings` extraction from Nitmod's
  `ui_mp_*.so` binary.
- No code, data structures, or display strings copied from the
  Nitmod binary.
- Per-mod color choices are VanguardMod's own decisions based on
  each mod's known community brand colors.
- Hook-point design (`UI_FeederItemText case FEEDER_MODS`) is
  VanguardMod's own implementation choice, based on ETLegacy's
  upstream UI architecture.

We acknowledge Nitmod for the design pattern inspiration. Both
projects are W:ET-derived under id Software's 2010 GPL release;
the pattern (lookup table → display string) is generic enough
that no copyright concerns apply, but the credit is
good-open-source-citizenship.

## Reference

- Architectural recon: `docs/notes/PHASE_BRANDING_2_AUDIT.md`
- Implementation: `src/ui/ui_vg_branding.{c,h}`
- Hook: `src/ui/ui_main.c::UI_FeederItemText case FEEDER_MODS`
- Diagnostic cvar: `vanguard_diag_branding` (declared in
  `src/ui/ui_cvars.{c,h}`, registered via `cvarTable[]`)
- Earlier branding work: `docs/notes/PHASE_BRANDING_AUDIT.md`
  (the v0.5.2.4 description.txt deployment fix — superseded by
  Branding 2 for consistent multi-mod display)
