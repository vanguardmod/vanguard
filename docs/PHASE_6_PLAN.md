# Phase 6 Plan — Multi-Box Hitbox System (Stufe 2)

Architektur-Plan für die Aktivierung der dormant ETLegacy/RtCW
multi-region Bone-Collision-Engine als VanguardMod-Pro-Feature.
Vorgelagerte Recherche: `docs/PHASE_6_AUDIT.md`. Skipt Stufe 1
(Number-Tuning) und geht direkt zu Stufe 2 (Wiring), weil Stufe 1
nur User-Befunde 1+3 löst, nicht 2/4/5/6 (siehe Audit Sektion 5.3).

**Target-Release:** v0.4.0
**Zeitrahmen:** 1.5–2 Wochen (4 Implementation-Phasen)
**Risiko-Profil:** mittel (Damage-Pfad-Touch, aber unterliegende
Engine-Pieces sind battle-tested aus RtCW-Lineage)

---

## Sektion 1 — Vision & Ziele

### 1.1 Was VanguardMod hier leistet

VanguardMod wird der **erste ETLegacy-Mod der die multi-region
Bone-Collision-Engine voll aktiviert und ins Damage-Modell
integriert.** Die Code-Stücke (mdx_hit_test, animScriptImpactPoint_t
mit 9 brauchbaren Regionen, .hit-File-Loader, Antilag-mit-Animation-State)
sind seit der RtCW/ETPro-Lineage in der Codebase, aber wurden
nie in den eigentlichen Combat-Pfad verdrahtet. Audit Sektion 2.1
zeigt: `mdx_hit_test` hat null Aufrufer im Damage-Code; die
`*.hit`-Files liegen nicht im etmain-Tree.

Phase 6 macht aus dieser dormant Infrastruktur ein **kompetitives
Feature**:

  - 9 Hit-Regionen statt 4 (HEAD/CHEST/GUT/GROIN/SHOULDER_L/R/
    KNEE_L/R/LEGS) — physiologisch sinnvoll, keine groben
    "ARMS"/"BODY"-Bucket
  - Bone-tracked Collision: die Box folgt der Animation, nicht
    der statischen Spieler-Origin
  - Per-Region Damage-Multiplier server-config-able
    (`vanguard_dmg_*`), Cup-Admins können fine-tunen ohne
    Recompile
  - Antilag bleibt korrekt — die Animation-State-History reicht
    bereits, MDX rechnet bones on-demand zur Trace-Zeit

### 1.2 User-Befund-Mapping

Bezug zu den 6 Live-Test-Befunden vom 27.04.:

| # | User-Befund | Stufe-1-Status | Stufe-2-Lösung |
|---|---|---|---|
| 1 | Box-Größe (40% Luft) | partiell (Tier 1 ±18→±16) | bone-tracked Capsules eng am Modell |
| 2 | Strafe-Lag | unverändert | bone-tracking folgt Render-Pose direkt |
| 3 | Crouch-Offset | partiell besser | per-region Capsules pro Pose |
| 4 | Prone bleibt vertikal | unverändert | Torso-Capsule rotiert mit Animation |
| 5 | Jump-Pose falsch | unverändert | per-Bone-Capsules folgen Beinen |
| 6 | AABB-Rotation-Limit | inhärent | Capsule-Primitives rotieren mit |

**Ergebnis:** Stufe 2 löst alle 6 Befunde architektonisch korrekt.

### 1.3 Story für Marketing / RELEASE_NOTES

> **VanguardMod v0.4.0 — Multi-Box Hitbox-System.** ETLegacy hat
> das ETPro/RtCW multi-region collision-system seit Jahren in der
> Codebase, aber unbenutzt: 10 Hit-Regionen, Bone-Tracking,
> Antilag-fähig — alle Engine-Pieces existieren, niemand hat sie
> je in den Damage-Pfad verdrahtet. VanguardMod aktiviert sie als
> Pro-Feature: Headshot bleibt ×2 Damage, aber Knee-Shots,
> Shoulder-Shots, Gut-Shots sind jetzt eigene Hit-Regions mit
> Per-Region-Multiplier, server-config-able für Cup-Admins. Die
> Box folgt jetzt der Animation, nicht dem Spieler-Origin —
> Strafing, Prone, Jump-Pose treffen alle bone-tracked, kein
> "Phantom-Hit"-Spielraum mehr. Heritage-activation nach 15
> Jahren.

### 1.4 Out of Scope (für spätere Phasen)

- Per-Klasse Hit-Region-Tuning (Heavy-Class breitere Capsules etc.)
  → Phase 7
- Pro-Pose .hit-Files (jump.hit, prone.hit zusätzlich zu base.hit)
  → Phase 8
