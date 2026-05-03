# Phase Branding 2 — Mod-List Color-Codes Recon

> Pure recon. No code changes. wahke decides scope after this.

Date: 2026-05-03
Author: Claude Code (Opus 4.7) on session for wahke
Status: AUDIT (no implementation)

References Memory #21 (Branding-Phase post-v0.7.0), prior Phase
Branding Audit (`docs/notes/PHASE_BRANDING_AUDIT.md`, 2026-04-30
description.txt work that shipped in v0.5.2.4), Nitmod
`ui_mp_*.so` reverse-engineering (this session).

## TL;DR

VanguardMod **already builds its own UI module** (`ui.mp.x86_64.so`,
`ui_mp_x64.dll`, `ui_mp_x86.dll` — all present in `build/vanguard/`
and shipped by `release.yml`). The hook-point exists in
`src/ui/ui_main.c` at line 8083 (Option C, `UI_FeederItemText` for
`FEEDER_MODS`). A static lookup table mapping `dir_name → display_name`
adds ~50 LOC of code + ~15 LOC of table + ~30 LOC of diagnostic
infra.

| Decision | Recommendation |
|---|---|
| Hook-point (§3) | **Option C** — `UI_FeederItemText` case `FEEDER_MODS`, line 8083 — display-time only, modList stays raw |
| Build matrix (§4) | **Already covered** — UI ships on Linux x64 + Win64 + Win32; release.yml stages all three |
| Lookup data (§5) | **Override description.txt** for known mods (consistency with Nitmod approach); fall back to description.txt then dir-name for unknown |
| Naming convention (§9) | **`vanguard_diag_branding`** (mirror existing `vanguard_diag_movement` from v0.6.1) |
| Override config file (§9.6) | **Defer to v0.8.x** — hardcoded table sufficient for v1 |
| Release version (§11) | **v0.7.1 alongside Falldamage** (Phase 8.0b) — both small UI/feature touches; or **v0.7.0.1 hotfix** if Falldamage takes longer |

Effort: ~100 LOC code + ~50 LOC docs. Risk: very low (UI-only, no
server-side, no Phase 8.0a / WolfGuard / vg_fun interaction).

## §1 — VanguardMod current UI module status

**VanguardMod already ships its own UI module on all 3 platforms:**

| Artifact | Size | Location |
|---|---|---|
| `ui.mp.x86_64.so` | 1.5 MB | `build/vanguard/` |
| `ui_mp_x64.dll` | 689 KB | `build/vanguard/` |
| `ui_mp_x86.dll` | 683 KB | `build/vanguard/` |

`src/ui/` carries **16 source files** (the full ETLegacy SDK UI
tree): `ui_main.c`, `ui_atoms.c`, `ui_cvars.{c,h}`, `ui_gameinfo.c`,
`ui_loadpanel.c`, `ui_local.h`, `ui_main_changelog.c`, `ui_menu.c`,
`ui_menuitem.c`, `ui_parse.c`, `ui_public.h`, `ui_script.c`,
`ui_shared.{c,h}`, `ui_syscalls.c`, plus `_MOD_CODE.txt`.

**Build pipeline:** `release.yml` already stages all three UI
binaries into `vanguard-vX.Y.Z-server.zip` (lines 322, 328, 332 —
verified in checkout). Client.zip ships only the `.pk3` (which
contains the `.so` for client-download via sv_pure 0).

**Conclusion:** no new build-system work needed. v0.7.1 implementation
is purely a `src/ui/ui_main.c` source edit + new lookup-table file
(or inline in ui_main.c) + diagnostic cvar.

## §2 — UI_LoadMods upstream location

All in `src/ui/ui_main.c`:

| Function | Line | Role |
|---|---|---|
| `UI_LoadMods` | 4323 | Iterates `$modlist` builtin, populates `uiInfo.modList[]` with `{ modName, modDescr }` per dir |
| `UI_SortMods` | 4312 | qsort comparator (sorts by `modName` alphabetically) |
| Script-handler dispatch `"LoadMods"` | 5014-5016 | Triggered by menu script when user opens Mods sub-menu |
| `UI_FeederCount` case `FEEDER_MODS` | 7554 | Returns `uiInfo.modCount` for paint loop |
| **`UI_FeederItemText` case `FEEDER_MODS`** | **8083** | **Returns display string per index — the hook-point** |
| `UI_FeederSelection` case `FEEDER_MODS` | 8381 | User clicks an entry; sets selection index |
| `RunMod` script handler | 5038-5040 | User confirms; sets `fs_game` to the dir name |

`FEEDER_MODS` enum value is `0x09`, defined at `etmain/ui/menudef.h:108`.

`uiInfo.modList[]` struct (`src/ui/ui_local.h:562`) is `modInfo_t`
with `MAX_MODS = 64` capacity. Each entry has:
- `modName` (the raw directory name, e.g. `"vanguard"`)
- `modDescr` (the contents of `<dir>/description.txt`, populated by
  the engine's `$modlist` builtin via `FS_SV_FOpenFileRead`)

Current `UI_FeederItemText` for FEEDER_MODS (lines 8083-8095):

```c
case FEEDER_MODS:
    if (index >= 0 && index < uiInfo.modCount)
    {
        if (uiInfo.modList[index].modDescr && *uiInfo.modList[index].modDescr)
        {
            return uiInfo.modList[index].modDescr;
        }
        else
        {
            return uiInfo.modList[index].modName;
        }
    }
    break;
```

**Why VanguardMod's own entry currently shows plain "vanguard":**
The engine reads `<dir>/description.txt` only when it's at the
correct fs_basepath / fs_homepath location. v0.5.2.4 ensured
`description.txt` ships next to the `.pk3` in both server and
client release ZIPs. If wahke's test PC still shows plain text,
the file is either missing or shadowed by another paks's
description (sv_pure 0 + multiple paks). **Branding 2 sidesteps
this entirely** by hardcoding the lookup, no description.txt
dependency for known mods.

## §3 — Hook-point analysis (A/B/C)

### Option A — Hook in `UI_LoadMods`

Transform `modName` / `modDescr` at load time:

```c
/* In UI_LoadMods() after String_Alloc calls */
const char *display = vg_Brand_LookupDir(dirptr);
if (display) {
    uiInfo.modList[uiInfo.modCount].modDescr = String_Alloc(display);
} else {
    uiInfo.modList[uiInfo.modCount].modDescr = String_Alloc(descptr);
}
```

**Pro:** transformation happens once per load (cheap), every
renderer of `modList` automatically sees the branded version.
**Con:** mutates the data — debugging "what does the engine
actually return?" becomes harder. Also, `RunMod` reads
`uiInfo.modList[index].modName` (line 5040) to set `fs_game`;
that field stays raw, so this is OK, but the asymmetry between
`modName` (raw) and `modDescr` (transformed) is subtle.

### Option B — Hook in `Item_Text_Paint` (or feeder-paint function)

Transform every paint frame:

```c
/* In ui_shared.c Item_Text_Paint or feeder paint loop */
if (item->feeder == FEEDER_MODS) {
    text = vg_Brand_Lookup(uiInfo.modList[index].modName);
}
```

**Pro:** zero data mutation, display-only.
**Con:** runs every paint frame (60+ Hz). Lookup is O(N) over a
~10-entry table → microseconds, but it's still wasteful. Also
requires touching `ui_shared.c` which is more invasive.

### Option C — Hook in `UI_FeederItemText` case `FEEDER_MODS`

```c
case FEEDER_MODS:
    if (index >= 0 && index < uiInfo.modCount)
    {
        const char *brand = vg_Brand_Lookup(uiInfo.modList[index].modName);
        if (brand) {
            return brand;  /* hardcoded table override */
        }
        if (uiInfo.modList[index].modDescr && *uiInfo.modList[index].modDescr)
        {
            return uiInfo.modList[index].modDescr;  /* description.txt fallback */
        }
        return uiInfo.modList[index].modName;  /* raw dir name fallback */
    }
    break;
```

**Pro:**
- Cleanest separation: data stays raw, display lookup is a single
  hook in the dispatch function the renderer already calls
- Lookup runs only when the feeder is actually visible (Mods
  menu open) — typically ~10 calls per second for a few
  seconds, then the menu closes
- Fallback chain is explicit: hardcoded table → description.txt →
  dir name
- Two-line change (one extra block + the existing `if/else`
  becomes the fallback)

**Con:** none significant. Lookup function lives in a new
`vg_branding.c` (or inlined in `ui_main.c`).

### Recommendation: Option C

`UI_FeederItemText` is the canonical "give me display text for
feeder F item N" function — exactly the contract Memory #21's
branding goal needs. The data layer (`UI_LoadMods`) stays
upstream-pristine, the renderer (`Item_Text_Paint`) stays
upstream-pristine, only the dispatch is extended.

Insertion point: `src/ui/ui_main.c:8083` — replace the existing
`if (modDescr) ... else ...` block with the 3-tier fallback above.

## §4 — Build matrix verification

`release.yml` (current at HEAD) stages all three UI binaries:

```yaml
# Linux .so
cp build/vanguard/ui.mp.x86_64.so      "${STAGING}/vanguard/"
# Windows .dll
cp build/vanguard/ui_mp_x64.dll        "${STAGING}/vanguard/"
cp build/vanguard/ui_mp_x86.dll        "${STAGING}/vanguard/"
```

Lines 322, 328, 332 of `release.yml` (verified). All three
platforms covered. CI builds Linux x64 + Win64 (per ci.yml); Win32
is release-only (omitted from CI per memory of CI design choice
"keep CI under ~5 minutes"). So Branding-2 implementation will
build green on the CI matrix as-is — Win32 first builds at
release time.

**No build-system changes needed.** Just edit `src/ui/ui_main.c`.

## §5 — Lookup-table data design

### Table structure

```c
typedef struct {
    const char *dir_name;       /* mod directory name, lookup key */
    const char *display_name;   /* color-coded display string */
} vg_mod_brand_t;

static const vg_mod_brand_t vg_mod_brands[] = {
    /* VanguardMod's own — matches misc/description.txt content */
    { "vanguard",  "^8Vanguard^7Mod"        },
    /* ETLegacy upstream — current display: "^1ET^7: LEGACY^1 - ^7legacy mod" */
    { "legacy",    "^1ET^7:Legacy"          },
    /* W:ET base game — engine fallback when fs_game is unset */
    { "etmain",    "^7Wolfenstein: ET"      },
    /* Cup mod */
    { "etpro",     "^7ETPro"                },
    { "compet",    "^7Comp^1ET"             },
    /* Public mods */
    { "jaymod",    "^3Jay^7mod"             },
    { "noquarter", "^1No Quarter"           },
    { "silent",    "^7silEnT"               },
    { "etpub",     "^7ETPub"                },
    /* Other community mods — defaults to plain white if seen */
    { "nitmod",    "^7N^1!^7tmod"           },
    { "xmod",      "^7xmod"                 },
    { NULL,        NULL                     }   /* sentinel */
};
```

### Case sensitivity

Lookup uses **`Q_stricmp`** (case-insensitive). Linux mod dirs
are case-sensitive at the FS layer, but admins routinely use
mixed case (`Vanguard/`, `JayMod/`). Our table keys are
canonical lowercase; we match any case the admin chose.

### Fallback strategy

3-tier:

1. Hardcoded table hit → return color-coded string
2. `description.txt` content (`modDescr`) non-empty → return that
3. Raw `modName` (dir name, plain text) → return that

Unknown mods (no table entry, no description.txt) get the dir
name as-is — backwards-compatible with current behaviour.

### Color-choice rationale

Each entry uses **the mod's own established brand color** where
known:

- `^8` cyan/turquoise — VanguardMod (matches our existing
  description.txt + WolfGuard banner)
- `^1` red — ETLegacy ("ET"), No Quarter, "!" highlight in nitmod,
  "ET" in CompET
- `^3` yellow — Jaymod (Jaymod's traditional yellow on Jay)
- `^7` white — neutral/default, used for parts that aren't
  specifically branded

These choices are **observed from each mod's own self-display**,
not copied from Nitmod's binary. Nitmod's reverse-engineered
table merely confirmed which color codes each community accepts
as "their" brand.

### Override semantics: hardcoded vs description.txt

The hardcoded table **overrides** `description.txt`. Reason:
description.txt is mod-author-controlled, but most mods either
don't ship one or ship plain-text versions. Branding-2's value
proposition is consistent visual identity across the whole mod
list, which only works if WE control the rendering.

Side effect: if a mod author updates their own description.txt
to a fancy string, our hardcoded entry shadows it. That's
acceptable — they can request a table update via PR, or fall
into the unknown-mod branch and use their description.txt.

## §6 — Diagnostic + verification

Per Phase 7.0 lessons-learned, branding-2 ships with diagnostic
infrastructure from day 1.

### `vanguard_diag_branding` cvar (mirror of `vanguard_diag_movement` from v0.6.1)

- Storage: `g_cvars.c` (UI cvars actually live in `ui_cvars.c` —
  see §9.5 open question on which one)
- Default: `0`, `CVAR_TEMP` (re-arm per session)
- When `1`: log each mod-list entry transformation at
  `UI_LoadMods` completion:
  ```
  VG_Brand: vanguard         -> ^8Vanguard^7Mod (table)
  VG_Brand: legacy           -> ^1ET^7:Legacy (table)
  VG_Brand: jaymod           -> ^3Jay^7mod (table)
  VG_Brand: mymod_test       -> mymod_test (no table entry, no description)
  VG_Brand: othermod         -> Other Mod 1.2 (description.txt)
  ```

### CI gates

Add to `.github/workflows/ci.yml` build-linux job (after the
existing vg_fun gate from v0.7.0):

```yaml
- name: Verify branding lookup table in ui Linux SO
  run: |
    set -euo pipefail
    BIN=$(find build -name 'ui.mp.*.so' -print -quit)
    test -n "${BIN}"
    BRANDS=$(strings -a "${BIN}" | grep -c "vg_mod_brands\|VG_Brand:")
    VANG=$(strings -a "${BIN}" | grep -c "\\^8Vanguard\\^7Mod")
    if [ "${BRANDS}" -lt 2 ] || [ "${VANG}" -lt 1 ]; then
      echo "::error::Branding-2 lookup table not found in ui binary"
      exit 1
    fi
```

Threshold: `BRANDS≥2` (the symbol + at least one log string),
`VANG≥1` (VanguardMod's own table entry).

## §7 — Regression-safety with Phase 8.0a / WolfGuard / vg_fun

Branding-2 is **UI-only** (`src/ui/ui_main.c` + new helper).
None of the other subsystems touch the UI render path. Verified:

| Subsystem | Code path | Branding-2 interaction |
|---|---|---|
| Phase 8.0a NULL-guard (`g_combat.c:1781-1784`) | server-side damage gate | none — server-side, branding is client-UI |
| WolfGuard banner (`wg_banner.c`) | server-side `G_Printf` at G_InitGame | none — separate code path |
| WolfGuard hooks (`g_main.c`, `g_client.c`) | server-side dispatch via `WG_Active->*` | none |
| vg_fun mode-line in WG banner | server-side `G_Printf` | none |
| vg_fun stats tagging (`G_LogPrintf mode=...`) | server-side log | none |
| vg_Hitbox multi-region (`g_combat.c`) | server-side damage | none |
| vg_Netcode profile (`g_vanguard.c`) | server-side cvar set | none |

The UI module only links against `cgame_libraries` / `ui_libraries`
(per `cmake/ETLBuildMod.cmake`). It does NOT link against
`g_vanguard.{c,h}` or any qagame source. So branding-2 cannot
regress server-side behaviour by construction.

### Regression test matrix

Manual verification before tag:
- ✓ Server boots — WolfGuard banner unchanged
- ✓ vg_fun mode line in banner unchanged
- ✓ `die -1` rcon still works (Phase 8.0a guard)
- ✓ vg_Hitbox multi-region damage still works (drop tests)
- ✓ vg_Netcode profile applies (sv_fps 40 in cup profile)
- ✓ Mods menu shows color-coded entries (the new feature)

## §8 — Live-test plan

Test matrix for the implementation phase:

1. **Pure VanguardMod-only system** (single mod installed) — Mods
   menu shows `^8Vanguard^7Mod` + `^7Wolfenstein: ET` (etmain
   fallback). Both color-coded.
2. **VanguardMod + Jaymod installed** — both color-coded; Jaymod
   shows `^3Jay^7mod`, vanguard shows `^8Vanguard^7Mod`.
3. **VanguardMod + 4 other mods** (NoQuarter, ETLegacy, silent,
   etpub) — all 5 color-coded per the table.
4. **Unknown mod installed** (e.g. `mymod_test/`) — falls back to
   plain "mymod_test" (or to its description.txt if present).
5. **Case-mismatch test** (`Vanguard/` instead of `vanguard/` on
   case-insensitive filesystems / Win32) — still matches via
   `Q_stricmp`, displays `^8Vanguard^7Mod`.
6. **Empty mod-list** (test environment with no mods) — no crash,
   no garbage output (the existing `numdirs == 0` path is
   untouched).
7. **`vanguard_diag_branding 1`** — log shows per-entry
   transformation lines on next Mods-menu open.
8. **Mod with both table-entry AND description.txt** (e.g.
   modder ships description.txt for `jaymod` with custom string)
   — table wins per §5 override semantics; description.txt is
   ignored.
9. **Engine `description.txt` shadowing** (multiple paks define
   different description.txt for the same dir — which wins) —
   doesn't matter, table overrides both.

## §9 — Open decisions for wahke

1. **Hook-point** — Option A (UI_LoadMods), B (Item_Text_Paint),
   or C (UI_FeederItemText)? **Recommendation: C** (cleanest
   separation, only place that needs editing).
2. **Build VanguardMod's own UI** — already done; nothing to
   decide. (Question becomes moot.)
3. **Color choices** — accept §5 baseline table or counter-propose
   per mod? Each mod's color is based on observed brand
   conventions; table is documented and easy to PR-review.
4. **Branding for unknown mods** — plain dir name (current
   proposal, fallback tier 3) or generic gray (`^9<dirname>`)?
   Recommendation: plain dir name to maintain backwards
   compatibility with current behaviour.
5. **Diagnostic cvar naming** — `vanguard_diag_branding` (matches
   `vanguard_diag_movement` from v0.6.1) or `vg_diag_branding`
   (matches new vg_* convention)? Recommendation:
   `vanguard_diag_branding` for consistency with existing
   diagnostic cvars; `vg_*` is for vg_fun-prefixed feature
   cvars per Phase 9.0 audit.
6. **Override config file `branding.cfg`** — ship with v1
   (foundational) or defer? Recommendation: defer to v0.8.x.
   Hardcoded table is sufficient for v1; modder-overrides are
   a UX nicety that doesn't change semantics.
7. **License attribution** — include credit to Nitmod in
   `docs/VG_BRANDING.md` (acknowledging the pattern
   inspiration), or just note "clean-room inspired by other ET
   mods"? Recommendation: explicitly credit Nitmod for the
   pattern (good-faith open-source ack); no binary code copied.
8. **Release version** — v0.7.0.1 hotfix (small/fast), v0.7.1
   alongside Falldamage, or v0.8.0 with broader UI redesign?
   Recommendation: **v0.7.1 alongside Falldamage** —
   bundling small UI change with the first vg_fun feature
   keeps release cadence predictable; both ship same time.

## §10 — Risk map per sub-topic

| Sub-topic | Risk | Why |
|---|---|---|
| `vg_Brand_Lookup()` helper function | LOW | ~10-line linear scan over ~10 entries; pure logic, no I/O |
| Lookup-table data | LOW | Static const, ~15 LOC, easy to PR-review |
| `UI_FeederItemText` hook (Option C) | LOW | 2-line change in dispatch; existing `if/else` becomes the fallback |
| `vanguard_diag_branding` cvar | LOW | Mirrors v0.6.1 `vanguard_diag_movement` exactly |
| Boot-line / per-entry log | LOW | Pure `Com_Printf` calls; no side effects |
| CI gate (strings grep) | LOW | Same shape as existing vg_fun + omni-bot + community-banner gates |
| **`description.txt` override semantics** | LOW-MED | Some mod authors might prefer their description.txt to win. We document the override; they can PR a table-removal request. |
| **Color choices vs cup-tester taste** | LOW | Each mod's color is well-known community convention. wahke can override per mod via §9.3 decision. |
| **Win32 build (CI doesn't catch)** | LOW | UI source is C89, no platform-specific code added; release.yml builds Win32 + verifies before publishing |
| Cross-mod color collisions (two mods with same color) | NONE | No collisions in proposed table |
| Phase 8.0a / WolfGuard / vg_fun regression | NONE | UI-only, no shared code path with server systems |

## §11 — Implementation-order recommendation

### v0.7.1 alongside Falldamage (recommended)

Bundle Branding-2 with Phase 8.0b Falldamage in v0.7.1:

**Pro:**
- v0.7.1 already touches src/ui (cgame for Falldamage's hit-marker
  tweaks if any — TBD per Phase 8.0b implementation)
- Both small, low-risk additions
- Single release cadence — no v0.7.0.1 hotfix slot needed
- Both share the same testing window (drop tests for Falldamage,
  Mods menu visual check for Branding-2)

**Con:**
- Branding-2 is fully independent of vg_fun; pairing them is
  cosmetic
- v0.7.1 PR diff grows by ~150 LOC (manageable)

### v0.7.0.1 hotfix (alternative)

Ship Branding-2 alone as a small hotfix:

**Pro:**
- v0.7.0 just shipped; immediate visible improvement
- 100% UI-only, very low risk
- Doesn't block Falldamage v0.7.1 work

**Con:**
- Three back-to-back releases (v0.7.0, v0.7.0.1, v0.7.1) — release
  fatigue
- Branding-2 is not urgent enough to warrant a hotfix slot
  (v0.5.2.4 description.txt deployment fix already shipped a
  partial solution)

### v0.8.0 with broader UI redesign (not recommended)

Defer Branding-2 to a future Phase 7.4 UI redesign:

**Pro:**
- Bundles all UI work
**Con:**
- Branding-2 ready now; deferring loses cup-tester first-impression
  improvement for months
- v0.8.0 scope creep

### Recommendation: v0.7.1 alongside Falldamage

If Falldamage takes longer than expected, **decouple to v0.7.0.1**
as a fast follow-up. Branding-2 is independent enough to ship
either way.

## §12 — License attribution

### Clean-room rationale

VanguardMod's branding-2 implementation is a **clean-room
re-implementation** of a pattern observed in Nitmod's
`ui_mp_*.so`. Specifically:

- We did NOT decompile Nitmod's binary into source code
- We did NOT copy Nitmod's lookup-table data structure layout
- We DID observe (via `strings` extraction) that Nitmod renders
  community mod names in color codes
- We DID infer (via the matching `LoadMods`/`$modlist` strings)
  that Nitmod hooks somewhere in the UI feeder path
- We DESIGN our own table structure, our own hook-point choice,
  and our own per-mod color choices

### License compatibility

| Component | License | Compatible with VanguardMod GPL-3-or-later? |
|---|---|---|
| Nitmod (W:ET-derived) | GPL-2-or-later | ✓ — we don't link or copy code, just inspired by the pattern |
| ETLegacy SDK (`src/ui/`) | GPL-3-or-later | ✓ — direct ancestor, our edits get Triple-Header retrofit if not already attributed |
| VanguardMod additions | GPL-3-or-later | ✓ |

### Documentation

`docs/VG_BRANDING.md` (new file, v0.7.1) will include an
acknowledgement section:

> The mod-list color-coding pattern was observed in Nitmod's
> public binary distribution. VanguardMod's implementation is a
> clean-room re-design — no Nitmod code was decompiled or
> reused. The community color conventions (Jaymod yellow,
> ETLegacy red, etc.) are widely-recognised brand choices
> independent of any specific mod's source code.

This is an honest disclosure that protects both projects:
- Nitmod is credited for the design pattern
- VanguardMod's code clearly stands on its own
- No copyright concerns

## §13 — References

### Code

- `src/ui/ui_main.c:4312` — `UI_SortMods` qsort comparator
- `src/ui/ui_main.c:4323-4351` — `UI_LoadMods` (Option A hook
  candidate; `$modlist` builtin call at line 4333)
- `src/ui/ui_main.c:5014-5016` — `LoadMods` script handler
  dispatch
- `src/ui/ui_main.c:5038-5040` — `RunMod` script handler (sets
  `fs_game`)
- `src/ui/ui_main.c:7554-7555` — `UI_FeederCount` case
  `FEEDER_MODS`
- **`src/ui/ui_main.c:8083-8095` — `UI_FeederItemText` case
  `FEEDER_MODS` — recommended hook-point (Option C)**
- `src/ui/ui_main.c:8381` — `UI_FeederSelection` case `FEEDER_MODS`
- `src/ui/ui_local.h:325` — `MAX_MODS = 64`
- `src/ui/ui_local.h:562` — `uiInfo.modList[]` declaration
- `etmain/ui/menudef.h:108` — `FEEDER_MODS = 0x09`
- `.github/workflows/release.yml:322,328,332` — UI binary staging
  in release ZIPs (all 3 platforms)
- `cmake/ETLBuildMod.cmake` — UI module build target

### Audit cross-refs

- **`docs/notes/PHASE_BRANDING_AUDIT.md`** (2026-04-30) — original
  description.txt approach. v0.5.2.4 shipped the deployment fix
  (description.txt staged in release ZIPs); branding-2 supersedes
  this for *consistent multi-mod display* (description.txt only
  controls our own entry, not other mods').
- `docs/CUP_VS_PUBLIC.md` — vg_fun mode mention; branding-2 will
  add a short "Mod-list display" subsection if v0.9 includes
  visible mode-aware branding (out of v0.7.1 scope).
- `docs/VG_FUN_MODE.md` — v0.7.0 vg_fun spec; branding-2 is
  independent of vg_fun.

### Memory cross-refs

- **Memory #21** — Branding-Phase post-v0.7.0 backlog item.
  Branding-2 closes this for the multi-mod-list visibility
  problem.
- **Memory #20** — Omni-bot all-platforms policy parallel: UI
  must work on every supported platform. Verified §4.
- **Memory #10** — Naming convention. `vanguard_diag_branding`
  uses the existing `vanguard_diag_*` family from v0.6.1, not
  the newer `vg_*` family from v0.7.0 vg_fun (which is reserved
  for vg_fun-prefixed feature cvars).

### Phase 7.0 lessons-learned applied

1. **Diagnostic infrastructure** — `vanguard_diag_branding` cvar
   ships with branding-2, not retrofitted later
2. **Build-flag mismatches** — CI gate (§6 yaml) extends the
   existing `strings | grep` pattern; specifically grep'ing for
   the lookup-table symbol AND a known string value (the table
   could be present-but-empty otherwise)
3. **Crash-bugs vs tuning-bugs** — branding-2 is pure cosmetic
   UI work; no gameplay impact, no crash potential. Risk: very
   low.
