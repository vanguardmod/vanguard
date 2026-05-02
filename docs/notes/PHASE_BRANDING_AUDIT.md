# Phase Branding Audit — Mod-list display string

Status: recon only, no code changes.
Author: Claude (Opus 4.7) for wahke
Date: 2026-04-30

## A. The question

In ETLegacy's mod-selection menu (Main Menu -> Mods), the upstream `legacy`
mod renders as

    ^1ET^7: LEGACY^1 - ^7legacy mod

(red "ET", white ": LEGACY", red " - ", white "legacy mod") via Quake3
colour codes. VanguardMod's entry in the same list shows up as the plain
lowercase directory name `vanguard`. We want VanguardMod to display its
own coloured brand string the same way.

## B. What the user already tried

1. Edited `misc/description.txt` to `^1Vanguard^7Mod`. No visible change
   in the mod list. (See current contents at
   `/home/wahke/projekte/vanguard/misc/description.txt:1`.)
2. GitHub code search `repo:etlegacy/etlegacy "^1ET^7: LEGACY^1 - ^7legacy mod"`
   confirms the literal string lives in the upstream tree as data, not
   engine-hardcoded.

## C. Where the string is defined

The brand string is stored as the **first 48 bytes of a loose file named
`description.txt` that lives inside the mod's directory (peer to
`etmain/`)**. For upstream ETLegacy this file ships with their installer
and contains exactly:

    ^1ET^7: LEGACY^1 - ^7legacy mod

In this repo, the source-of-truth file is:

  - `/home/wahke/projekte/vanguard/misc/description.txt`
    (currently contains `^1Vanguard^7Mod` — the user's edit, but it
    never reaches the runtime, see section E).

The CMake install rule that places `description.txt` into the mod
directory at install-time is:

  - `cmake/ETLInstall.cmake:9-13`
    ```
    # description file - see FS_GetModList
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/misc/description.txt"
        DESTINATION "${INSTALL_DEFAULT_MODDIR}/${MODNAME}"
        PERMISSIONS OWNER_WRITE OWNER_READ GROUP_READ WORLD_READ
    )
    ```
  - With `MODNAME="vanguard"` (`CMakeLists.txt:38`) this resolves to
    `/usr/lib/etlegacy/vanguard/description.txt` on Linux installs (or
    the equivalent under whatever `CMAKE_INSTALL_PREFIX` was used).

## D. Where the engine reads it

Call chain, all in this repo's vendored upstream source:

1. **UI build-up** — when the user opens the Mods menu, the UI feeder
   issues a virtual file listing query:
     `src/ui/ui_main.c:4327` --
     `numdirs = trap_FS_GetFileList("$modlist", "", dirlist, sizeof(dirlist));`
2. **Engine routing** — the `$modlist` token is intercepted in qcommon:
     `src/qcommon/files.c:3212-3215` --
     ```
     if (Q_stricmp(path, "$modlist") == 0)
     {
         return FS_GetModList(listbuf, bufsize);
     }
     ```
3. **Mod enumeration + description load** — `FS_GetModList` walks
   `fs_homepath` and `fs_basepath` for sibling directories that contain
   at least one `.pk3`, then for each candidate mod constructs a per-mod
   `description.txt` path and reads up to 48 bytes (one line, including
   the colour codes):
     `src/qcommon/files.c:3336-3450` -- in particular
     - line 3413: `Com_sprintf(descPath, sizeof(descPath), "%s%cdescription.txt", name, PATH_SEP);`
     - line 3415: `nDescLen = FS_SV_FOpenFileRead(descPath, &descHandle);`
     - line 3423: `nDescLen = fread(descPath, 1, 48, file);`
     - line 3432 (no-description fallback): `Q_strncpyz(descPath, name, sizeof(descPath));`
4. **UI consumption** — the listbuf returned by the trap is parsed into
   `uiInfo.modList[i].modName` (directory name) and `.modDescr`
   (contents of `description.txt`):
     `src/ui/ui_main.c:4317-4345` (`UI_LoadMods`)
   Display logic prefers `modDescr` and only falls back to `modName`
   when `modDescr` is empty:
     `src/ui/ui_main.c:8077-8088`
     ```
     case FEEDER_MODS:
         if (uiInfo.modList[index].modDescr && *uiInfo.modList[index].modDescr)
             return uiInfo.modList[index].modDescr;
         else
             return uiInfo.modList[index].modName;
     ```
   The bare lowercase `vanguard` the user sees is exactly this fallback
   firing because `modDescr` is empty.

### Search-path semantics of `FS_SV_FOpenFileRead`