- Cgame-Renderer mit echten bone-Positions (statt Approximate-Mode)
  → Phase 6.1 Polish (eigene Mini-Phase nach 6.0)
- Detailed per-region console-output für Tuning-Sessions
  → Phase 9

---

## Sektion 2 — Architektur

### 2.1 Subsystem-Layout

| Datei | Status | Zweck |
|---|---|---|
| `src/game/g_vanguard_hitbox.c` | NEU | vg_Hitbox_* subsystem — cvar lifecycle, .hit-load, damage-region-mapping, debug-print |
| `src/game/g_vanguard_hitbox.h` | NEU | public API: `vg_Hitbox_Init`, `vg_Hitbox_Shutdown`, `vg_Hitbox_TraceShot`, `vg_Hitbox_DamageMultiplierFor` |
| `src/game/g_vanguard.c` | EDIT | `vg_Hitbox_Init/Shutdown` aus G_InitGame / G_ShutdownGame triggern |
| `src/game/g_vanguard.h` | EDIT | `#include "g_vanguard_hitbox.h"` für die public-API-Forwards |
| `src/game/g_combat.c` | EDIT | G_Damage: gateway auf `vanguard_hitbox_mode`, ruft entweder legacy IsHeadShot/IsLegShot/IsArmShot ODER `vg_Hitbox_TraceShot` |
| `src/cgame/cg_vanguard_dev.c` | EDIT | Disclaimer-Banner ergänzen "hitboxes shown approximate, server uses bone-tracked collision" |
| `etmain/animations/human_base.hit` | NEU asset | 9-Region Hit-Definition-File. **Filename ist auto-discovery-bedingt:** `mdx_LoadHitsFile` (g_mdx.c:1331-1343) konstruiert den Pfad aus `characterDef.animationgroup` ("animations/human_base.anim"), ersetzt Extension durch ".hit". Alle 10 ETLegacy-Vanilla-Klassen teilen diesen animationGroup → ein File deckt alles. **Additive Asset** — Vanilla-ETLegacy hat keine `.hit`-Files, kein Override-Konflikt. |
| `docs/HITS_FORMAT.md` | NEU | Format-Spec aus mdx_LoadHitsFile rekonstruiert |
| `docs/HITBOX_SYSTEM.md` | NEU | User-facing Doku für Cup-Admins (Cvar-Tabelle, Tuning-Hints) |

VANGUARD-Marker in jeder geänderten `g_combat.c`-Stelle (analog zu
Phase 5.4 Vorgehen), kein Engine-Code-Refactor.

### 2.2 Cvar-Liste

**Master-Toggle:**

```
vanguard_hitbox_mode  default 1  CVAR_SERVERINFO | CVAR_LATCH
```

| Wert | Verhalten |
|---|---|
| 0 | Legacy: IsHeadShot/IsLegShot/IsArmShot Pfad (vanilla ETLegacy) |
| 1 | Multi-Box: 9-Region mdx_hit_test gegen `human_base.hit` |

CVAR_LATCH weil per-map-konsistent — mid-match Switch wäre verwirrend
für Spieler. CVAR_SERVERINFO damit cgame im Disclaimer-Banner den
Mode kommunizieren kann.

> **Note zu Mode 2 (entfernt):** ursprünglich als Capsule-Override
> geplant, aber das `.hit`-Format unterstützt KEIN dediziertes Capsule-
> Primitive (nur Sphere/Box/Cylinder/Box2 — siehe
> `docs/HITS_FORMAT.md` Sektion 4). Cylinder mit angemessen großen
> Endpunkten approximiert eine Capsule ausreichend für alle
> Phase-6-Ziele. Falls echte Capsule-Halbkugel-Endkappen später gewollt
> sind: eigene Phase 6.x mit `mdx_hit_test_capsule()`-C-Code-Erweiterung.

**Per-Region Damage-Multiplier:**

`animScriptImpactPoint_t` (`bg_public.h:2494-2508`) hat 9 brauchbare
Regionen — kein L/R-Split für Beine, nur ein einzelnes
`IMPACTPOINT_LEGS`. Ergibt **9 Damage-Cvars**:

| Cvar | Default | Bezug |
|---|---|---|
| `vanguard_dmg_head` | 2.0 | Headshot, behält Vanilla-×2-Behavior |
| `vanguard_dmg_chest` | 1.3 | Brust-Treffer, höhere Letality |
| `vanguard_dmg_gut` | 1.1 | Bauchbereich |
| `vanguard_dmg_groin` | 1.2 | unterer Torso |
| `vanguard_dmg_shoulder_l` | 0.8 | linke Schulter — Limb |
| `vanguard_dmg_shoulder_r` | 0.8 | rechte Schulter — Limb |
| `vanguard_dmg_knee_l` | 0.6 | linkes Knie — Limb extrem |
| `vanguard_dmg_knee_r` | 0.6 | rechtes Knie — Limb extrem |
| `vanguard_dmg_legs` | 0.7 | Beine (single, kein L/R-Split im Enum) |

