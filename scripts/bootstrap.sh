#!/usr/bin/env bash
# bootstrap.sh — Empty VanguardMod scaffold to a first green build, in one shot.
#
# This is the consolidated end-to-end bootstrap. Everything we learned through
# iterative debugging is baked in here, so a fresh clone reaches a successful
# build with one command.
#
# Usage:
#   ./scripts/bootstrap.sh                            # full run: deps + clone + build
#   ./scripts/bootstrap.sh --skip-deps                # skip apt install
#   ./scripts/bootstrap.sh --skip-build               # configure but do not compile
#   ./scripts/bootstrap.sh --skip-deps --skip-build   # just import sources
#
# Environment overrides:
#   VANGUARD_UPSTREAM_URL — git URL (default: https://github.com/etlegacy/etlegacy.git)
#   VANGUARD_UPSTREAM_REF — git ref/branch (default: master)

set -euo pipefail

# -----------------------------------------------------------------------------
# Argument parsing
# -----------------------------------------------------------------------------

SKIP_DEPS=0
SKIP_BUILD=0
for arg in "$@"; do
    case "$arg" in
        --skip-deps)  SKIP_DEPS=1 ;;
        --skip-build) SKIP_BUILD=1 ;;
        -h|--help)
            sed -n '2,/^$/p' "$0" | sed 's/^# \?//'
            exit 0
            ;;
        *) echo "unknown arg: $arg" >&2; exit 1 ;;
    esac
done

# -----------------------------------------------------------------------------
# Setup
# -----------------------------------------------------------------------------

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

UPSTREAM_URL="${VANGUARD_UPSTREAM_URL:-https://github.com/etlegacy/etlegacy.git}"
UPSTREAM_REF="${VANGUARD_UPSTREAM_REF:-master}"
STAGE_DIR="_etlegacy-import"

log() { echo ">> $*"; }
err() { echo "error: $*" >&2; exit 1; }

# -----------------------------------------------------------------------------
# 1. Sanity checks
# -----------------------------------------------------------------------------

command -v git   >/dev/null || err "git not found in PATH"
command -v cmake >/dev/null || err "cmake not found in PATH"

if [ -d "src/cgame" ]; then
    err "src/cgame already exists — bootstrap appears to have run before. Remove src/ to re-import."
fi

# -----------------------------------------------------------------------------
# 2. Build dependencies
# -----------------------------------------------------------------------------

if [ "$SKIP_DEPS" -eq 0 ]; then
    log "Installing build dependencies via apt (will prompt for sudo)"
    sudo apt update -qq
    # cJSON is vendored under vendor/cjson/ and built directly into each mod,
    # so libcjson-dev is intentionally not installed (this also matters for
    # MinGW cross builds that have no system libcjson available).
    # mingw-w64 is required for the Windows x64 + x86 cross builds that the
    # server delivers to Windows clients on connect. Both compiler triples
    # (i686-w64-mingw32, x86_64-w64-mingw32) come from the mingw-w64 metapkg.
    sudo apt install -y \
        build-essential cmake git pkg-config \
        libsqlite3-dev \
        mingw-w64
else
    log "Skipping apt install (--skip-deps)"
fi

# -----------------------------------------------------------------------------
# 3. Clone upstream
# -----------------------------------------------------------------------------

log "Cloning ${UPSTREAM_URL} (ref: ${UPSTREAM_REF})"
rm -rf "${STAGE_DIR}"
git clone --depth 1 --branch "${UPSTREAM_REF}" "${UPSTREAM_URL}" "${STAGE_DIR}"

UPSTREAM_COMMIT="$(cd "${STAGE_DIR}" && git rev-parse HEAD)"

# -----------------------------------------------------------------------------
# 4. Overlay required files into our project
# -----------------------------------------------------------------------------

log "Importing source trees from upstream"
mkdir -p src cmake docs
cp -r "${STAGE_DIR}/src/."   src/
cp -r "${STAGE_DIR}/cmake/." cmake/
cp -r "${STAGE_DIR}/etmain"  ./
cp -r "${STAGE_DIR}/vendor"  ./
cp -r "${STAGE_DIR}/misc"    ./

log "Importing root files referenced by upstream CMake"
# Preserve our scaffold CMakeLists if not yet backed up
if [ -f "CMakeLists.txt" ] && [ ! -f "CMakeLists.scaffold.txt.bak" ]; then
    mv CMakeLists.txt CMakeLists.scaffold.txt.bak
