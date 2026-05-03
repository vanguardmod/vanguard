# Phase Copyright-Sweep — Recon

> Pure recon. No code changes. wahke decides Phase B scope after this.

Date: 2026-05-03
Author: Claude Code (Opus 4.7) on session for wahke
Status: AUDIT (no implementation)

## TL;DR

VanguardMod has touched **423** files in `src/{game,cgame,ui,qcommon,
botlib,renderer*,server,sys,client}/` since project start, but only
**16** carry real post-import VanguardMod modifications. All 16 still
ship with the original ETLegacy dual-copyright header (id Software +
ET:Legacy team) and need the v0.6.x **Triple-Header** retrofit per
Memory #10.

Recommended Phase B scope: **11 clear-cut files** (≥10 lines of post-
import VanguardMod insertions) + a wahke decision on **5 borderline
files** (1–9 lines). The spec mentions `bg_pmove.c` as a confirmed
target but git data shows it has **zero post-import modifications** —
defer to v0.7.0 (falldamage redesign per Phase 7.3 audit will be the
first commit to actually touch it).

Plus a separate Dual-Header (ETL + Vanguard, no id Software) decision
for `g_mdx.c` / `g_mdx.h` — ETLegacy-introduced files we modified
heavily (100 / 41 inserts) that have no id Software origin.

| Tier | Count | Action |
|---|---|---|
| Triple-Header (id + ETL + VG) **clear-cut** | 11 | Phase B retrofit |
| Triple-Header **borderline** (1–9 inserts) | 5 | wahke decides |
| Dual-Header (ETL + VG only) candidates | 2 | wahke decides — not in original spec |
| Already correct (VG SPDX-only, new files) | 5 | no action |
| Out-of-scope third-party single-file libs | 9 | no action |
| Out-of-scope ETL-introduced unmodified | 4 | no action |
| Out-of-scope (touched only by initial import) | 377 | no action |

## §1 — File inventory

Methodology: `git log --name-only --pretty=format: --all` over the
listed source dirs gave **429** files. Excluding pure-VanguardMod
files (`/wolfguard/`, `/vg_*`, `/wg_*`, `/g_vanguard*` paths) yielded
**423** modified-ETLegacy candidates. **2 files** in the input list
no longer exist (`src/game/g_wolfguard_null.c`, `src/game/wolfguard.h`,
both deleted in v0.6.0) — skipped, leaving **421** files actually
classified.

### Header classification (top 30 lines of each file)

| Category | Definition | Count |
|---|---|---|
| **A** | id Software + ETLegacy + VanguardMod (Triple-Header present) | **0** |
| **B** | id Software + ETLegacy, no VanguardMod | **393** |
| **C** | Other (no id, no ETL, custom, or only one of the three) | **28** |

Zero Category A means **no source file currently carries the
Triple-Header.** Memory #11's v0.4.3 SPDX sweep added SPDX-License-
Identifier tags but did not add the VanguardMod copyright lines to
the existing id+ETL block.

### Category B post-import modification volume

Of the 393 Category B files, only **16 have non-zero post-import
commits.** The other **377** were touched only by the initial
ETLegacy import commit (`de8ab0a chore: import etlegacy mod sdk and
rename mod identity to vanguard`) and have never been edited by
VanguardMod since. Those 377 are correctly id+ETL only — out of
scope for any retrofit.

## §2 — Category B target list (sorted by post-import insertions)

Range: `de8ab0a..HEAD` (after the import commit, all changes are
VanguardMod work).

### Clear-cut targets (≥10 inserts post-import)