Optional: `vanguard_dmg_default` (1.0) als Fallback für Hits auf
Hit-Areas mit `IMPACTPOINT_UNUSED` oder ohne explizit gesetzten
impactpoint.

Defaults sind **konservativ**: Sum-of-Body-Multipliers ≈ 7.7 ÷ 8 Regionen
≈ 0.96 average — knapp unter Vanilla-Niveau, ähnliche Gesamt-TTK.
Cup-Admins können fine-tunen für Pro-League-Spielgefühl.

CVAR_ARCHIVE damit Server-Operator sie persistent speichern können,
NICHT CVAR_LATCH (mid-match Damage-Tuning soll möglich bleiben für
Test-Sessions).

### 2.3 Damage-Pfad-Integration

**Konzeptioneller Sketch** (`src/game/g_combat.c` G_Damage Region):

```c
void G_Damage(gentity_t *targ, gentity_t *inflictor, gentity_t *attacker,
              vec3_t dir, vec3_t point, int damage, int dflags,
              meansOfDeath_t mod)
{
    // ... existing pre-damage logic, friendly-fire checks, etc ...

    /* VANGUARD: multi-box hit-detection branch. Mode 0 falls back to
     * the upstream IsHeadShot / IsLegShot / IsArmShot pipeline; modes
     * 1+ route through mdx_hit_test against human_base.hit and apply
     * per-region damage multipliers. */
    if (vanguard_hitbox_mode.integer >= 1 && targ->client && IsValidPlayer(targ))
    {
        animScriptImpactPoint_t impactpoint;
        int hit_type;
        vec_t fraction;
        grefEntity_t refent;

        mdx_gentity_to_grefEntity(targ, &refent,
            targ->timeShiftTime ? targ->timeShiftTime : level.time);

        if (mdx_hit_test(muzzle, point, targ, &refent, &hit_type,
                         &fraction, &impactpoint))
        {
            float multiplier = vg_Hitbox_DamageMultiplierFor(impactpoint);
            int   region    = vg_Hitbox_RegionFor(impactpoint);
            take = (int)(damage * multiplier);

            G_LogRegionHit(attacker, region);
            // headshot bookkeeping (EF_HEADSHOT, hit-event)
            if (impactpoint == IMPACTPOINT_HEAD)
            {
                targ->client->ps.eFlags |= EF_HEADSHOT;
                hr = HR_HEAD;
            }
        }
        else
        {
            /* Hit landed on the AABB but not on any defined hit-area —
             * treat as legacy body damage (no multiplier change). */
            take = damage;
            hr   = HR_BODY;
        }
    }
    else
    {
        /* Legacy path: existing IsHeadShot/IsLegShot/IsArmShot chain */
        if (IsHeadShot(...)) { ... }
        else if (IsLegShot(...)) { ... }
        else if (IsArmShot(...)) { ... }
    }

    // ... existing post-damage logic ...
}
```

**Was an der Antilag-Integration NICHT geändert werden muss:**
`G_HistoricalTrace` ist bereits vor G_Damage gelaufen — `targ`
befindet sich also schon in der historisch-rewindeten Pose, und
`targ->timeShiftTime` ist gesetzt (siehe Audit Sektion 3.3). Der
`mdx_gentity_to_grefEntity`-Aufruf liest exakt diese historischen
Animation-Frames, `mdx_calculate_bones` rekonstruiert die korrekten
bone-Positions zur Schuss-Zeit.

### 2.4 .hit-File-Spec (Vorab-Sketch)

> **Status:** vollständige Spec in `docs/HITS_FORMAT.md` (Phase 6.0
> Tag 1 ✅). Was hier folgt war der Vorab-Sketch — die echten Details
> stehen in der Spec-Doku.

Die exakte Spec wurde in **Phase 6.0 Tag 1 aus `mdx_LoadHitsFile()` Code
rekonstruiert** (siehe `docs/HITS_FORMAT.md`). Bekannte Struktur aus
`g_mdx.h:222`:

```c
struct hit_area {
    int     tag[2];          // 1-2 bone-tags als Anchor
                             //   1 tag = Sphere/Box around point
                             //   2 tags = Capsule/Box along axis
    vec3_t  axis[3];         // 3D rotation matrix für die Hit-Box
    float   scale[2][3];     // x/y/z scale per tag (radius o.ä.)
    qboolean isbox;          // false = cylinder, true = box
    qboolean ishead[2];      // tag is the head-bone variant
    animScriptImpactPoint_t impactpoint;  // welche der 9 brauchbaren Regionen
};
```

