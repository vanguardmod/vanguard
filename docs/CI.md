# CI / CD

VanguardMod has two GitHub Actions workflows:

| File | Trigger | Purpose |
|------|---------|---------|
| `.github/workflows/ci.yml` | push to main, PRs targeting main | build-validation gate (Linux + Windows x64 only, no pk3) |
| `.github/workflows/release.yml` | push of any tag matching `v*` | full multi-platform build + GitHub Release with ZIP assets |

Both run on `ubuntu-22.04` runners. Windows builds are MinGW
cross-compiles — there is no native Windows runner in this project.
The two cross-toolchain files (`cmake/Toolchain-cross-mingw-x64-linux.cmake`,
`cmake/Toolchain-cross-mingw-linux.cmake`) come from upstream ETLegacy
and are vendored via `scripts/bootstrap.sh`.

## Releasing

Cutting a release is a two-step manual process:

```bash
# 1. Bump version in the six-spot files + RELEASE_NOTES (per
#    docs/RELEASE_PROCESS.md), commit, push to main.
git push origin main

# 2. Tag the version-bump commit and push the tag.
git tag v0.4.4
git push origin v0.4.4
```

The tag-push triggers `release.yml`. Within ~6–7 minutes a draft
release will appear at
`https://github.com/${REPO}/releases/tag/v0.4.4` with two assets:

- `vanguard-v0.4.4-server.zip` — server binaries (Linux x86_64 +
  Windows x64/x86) plus the multi-arch `vanguard_v0.4.4.pk3`.
- `vanguard-v0.4.4-client.zip` — client `.pk3` only.

The release **starts as a draft** (controlled by the `RELEASE_DRAFT`
env at the top of `release.yml`). Review the assets in the GitHub
UI, then publish manually. After a few releases of confidence, flip
the env var to `"false"` to publish straight away.

## Workflow architecture

```
        ┌────────────────────┐
        │ build-windows-x64  │
        │  (mingw x86_64)    │
        └──────┬─────────────┘
               │
               │  win-x64-dlls artifact
               ▼
        ┌────────────────────────────────┐
        │ build-linux-and-package        │
        │  • download win-x64-dlls       │
        │  • download win-x86-dlls       │
        │  • cache + fetch Omni-Bot      │
        │  • cmake -B build              │
        │      -DBUILD_MOD_PK3=ON        │
        │      -DFEATURE_OMNIBOT=ON      │
        │  • cmake --build build         │
        │      → produces vanguard_v*.pk3│
        │  • assemble server-zip         │
        │  • assemble client-zip         │
        │  • create GitHub Release       │
        └────────────────────────────────┘
               ▲
               │  win-x86-dlls artifact
               │
        ┌──────┴─────────────┐
        │ build-windows-x86  │
        │  (mingw i686)      │
        └────────────────────┘
```

The two Windows jobs run **in parallel**. The Linux job is gated
behind both via `needs:` and runs once they finish.

### Why Linux must come last

This is the **load-bearing** part of the architecture. Skip it when
debugging at your peril.

`cmake/ETLBuildMod.cmake`'s `mod_pk3` target — the one that produces
`vanguard_v*.pk3` — declares its dependency list using a
`FILE(GLOB)` over `build-windows*/vanguard/*.dll`. The glob is
evaluated **at cmake configure time**, so when the Linux build runs
`cmake -B build -DBUILD_MOD_PK3=ON ...`, the Windows DLLs must
already exist on disk in `build-windows/vanguard/` and
`build-windows-32/vanguard/` — otherwise the pk3 will be missing
the cross-built DLLs and Windows clients can't connect.

The release workflow handles this by:
1. Running both Windows builds first as separate jobs, with each
   uploading its DLLs as a `win-*-dlls` artifact.
2. Having the Linux job download both artifacts into the
   `build-windows*/vanguard/` directories before configuring cmake.
3. Configuring + building Linux + pk3 in that order.

Reordering the jobs (e.g. running Linux in parallel with the Windows
builds) silently produces a Linux-only pk3. No build-time error,
just a broken release.

### Why CI doesn't fetch Omni-Bot

CI's job is to validate that the C code compiles, not to produce a
deployable artifact. Skipping the Omni-Bot tarball fetch
(`FEATURE_OMNIBOT=OFF`) saves ~20 MB and several seconds of CI
time on every push. The release workflow, which does need to ship a
working pk3, fetches it (and caches the result with
`actions/cache@v4` keyed on the `bootstrap.sh` content hash —
changing the URL or extraction logic invalidates the cache).

## Pre-flight checklist (before pushing a tag)