fi
cp    "${STAGE_DIR}/CMakeLists.txt"   ./
cp    "${STAGE_DIR}/COPYING.txt"      ./
cp    "${STAGE_DIR}/docs/INSTALL.txt" docs/
cp    "${STAGE_DIR}/VERSION.txt"      ./   2>/dev/null || true
cp    "${STAGE_DIR}/CHANGELOG.yml"    ./   2>/dev/null || true

# -----------------------------------------------------------------------------
# 5. Patch out broken-upstream files
# -----------------------------------------------------------------------------

log "Disabling g_xp_saver.c (upstream-flagged as needing rework) and adding stubs"
mv src/game/g_xp_saver.c src/game/g_xp_saver.c.disabled

cat > src/game/g_xp_saver_stub.c << 'STUB_EOF'
/*
 * g_xp_saver_stub.c — VanguardMod no-op stubs for XP-Saver functions.
 *
 * The upstream implementation (g_xp_saver.c.disabled) depends on a
 * SDK-internal sqlite layer (level.database.*) that is not exposed by
 * the public ETLegacy API and is upstream-flagged as needing rework.
 * These stubs let qagame link cleanly with the call sites in
 * g_main.c, g_client.c and g_svcmds.c untouched.
 *
 * When VanguardMod's own persistence layer lands (likely via the
 * vanguardmod.com backend rather than local sqlite), either restore
 * the upstream file or replace these stubs with real implementations.
 */

#include "g_local.h"

void G_XPSaver_Load(gclient_t *cl)  { (void)cl; }
void G_XPSaver_Store(gclient_t *cl) { (void)cl; }
int  G_XPSaver_Clear(void)          { return 0; }
void G_XPSaver_Convert(void)        { }
STUB_EOF

# -----------------------------------------------------------------------------
# 6. Cleanup staging
# -----------------------------------------------------------------------------

log "Cleaning up staging dir"
rm -rf "${STAGE_DIR}"

# -----------------------------------------------------------------------------
# 7. Record upstream import metadata
# -----------------------------------------------------------------------------

cat > UPSTREAM.txt << EOF
VanguardMod upstream import
===========================

Source:    ${UPSTREAM_URL}
Ref:       ${UPSTREAM_REF}
Commit:    ${UPSTREAM_COMMIT}
Imported:  $(date -u +%Y-%m-%dT%H:%M:%SZ)
Bootstrap: $(basename "$0") $*

Modifications applied to upstream:
  - src/game/g_xp_saver.c     -> .disabled (replaced by g_xp_saver_stub.c)

The original VanguardMod scaffold CMakeLists.txt is in
CMakeLists.scaffold.txt.bak in case you need to reference our
intended top-level wiring (WolfGuard switch etc.) when migrating
to a layered build later on.
EOF

# -----------------------------------------------------------------------------
# 8. Configure & build
# -----------------------------------------------------------------------------

# Common CMake flags shared between Linux and Windows cross builds.
# BUILD_MOD_PK3 is intentionally NOT set here — each build picks it on/off
# explicitly (only the Linux build emits the redistributable .pk3).
# FEATURE_OMNIBOT is also picked per-build: ON for the Linux mod (test
# server), OFF for the Windows cross builds (we don't bot-test on
# Windows).
COMMON_CMAKE_FLAGS=(
    -DBUILD_CLIENT=OFF -DBUILD_SERVER=OFF
    -DBUILD_MOD=ON
    -DBUNDLED_LIBS=OFF
    -DFEATURE_LUA=OFF
    -DFEATURE_DBMS=OFF -DFEATURE_RATING=OFF -DFEATURE_PRESTIGE=OFF
    -DINSTALL_EXTRA=OFF
)

# -----------------------------------------------------------------------------
# Omni-Bot runtime cache (Linux only)
# -----------------------------------------------------------------------------
#
# qagame, when built with FEATURE_OMNIBOT=ON, dlopen()s an omnibot_et.so
# from <fs_game>/omni-bot/ at map load. That runtime is a ~20MB tarball
# distributed by the ETLegacy team — not in any apt repo, not in vendor/
# (only the headers are). We cache it under vendor/omnibot-runtime/ so
# `rm -rf build` doesn't trigger a re-download, and copy a fresh tree
# into build/vanguard/omni-bot/ after the Linux mod build finishes.