`src/qcommon/files.c:977-1022` -- `FS_SV_FOpenFileRead` uses
`Sys_FOpen()` directly on the OS path, **never** consulting the pk3
zipfile index. Order:

  1. `<fs_homepath>/<modname>/description.txt`
  2. `<fs_basepath>/<modname>/description.txt`

(Filesystem-only, single-line, max 48 bytes read.)

### Caching

`UI_LoadMods` (`src/ui/ui_main.c:5010`) is invoked once per visit to
the mod-selection menu (from `Menus_ActivateByName` setup paths), not
per frame. The disk read is therefore cheap and effectively
"on-demand"; restarting the menu is enough to pick up edits.

## E. Why `description.txt` did not work for VanguardMod

Two compounding reasons, both demonstrated by the engine source above:

### E.1 The file is loaded as a loose OS file, not from the pk3

`FS_SV_FOpenFileRead` (`src/qcommon/files.c:977`) goes straight to
`Sys_FOpen` on `fs_homepath` then `fs_basepath`. Even if
`description.txt` were packed into `vanguard_v0.5.2.2.pk3` it would be
ignored. The pk3 packer in `cmake/ETLBuildMod.cmake:357,363` globs
`etmain/*` only and never reaches `misc/`, so it isn't packed anyway.

### E.2 No build step delivers `misc/description.txt` to the runtime path

`misc/description.txt` is consumed exclusively by
`cmake/ETLInstall.cmake:10` during `cmake --install`. The repo's
canonical configure (`scripts/bootstrap.sh:191-201`) and the test
launcher (`scripts/testserver/run.sh:53-60`) both run the binary
**without** ever running `cmake --install`. They configure
`fs_basepath="$REPO_ROOT"`, `fs_game=vanguard`, so the engine looks
for the description file at:

  - `$HOME/.etlegacy-vanguard-test/vanguard/description.txt`  (homepath, miss)
  - `$REPO_ROOT/vanguard/description.txt`                      (basepath, miss)

Neither path exists. There is **no `vanguard/` directory peer to
`etmain/` in the repo at all** — the build artefacts land in
`$REPO_ROOT/build/vanguard/`, which is not what fs_basepath points at.
Result: `FS_SV_FOpenFileRead` returns 0, the fallback at line 3432
copies `name` (`"vanguard"`) into `descPath`, and the UI shows the
bare directory name.

In short: `misc/description.txt` is the right *source* file but
nothing in the repo currently *delivers* it to a path the running
engine sees.

## F. Implementation proposal

We need `description.txt` to land in `<fs_basepath>/vanguard/` (and
ideally also be installed alongside the `.pk3` for end-users). The
cleanest option in this repo is a small CMake change that copies the
file into the build's mod staging directory (which is also where the
`.pk3` is written, and which the testserver script already uses as
`fs_basepath` via the build-output path).

### F.1 Required changes

1. **Pin source content** — fix the brand string in
   `misc/description.txt` to one of the candidates in section G.
   (The file already exists; this is a 1-line text edit.)

2. **Wire it into the mod stage in CMake** — extend
   `cmake/ETLBuildMod.cmake` (around line 355-367, the
   `add_custom_command(OUTPUT ... mod pk3 ...)` block) so the build
   also stages `description.txt` into the mod output directory next
   to the `.pk3`. One added line, e.g.:

       COMMAND ${CMAKE_COMMAND} -E copy_if_different
           ${CMAKE_CURRENT_SOURCE_DIR}/misc/description.txt
           ${CMAKE_CURRENT_BINARY_DIR}/${MODNAME}/description.txt

   Plus add the source to the `DEPENDS` list so re-edits trigger a
   re-stage.

3. **(Optional, recommended) Update `scripts/testserver/run.sh`** to
   point `fs_basepath` at `$REPO_ROOT/build` rather than `$REPO_ROOT`,
   or copy `description.txt` into `$HOMEPATH/vanguard/` the same way
   it copies `server.cfg` (line 43). The current bootstrap layout
   places the runtime mod dir at `$REPO_ROOT/build/vanguard/`, but
   `fs_basepath="$REPO_ROOT"` (line 55) tells the engine to look at
   `$REPO_ROOT/vanguard/` — these don't line up. If the testserver
   actually finds the mod today via the `build/` path (it does, since
   the .so/.pk3 load), then `fs_basepath` is being interpreted to
   include `build/` somewhere, but that's worth verifying. Simplest
   safety net: add a `cp` line after run.sh:43 mirroring the
   `server.cfg` pattern:

       cp "$REPO_ROOT/misc/description.txt" "$HOMEPATH/vanguard/description.txt"

   That guarantees `fs_homepath` has the file regardless of basepath
   resolution quirks.