| File | Inserts | Deletes | Commits | Notes |
|---|---|---|---|---|
| `src/game/g_combat.c` | 537 | 112 | 9 | multi-region damage path, NULL-guard, strict-mode, capsule diag |
| `src/game/bg_animgroup.c` | 73 | 0 | 2 | hitbox animgroup loader |
| `src/game/g_main.c` | 71 | 8 | 6 | WolfGuard hooks, vg_*_Init calls, diag log line |
| `src/cgame/cg_weapons.c` | 69 | 69 | 2 | weapon-table edits |
| `src/game/bg_public.h` | 38 | 0 | 2 | shared enums / structs added for hitbox |
| `src/cgame/cg_loadpanel.c` | 37 | 4 | 1 | branding |
| `src/game/g_client.c` | 31 | 7 | 3 | WolfGuard hooks, footprint mins/maxs |
| `src/ui/ui_main.c` | 13 | 2 | 1 | UI integration |
| `src/game/g_svcmds.c` | 12 | 0 | 1 | wg_status command (v0.6.0) |
| `src/cgame/cg_cvars.c` | 11 | 0 | 2 | cgame cvar additions |
| `src/game/g_cvars.c` | 11 | 0 | 1 | vanguard_diag_movement (v0.6.1) |

### Borderline targets (1–9 inserts post-import)

| File | Inserts | Deletes | Commits | Notes |
|---|---|---|---|---|
| `src/cgame/cg_cvars.h` | 7 | 0 | 2 | header for cgame cvar adds |
| `src/cgame/cg_servercmds.c` | 3 | 0 | 1 | tiny touch |
| `src/qcommon/common.c` | 2 | 2 | 1 | very small |
| `src/cgame/cg_view.c` | 2 | 0 | 1 | very small |
| `src/cgame/cg_local.h` | 1 | 0 | 1 | one-line header touch |

**Spec discrepancy:** the prompt mentions `bg_pmove.c` as a confirmed
Phase B target but `git log de8ab0a..HEAD -- src/game/bg_pmove.c`
returns no commits. The file is unmodified post-import. It will
become a target when v0.7.0 (falldamage redesign per Phase 7.3 audit
§5) actually edits it; until then, no attribution is owed.

## §3 — Category C review

28 files fell outside the standard id+ETL header pattern. Subdivided
by sub-category:

### C1 — VanguardMod-original (already correct, NOT Phase B targets)

These files were created by VanguardMod and carry SPDX-only headers
with VanguardMod copyright. They should NOT receive the Triple-Header
because they have no id Software / ETLegacy origin. The Phase A
filter regex (`/vg_|/wg_|/g_vanguard`) didn't catch them because their
filenames start with `cg_vanguard_` and `g_xp_saver_stub_`, not the
`vg_` / `wg_` prefix.

| File | Inserts | Notes |
|---|---|---|
| `src/cgame/cg_vanguard_dev.c` | 1188 | Phase 6/7 client-side dev mode (multi-region wireframe) |
| `src/cgame/cg_vanguard_mdx.c` | 799 | client-side MDX bone calc port (Strategy I) |
| `src/cgame/cg_vanguard_mdx.h` | 116 | header for above |
| `src/cgame/cg_vanguard_dev.h` | 45 | header for above |
| `src/game/g_xp_saver_stub.c` | 24 | bootstrap-replaced stub per CLAUDE.md |

**Action: none.** Correctly attributed today.

### C2 — Dual-Header (ETL + VG) candidates — NOT in original spec scope

ETLegacy-introduced files (no id Software origin) with significant
VanguardMod modifications. The spec calls for Triple-Header (id + ETL
+ VG) but these don't have id Software in their lineage — they were
added by ETLegacy as new files (MDX support is an ETLegacy feature).
Correct retrofit would be a **Dual-Header (ETL + VG)**.

| File | Inserts | Commits | Notes |
|---|---|---|---|
| `src/game/g_mdx.c` | 100 | 3 | bone-axis output for VG_DIAG_DUMP, multi-region helpers |
| `src/game/g_mdx.h` | 41 | 3 | matching header changes |

**wahke decision needed:** include these in Phase B with a Dual-Header
(ETL + VG, omit id Software block), or hold for a separate ETLegacy-
files-attribution PR? My recommendation: include in Phase B with the
Dual-Header variant — same retrofit batch, same review effort, cleaner
end state.

### C3 — Out-of-scope third-party single-file libs

Files with their own original-author headers (zero VanguardMod
post-import changes). Leave alone.

`src/qcommon/md5.c`, `src/qcommon/puff.c`, `src/qcommon/puff.h`,
`src/qcommon/i18n_findlocale.c`, `src/qcommon/i18n_findlocale.h`,
`src/game/et-antiwarp.c`, `src/game/g_lua.c`, `src/game/g_lua.h`,
`src/game/g_sha1.c`.