**Beispiel-Snippet** (illustrativ, exakte Syntax kommt aus 6.0):

```
// human_base.hit — VanguardMod hit-region definitions
// Path: etmain/animations/human_base.hit (auto-discovered via
// characterDef.animationgroup → see docs/HITS_FORMAT.md Sektion 2)
//
// One block per hit-area. The 9 useful animScriptImpactPoint_t
// regions can each have one or more blocks. Multiple blocks per
// region allowed (e.g. legs = left calf cylinder + right calf
// cylinder both mapped to IMPACTPOINT_LEGS).

hit {
    name        "head"
    impactpoint HEAD
    tag         "tag_head"
    primitive   sphere
    scale       6 6 6
}

hit {
    name        "chest"
    impactpoint CHEST
    tag1        "tag_chest"
    tag2        "tag_torso"
    primitive   box
    scale1      8 8 4
    scale2      8 8 4
    axis_yaw    0
}

hit {
    name        "knee_left"
    impactpoint KNEE_LEFT
    tag1        "tag_knee_left"
    tag2        "tag_foot_left"
    primitive   cylinder
    scale1      3 3 0
    scale2      3 3 0
}
```

Real-Format wird sich wahrscheinlich von obigem unterscheiden — das
Beispiel ist nur Illustration; Phase 6.0 produziert die echte Spec.

### 2.5 Sektion-2-Konklusion

**Architektur-Schwerpunkte:**
- Komplette Logik in **eigenem File** `g_vanguard_hitbox.{c,h}` →
  saubere Resync-Diff-Trennung, eine Stelle für Cvar-Lifecycle +
  Region-Mapping + .hit-Asset-Loading
- G_Damage-Touch ist **klein** (eine if-else-Branch, ~30 Zeilen) und
  **opt-in** (mode=0 fällt zurück) → Risk-controlled
- Antilag bleibt **unverändert** — alle History-Daten sind schon da

---

## Sektion 3 — Implementation-Phasen

### Phase 6.0 — Foundation (4-5 Tage)

**Ziel:** .hit-Format verstanden, `human_base.hit` geladen, Cvars
registriert. Noch kein Damage-Path-Touch.

**Tasks:**

1. **mdx_LoadHitsFile-Code-Reading** (g_mdx.c:1329) →
   `docs/HITS_FORMAT.md` mit:
   - Token-für-Token Format-Spec
   - Welche Tags sind erlaubt
   - Welche Primitives (box/box2/cylinder/sphere) und ihre Scale-Semantik
   - Wo .hit-Files gesucht werden (filesystem path)
   - Welche Animation-Groups die Lookup nutzen

   **Status:** ✅ erledigt am 2026-04-27 — `docs/HITS_FORMAT.md` (598
   Zeilen, 8 Sektionen) deckt alle Aspekte ab. Code-Reading hat
   drei Plan-Korrekturen produziert (Filename, Cvar-Count, Mode-2),
   die in einem separaten Commit nachgepflegt wurden.

1.5. **Bone-Namen-Discovery** — Pre-Requirement für Task 2.

   Code-Reading allein liefert nur die `tag_*`-Namen aus den
   cgame-Render-Aufrufen (tag_head, tag_torso, tag_chest, tag_back,
   tag_footleft, tag_footright). Die echten **Bone-Namen** im
   .mdx-Skeleton sind Binary-Format-Inhalt und nicht aus Source-
   Reading bestimmbar (siehe `docs/HITS_FORMAT.md` Sektion 5.2).

   - Temporärer Debug-Print in `mdx_load()` (g_mdx.c:1298 Pfad)
     der `mdx->bones[i].name` für alle bones loggt nach dem
     Parse — ggf. mit `i`, parent-index und `parent_dist` für
     Skeleton-Topologie-Verständnis
   - Test-Server einmal starten (lokales etlded reicht), Bone-Liste
     aus stdout/server-log capturen
   - Liste in einer Notiz-Datei (z.B. `/tmp/vg.bones.txt`) speichern
     für Task 2
   - **Debug-Patch DANACH ENTFERNEN — NICHT committen.** Der
     Bone-Dump ist ein Werkzeug, kein Feature.

   **Definition of Done für 1.5:**
   - [ ] Bone-Liste vorhanden (typisch 30-50 bones bei ETLegacy-
     human-skeleton)
   - [ ] Hierarchie sichtbar (parent-bone-Relationen)
   - [ ] Knee-/Shoulder-Bone-Kandidaten identifiziert (siehe
     HITS_FORMAT.md Sektion 5.3 Empfehlung)
   - [ ] Debug-Patch revertiert, working tree clean außer der
     externen Notiz

