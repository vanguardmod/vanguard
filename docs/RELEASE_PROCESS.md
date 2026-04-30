# Release process

How to ship a new VanguardMod build that the world can actually run.
For the user-visible changelog, see `docs/RELEASE_NOTES.md`.

## When to bump the version

  - **Patch (`0.1.X`):** any code change that ships to clients —
    qagame logic, cgame rendering, ui layout, included config
    presets. If clients connect to a dev/cup server and need the
    new binaries to behave correctly, that's a patch bump.
  - **Minor (`0.X.0`):** a coherent feature group, an architectural
    refactor with externally visible surface, or anything you would
    write release notes for that goes beyond a single bullet.
  - **Major (`X.0.0`):** never, until the mod hits 1.0. Below 1.0
    we treat the API as fluid and bump minor for breaking changes.

Pure documentation, build-script-only, or test-server-cfg-only
changes don't need a bump — there's nothing for clients to
re-download.

## Why every code change needs a fresh pk3

ETLegacy's pure-server enforcement only validates assets that come
from a hash-checked `.pk3`. Loose `.so`/`.dll` modules in the mod
directory are silently ignored on a pure server. So:

  - Updating `build/vanguard/qagame.mp.x86_64.so` on a Pterodactyl
    host **does nothing** for connected clients — they download the
    pk3 (or already have it cached) and run the cgame inside it.
  - Multi-arch matters: even if you only changed a Linux file, the
    pk3 must contain freshly compiled Windows `.dll`s too, or
    Windows clients will load whatever stale binary the previous
    pk3 had.
  - Therefore: **every shipped code change requires a full
    multi-platform rebuild + pk3 repack + version bump**.

## Release helper script

Use `scripts/release.sh` to automate the pre-flight portion:

    ./scripts/release.sh v0.X.Y[-rcN]

It validates the working state (correct branch, clean tree, tag
doesn't already exist locally or on `origin`, version format
matches `vMAJOR.MINOR.PATCH[-suffix]`), writes the new version
to `VANGUARD_VERSION`, prompts you to add a section to
`docs/RELEASE_NOTES.md` if missing, then prints the exact `git`
commands to commit, push, tag, and push the tag.

The script does **NOT** auto-execute any git commands — the
actual release decision stays a manual step. It's a pre-flight
checklist that catches the common typos and "wait did I push
main yet" mistakes.

`--dry-run` skips the file modification:

    ./scripts/release.sh --dry-run v0.X.Y

Useful for reviewing what the script would do before running it
for real (or for piping the printed git commands into an
external tool).

## Versioning is auto-derived from git tags

Up to v0.4.3 the version was maintained at six hardcoded spots
(`scripts/bootstrap.sh` ×4, `cmake/CMakeLists.scaffold.txt:4`,
`docs/DEV_MODE.md`, `docs/BUILDING.md`). Each release required
keeping all of them in sync, which was tedious and broke in
predictable ways (a stale doc reference, a manual rebuild that
forgot to set `CI_ETL_TAG`, …).

Since v0.4.4 the version flows from a single source of truth:
**`git describe --tags`**. To cut a release:

```bash
# 1. Push the version-bump commit to main (RELEASE_NOTES section,
#    optionally bump VANGUARD_VERSION to match).
git push origin main

# 2. Tag the commit and push the tag.
git tag v0.4.4
git push origin v0.4.4
```

The release workflow (`.github/workflows/release.yml`) builds, packages
and publishes the GitHub Release based on the tag name. No further
manual edits are required. See `docs/CI.md` for the workflow walkthrough.

### What still needs editing per release

  - **`docs/RELEASE_NOTES.md`** — prepend a `## vX.Y.Z — DATE — TITLE`
    section. The release workflow extracts this section verbatim as
    the GitHub Release body, so the format matters.
  - **`VANGUARD_VERSION`** (optional) — only matters for non-git
    builds (tarball downloads of the source). If you skip this, a
    tarball rebuild lands on whatever was committed last, which is
    almost always fine.