OMNIBOT_CACHE_DIR="${REPO_ROOT}/vendor/omnibot-runtime"
OMNIBOT_TARBALL_URL="https://mirror.etlegacy.com/omnibot/omnibot-linux-latest.tar.gz"
OMNIBOT_TARBALL_PATH="${OMNIBOT_CACHE_DIR}/omnibot-linux-latest.tar.gz"
OMNIBOT_EXTRACT_DIR="${OMNIBOT_CACHE_DIR}/extracted"

fetch_omnibot_runtime() {
    mkdir -p "${OMNIBOT_CACHE_DIR}"

    if [ -d "${OMNIBOT_EXTRACT_DIR}/omni-bot" ]; then
        log "Omni-Bot runtime cache hit (${OMNIBOT_EXTRACT_DIR}/omni-bot)"
        return 0
    fi

    if [ ! -f "${OMNIBOT_TARBALL_PATH}" ]; then
        log "Downloading Omni-Bot runtime from ${OMNIBOT_TARBALL_URL}"
        if ! curl --fail --location --output "${OMNIBOT_TARBALL_PATH}" "${OMNIBOT_TARBALL_URL}"; then
            rm -f "${OMNIBOT_TARBALL_PATH}"
            err "Omni-Bot download failed (mirror unreachable?). Manual fallback:
    1. Obtain omnibot-linux-latest.tar.gz from any ETLegacy Linux mirror
    2. Place it at: ${OMNIBOT_TARBALL_PATH}
    3. Re-run scripts/bootstrap.sh"
        fi
    else
        log "Using cached Omni-Bot tarball at ${OMNIBOT_TARBALL_PATH}"
    fi

    log "Extracting Omni-Bot runtime to ${OMNIBOT_EXTRACT_DIR}"
    rm -rf "${OMNIBOT_EXTRACT_DIR}"
    mkdir -p "${OMNIBOT_EXTRACT_DIR}"
    tar -xzf "${OMNIBOT_TARBALL_PATH}" -C "${OMNIBOT_EXTRACT_DIR}"

    if [ ! -d "${OMNIBOT_EXTRACT_DIR}/omni-bot" ]; then
        err "Omni-Bot tarball did not contain expected 'omni-bot/' top-level directory"
    fi
}

deploy_omnibot_runtime() {
    local target="${REPO_ROOT}/build/vanguard/omni-bot"
    if [ ! -d "${OMNIBOT_EXTRACT_DIR}/omni-bot" ]; then
        err "Omni-Bot runtime not staged in cache; fetch_omnibot_runtime must run first"
    fi
    log "Deploying Omni-Bot runtime into ${target}"
    rm -rf "${target}"
    cp -r "${OMNIBOT_EXTRACT_DIR}/omni-bot" "${target}"
}

if [ "$SKIP_BUILD" -ne 0 ]; then
    log "Skipping configure/build (--skip-build)"
    cat <<HINT

To build later:

    # Windows x86_64 (delivered to 64-bit clients) — must run before Linux
    # so the multi-arch pk3 picks up the cross-built DLLs at configure time.
    cmake -B build-windows \\
        -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-cross-mingw-x64-linux.cmake \\
        -DCROSS_COMPILE32=OFF -DBUILD_MOD_PK3=OFF -DFEATURE_OMNIBOT=OFF ${COMMON_CMAKE_FLAGS[*]}
    cmake --build build-windows -j

    # Windows x86 (delivered to 32-bit clients)
    cmake -B build-windows-32 \\
        -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-cross-mingw-linux.cmake \\
        -DCROSS_COMPILE32=ON -DBUILD_MOD_PK3=OFF -DFEATURE_OMNIBOT=OFF ${COMMON_CMAKE_FLAGS[*]}
    cmake --build build-windows-32 -j

    # Linux x86_64 + the multi-arch vanguard_v0.3.1.pk3 the server hands out.
    # FEATURE_OMNIBOT=ON requires the runtime tarball to be present in
    # vendor/omnibot-runtime/extracted/omni-bot/ — the bootstrap fetches it.
    CI_ETL_TAG=v0.3.1 CI_ETL_DESCRIBE=v0.3.1 cmake -B build \\
        -DCROSS_COMPILE32=OFF -DBUILD_MOD_PK3=ON -DFEATURE_OMNIBOT=ON ${COMMON_CMAKE_FLAGS[*]}
    cmake --build build -j

HINT
    exit 0
fi