2. **`human_base.hit` hand-schreiben** mit den 9 Regionen
   (HEAD, CHEST, GUT, GROIN, SHOULDER_L/R, KNEE_L/R, LEGS).
   Bone-Tag-Choice basiert auf der Bone-Liste aus Task 1.5. Falls
   benötigte Tags nicht existieren (z.B. dedicated knee-bone):
   - Fallback auf nächst-passende Bones (Calf/Thigh-Mittelpunkt
     für Knee) mit manuell-tunbarem `scale`/`offset`
   - Bone-Position-Lookup direkt im g_mdx-Skeleton-Index (advanced
     wenn Calf/Thigh-Approach nicht reicht)

3. **g_vanguard_hitbox.{c,h} Skeleton:**
   ```c
   typedef struct {
       vmCvar_t modeCvar;
       vmCvar_t damageCvars[10];  // one per IMPACTPOINT_*
       qboolean hitsFileLoaded;
   } vg_hitbox_state_t;

   void vg_Hitbox_Init(void);
   void vg_Hitbox_Shutdown(void);
   float vg_Hitbox_DamageMultiplierFor(animScriptImpactPoint_t impactpoint);
   int vg_Hitbox_RegionFor(animScriptImpactPoint_t impactpoint);  // -> HR_*
   const char *vg_Hitbox_RegionName(animScriptImpactPoint_t impactpoint);
   ```

4. **Cvar-Registration:** `vanguard_hitbox_mode` plus 9
   damage-cvars (siehe Sektion 2.2 Tabelle) in `vg_Hitbox_Init`.
   CVAR_LATCH für mode, CVAR_ARCHIVE für damage-cvars.

5. **Hit-File-Load-Integration:** `mdx_LoadHitsFile` wird bereits
   automatisch von `g_character.c:246` aufgerufen sobald
   `FEATURE_SERVERMDX=ON` (default). VanguardMod muss **nichts
   selbst aufrufen** — es reicht das `human_base.hit`-File ans
   richtige etmain-Pfad zu legen. `vg_Hitbox_Init` printet zur
   Verifikation den `hits[]`-Pool-Status nach erstem Map-Spawn:
   "vg_Hitbox: loaded N hit-areas from human_base.hit" oder eine
   **LOUD WARNING** falls 0 (silent-fail vs file-not-found, siehe
   `docs/HITS_FORMAT.md` Sektion 6.1).

6. **Build + verify:** `cmake --build build`, Map-Spawn → Server-Log
   zeigt "vg_Hitbox: loaded ≥9 hit-areas".

**Definition of Done:**
- [x] `docs/HITS_FORMAT.md` existiert, beschreibt das Format
  vollständig mit Code-Referenzen auf mdx_LoadHitsFile
- [ ] Bone-Namen-Liste aus Task 1.5 vorhanden (für Task 2)
- [ ] `etmain/animations/human_base.hit` enthält 9+ Hit-Areas mit
  bone-Anchors für alle 9 IMPACTPOINT_*-Regionen außer UNUSED
- [ ] `g_vanguard_hitbox.{c,h}` kompiliert, exportiert die 4
  public Functions
- [ ] `vanguard_hitbox_mode 0/1` registriert, default 1
- [ ] 9× `vanguard_dmg_*` registriert mit Defaults aus Tabelle 2.2
- [ ] Map-Load zeigt "vg_Hitbox: loaded N hit-areas" im Server-Log
- [ ] LOUD WARNING fires wenn human_base.hit fehlt (negativ-Pfad
  manuell triggern: pk3-Inhalt vorübergehend ohne hit-File bauen)
- [ ] Damage-Path NOCH NICHT geändert (Sicherheits-Stufe)

**Estimate:** 4-5 Tage
- Tag 1: mdx_LoadHitsFile-Reading + HITS_FORMAT.md ✅
- Tag 1.5: Bone-Namen-Discovery via temporärem mdx_load Debug-Print
- Tag 2-3: human_base.hit schreiben + tunen (per-region scale/radius
  gegen Modell-Screenshots)
- Tag 4: g_vanguard_hitbox.* Skeleton + Cvars + Integration
- Tag 5: Buffer für Iteration (typischerweise scale-tuning nach
  ersten Live-Tests im Dev-Mode-Renderer)

### Phase 6.1 — Damage-Path Wiring (3-4 Tage)

**Ziel:** Multi-Box aktiv, Damage-Multiplier per Region, Antilag
funktioniert weiterhin korrekt.

**Tasks:**

1. **G_Damage erweitern** wie Sektion 2.3 skizziert. VANGUARD-
   Marker mit Verweis auf Plan + Audit. Legacy-Pfad (mode 0)
   bleibt EXAKT identisch zu vanilla.

