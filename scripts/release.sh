#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 wahke <info@wahke.lu> (https://wahke.lu)
# SPDX-FileCopyrightText: 2026 VanguardMod Project Contributors
#
# This file is part of VanguardMod.
# Built on ETLegacy (https://www.etlegacy.com), licensed under GPL-3.0-or-later.
# Licensed under GPL-3.0-or-later. See LICENSE for details.
#
# release.sh — release-prep helper, NO auto-execution of git commands.
#
# Usage:
#   ./scripts/release.sh v0.X.Y[-rcN]
#   ./scripts/release.sh --dry-run v0.X.Y
#   ./scripts/release.sh --help
#
# What it does:
#   1. Pre-flight validation (branch / repo / clean tree / tag-doesn't-exist).
#   2. Writes the version to VANGUARD_VERSION.
#   3. Reminds you to add a section to docs/RELEASE_NOTES.md.
#   4. Prints the exact git commands you then run by hand.
#
# What it does NOT do:
#   * Run `git commit`.
#   * Run `git push`.
#   * Run `git tag`.
#   * Push the tag.
# Those steps stay manual — the actual release decision is yours.

set -euo pipefail

# ---------------------------------------------------------------------------
# CLI parsing
# ---------------------------------------------------------------------------

DRY_RUN=0
VERSION=""

show_help() {
    sed -n '12,28p' "$0" | sed 's/^# \{0,1\}//'
}

for arg in "$@"; do
    case "$arg" in
        -h|--help)
            show_help
            exit 0
            ;;
        --dry-run)
            DRY_RUN=1
            ;;
        v*)
            if [ -n "$VERSION" ]; then
                echo "error: multiple versions given (\"$VERSION\" and \"$arg\")" >&2
                exit 1
            fi
            VERSION="$arg"
            ;;
        *)
            echo "error: unknown argument: $arg" >&2
            echo "use --help for usage." >&2
            exit 1
            ;;
    esac
done

if [ -z "$VERSION" ]; then
    echo "error: no version given." >&2
    echo "usage: ./scripts/release.sh v0.X.Y[-rcN] [--dry-run]" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Output formatting
# ---------------------------------------------------------------------------

if [ -t 1 ]; then
    GREEN=$'\033[0;32m'
    RED=$'\033[0;31m'
    YELLOW=$'\033[0;33m'
    BLUE=$'\033[0;34m'
    BOLD=$'\033[1m'
    NC=$'\033[0m'
else
    GREEN=""; RED=""; YELLOW=""; BLUE=""; BOLD=""; NC=""
fi

ok()    { printf '%s   %s%s\n' "${GREEN}✅" "$1" "${NC}"; }
warn()  { printf '%s   %s%s\n' "${YELLOW}⚠️ " "$1" "${NC}"; }
fail()  { printf '%s   %s%s\n' "${RED}❌" "$1" "${NC}"; }
note()  { printf '%s%s%s\n' "${BLUE}" "$1" "${NC}"; }
hdr()   { printf '\n%s%s%s\n' "${BOLD}" "$1" "${NC}"; }

# ---------------------------------------------------------------------------
# Locate repo root + sanity check we're inside the VanguardMod tree
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

if [ ! -f "VANGUARD_VERSION" ] || [ ! -f "scripts/bootstrap.sh" ]; then
    fail "not inside the VanguardMod repo root (no VANGUARD_VERSION / scripts/bootstrap.sh)."
    exit 1
fi

# ---------------------------------------------------------------------------
# 1. Pre-flight checks
# ---------------------------------------------------------------------------

hdr "🔍 Pre-flight checks for ${VERSION}"

# Version format: vMAJOR.MINOR.PATCH or vMAJOR.MINOR.PATCH-rcN / -dev / etc.
if ! [[ "${VERSION}" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(\.[0-9]+)?(-[0-9a-zA-Z.-]+)?$ ]]; then
    fail "version \"${VERSION}\" doesn't match expected vMAJOR.MINOR.PATCH[-suffix]"
    exit 1
fi
ok "Version format: ${VERSION}"

# Branch
CURRENT_BRANCH="$(git symbolic-ref --short HEAD 2>/dev/null || echo "(detached)")"
if [ "${CURRENT_BRANCH}" != "main" ]; then
    warn "On branch \"${CURRENT_BRANCH}\", not main."
    warn "Releases normally come off main. Continue anyway? [y/N]"
    read -r REPLY
    if [ "${REPLY}" != "y" ] && [ "${REPLY}" != "Y" ]; then
        fail "Aborted by user."
        exit 1
    fi
else
    ok "On branch main"
fi

# Repo name from origin
REPO_URL="$(git config --get remote.origin.url 2>/dev/null || echo "")"
if [ -n "${REPO_URL}" ]; then
    # Pull "owner/name" out of either git@github.com:owner/name(.git) or
    # https://github.com/owner/name(.git)
    REPO_NAME="$(echo "${REPO_URL}" | sed -E 's#^.*[:/]([^/:]+/[^/]+)$#\1#; s#\.git$##')"
    ok "Repo: ${REPO_NAME}"