# Pre-fetch the Omni-Bot runtime now so a network failure aborts before
# we burn ~minutes on Windows + Linux compiles.
fetch_omnibot_runtime

# Vanguard release version. Injected into upstream's git-describe-driven
# ETLVersion.cmake so the resulting pk3 is named vanguard_v0.3.1.pk3 instead
# of falling back to the imported ETLEGACY_VERSION (2.83.x).
# See docs/RELEASE_PROCESS.md for the full bump checklist.
export CI_ETL_TAG="${VANGUARD_VERSION:-v0.3.1}"
export CI_ETL_DESCRIBE="${CI_ETL_TAG}"

# Order matters: Windows cross builds run *before* the Linux configure so the
# Linux build's BUILD_MOD_PK3=ON target can pick up the cross-built DLLs and
# bundle them into a single multi-arch .pk3 (see cmake/ETLBuildMod.cmake).

# -- Windows x86_64 (cross) ---------------------------------------------------
# FEATURE_OMNIBOT=OFF on Windows: we don't bot-test on Windows clients,
# and the runtime tarball we fetch is Linux-only.
if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    log "Configuring Windows x86_64 cross build"
    rm -rf build-windows
    cmake -B build-windows \
        -DCMAKE_TOOLCHAIN_FILE="${REPO_ROOT}/cmake/Toolchain-cross-mingw-x64-linux.cmake" \
        -DCROSS_COMPILE32=OFF \
        -DBUILD_MOD_PK3=OFF \
        -DFEATURE_OMNIBOT=OFF \
        "${COMMON_CMAKE_FLAGS[@]}"
    log "Building Windows x86_64"
    cmake --build build-windows -j
else
    log "WARNING: x86_64-w64-mingw32-gcc not found, skipping Windows x64 build"
fi

# -- Windows x86 (cross) ------------------------------------------------------
if command -v i686-w64-mingw32-gcc >/dev/null 2>&1; then
    log "Configuring Windows x86 cross build"
    rm -rf build-windows-32
    cmake -B build-windows-32 \
        -DCMAKE_TOOLCHAIN_FILE="${REPO_ROOT}/cmake/Toolchain-cross-mingw-linux.cmake" \
        -DCROSS_COMPILE32=ON \
        -DBUILD_MOD_PK3=OFF \
        -DFEATURE_OMNIBOT=OFF \
        "${COMMON_CMAKE_FLAGS[@]}"
    log "Building Windows x86"
    cmake --build build-windows-32 -j
else
    log "WARNING: i686-w64-mingw32-gcc not found, skipping Windows x86 build"
fi

# -- Linux x86_64 + multi-arch pk3 --------------------------------------------
# BUILD_MOD_PK3=ON triggers upstream's mod_pk3 target (cmake/ETLBuildMod.cmake)
# which we patched to also bundle qagame/tvgame and the Windows DLLs found in
# build-windows{,-32}/${MODNAME}/. Configure must happen *after* the Windows
# builds because the .dll list is captured by file(GLOB) at configure time.
# FEATURE_OMNIBOT=ON enables the qagame <-> Omni-Bot interface; the actual
# omnibot_et.so is dropped in by deploy_omnibot_runtime below, not by CMake.
log "Configuring Linux x86_64 (mod + multi-arch pk3 + Omni-Bot)"
rm -rf build
cmake -B build \
    -DCROSS_COMPILE32=OFF \
    -DBUILD_MOD_PK3=ON \
    -DFEATURE_OMNIBOT=ON \
    "${COMMON_CMAKE_FLAGS[@]}"
log "Building Linux x86_64 + mod_pk3"
cmake --build build -j

deploy_omnibot_runtime

SERVER_MOD_DIR="${REPO_ROOT}/build/vanguard"

# -----------------------------------------------------------------------------
# 9. Summary
# -----------------------------------------------------------------------------

echo
echo "=================================================================="
echo " VanguardMod bootstrap complete"
echo "=================================================================="
echo
echo " Build artefacts in build/vanguard/:"
ls -la "${SERVER_MOD_DIR}" 2>/dev/null | awk 'NR>1 && ($NF ~ /\.(so|dll|pk3)$/) {printf "   %-30s %s\n", $NF, $5}'
echo
echo " Upstream commit: ${UPSTREAM_COMMIT:0:12}"
echo " See UPSTREAM.txt for full import metadata."
echo
echo " Next: WolfGuard hook integration (see docs/WOLFGUARD.md)."
echo "=================================================================="