2. **Stats-Tracking erweitern** in g_stats.c:
   - HR_NUM_HITREGIONS bleibt 4 (nicht refactor wegen Wire-Format
     für Debriefing)
   - 9 IMPACTPOINT-Regionen via `vg_Hitbox_RegionFor` auf 4
     HR_*-Klassen mappen:
     - HR_HEAD: HEAD
     - HR_BODY: CHEST, GUT, GROIN
     - HR_ARMS: SHOULDER_L, SHOULDER_R
     - HR_LEGS: KNEE_L, KNEE_R, LEGS

3. **Sanity-Test mit Bots auf Test-Server:**
   - vanguard_hitbox_mode 0: sniper-headshot stehender Bot →
     1-Shot-Kill (legacy verifiziert weiter funktional)
   - vanguard_hitbox_mode 1: gleiche Sequenz → 1-Shot-Kill
     (multi-box findet HEAD-region, ×2.0 multiplier)
   - mp40-bodyshot stehender Bot → ~6-8 Schüsse zum Kill
   - knee-shot-cvar `vanguard_dmg_knee_l 0.3` setzen →
     bot stirbt deutlich langsamer bei knee-only-Schuss
   - prone+strafe Tests: Multi-Box-Hits sollten korrekt
     registrieren (Befunde 4+5)

4. **Antilag-Regression-Check:** Bot-Test mit zwei Sniper-Bots
   vs einander für 30+ Minuten. Antilag muss weiterhin
   funktionieren — keine "trifft sich nicht obwohl direkt im
   Sichtfeld"-Regressions.

**Definition of Done:**
- [ ] G_Damage routet bei mode>=1 durch mdx_hit_test
- [ ] Damage-Multipliers werden angewandt
- [ ] Stats-Tracking zeigt korrekte HR_*-Mappings im Debriefing
- [ ] mode=0 Legacy-Pfad funktional unverändert (Bot-Test)
- [ ] mode=1 Multi-Box trifft Bots korrekt — sniper-headshot
  funktioniert weiterhin
- [ ] Per-Region damage-cvars wirken (sichtbar in Bot-Damage-Log)
- [ ] Antilag-30-min-Bot-Test ohne Regression
- [ ] Befunde 1, 2, 3, 4, 5, 6 alle visuell verbessert in
  Live-Bot-Test (User-Verifikation)

**Estimate:** 3-4 Tage
- Tag 1: G_Damage-Wire + region-mapping
- Tag 2: Stats-Mapping + Sanity-Tests
- Tag 3: Antilag-Regression-Test + Cvar-Tuning
- Tag 4: Buffer für Bug-Fixes

### Phase 6.2 — Polish + CPU-Profile (2-3 Tage)

**Ziel:** Render-Disclaimer, CPU-Mikro-Benchmark, User-Doku.

**Tasks:**

1. **Cgame-Disclaimer-Banner** in `cg_vanguard_dev.c`:
   ```c
   if (cgs.vanguardHitboxMode >= 1) {
       CG_DrawText(banner_x, banner_y, banner_size,
           "^3Hitboxes shown approximate. Server uses bone-tracked"
           " collision (vanguard_hitbox_mode 1).");
   }
   ```
   Text erscheint nur wenn dev-mode-overlay aktiv ist.

2. **Mikro-Benchmark `mdx_hit_test` CPU-Cost:**
   - Test-Harness: 1000 Calls in tight loop, mehrere
     Animation-Frames durchgehen, Wall-Clock-Zeit messen
   - Dokumentieren in `docs/HITBOX_PERF.md`
   - Zielwert: <0.5ms pro 1000 Calls (= sub-1µs pro Call)
   - Bei 32 Spielern × 40 Bullets/sec × 1µs = 1.3ms/sec = <1% CPU
   - Falls drüber: Bone-Snapshot-Cache pro Frame als Fallback
     (nicht implementieren, nur dokumentieren als Phase 6.x)

