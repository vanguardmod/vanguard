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
COMMON_CMAKE_FLAGS=(
    -DBUILD_CLIENT=OFF -DBUILD_SERVER=OFF
    -DBUILD_MOD=ON -DBUILD_MOD_PK3=OFF
    -DBUNDLED_LIBS=OFF
    -DFEATURE_LUA=OFF -DFEATURE_OMNIBOT=OFF
    -DFEATURE_DBMS=OFF -DFEATURE_RATING=OFF -DFEATURE_PRESTIGE=OFF
    -DINSTALL_EXTRA=OFF
)

if [ "$SKIP_BUILD" -ne 0 ]; then
    log "Skipping configure/build (--skip-build)"
    cat <<HINT

To build later:

    # Linux x86_64 (server-side native)
    cmake -B build -DCROSS_COMPILE32=OFF ${COMMON_CMAKE_FLAGS[*]}
    cmake --build build -j

    # Windows x86_64 (delivered to 64-bit clients)
    cmake -B build-windows \\
        -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-cross-mingw-x64-linux.cmake \\
        -DCROSS_COMPILE32=OFF ${COMMON_CMAKE_FLAGS[*]}
    cmake --build build-windows -j

    # Windows x86 (delivered to 32-bit clients)
    cmake -B build-windows-32 \\
        -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-cross-mingw-linux.cmake \\
        -DCROSS_COMPILE32=ON ${COMMON_CMAKE_FLAGS[*]}
    cmake --build build-windows-32 -j

HINT
    exit 0
fi

# -- Linux x86_64 -------------------------------------------------------------
log "Configuring Linux x86_64 (mod-only)"
rm -rf build
cmake -B build -DCROSS_COMPILE32=OFF "${COMMON_CMAKE_FLAGS[@]}"
log "Building Linux x86_64"
cmake --build build -j

# -- Windows x86_64 (cross) ---------------------------------------------------
if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    log "Configuring Windows x86_64 cross build"
    rm -rf build-windows
    cmake -B build-windows \
        -DCMAKE_TOOLCHAIN_FILE="${REPO_ROOT}/cmake/Toolchain-cross-mingw-x64-linux.cmake" \
        -DCROSS_COMPILE32=OFF \
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
        "${COMMON_CMAKE_FLAGS[@]}"
    log "Building Windows x86"
    cmake --build build-windows-32 -j
else
    log "WARNING: i686-w64-mingw32-gcc not found, skipping Windows x86 build"
fi

# -- Stage Windows DLLs alongside the Linux .so for the dedicated server -----
# build/vanguard/ is what the dedicated server reads at runtime — keep loose
# binaries there for direct local loading so a connecting Windows client can
# pick the matching architecture from the same mod folder.
SERVER_MOD_DIR="${REPO_ROOT}/build/vanguard"
if [ -d "${SERVER_MOD_DIR}" ]; then
    for variant in build-windows build-windows-32; do
        if compgen -G "${REPO_ROOT}/${variant}/vanguard/*.dll" > /dev/null; then
            log "Staging ${variant}/vanguard/*.dll into build/vanguard/"
            cp "${REPO_ROOT}/${variant}/vanguard/"*.dll "${SERVER_MOD_DIR}/"
        fi
    done
fi

# -----------------------------------------------------------------------------
# 9. Summary
# -----------------------------------------------------------------------------

echo
echo "=================================================================="
echo " VanguardMod bootstrap complete"
echo "=================================================================="
echo
echo " Build artefacts in build/vanguard/:"
ls -la "${SERVER_MOD_DIR}" 2>/dev/null | awk 'NR>1 && ($NF ~ /\.(so|dll)$/) {printf "   %-30s %s\n", $NF, $5}'
echo
echo " Upstream commit: ${UPSTREAM_COMMIT:0:12}"
echo " See UPSTREAM.txt for full import metadata."
echo
echo " Next: WolfGuard hook integration (see docs/WOLFGUARD.md)."
echo "=================================================================="