### What does NOT need editing per release

  - `scripts/bootstrap.sh` — auto-detects from git via cmake.
  - `cmake/CMakeLists.scaffold.txt` — VERSION line removed
    (was unused; the active root is upstream's CMakeLists.txt).
  - `docs/BUILDING.md`, `docs/DEV_MODE.md`, `docs/INSTALL_*.md` —
    use `vX.Y.Z` as a placeholder where they used to hardcode the
    current version.

### How auto-versioning resolves

`cmake/ETLVersion.cmake` consults sources in this order, taking the
first non-empty result:

  1. `CI_ETL_TAG` / `CI_ETL_DESCRIBE` env var or cmake cache var
     (override path — release.yml passes `${{ github.ref_name }}`,
     manual builders can pass `-DCI_ETL_TAG=vX.Y.Z`).
  2. `git describe --tags --abbrev=0` for the short tag and
     `git describe --tags --abbrev=7` for the full version-including-
     commits-since-tag string. Works with both annotated and
     lightweight tags. Empty for tarball / non-git checkouts.
  3. `VANGUARD_VERSION` file at the repo root. One line, format
     `vX.Y.Z`. Final fallback for non-git builds.
  4. `VERSION.txt` (upstream's, ETLegacy 2.83.x). If we get here
     it's a bug — the resulting pk3 will identify as ETLegacy
     rather than VanguardMod. Look for "VANGUARD_VERSION fallback"
     in the cmake log to confirm whether step 3 fired.

## Build sequence

Three platforms, in this exact order (the Linux build's `mod_pk3`
target globs the Windows DLLs at configure time). With v0.4.4-prep
auto-versioning, no `-DCI_ETL_TAG=...` flag is required when you've
tagged the commit you're building — cmake reads `git describe --tags`
itself. Pass the override only when you need to force a different
version (dev build off an untagged branch, or rebuilding a tarball
without git history).

```bash
# 1. Windows x86_64
rm -rf build-windows
cmake -B build-windows \
    -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-cross-mingw-x64-linux.cmake \
    -DCROSS_COMPILE32=OFF -DBUILD_MOD_PK3=OFF -DFEATURE_OMNIBOT=OFF \
    -DBUILD_CLIENT=OFF -DBUILD_SERVER=OFF -DBUILD_MOD=ON \
    -DBUNDLED_LIBS=OFF -DFEATURE_LUA=OFF \
    -DFEATURE_DBMS=OFF -DFEATURE_RATING=OFF -DFEATURE_PRESTIGE=OFF \
    -DINSTALL_EXTRA=OFF
cmake --build build-windows -j

# 2. Windows x86
rm -rf build-windows-32
cmake -B build-windows-32 \
    -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-cross-mingw-linux.cmake \
    -DCROSS_COMPILE32=ON -DBUILD_MOD_PK3=OFF -DFEATURE_OMNIBOT=OFF \
    -DBUILD_CLIENT=OFF -DBUILD_SERVER=OFF -DBUILD_MOD=ON \
    -DBUNDLED_LIBS=OFF -DFEATURE_LUA=OFF \
    -DFEATURE_DBMS=OFF -DFEATURE_RATING=OFF -DFEATURE_PRESTIGE=OFF \
    -DINSTALL_EXTRA=OFF
cmake --build build-windows-32 -j

# 3. Linux + multi-arch pk3
rm -rf build
cmake -B build \
    -DCROSS_COMPILE32=OFF -DBUILD_MOD_PK3=ON -DFEATURE_OMNIBOT=ON \
    -DBUILD_CLIENT=OFF -DBUILD_SERVER=OFF -DBUILD_MOD=ON \
    -DBUNDLED_LIBS=OFF -DFEATURE_LUA=OFF \
    -DFEATURE_DBMS=OFF -DFEATURE_RATING=OFF -DFEATURE_PRESTIGE=OFF \
    -DINSTALL_EXTRA=OFF
cmake --build build -j

# Override example (dev build, untagged branch, force a name):
#   cmake -B build -DCI_ETL_TAG=v0.4.4-dev -DCI_ETL_DESCRIBE=v0.4.4-dev ...
```

Note: `bootstrap.sh` runs this exact sequence (plus the Omni-Bot
fetch + deploy), but it refuses to run on a pre-imported tree.
Use it for fresh clones; use the explicit cmake sequence above for
re-builds on an established checkout.

### Why the `-D` form matters (v0.3.2 incident)

`cmake/ETLVersion.cmake` reads the version from either the
`CI_ETL_TAG` cmake cache variable or the same-named environment
variable. If neither is set, it falls through to `git describe`,
which fails on this repo (we don't tag) and ends up using the
upstream fallback `${VERSION_MAJOR}.${VERSION_MINOR}-dirty` =
`"2.83-dirty"` for whatever ETLegacy `VERSION.txt` currently
holds. That value gets baked into the binaries' `etlegacy_version[]`
global.

The v0.3.2 build initially shipped with this exact mistake on the
Windows DLLs — the developer ran the Linux configure with
`CI_ETL_TAG=v0.3.2 cmake ...` (env-prefix), but the Windows
configures in fresh shells didn't inherit the env. All eight
Windows DLLs ended up with `2.83-dirty` baked in while Linux had
`v0.3.2`. The fix landed in v0.3.2 itself: `bootstrap.sh` and this
doc now use `-D` consistently, and `cmake/ETLVersion.cmake`
accepts both forms.

## Mod version vs engine version in C code

Two version macros that look interchangeable but are not:

  - `ETL_BUILD_VERSION` — a literal-string `#define` from
    `cmake/version_generated.h.in`, configured by CMake at build
    time from `CI_ETL_TAG`. **Compile-time, inlined at every call
    site.** This is the right macro to display the mod's own
    version on screens / overlays — it always reads "v0.3.X"
    matching the cgame.so / qagame.so / ui.so we built, regardless
    of what engine the player is running.
  - `ETLEGACY_VERSION` — defined in `src/qcommon/version.h` as a
    pointer to the global `etlegacy_version[]` (defined in
    `src/qcommon/version.c`). Compiled into every binary that
    consumes that file (engine + each VM). When a VM is dlopen'd
    by the engine, ELF dynamic-symbol resolution unifies the
    global and the **main executable's copy wins**. So evaluating
    `ETLEGACY_VERSION` at runtime inside cgame returns the
    engine's version, not the cgame's. This is the right macro
    to display the engine version (diagnostic output: which
    ETLegacy build is the host running).

If you ever see a Vanguard screen rendering the engine's version
when you expected the mod's version, that's the symptom. Switch
the `printf`/`va` site from `ETLEGACY_VERSION` to
`ETL_BUILD_VERSION`. See the v0.3.2 release notes for the canonical
example fix in `cg_loadpanel.c:350`.

The hardcoded "Built on ETLegacy 2.83" prose strings (currently
just `cg_loadpanel.c`) need updating on each upstream resync if
the ETLegacy `VERSION.txt` major.minor changes. There is no
automatic propagation — ETLegacy's engine version is intentionally
not tied to our mod's `ETL_BUILD_VERSION`.

## PK3 verification

After the Linux build finishes:

```bash
ls -la build/vanguard/vanguard_v0.1.X.pk3       # ~22-23 MB
unzip -l build/vanguard/vanguard_v0.1.X.pk3 | grep -E '\.(so|dll)\b'
```

The manifest must list **12** module binaries — `cgame`, `qagame`,
`tvgame`, `ui` × 3 platforms (`.x86_64.so`, `_mp_x64.dll`,
`_mp_x86.dll`) — and each entry's date column must read **today**.
A stale date on any of the twelve means that platform's build did
not actually re-run; investigate before shipping.

## Sample bump procedure

```bash
# starting from a clean working tree, on main, post-merge of the
# code change to be released:

OLD=v0.1.0 NEW=v0.1.1
echo "Bumping from $OLD to $NEW"

# 1. Edit the six locations (see table above).
grep -rln "${OLD#v}" scripts/bootstrap.sh cmake/CMakeLists.scaffold.txt docs/

# 2. Full multi-platform rebuild (see "Build sequence" above).
#    Or re-run scripts/bootstrap.sh on a fresh clone.

# 3. Verify the pk3 contains today's binaries.
unzip -l build/vanguard/vanguard_${NEW}.pk3 | grep -E '\.(so|dll)\b'

# 4. Test on a real server (Pterodactyl or self-hosted) before
#    committing the bump. Reverting a wrong version after the pk3
#    is uploaded somewhere is more painful than catching it locally.

# 5. Commit: bump + RELEASE_NOTES.md entry + any feature commits
#    that ship in this release. Keep the bump as its own commit so
#    a `git revert` of the version is clean if the release is
#    pulled.
```

## Future improvement: centralise the version

The six-location bump is busywork and a foot-gun (forget one and
the docs lie). A clean fix:

  1. Add `VANGUARD_VERSION.txt` at repo root with one line:
     `0.1.X`.
  2. Source it in `scripts/bootstrap.sh` for the
     `VANGUARD_VERSION` default.
  3. Have CMake `configure_file` it into a header for runtime
     fallback (today the runtime gets the version from
     `CI_ETL_TAG`, which only flows through during a CI/bootstrap
     build).
  4. Generate the doc strings via cmake `configure_file` from
     `.md.in` templates, or live with the prose-references
     pointing at "current release" instead of a literal version.

This was deferred from the v0.1.1 release because it's a
schema-refactor and we needed to ship a deployment fix, not
restructure the build. Worth picking up before the next bump if
the six-spot edit causes any churn.


---

**Copyright Notice**

Copyright (c) 2026 wahke <info@wahke.lu> (https://wahke.lu)  
Copyright (c) 2026 VanguardMod Project Contributors

Licensed under GPL-3.0-or-later. Part of VanguardMod project.
Built on ETLegacy (https://www.etlegacy.com).