4. **End-user install path is already correct** — `cmake/ETLInstall.cmake:10`
   already places `misc/description.txt` next to the `.pk3` for
   `cmake --install` consumers. No change needed for distribution.

### F.2 LoC and files-touched estimate

  - `misc/description.txt`             -- 1 line edit (the brand string)
  - `cmake/ETLBuildMod.cmake`          -- ~3 lines added (1 `COMMAND`
    clause, 1 entry in `DEPENDS`, optional comment)
  - `scripts/testserver/run.sh`        -- 1 line added (optional but
    recommended `cp` for fs_homepath)

Total: roughly **5 lines across 3 files**, all of them additive and
upstream-friendly (the cmake change can be marked with a
`# Vanguard:` comment matching the existing convention at
ETLBuildMod.cmake:243-254 etc.).

### F.3 Verification path (after implementation, not done here)

  1. Re-run `cmake --build build`.
  2. Confirm `build/vanguard/description.txt` exists with the new
     content.
  3. Run `scripts/testserver/run.sh`, then on a client `\fs_game vanguard`
     and open Main Menu -> Mods. The list entry for `vanguard` should
     render with the colour codes interpreted.
  4. The whole load chain is one-shot per menu-open; close and re-open
     the Mods menu after a content edit to refresh.

## G. Brand string recommendation

The upstream pattern is:

    ^1ET^7: LEGACY^1 - ^7legacy mod
    [red brand-prefix][white brand-suffix][red separator][white tagline]

That is: a two-tone wordmark, a coloured separator, and a short
lowercase tagline that describes the mod's character. Keeping this
shape preserves visual continuity in the mod list.

Of the user's three options:

  1. `^1Vanguard^7Mod ^9- ^7competitive cup foundation`
     - Pros: matches structure exactly (red/white wordmark, separator,
       white tagline); colour `^9` is grey, which is a fine subdued
       separator. "competitive cup foundation" is descriptive and
       on-brand.
     - Cons: "foundation" is a bit aspirational/marketing-y for a
       mod-list entry; 47 chars total -- right at the 48-byte
       fread limit (`src/qcommon/files.c:3423`). Will fit but with
       zero headroom. **Not recommended for that reason alone.**

  2. `^4Vanguard^7Mod ^9- ^7cup mode`
     - Pros: short (29 chars), well under the 48-byte cap; `^4` is
       blue which differentiates from ETLegacy red and reads as a
       deliberate brand choice.
     - Cons: "cup mode" is slightly understated; you might want
       something more evocative on first impression. But it's clean.

  3. `^2Vanguard^7Mod ^7- ^9military-grade hitreg`
     - Pros: confident tagline, leans into the actual technical
       angle (Phase 6.x hitbox/hitreg work), 39 chars. `^2` is green.
     - Cons: separator and tagline use different colours (`^7` vs
       `^9`) which inverts the upstream pattern (upstream uses red
       separator + white tagline, both mid-saturation). Mixing white
       sep + grey tagline reads slightly off. Also "military-grade"
       is a marketing claim that may invite scrutiny.

### G.1 Recommended

**Option 2 with a minor polish, or a fourth option that mirrors
upstream's exact rhythm:**

  - **Pick if you want safety:** option 2 as-is --
    `^4Vanguard^7Mod ^9- ^7cup mode`
  - **Suggested fourth option (closer to upstream tone):**
    `^4Vanguard^7Mod^4 - ^7competitive cup mod`
    - 32 chars, well under the 48-byte cap.
    - Mirrors upstream's `^1...^7...^1 - ^7...` shape, just with
      blue (`^4`) instead of red.
    - Tagline reads as a peer to "legacy mod" rather than as a
      slogan, which matches the menu's information density.
    - Avoids both the borderline length of option 1 and the colour-
      mismatch of option 3.

If the user prefers green (option 3's `^2`) for the wordmark, the
same shape works:
    `^2Vanguard^7Mod^2 - ^7competitive cup mod`

If they want to keep it punchy, **option 2 unchanged** is fine. The
mod-list cell is small and reads better when the tagline is short.

### G.2 Hard constraints to respect

  - **Strict 48-byte cap** on the file's first line (counted including
    `^N` codes; each `^N` is 2 bytes). Anything past 48 bytes is
    silently truncated by `fread(descPath, 1, 48, file)` at
    `src/qcommon/files.c:3423`.
  - **Single line.** The buffer is reused as a path buffer and
    NUL-terminated at the byte returned by `fread`; embedded newlines
    will appear literally and corrupt the display. Keep `\n` only as
    the trailing line terminator (already correct in the current
    file).
  - **Avoid `^0`** (black) on the dark menu background -- invisible.

---

End of audit.