- [ ] `docs/RELEASE_NOTES.md` has a `## v0.X.Y — DATE — TITLE`
      section for the version you're tagging. The release workflow
      pulls the section as the GitHub Release body. (Falls back to
      `git log` if the section is missing — works but reads worse.)
- [ ] Six-spot version is consistent: `cmake/CMakeLists.scaffold.txt`,
      `docs/BUILDING.md`, `docs/DEV_MODE.md`, `scripts/bootstrap.sh`
      (4 places in that file). `grep -rn "v0\.X\.Y" --include="*.sh"
      --include="*.md" --include="*.txt"` should show exactly the
      expected six matches.
- [ ] CI (`ci.yml`) has been green for the commit you're about to tag.
- [ ] You've actually committed and pushed the version-bump commit
      to main before tagging — `git push origin main` first, then
      `git tag` + `git push origin <tag>`.
- [ ] The tag name matches the `v*` pattern (`v0.4.4`, `v1.0.0-rc1`).
      Tags without the leading `v` will not trigger the workflow.

## Local validation (optional, before tagging)

The release workflow runs the same cmake commands you can run by
hand. To smoke-test the whole sequence locally before pushing a
tag:

```bash
./scripts/bootstrap.sh --skip-deps --skip-build  # idempotent on a re-run
# (or use the manual sequence in docs/RELEASE_PROCESS.md)

VFLAGS=(-DCI_ETL_TAG=v0.4.4 -DCI_ETL_DESCRIBE=v0.4.4)
COMMON_FLAGS=(-DBUILD_CLIENT=OFF -DBUILD_SERVER=OFF -DBUILD_MOD=ON
              -DBUNDLED_LIBS=OFF -DFEATURE_LUA=OFF
              -DFEATURE_DBMS=OFF -DFEATURE_RATING=OFF
              -DFEATURE_PRESTIGE=OFF -DINSTALL_EXTRA=OFF)

# 1. Win64
rm -rf build-windows
cmake -B build-windows \
    -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-cross-mingw-x64-linux.cmake \
    -DCROSS_COMPILE32=OFF -DBUILD_MOD_PK3=OFF -DFEATURE_OMNIBOT=OFF \
    "${VFLAGS[@]}" "${COMMON_FLAGS[@]}"
cmake --build build-windows -j

# 2. Win32
rm -rf build-windows-32
cmake -B build-windows-32 \
    -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-cross-mingw-linux.cmake \
    -DCROSS_COMPILE32=ON -DBUILD_MOD_PK3=OFF -DFEATURE_OMNIBOT=OFF \
    "${VFLAGS[@]}" "${COMMON_FLAGS[@]}"
cmake --build build-windows-32 -j

# 3. Linux + pk3
rm -rf build
cmake -B build \
    -DCROSS_COMPILE32=OFF -DBUILD_MOD_PK3=ON -DFEATURE_OMNIBOT=ON \
    "${VFLAGS[@]}" "${COMMON_FLAGS[@]}"
cmake --build build -j

ls -lh build/vanguard/vanguard_v0.4.4.pk3
```

