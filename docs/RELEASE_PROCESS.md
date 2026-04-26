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

## Six locations to bump

For now, version lives in six spots. They must all match, otherwise
the runtime banner, the pk3 filename, and the docs disagree.

| File                                | Purpose                                             |
|-------------------------------------|-----------------------------------------------------|
| `scripts/bootstrap.sh:272`          | comment in `--skip-build` HINT (cosmetic)           |
| `scripts/bootstrap.sh:275`          | example invocation in HINT (cosmetic)               |
| `scripts/bootstrap.sh:288`          | comment over the export (cosmetic)                  |
| `scripts/bootstrap.sh:290`          | **`VANGUARD_VERSION` default — the build truth**    |
| `cmake/CMakeLists.scaffold.txt:4`   | scaffold project version (kept in sync)             |
| `docs/DEV_MODE.md:213`              | "vanguard_v0.1.X.pk3" prose reference               |
| `docs/BUILDING.md:69`               | "vanguard_v0.1.X.pk3" prose reference               |

(The line numbers drift; `grep -rn 'vanguard_v0\.' .` is the
authoritative finder.)

The runtime version string is computed from `CI_ETL_TAG` /
`CI_ETL_DESCRIBE` env vars by `cmake/ETLVersion.cmake`. Bootstrap
exports them from `VANGUARD_VERSION`, so the bootstrap.sh export is
the single source of truth at build time — the rest are
documentation.

## Build sequence

Three platforms, in this exact order (the Linux build's `mod_pk3`
target globs the Windows DLLs at configure time):

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

# 3. Linux + multi-arch pk3 (CI_ETL_TAG must match the bumped version)
rm -rf build
CI_ETL_TAG=v0.1.X CI_ETL_DESCRIBE=v0.1.X cmake -B build \
    -DCROSS_COMPILE32=OFF -DBUILD_MOD_PK3=ON -DFEATURE_OMNIBOT=ON \
    -DBUILD_CLIENT=OFF -DBUILD_SERVER=OFF -DBUILD_MOD=ON \
    -DBUNDLED_LIBS=OFF -DFEATURE_LUA=OFF \
    -DFEATURE_DBMS=OFF -DFEATURE_RATING=OFF -DFEATURE_PRESTIGE=OFF \
    -DINSTALL_EXTRA=OFF
CI_ETL_TAG=v0.1.X CI_ETL_DESCRIBE=v0.1.X cmake --build build -j
```

Note: `bootstrap.sh` runs this exact sequence (plus the Omni-Bot
fetch + deploy), but it refuses to run on a pre-imported tree.
Use it for fresh clones; use the explicit cmake sequence above for
re-builds on an established checkout.

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