### C4 — Out-of-scope ETL-introduced unmodified files

ETL-introduced files (no id Software origin) that VanguardMod has
not touched post-import. No attribution owed.

`src/game/bg_b64.{c,h}`, `src/game/bg_ebs.{c,h}`, `src/game/g_db.c`,
`src/game/g_match_tokens.c`, `src/game/g_mdx_lut.h`,
`src/game/g_prestige.c`, `src/game/g_skillrating.c`,
`src/server/sv_wallhack.c`, `src/sys/mos_libnix_so.c`.

### C5 — Investigate

| File | Notes |
|---|---|
| `src/cgame/cg_hud_iconfeed.c` | id=0 etl=0 vg=0, 0 post-import inserts. Likely a single-author file with no header at all — leave alone unless wahke flags it. |

## §4 — Borderline cases (re-iterated)

The 5 files with 1–9 inserts post-import are listed in §2 above. Two
schools of thought:

**Inclusive:** every line of post-import VanguardMod work earns
attribution. Even `cg_local.h:1 line` warrants the Triple-Header.
Defensible legally + low maintenance friction (one batch).

**Selective:** 1–2 line touches (e.g. adding an `extern vmCvar_t`
declaration, fixing a typo) are cosmetic and don't add original
copyrightable content. Skip them; bump the cutoff to ~10 inserts.
Reduces visual diff churn for trivial files.

**My recommendation:** include all 5 in Phase B. The retrofit work is
fixed-cost per file (header replacement is mechanical), the legal
posture is cleaner, and a future maintainer doesn't have to debate
"is this 7-line delta worth attribution?" — the answer is already
yes by the precedent of including all of them now.

## §5 — Phase B scope recommendation

### Definite Triple-Header retrofit (id + ETL + VG, 11 files)

```
src/game/g_combat.c
src/game/bg_animgroup.c
src/game/g_main.c
src/cgame/cg_weapons.c
src/game/bg_public.h
src/cgame/cg_loadpanel.c
src/game/g_client.c
src/ui/ui_main.c
src/game/g_svcmds.c
src/cgame/cg_cvars.c
src/game/g_cvars.c
```

### Borderline Triple-Header (wahke decides, 5 files)

```
src/cgame/cg_cvars.h
src/cgame/cg_servercmds.c
src/qcommon/common.c
src/cgame/cg_view.c
src/cgame/cg_local.h
```

### Dual-Header retrofit candidates (ETL + VG, wahke decides, 2 files)

```
src/game/g_mdx.c
src/game/g_mdx.h
```

These need an ETL+VG variant of the header (no id Software block).
Recommend including in Phase B with a separate template (§6.b below).

### Out of scope (no action)

- 377 imported-but-never-modified Category B files
- 5 VanguardMod-original Cat C files (already correct SPDX-only)
- 9 third-party single-file libs (own author headers)
- 9 ETL-introduced files VanguardMod hasn't touched post-import
- `cg_hud_iconfeed.c` (no header, no VG mods — leave alone)
- `bg_pmove.c` (claimed in spec, but zero post-import changes — defer
  to v0.7.0 when falldamage redesign actually edits it)

### Estimated Phase B effort

11 files (definite) × header replacement = ~30 min mechanical work +
build verification. +5 borderline if wahke says yes (~15 min). +2
Dual-Header if wahke says yes (~10 min including template variant).
Total: 30–55 min depending on scope. Single PR, single commit, no
behavioural change.

## §6 — Triple-Header template

### §6.a — Triple-Header (id + ETL + VG) for files in §5 §Definite + §Borderline