else
    warn "No remote origin URL — manual release will need explicit GitHub URL."
    REPO_NAME="<unknown>"
fi

# Working tree
if ! git diff --quiet HEAD 2>/dev/null || ! git diff --cached --quiet 2>/dev/null; then
    warn "Working tree has uncommitted changes:"
    git status --short | sed 's/^/      /'
    warn "Continue anyway? [y/N]"
    read -r REPLY
    if [ "${REPLY}" != "y" ] && [ "${REPLY}" != "Y" ]; then
        fail "Aborted by user — commit or stash first."
        exit 1
    fi
else
    ok "Working tree clean"
fi

# Tag exists locally?
if git rev-parse "${VERSION}" >/dev/null 2>&1; then
    fail "Tag ${VERSION} already exists locally. Use \`git tag -d ${VERSION}\` first."
    exit 1
fi
ok "Tag ${VERSION} does not exist locally"

# Tag exists remotely?
if git ls-remote --exit-code --tags origin "refs/tags/${VERSION}" >/dev/null 2>&1; then
    fail "Tag ${VERSION} already exists on origin. Cannot retag a published release."
    fail "(If the release was a draft and unused, delete remote tag first:"
    fail " \`git push --delete origin ${VERSION}\`)"
    exit 1
fi
ok "Tag ${VERSION} does not exist on origin"

# ---------------------------------------------------------------------------
# 2. VANGUARD_VERSION update
# ---------------------------------------------------------------------------

hdr "📝 Updating VANGUARD_VERSION → ${VERSION}"

CURRENT_VERSION="$(cat VANGUARD_VERSION 2>/dev/null | head -1 || echo "(unset)")"
note "Current: ${CURRENT_VERSION}"
note "    New: ${VERSION}"

if [ "${DRY_RUN}" -eq 1 ]; then
    warn "[--dry-run] would write \"${VERSION}\" to VANGUARD_VERSION (skipped)"
else
    if [ "${CURRENT_VERSION}" = "${VERSION}" ]; then
        ok "VANGUARD_VERSION already at ${VERSION} — no change"
    else
        echo "${VERSION}" > VANGUARD_VERSION
        ok "VANGUARD_VERSION written"
    fi
fi

# ---------------------------------------------------------------------------
# 3. RELEASE_NOTES.md reminder
# ---------------------------------------------------------------------------

hdr "✏️  RELEASE_NOTES.md section for ${VERSION}"

if grep -qE "^## ${VERSION}( |$)" docs/RELEASE_NOTES.md 2>/dev/null; then
    ok "Section already exists in docs/RELEASE_NOTES.md"
else
    warn "No section for ${VERSION} found in docs/RELEASE_NOTES.md."
    note "Add a header at the top of the file in this format:"
    note ""
    note "    ## ${VERSION} — $(date -u +%Y-%m-%d) — TITLE"
    note ""
    note "    body content..."
    note ""
    if [ "${DRY_RUN}" -eq 1 ]; then
        warn "[--dry-run] skipping editor prompt"
    else
        note "Open docs/RELEASE_NOTES.md in your editor now, then press ENTER"
        note "to continue (or Ctrl-C to abort)."
        read -r _
        if grep -qE "^## ${VERSION}( |$)" docs/RELEASE_NOTES.md 2>/dev/null; then
            ok "Section now present"
        else
            warn "Still no section for ${VERSION}. The release-notes auto-extract"
            warn "in .github/workflows/release.yml will fall back to a git-log"
            warn "summary. Recommended to add the section before tagging."
        fi
    fi
fi

# ---------------------------------------------------------------------------
# 4. Commit + push reminder (NOT executed)
# ---------------------------------------------------------------------------

hdr "🚀 Ready to commit + push. Run these commands by hand:"

cat <<EOF

   git add VANGUARD_VERSION docs/RELEASE_NOTES.md
   git commit -m "docs: prepare ${VERSION} release notes"
   git push origin ${CURRENT_BRANCH}

EOF

# ---------------------------------------------------------------------------
# 5. Tag-push reminder (NOT executed)
# ---------------------------------------------------------------------------

hdr "⏸️  WAIT FOR CI GREEN before tagging!"

if [ "${REPO_NAME}" != "<unknown>" ]; then
    note "  Check: https://github.com/${REPO_NAME}/actions"
fi

hdr "🏷️  Then tag + push (annotated tag recommended):"

cat <<EOF

   git tag -a ${VERSION} -m "VanguardMod ${VERSION}"
   git push origin ${VERSION}

EOF

hdr "📦 The release workflow will run for ~6-7 minutes, then create a"
hdr "    DRAFT release on:"
if [ "${REPO_NAME}" != "<unknown>" ]; then
    note "  https://github.com/${REPO_NAME}/releases/tag/${VERSION}"
else
    note "  the GitHub Releases page for this repo"
fi

note ""
note "Review the assets, then hit Publish in the GitHub UI."
note ""

if [ "${DRY_RUN}" -eq 1 ]; then
    warn "[--dry-run] no files were modified."
fi

ok "Done."