You can also run the full GitHub Actions workflow locally via
[`act`](https://github.com/nektos/act), but it's a heavier setup
than just running the cmake commands — `act` pulls the runner
image, can hit subtle Linux/macOS differences, and takes more
disk than a couple of `build-*` directories. Use it if you're
debugging the workflow YAML itself (not the build).

## Common failures + fixes

### MinGW packages outdated or missing

**Symptom:** `mingw-w64` install step succeeds but `cmake -B
build-windows` fails to find `x86_64-w64-mingw32-gcc` or
`i686-w64-mingw32-gcc`.

**Fix:** the toolchain file expects the standard Debian MinGW
package layout. Make sure the install line in the workflow is
explicit about the metapackage:

```yaml
sudo apt-get install -y --no-install-recommends mingw-w64
```

`mingw-w64` (not `gcc-mingw-w64`) pulls in the `binutils-mingw-w64`
+ `g++-mingw-w64` + `gcc-mingw-w64` chain. If GitHub's runner image
ages out of those packages, pin to a tagged runner like
`ubuntu-22.04` (we already do) and update `apt-get` against
`security.ubuntu.com`.

### Omni-Bot mirror unreachable

**Symptom:** the `Fetch Omni-Bot runtime` step fails with a curl
404/timeout/SSL error.

**Quick fix:** rerun the workflow once — the mirror is occasionally
flaky for a few minutes at a time and the Omni-Bot cache means
subsequent runs after a successful one don't hit the mirror.

**Permanent fallback:** flip `FEATURE_OMNIBOT=ON` to `OFF` in the
release workflow's Linux configure step. The release pk3 will be
produced without the runtime; servers running with bots won't have
botting until the next release. Document the change in the release
notes so admins know to keep their previous omni-bot/ tree.

### Cache miss costs ~20 MB extra fetch

**Symptom:** the very first run after CI was added (or after
`scripts/bootstrap.sh` is edited) fetches the Omni-Bot tarball from
scratch.

**Fix:** that's expected. The cache key is
`omnibot-${{ hashFiles('scripts/bootstrap.sh') }}`, so a single
warm run populates the cache. Subsequent releases reuse it. To
warm the cache deliberately before a release, push any commit to
main that touches `scripts/bootstrap.sh` minimally (e.g. a comment),
let the (CI) workflow primes it, then unset.

Note: the CI workflow doesn't fetch Omni-Bot, so the cache only
warms on actual release runs.

### Tag already exists / wrong commit tagged

**Symptom:** `git push origin v0.4.4` fails with
`! [rejected]    v0.4.4 -> v0.4.4 (already exists)`.

**Fix (untagged on remote):**
```bash
git tag -d v0.4.4
git tag v0.4.4 <correct-commit-sha>
git push origin v0.4.4
```

**Fix (already on remote, release was a draft, no users yet):**
```bash
# Delete remote tag + local tag, retag, push
git push --delete origin v0.4.4
git tag -d v0.4.4
git tag v0.4.4 <correct-commit-sha>
git push origin v0.4.4
# Optionally delete the prior draft release in the GitHub UI.
```

**Fix (released and people may have downloaded):** don't retag.
Cut a fresh patch version (e.g. v0.4.5) and amend
RELEASE_NOTES.md with a "supersedes v0.4.4 — corrected build"
note.

### Release shows up empty / no assets

**Symptom:** the workflow finishes green but the GitHub Release
page is missing one or both ZIP files.

**Fix:** check the `Create GitHub Release` step's logs — the
`fail_on_unmatched_files: true` setting in `release.yml` should
turn this into a step failure. If the step succeeded but a file is
missing, look at the preceding `Assemble server ZIP` /
`Assemble client ZIP` steps — most likely a `cp` failed silently
because of a typo in the version-suffixed pk3 name or because a
build artifact was never produced.

### CI keeps timing out at apt-get install

**Symptom:** the apt-get step takes >2 minutes consistently.

**Fix:** GitHub's mirror occasionally throttles. There's not much
to do about it from inside the workflow other than retry. If it's
chronic, add `actions/cache` for `/var/cache/apt/archives` keyed on
the runner image version.

## Switching from draft to published releases

After enough successful runs that you trust the workflow:

```diff
 # Single env-level toggle: set to "false" once the workflow is
 # trusted to publish releases without manual review.
 env:
-  RELEASE_DRAFT: "true"
+  RELEASE_DRAFT: "false"
```

That's a one-line edit in `.github/workflows/release.yml`. No code
change downstream — `softprops/action-gh-release` reads the boolean
and either creates a draft or a published release accordingly.

## What's NOT in CI yet

The following are deliberate omissions from this first iteration —
add them as separate tickets when needed:

- **Native Windows builds.** All Windows artefacts come from MinGW
  cross-compile. Native MSVC builds would need a `windows-2022`
  runner and a separate cmake configure path.
- **macOS builds.** `BUILD_MOD=ON` should work on macOS x86_64 /
  arm64, but the toolchain isn't set up.
- **WolfGuard private-build CI.** The closed-source WolfGuard
  implementation lives in `wolfguard/private/` (gitignored, only
  on trusted build hosts). A separate, secret-protected workflow
  would be needed to build the protected variant.
- **Cgame static analysis.** No `clang-tidy` / `cppcheck` /
  `scan-build` step. Adding one is cheap and catches a class of
  bugs the current CI silently accepts.
- **Code coverage.** No coverage instrumentation — the codebase is
  primarily integration-tested via live play, not unit-tested.
- **Pterodactyl auto-deploy.** Test-server deployment is manual
  via the Pterodactyl panel. A workflow that SCPs the produced
  artefacts onto a staging server would shorten the live-test
  loop, but needs SSH-key secrets.

---

**Copyright Notice**

Copyright (c) 2026 wahke <info@wahke.lu> (https://wahke.lu)  
Copyright (c) 2026 VanguardMod Project Contributors

Licensed under GPL-3.0-or-later. Part of VanguardMod project.
Built on ETLegacy (https://www.etlegacy.com).
