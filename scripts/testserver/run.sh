#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 wahke <info@wahke.lu> (https://wahke.lu)
# SPDX-FileCopyrightText: 2026 VanguardMod Project Contributors
#
# This file is part of VanguardMod.
# Built on ETLegacy (https://www.etlegacy.com), licensed under GPL-3.0-or-later.
# Licensed under GPL-3.0-or-later. See LICENSE for details.
#
# VanguardMod local test server launcher.
#
# Brings up etlded with our mod loaded against an isolated fs_homepath so
# the real ~/.etlegacy is not touched. Logs go to scripts/testserver/server.log
# (overwritten each run; tail -F it from another shell).
#
# Stop with Ctrl+C (sends SIGINT to etlded, which shuts down cleanly).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

ETLDED="$REPO_ROOT/build-server/etlded.x86_64"
LOG="$SCRIPT_DIR/server.log"
HOMEPATH="$HOME/.etlegacy-vanguard-test"

if [ ! -x "$ETLDED" ]; then
    echo "error: $ETLDED not found or not executable" >&2
    echo "Build it first — see docs/TESTING.md." >&2
    exit 1
fi

if [ ! -e "$REPO_ROOT/etmain/pak0.pk3" ]; then
    echo "error: etmain/pak0.pk3 missing — original WET assets required for map load" >&2
    exit 1
fi

mkdir -p "$HOMEPATH/vanguard"

# Stage server.cfg into a path the engine actually searches (fs_homepath/fs_game/).
# Source of truth stays in scripts/testserver/server.cfg; this is just delivery.
cp "$SCRIPT_DIR/server.cfg" "$HOMEPATH/vanguard/server.cfg"

# Stage misc/description.txt into fs_homepath as a safety net so the
# engine's FS_GetModList (qcommon/files.c:3413) finds the brand string
# regardless of how fs_basepath resolves at run-time. The .pk3 build
# also stages it under build/<MODNAME>/, but fs_homepath wins the
# search order and this guarantees a hit during local tests. v0.5.2.3.
cp "$REPO_ROOT/misc/description.txt" "$HOMEPATH/vanguard/description.txt"

echo ">> VanguardMod test server"
echo "   binary       : $ETLDED"
echo "   fs_basepath  : $REPO_ROOT"
echo "   fs_game      : vanguard  (-> $REPO_ROOT/build/vanguard)"
echo "   fs_homepath  : $HOMEPATH"
echo "   log          : $LOG"
echo

exec "$ETLDED" \
    +set dedicated 1 \
    +set fs_basepath "$REPO_ROOT" \
    +set fs_homepath "$HOMEPATH" \
    +set fs_game vanguard \
    +exec server.cfg \
    +map oasis \
    >"$LOG" 2>&1