```c
/*
 * Wolfenstein: Enemy Territory GPL Source Code
 * Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.
 *
 * ET: Legacy
 * Copyright (C) 2012-2024 ET:Legacy team <mail@etlegacy.com>
 *
 * VanguardMod
 * Copyright (C) 2026 wahke <info@wahke.lu> (https://wahke.lu)
 * Copyright (C) 2026 VanguardMod Project Contributors
 *
 * This file is part of ET: Legacy - http://www.etlegacy.com
 *
 * ET: Legacy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ET: Legacy is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ET: Legacy. If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, Wolfenstein: Enemy Territory GPL Source Code is also
 * subject to certain additional terms. You should have received a copy
 * of these additional terms immediately following the terms and
 * conditions of the GNU General Public License which accompanied the
 * Wolfenstein: Enemy Territory GPL Source Code.  If not, please request
 * a copy in writing from id Software at the address below.
 *
 * If you have questions concerning this license or the applicable
 * additional terms, you may contact in writing id Software LLC, c/o
 * ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
```

**Notes for Phase B:**

- "wahke" is **always lowercase**.
- The **VanguardMod block** is inserted between the existing ET:Legacy
  block and the GPL boilerplate — preserves chronological attribution
  order (id → ETL → VanguardMod).
- The existing ETLegacy boilerplate text varies slightly across files
  (some end at `Maryland 20850 USA.`, some have minor wording
  differences). Phase B should replace the entire header in one
  operation per file, not splice into the existing one — easier to
  diff, easier to verify.
- Add `SPDX-License-Identifier: GPL-3.0-or-later` if not already
  present at the end of the comment block.

### §6.b — Dual-Header (ETL + VG) variant for §5 §Dual-Header candidates

For ETLegacy-introduced files without id Software lineage. Use
**only** for files §5 explicitly tags as Dual-Header (currently
`g_mdx.c`, `g_mdx.h`).

```c
/*
 * ET: Legacy
 * Copyright (C) 2012-2024 ET:Legacy team <mail@etlegacy.com>
 *
 * VanguardMod
 * Copyright (C) 2026 wahke <info@wahke.lu> (https://wahke.lu)
 * Copyright (C) 2026 VanguardMod Project Contributors
 *
 * This file is part of ET: Legacy - http://www.etlegacy.com
 *
 * ET: Legacy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ET: Legacy is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ET: Legacy. If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
```

(Same as §6.a with the id Software copyright + W:ET-specific
boilerplate paragraphs removed.)

## §7 — Phase B verification plan (sketch for the next PR)

For Phase B to land safely:

1. Build green after each header replacement (build cache catches
   syntax errors fast).
2. `git diff --stat` shows only header lines changed (no accidental
   code edits creeping in).
3. CI pass (Linux + Win64).
4. Spot-check 3 files with `head -45 <file>` to confirm header reads
   correctly + `SPDX-License-Identifier` present.
5. Commit message: `chore(copyright): retrofit Triple-Header to N
   modified ETLegacy files (Phase Copyright-Sweep B)`.

## §8 — Open questions for wahke

1. **Borderline 5 files** — include in Phase B (recommended) or skip?
2. **Dual-Header for `g_mdx.{c,h}`** — include in Phase B (same PR) or
   defer to a separate ETL-files-attribution PR?
3. **`bg_pmove.c`** — defer to v0.7.0 (recommended; nothing to
   attribute today), or attach the Triple-Header pre-emptively now so
   v0.7.0 work doesn't trigger a header churn?
4. **Phase B execution model** — single mechanical PR (all files at
   once, easy to review per-file), or split into 2–3 PRs by directory
   (`src/game/`, `src/cgame/`, `src/{ui,qcommon}/`)?

## §9 — Phase B execution log

Date: 2026-05-03
Branch: `feat/v0.6.2-copyright-sweep` → PR #3
Commits (4 source + 1 docs):

  - `cdc1d11 chore(copyright): add VanguardMod attribution to src/game/*.c`
  - `d48e864 chore(copyright): add VanguardMod attribution to src/cgame/*`
  - `871a48c chore(copyright): add VanguardMod attribution to src/ui/*`
  - `77b960a chore(copyright): add VanguardMod attribution to src/qcommon/*`
  - `<this commit> docs(notes): record Phase B execution + v0.6.2 release notes`

### Final scope: 13 files (Triple-Header inserted)