3. **`docs/HITBOX_SYSTEM.md`** für Cup-Admins:
   - Was Multi-Box macht (kurz)
   - Cvar-Tabelle mit Erklärungen
   - Tuning-Hints (z.B. "knee-multiplier <0.5 macht
     Maus-Aim-Belohnung sehr deutlich")
   - Bekannte Limitations (cgame-Approximate-Render,
     erst Phase 6.x)

4. **Live-Test mit echten Spielern:** 5+ Leute auf
   Test-Server für 30+ Minuten Free-for-All. Feedback
   sammeln zu:
   - Trifft-Gefühl vs vanilla
   - Headshots fühlen sich anders an?
   - Phantom-Hit-Rate (subjektiv)
   - Performance (FPS-stable?)

**Definition of Done:**
- [ ] Cgame-Disclaimer wird gezeigt im dev-mode wenn
  vanguard_hitbox_mode>=1
- [ ] `docs/HITBOX_PERF.md` mit Mikro-Benchmark-Ergebnis
- [ ] `docs/HITBOX_SYSTEM.md` für Cup-Admin-Audience
- [ ] Live-Test 30 min × 5+ Spieler ohne Game-Breaking-Bug
- [ ] CPU-Overhead unter 5% (gemessen, nicht angenommen)

**Estimate:** 2-3 Tage
- Tag 1: Disclaimer + HITBOX_SYSTEM.md
- Tag 2: Benchmark + HITBOX_PERF.md
- Tag 3: Live-Test + Feedback-Iterations

### Phase 6.3 — Release v0.4.0 (1 Tag)

**Ziel:** v0.4.0 ausliefern.

**Tasks:**

1. Version-Bump v0.3.X → v0.4.0 (Minor-Bump, signifikantes Feature)
   per `docs/RELEASE_PROCESS.md` Six-Spot-Verfahren
2. RELEASE_NOTES.md v0.4.0-Eintrag mit der "Heritage Activation"-
   Story aus Sektion 1.3
3. Multi-Plattform-Build, vanguard_v0.4.0.pk3 produzieren,
   alle 12 Module verifizieren
4. Live-Test final
5. Commits + Push

**Definition of Done:**
- [ ] v0.4.0.pk3 gebaut, alle 12 Modules mit v0.4.0-strings
- [ ] Live-Test final OK
- [ ] Release-Notes-Story geschrieben
- [ ] Commits per Variante-A-Pattern (feat / chore-bump split)

**Estimate:** 1 Tag

---

## Sektion 4 — Risk Assessment

| ID | Risk | Wahrscheinlichkeit | Impact | Mitigation |
|---|---|---|---|---|
| R1 | .hit-Format ist nicht klar dokumentiert in der Codebase | hoch | mittel | Phase 6.0 startet mit Code-Reading; Fallback: hand-schreiben basierend auf hit_area-Struct ohne Loader-File-Format (direkt programmatic load) |
| R2 | CPU-Overhead bei Multi-Box höher als 5% | mittel | mittel | Mikro-Benchmark in Phase 6.2 misst real; Bone-Snapshot-Cache als Phase-6.x-Optimization wenn nötig |
| R3 | Damage-Balance kaputt durch 10-Region-Multipliers | mittel | hoch | Defaults konservativ (Sum ≈ 8.6 ÷ 8 = 1.07 average, nahe Vanilla); Cup-Admin-Override über Cvars; Live-Test mit 5+ Spielern Phase 6.2 |
| R4 | Antilag-Bug bei Multi-Box-Replay | niedrig | hoch | Antilag-State ist bereits replay-fähig (Audit Sektion 3); 30-min-Bot-Test in Phase 6.1; bei jedem Bug sofort revert auf mode=0 |
| R5 | Render-Disclaimer verwirrt User ("warum sind Hitboxen ungefähr?") | mittel | niedrig | Klare Doku in HITBOX_SYSTEM.md; Phase 6.x-Polish für echte bone-Render |
| R6 | Per-Region-Damage als unfair empfunden in kompetitivem Spiel | mittel | hoch | Defaults-konservativ macht es nahe-Vanilla in TTK; Cup-Admins können einzelne Regionen auf 1.0 zurückstellen für strict-vanilla-feel |
| R7 | Bone-Tags fehlen für Knee/Shoulder im Vanilla-Player-Model | hoch | mittel | Phase 6.0 prüft Tag-Verfügbarkeit; Fallback: nähere Tags (tag_torso) mit manueller Offset-Calc, oder direkter Bone-Index-Zugriff über g_mdx-API |
| R8 | mdx_hit_test hat dormant-Bugs nach 15 Jahren ohne Live-Test | niedrig | hoch | Phase 6.1 mit Bots ist exact-detection; Phase 6.2 mit Spielern fängt erst-Live-Bugs |

**Gesamt-Risk-Profil:** mittel. Engine-Pieces sind battle-tested
aus RtCW-Lineage, aber 15 Jahre dormant ist eine reale Quelle
seltener Bugs. Phasing reduziert das per-Schritt-Risiko.

---

## Sektion 5 — Success Criteria

### 5.1 User-Befund-Lösungs-Matrix

| # | Befund | Messbar via | Erfolg |
|---|---|---|---|
| 1 | Box-Größe (40% Luft) | Visual: Live-Test-Screenshot vorher/nachher | bone-tracked Capsules sichtbar enger am Modell |
| 2 | Strafe-Lag | Visual: Strafe-Animation Hitbox-vs-Modell | Hitbox folgt Render-Pose innerhalb <2cm visuell |
| 3 | Crouch-Offset | Visual: Crouch-Pose Hitbox-Position | Hitbox-Center = Modell-Center pro Pose |
| 4 | Prone bleibt vertikal | Visual: Prone-Pose Hitbox-Layout | horizontale Capsule-Anordnung (Torso + Beine) statt vertikaler Stub |
| 5 | Jump-Pose falsch | Visual: Jump-Animation Hitbox | Beine ragen nicht aus Box raus |
| 6 | AABB-Rotation-Limit | Visual: diagonale Pose | Capsules rotieren mit |

### 5.2 Qualitäts-Metriken

| Metrik | Schwelle | Messung |
|---|---|---|
| CPU-Overhead bei Multi-Box | <5% bei 32 Spielern × 40 bullets/sec | Phase 6.2 Mikro-Benchmark |
| Hit-Detection-Genauigkeit | <2cm visuelle Abweichung Box vs Modell | Phase 6.2 Live-Test-Screenshots |
| Antilag-Stabilität | 30 min Bot-Test ohne missed-hits | Phase 6.1 Definition of Done |
| Live-Player-Test | 5+ Spieler × 30 min × 0 game-breaking bugs | Phase 6.2 Live-Test |
| Damage-Balance | TTK ≈ Vanilla ±15% | Phase 6.1+6.2 Bot/Live-Test mit Default-Cvars |

### 5.3 Akzeptanz-Bedingung für Release

v0.4.0 wird ausgeliefert wenn:
- [ ] Alle Definition-of-Done-Punkte aus Phasen 6.0/6.1/6.2/6.3 grün
- [ ] Befunde 1-6 visuell verbessert (User-Verifikation)
- [ ] Qualitäts-Metriken alle erreicht
- [ ] User-Live-Verifikation erfolgreich

---

## Sektion 6 — Open Questions / Future Work

### 6.1 Direkt nach Phase 6.0 zu klären

**Q1:** Welche bone-Tags hat das Vanilla-ETLegacy-Player-Model
tatsächlich? Wir wissen `tag_head`, `tag_torso`, `tag_chest`,
`tag_weapon`, `tag_footleft`, `tag_footright`. Für Knee und
Shoulder-spezifische Hit-Areas eventuell Fallback nötig.

**Q2:** Hat `mdx_LoadHitsFile` einen Pfad-Discovery-Mechanismus
(sucht in animations/ pro Anim-Group) oder muss der Pfad explizit
übergeben werden? Beeinflusst .hit-File-Placement.

**Q3:** Was ist `animationGroup` als Parameter zu mdx_LoadHitsFile?
Kommt das aus character.cfg? Müssen wir mehrere .hit-Files für
verschiedene Klassen schreiben oder reicht eines?

### 6.2 Future Phases

**Phase 6.x — Cgame-Bone-Render (Polish):**
- Echte bone-Position via cgame-side trap_R_LerpTag oder
  Server-Bone-Stream
- Disclaimer-Banner entfällt
- Cgame-Renderer und Damage-Trace zeigen exakt das Gleiche
- Schätzung: 3-4 Tage, eigene Phase

**Phase 7 — Per-Klasse-Hit-Region-Tuning:**
- Soldier-Class breitere Capsules (heavy-armor-Roleplay)
- Medic schmalere Capsules (agile-Roleplay)
- Cvar: `vanguard_dmg_<region>_<class>`
- Schätzung: 1 Woche

**Phase 8 — Pro-Pose .hit-Files:**
- jump.hit, prone.hit, crouch.hit zusätzlich zu base.hit
- Selection per `legsAnim`-Lookup zur Trace-Zeit
- Erlaubt sehr enge Pose-spezifische Capsules
- Schätzung: 1-2 Wochen

**Phase 9 — Hit-Visualization-Server:**
- Detailed per-region console-output ("HEAD hit by sniper, dmg
  98×2.0 = 196, kill")
- Replay-Tool zum Hit-Tuning
- Cup-Admin-Modus für Match-Dispute-Analysis
- Schätzung: 1 Woche

### 6.3 Cross-Reference

- Audit-Findings: `docs/PHASE_6_AUDIT.md`
- Release-Process: `docs/RELEASE_PROCESS.md`
- Bestehender Dev-Mode: `docs/DEV_MODE.md`
- Phase 5 Tier 1 (Number-Tuning, vorgelagert): `docs/RELEASE_NOTES.md` v0.2.0

---

*Plan-Ende. Phase 6.0 startet auf User-Yes — erste Aktion ist
mdx_LoadHitsFile-Code-Reading + docs/HITS_FORMAT.md.*


---

**Copyright Notice**

Copyright (c) 2026 wahke <info@wahke.lu> (https://wahke.lu)  
Copyright (c) 2026 VanguardMod Project Contributors

Licensed under GPL-3.0-or-later. Part of VanguardMod project.
Built on ETLegacy (https://www.etlegacy.com).