`src/game/g_main.c`, `src/game/g_client.c`, `src/game/g_svcmds.c`,
`src/game/g_cvars.c`, `src/cgame/cg_weapons.c`, `src/cgame/cg_loadpanel.c`,
`src/cgame/cg_cvars.c`, `src/cgame/cg_cvars.h`, `src/cgame/cg_servercmds.c`,
`src/cgame/cg_view.c`, `src/cgame/cg_local.h`, `src/ui/ui_main.c`,
`src/qcommon/common.c`.

### Recon-vs-execution discrepancy (lessons-learned)

Phase A's `head -30` window for header classification missed the
v0.4.3-era "Modifications for VanguardMod" comment blocks that some
files carry as a *separate* block following the GPL boilerplate
(starting around line 31). The recon flagged 5 files as Cat B
(needs retrofit) when they were actually already attributed:

  - `src/game/g_combat.c` — separate VG block at lines 32-46
  - `src/game/bg_animgroup.c` — same pattern
  - `src/game/bg_public.h` — same pattern
  - `src/game/g_mdx.c` — separate VG block at lines 31-44
    (after Christopher Lais zlib-style header — no ETLegacy boilerplate)
  - `src/game/g_mdx.h` — same pattern at lines 29-41

**Decision (per wahke review of Phase A §8):** Option α — skip the 5
already-attributed files. Reasons:

  1. They are legally correct under GPL-3.0 already; reformatting
     would not add legal value.
  2. They carry *richer* attribution (descriptive paragraphs explain
     what was modified). Replacing with the boilerplate Triple-Header
     would lose that information.
  3. Spec mandated "Pure header-insertion, no formatting changes" —
     migrating violates that.
  4. Format-uniformity is not the goal; legal correctness is.

**Lessons-learned for future header recons:**

  - Use `head -80` (or larger) for the classification grep — the
    `head -30` window is too narrow because some files have a
    second comment block at line 31+ for the VG attribution.
  - Cross-check `vg-block` count separately from header counts
    (e.g. grep for `Modifications for VanguardMod` literally).
  - Check the closing-anchor line range too (`tail -10` of the
    header) since some files end with extra license terms.

### Final scope: 18 → 13 files

| Original recon | Decision | Final |
|---|---|---|
| 11 clear-cut Triple | execute | 11 |
| 5 borderline Triple | include (Q1=yes per wahke) | 5 |
| 2 Dual-Header (g_mdx.{c,h}) | skip (already attributed) | 0 |
| **`bg_pmove.c` deferred** | v0.7.0 (Q3) | 0 |
| **3 already-attributed (g_combat, bg_animgroup, bg_public)** | skip (Option α) | 0 |
| **TOTAL** | | **13** |

### Build verification

`cmake --build build -j` ran green after each per-directory commit:

  - after src/game/*.c → green
  - after src/cgame/* → green
  - after src/ui/* → green
  - after src/qcommon/* → green

### Per-file header verification (all 13)

Each file passes:

  - `head -45 <file> | grep -c "VanguardMod\|wahke\|Project Contributors"` → 3
  - `head -45 <file> | grep -c "SPDX-License-Identifier"` → 1
  - `head -45 <file> | grep -c "id Software"` → 3 (pre-existing
    boilerplate — top, body, closing)
  - `git diff main..<commit-of-file>` shows only header-area changes
    (4 line VG block insert + 2 line SPDX append — total +6 LOC per
    file)

### Files explicitly NOT touched

  - `src/game/bg_pmove.c` — deferred to v0.7.0 (no post-import
    modifications today).
  - `src/game/g_combat.c`, `src/game/bg_animgroup.c`,
    `src/game/bg_public.h`, `src/game/g_mdx.c`, `src/game/g_mdx.h`
    — already attributed (separate VG block from v0.4.3).
  - 5 VanguardMod-original files filtered out by recon
    (`cg_vanguard_*`, `g_xp_saver_stub.c`).
  - 9 third-party single-file libs (md5, puff, i18n, et-antiwarp,
    g_lua, g_sha1).
  - 9 ETLegacy-introduced files VanguardMod hasn't touched
    post-import (bg_b64, bg_ebs, g_db, etc.).
  - 377 import-only files (just vendored, never edited).
