# Phase 6 Audit — Multi-Box Hitbox System

Audit für die Phase-6-Architektur-Diskussion. Status der bestehenden
Hitbox-, Animation- und Antilag-Infrastruktur in der ETLegacy-Codebase
plus VanguardMod-Tier-1-Modifikationen. **Reine Recherche — kein
Code-Change.**

Zusammenhang: Live-Test-Screenshots vom 27.04. zeigen sechs Defizite
am aktuellen single-AABB-Tier-1-System (Box deutlich größer als
Modell, Strafe-Lag, Crouch-Offset, Prone bleibt vertikal, Jump-Pose
falsch, AABB-Rotation-Limit).

## TL;DR

> **Die Multi-Box-Engine existiert bereits in der ETLegacy-Codebase** —
> `mdx_hit_test()` in `src/game/g_mdx.c:2745` macht bone-tracked
> 10-Region-Collision-Tests mit box/box2/cylinder/sphere-Primitives.
> Sie ist aber **nicht in den Damage-Pfad verdrahtet**. Der Combat-Code
> nutzt simple `trap_Trace` gegen die AABB plus drei region-spezifische
> tempEntity-Helfer (`G_BuildHead`, `G_BuildLeg`, `IsArmShot`). Antilag
> hat **alle nötigen Animation-States** für historische Bone-Recomputation
> bereits gespeichert (40-Frame-Ringbuffer × 32 Clients ≈ 180 KB).
> Tier 2 ist also primär ein **Wiring-Job**, kein Engine-Neubau.

---

## Sektion 1 — Aktuelles Hitbox-System (g_client.c)

### 1.1 Tier-1 Bounds (`src/game/g_client.c:55-66`)

```c
/* VANGUARD: tighter XY footprint for competitive play.
 * Reduces phantom hits from overly generous bounding boxes.
 * Original ETLegacy values were +-18 (36x36 unit footprint),
 * reduced to +-16 (32x32, ~11% smaller). Z-axis unchanged
 * (-24/+48) to preserve crouch-jump physics and view-height
 * relationships. ... */
vec3_t playerMins = { -16, -16, -24 };
vec3_t playerMaxs = { 16, 16, 48 };
```

**Lebenszyklus**:
- Definition: g_client.c:65-66 (statische Globals)
- Kopiert in `ent->r.mins`/`r.maxs` bei Spawn: g_client.c:3271-3272
- Re-Copy nach Respawn / Stance-Reset: g_client.c:731-734 (corpse), :249-250 (spawn point trace)
- Server-Side `trap_Trace` Aufrufe: g_cmds.c:4262, :4268, :4274, :4280
- Antilag-Kopie: store via `G_StoreClientPosition` (g_antilag.c:125), restore via `G_AdjustSingleClientPosition`

**Bezug auf User-Befund 1 (40% Luft):** Standing-AABB ist `32 × 32 × 72` (Z 0..72 nach Origin-Add). Modell-Footprint typisch ~`22 × 22`. ±16 ist **immer noch ~45% breiter als das Modell**. Selbst Tier-1 ist großzügig.

### 1.2 Stance-spezifische Bounds

| Stance | XY-Footprint | maxs[2] (Top-Z) | Quelle |
|---|---|---|---|
| Standing | ±16 (Tier 1) | 48 | playerMins/Maxs |
| Crouching | ±16 (geerbt) | 24 (`CROUCH_BODYHEIGHT`) | bg_public.h:78, pmove sets r.maxs[2] |
| Prone | ±16 (geerbt) | 16 (`PRONE_BODYHEIGHT_BBOX`) | bg_public.h:89 |
| Dead | ±16 (geerbt) | 0 (`DEAD_BODYHEIGHT_BBOX`) | bg_public.h:90 |
| Prone legs (separate box) | ±13.5 | -14.4..-24 | bg_misc.c:84-85 (`playerlegsProneMins/Maxs`) |
| Prone head (separate box) | ±6 | -12..0 | bg_misc.c:87-88 (`playerHeadProneMins/Maxs`) |

**Bezug auf User-Befund 4 (Prone bleibt vertikal):** Bestätigt — die Standard-AABB rotiert NICHT. Statt Rotation gibt es separate Prone-Legs-Box + Prone-Head-Box, die **parallel** zum vertikalen Body-Stub gespannt werden. Aber: die separate Prone-Boxes werden nur in `IsHeadShot`/`IsLegShot`-Pfaden verwendet, nicht im Body-Damage-Trace. Der primäre Body-Hit-Trace prüft also weiterhin gegen die vertikale AABB mit reduzierter Höhe (16). Der liegende Torso passt nicht in einen 32×32×16 Z-Stub.

### 1.3 Hit-Detection-Logik (`src/game/g_combat.c`)

**Damage-Entry:**
```c
void G_Damage(gentity_t *targ, gentity_t *inflictor, gentity_t *attacker,
              vec3_t dir, vec3_t point, int damage, int dflags,
              meansOfDeath_t mod)  // line 1422
```

**Region-Tests (Reihenfolge im Damage-Code, g_combat.c:1696-1810):**

1. `IsHeadShot(targ, dir, point, mod, &refent, qtrue)` — line 1696. Baut tempEntity via `G_BuildHead`, dann `trap_Trace(start, NULL, NULL, end, ...)` (point-trace) gegen die tempEntity-AABB. Bei MDX-On (`g_realHead & REALHEAD_HEAD = 1`) wird die tempEntity an `mdx_head_position` (bone-getrackt) angeordnet.
2. `IsLegShot(targ, ...)` — line 1748. Nur falls `eFlags & EF_PRONE`. Baut `G_BuildLeg`-tempEntity, gleicher Trace-Mechanismus.
3. `IsArmShot(targ, attacker, point, mod)` — line 1760. Eigene Logic in g_combat.c:1319.
4. **Else (default body):** kein zusätzlicher Trace, das ursprüngliche `G_HistoricalTrace`-Result (gegen AABB von `r.mins/maxs`) zählt als Body-Hit.

**Headshot-Damage-Multiplikator (line 1701):** `take = MAX(50, take * 2)`. Headshot-Detection ist also direkt gameplay-relevant.

### 1.4 Bestehende Head-Hitbox

Die **rote Head-Box** in den User-Screenshots wird vom **VanguardMod-Dev-Renderer** gemalt (`cg_vanguard_dev.c`), nicht vom upstream-Engine. Sie ist eine clientseitige **Visualisierung** — die tatsächliche Damage-Hit-Box ist serverseitig und kommt aus `G_BuildHead`:

- **No-MDX-Fallback** (g_combat.c:980-1044): rect `{-6,-6,-2}` bis `{6,6,10}`, Position aus viewheight + viewangles + offsets. Cgame-Renderer ported genau diesen Pfad.
- **MDX-On** (g_combat.c:958-972): `mdx_head_position` returnt eine bone-getrackte Helm-Origin, dann fixed `{-6,-6,-6}` bis `{6,6,6}`. **Default in unserem Build**, weil `g_realHead = 1` und `REALHEAD_HEAD = 1`.

**Bezug auf User-Befund 6 (AABB-Rotation):** Die Head-Box rotiert ebenfalls nicht — auch im MDX-Pfad ist sie ein axis-aligned `12³`-Würfel um die bone-Position herum.

### 1.5 Sektion-1-Konklusion

**Wir wissen jetzt:**
- Tier 1 ist **eine** AABB für den Body, plus zwei optionale separate AABBs (Head, Prone-Legs)
- Damage-Trace nutzt simple `trap_Trace` ohne Multi-Region-Engine
- HR_HEAD/HR_ARMS/HR_BODY/HR_LEGS-Granularität existiert **nur für Stats**, nicht für unterschiedliche Damage-Multipliers (außer Head)
- Prone-Modell-Mismatch ist real: vertikaler `32×32×16`-Stub modelliert horizontalen Liegenden nicht ansatzweise

**Architektur-Implikation:**
- Multi-Box per Wiring an `mdx_hit_test` ist machbar ohne neue Damage-Path-Refactor
- Per-Stance-AABB-Tuning (Stufe 1) hilft Befund 1 (Größe), aber NICHT Befund 4 (Prone-Rotation) und nur teilweise Befund 5 (Jump). Echte Lösung dieser Befunde braucht bone-tracked Multi-Box.

---

## Sektion 2 — Animation/Skeleton-System (MDX)

### 2.1 ETLegacy MDX

**Files:**
- `src/game/g_mdx.h` (~380 Zeilen, public API)
- `src/game/g_mdx.c` (~3000 Zeilen, implementation)
- `src/game/g_mdx_lut.h` (lookup tables)

**Build-Status:** `FEATURE_SERVERMDX=ON` per default in `CMakeLists.txt:84`. **Aktiviert in unseren Builds** (verifiziert via `ETL_BUILD_FEATURES` String).

**Public API (Auszug aus g_mdx.h:339+):**

| Symbol | Zweck | Datei:Zeile |
|---|---|---|
| `mdx_PlayerAnimation(ent)` | Per-Frame Animation-Update; ruft `mdx_calculate_bones` | g_mdx.c:2484 |
| `mdx_calculate_bones_single(refent, i)` | Bone-Position-Berechnung für refent | g_mdx.c:1540 |
| `mdx_head_position(ent, refent, org)` | Bone-Position des Helms | g_mdx.c:2937 |
| `mdx_legs_position(ent, refent, org)` | Bone-Position der Beine | g_mdx.c:2997 |
| `mdx_tag_position(ent, refent, org, tagName, up_offset, forward_offset)` | Beliebiger Tag-anchored Position | g_mdx.c:2973 |
| `mdx_gentity_to_grefEntity(ent, refent, lerpTime)` | gentity → refent für mdx-internal use | g_mdx.c:294 |
| **`mdx_hit_test(start, end, ent, refent, *hit_type, *fraction, *impactpoint)`** | **Multi-Region Bone-Collision-Test** | **g_mdx.c:2745** |
| `mdx_LoadHitsFile(animationGroup, animModelInfo)` | Lädt Hit-Area-Defs | g_mdx.c:1329 |

**Aufruf-Chain in der Engine (heute):**
- `g_active.c:2392` ruft `mdx_PlayerAnimation(ent)` jeden Frame → bones immer aktuell
- `g_client.c:711` ruft `mdx_PlayerAnimation(body)` für Corpses
- `G_BuildHead`/`G_BuildLeg` rufen `mdx_head_position`/`mdx_legs_position` für Single-Region Headshot/Legshot
- `mdx_hit_test` selbst hat **NULL Aufrufer im Damage-Pfad** — Multi-Region-Engine ist dormant

### 2.2 Hit-Area-Datenstruktur

`g_mdx.h:222` `struct hit_area`:
- `tag[2]` (1-2 bone-tags die das Region anchoren — 1 für Sphere, 2 für Capsule/Box mit Achse)
- `axis[3]` (3D rotation matrix für die Hit-Box)
- `scale[2][3]` (per-tag x/y/z scale)
- `isbox` (bool: box vs cylinder)
- `ishead[2]` (bool: nutze Head-Tag-Variante per tag)

`g_mdx.h:315` `hit_t`:
- `hit_count`, `hits[]` — Array von hit_areas pro Animation-Group
- Loaded aus einem **".hits"-Datei pro animationGroup** via `mdx_LoadHitsFile`
- Wo: aktuell **kein .hits-File in unserem etmain-Tree** — damit ist der Multi-Region-Pfad zur Laufzeit leer (loadcheck `i >= hit_count → return qfalse` in mdx_hit_test:2771)

### 2.3 Hit-Region-Klassen (`bg_public.h:2494-2508`)

```c
typedef enum {
    IMPACTPOINT_UNUSED = 0,
    IMPACTPOINT_HEAD,
    IMPACTPOINT_CHEST,
    IMPACTPOINT_GUT,
    IMPACTPOINT_GROIN,
    IMPACTPOINT_SHOULDER_RIGHT,
    IMPACTPOINT_SHOULDER_LEFT,
    IMPACTPOINT_KNEE_RIGHT,
    IMPACTPOINT_KNEE_LEFT,
    IMPACTPOINT_LEGS,
    NUM_ANIM_COND_IMPACTPOINT
} animScriptImpactPoint_t;
```

**10 Regionen, full body.** Output von `mdx_hit_test` via `*impactpoint`-Parameter. Aktuell wird der Wert in der Animation-Pain-Logik verwendet (welche Treffer-Animation der Spieler abspielt) — **nicht** in der Damage-Berechnung.

### 2.4 Tag-System

Bestehende Tag-Refs im Spielermodell:
- `tag_weapon` / `tag_weapon2` — Waffe (cgame-side, cg_weapons.c)
- `tag_torso`, `tag_chest`, `tag_back` — Körper-Anchors (cg_effects.c)
- `tag_head` — Helm (mdx_head_position fallback path)
- `tag_footleft`, `tag_footright` — Füße (mdx_t struct member)

Plus die internen MDX-bones (siehe `mdx_calculate_bones`) — typischerweise 30-50 bones pro Player-Modell, je nach Animation-Skeleton.

### 2.5 Bone-Verfügbarkeit Server vs Client

| Wo | Status | Kommentar |
|---|---|---|
| Server (qagame.so) | **Voll vorhanden** mit FEATURE_SERVERMDX=ON | mdx_PlayerAnimation läuft jeden Frame, alle bones aktuell |
| Client (cgame.so) | Engine-side via refEntity_t / trap_R_LerpTag, mod-side aktuell **nicht genutzt** | `cg_vanguard_dev.c` ist der Visualisierer, der explizit den no-MDX-Fallback nutzt |

**Server-Authority für Multi-Box ist gegeben.** Der Server kennt zur Hit-Detection-Zeit alle bone-Positions des Targets. Antilag-rewind (siehe Sektion 3) kann diese auch historisch reproduzieren.

### 2.6 Sektion-2-Konklusion

> **🔄 Korrektur 2026-04-27 (Phase 6.0 Aktivierungs-Live-Test):** Die
> ursprüngliche Audit-Annahme hier — "dormant weil kein .hits-File
> geladen" und "Multi-Box-Aktivierung = .hits-File schreiben +
> G_Damage-Aufruf" — war **unvollständig**. Tatsächlich war die
> komplette `BONE_HITTESTS`-Pipeline upstream **compile-out**: der
> Define ist in `g_mdx.h:34-39` in einem `/* */` Block-Comment mit
> dem TODO "figured out how the fuck it works". Plus drei
> Pointer-/Argument-Order-Typos in `g_mdx.c` und ein
> CMake-Define-Sichtbarkeits-Problem für `q_math.c` blockierten den
> Build sobald BONE_HITTESTS aktiviert wurde. Aktivierung erforderte
> daher (alle in Phase 6.0):
>
> 1. Vier Upstream-Compile-Bugs fixen (commit dd14be1)
> 2. .hit-File mit korrektem Format schreiben (TAG-Bridge +
>    single-line, NICHT multi-line + bone-direkt wie ursprünglich
>    angenommen — siehe `docs/HITS_FORMAT.md` Q7)
> 3. `BONE_HITTESTS` via VanguardMod-Marker in `g_mdx.h` und
>    `target_compile_definitions(qagame ...)` in
>    `cmake/ETLBuildMod.cmake` aktivieren
>
> Wiring in `G_Damage` ist Phase 6.1 (separater Aufwand). Live-Verify:
> 10 hit-areas erfolgreich registriert (siehe
> `docs/notes/hitdump_2026-04-27.txt`).

**Wir wissen jetzt (aktualisiert):**
- MDX-Skeleton-System ist auf dem Server **voll funktional**, jede Frame frisch
- `mdx_hit_test` ist eine vollständige 10-Region-Multi-Primitive-Collision-Engine, **jetzt aktiv** (war upstream compile-out, Phase 6.0 hat aktiviert) — Aufruf aus G_Damage steht noch aus (Phase 6.1)
- Tag-Infrastruktur reicht für Anchor-Punkte aller Multi-Box-Regionen
- `.hit`-Format ist parser-verifiziert (TAG-Bridge + single-line)

**Architektur-Implikation (aktualisiert):**
- Multi-Box-Aktivierung erforderte: 4 Bug-Fixes + Format-Korrektur + Aktivierungs-Define + (noch ausstehend) Aufruf aus G_Damage
- Kein neues Render/Network-Pfad nötig — alles serverseitig im bestehenden Damage-Tick
- "Eigene" Hit-Region-Definition (z.B. neuer kleinerer Helm-Box, schmalerer Body) braucht jetzt **nur eine .hit-Datei** (mit TAG-Bridge + single-line Format), keinen C-Code

---

## Sektion 3 — Lag-Compensation (`src/game/g_antilag.c`)

### 3.1 Antilag-Architektur

**Datei:** `src/game/g_antilag.c` (1047 Zeilen)

**History-Buffer:** `clientMarker_t clientMarkers[MAX_CLIENT_MARKERS]` pro Client. `MAX_CLIENT_MARKERS = 40` (g_local.h:912). Bei `sv_fps 20` = **2 Sekunden** Geschichte.

**`clientMarker_t` Struktur (g_local.h:868-910):**

```c
typedef struct {
    vec3_t mins, maxs, origin;          // AABB + position
    int eFlags, viewheight, pm_flags;   // stance state
    vec3_t viewangles;
    int groundEntityNum;
    int time;                           // marker timestamp

    // Torso animation state (sufficient to recompute bones)
    qhandle_t torsoOldFrameModel, torsoFrameModel;
    int torsoOldFrame, torsoFrame;
    int torsoOldFrameTime, torsoFrameTime;
    float torsoYawAngle, torsoPitchAngle;
    int torsoYawing, torsoPitching;
    int torsoAnimationMovetype;

    // Legs animation state
    qhandle_t legsOldFrameModel, legsFrameModel;
    int legsOldFrame, legsFrame;
    int legsOldFrameTime, legsFrameTime;
    float legsYawAngle, legsPitchAngle;
    int legsYawing;
    qboolean legsPitching;
    int legsAnimationMovetype;
} clientMarker_t;
```

**Speicherung:** `G_StoreClientPosition(ent)` (g_antilag.c:125) bei jedem `ClientThink` Tick.

**Time-Shift:** `G_HistoricalTrace(...)` (g_antilag.c:761) ↔ `G_AdjustSingleClientPosition`. Alle Daten incl. animation-frame-state werden in `ent->r.mins/maxs/origin/...` und `ent->torsoFrame/legsFrame.*` zurückgeschrieben. Plus `ent->timeShiftTime = marker.time` (g_antilag.c:478) als Marker-Stempel.

### 3.2 Multi-Box-Memory-Footprint

**Aktuell** (single-AABB plus animation state):
- `clientMarker_t ≈ 140 bytes` (geschätzt: 3×vec3=36 + 4×int=16 + vec3=12 + int×3=12 + qhandle×4=16 + int×8=32 + float×4=16 ≈ 140)
- 40 markers × 140 bytes = **5.6 KB** pro Client
- 32 Clients × 5.6 KB = **~180 KB total** für Antilag-History

**Mit Multi-Box-Aktivierung (kein zusätzlicher Speicher!):**
- Bones werden **nicht persistiert** — sondern **on-demand neu berechnet** aus dem gespeicherten Animation-State
- `mdx_calculate_bones(refent)` wird beim Trace-Zeitpunkt aufgerufen, nutzt die aktuellen `ent->torsoFrame.*` und `ent->legsFrame.*` Felder (die durch `G_AdjustSingleClientPosition` schon historisch zurückgeschrieben wurden)
- Kosten: CPU statt RAM. Pro Trace 1× `mdx_calculate_bones` + N× `mdx_tag_orientation` für N hit-areas

**Fazit:** Bestehende clientMarker_t-Struktur **enthält bereits alle Daten** die `mdx_hit_test` zum Zeitpunkt eines Past-Frame-Traces braucht. Kein History-Buffer-Refactor.

### 3.3 Animation-Replay-Pfad

```
G_HistoricalTrace(start, mins, maxs, end, ...)         // antilag entry
  G_AdjustClientPositions(skip, time, qtrue)           // backwards in time
    G_AdjustSingleClientPosition(list, time)
      // restore from clientMarkers[j]:
      //   r.origin/mins/maxs <- marker
      //   torsoFrame.* <- marker
      //   legsFrame.* <- marker
      //   timeShiftTime <- marker.time
      trap_LinkEntity(ent)                             // re-position in BSP
  trap_Trace(...)                                       // hit-test against past pose
  G_AdjustClientPositions(skip, 0, qfalse)             // restore present
```

Für Multi-Box-Wiring im historischen Trace würde der innere `trap_Trace` durch (oder ergänzt um) `mdx_hit_test` ersetzt — die `gentity_t` ist bereits in der historischen Pose, `mdx_calculate_bones` errechnet die korrekten bone-Positionen aus dem geschriebenen `torsoFrame/legsFrame` State.

### 3.4 Sektion-3-Konklusion

**Wir wissen jetzt:**
- Antilag speichert genau die Animation-State-Variablen die MDX zum Bone-Recompute braucht (frame, oldFrame, frameTime, frameModel, yawAngle, pitchAngle, animationMovetype) — kein zusätzlicher Speicher für Multi-Box notwendig
- Multi-Box = **CPU-Cost** (mdx_calculate_bones pro Trace), kein **RAM-Cost** (Bone-Snapshots)
- Der `timeShiftTime`-Mechanismus (g_antilag.c:478, gelesen in `G_BuildHead`/`G_BuildLeg`/`mdx_gentity_to_grefEntity`) signalisiert MDX bereits korrekt "zeitverschobener Zustand"

**Architektur-Implikation:**
- Multi-Box ohne Memory-Blowup machbar — Antilag-Code bleibt unverändert
- Worst-Case CPU: ~30 bones × 32 clients × ~40 bullets/sec * 1 mdx_calculate_bones = überschaubar (<5% CPU)
- Bone-Snapshot-Variante (für CPU-Optimierung wenn nötig) wäre opt-in später, aber unnötig für ersten Wurf

---

## Sektion 4 — Existing Multi-Box-Mods und Inspiration

### 4.1 ETLegacy-Source-Status

**HITBOXBIT-Konstanten** (`bg_public.h:3081-3083`):
```c
#define HITBOXBIT_HEAD   1024
#define HITBOXBIT_LEGS   2048
#define HITBOXBIT_CLIENT 4096
```
Verwendet als Bit-Flags im Entity-Index für Visualisierungs-Pfad (`G_RailBox` in g_combat.c:1187/1273) und im upstream `cg_debugPlayerHitboxes`-Pfad (cg_players.c:3258/3269) — **nicht** im Damage-Path. Bilden die Basis für client-side Box-Render-Differenzierung, kein Hit-Test-Konzept.

**`g_realHead`-Cvar** (`g_cvars.c:646`):
```c
{ &g_realHead, "g_realHead", "1", 0, 0, qfalse, qfalse },
```
Bitmask-Cvar mit drei Flags:
| Bit | Konstante | Wirkung | Zustand |
|---|---|---|---|
| 1 | `REALHEAD_HEAD` (g_combat.c:938) | mdx-bone für Head-Position in IsHeadShot | **Default ON** |
| ? | `REALHEAD_LEGS` | (referenziert in g_combat.c:1082) | check needed |
| 128 | `REALHEAD_BONEHITS` (g_mdx.h:41) | Aktiviert mdx_hit_test im Damage-Pfad | **Default OFF** |
| 256/512/1024 | `REALHEAD_DEBUG_HEAD/LEGS/BODY` | Visualisierung | OFF |

→ **Schalter `g_realHead 128` würde Multi-Box-Hit-Test einschalten — wenn er gewired wäre.** Aktuell ist es nur eine Konstanten-Definition, kein Code-Pfad nutzt sie.

**Hinweis im Code (g_cvars.c:309):**
```c
vmCvar_t g_realHead; // b_realHead functionality from ETPro
```
Stammt aus ETPro (kompetitiver ET-Mod der frühen 2000er). ETLegacy hat das übernommen aber nur teilweise verdrahtet.

### 4.2 ETPro / RtCW SP / TCE Inspiration

**ETPro** (closed-source, RtCW Mod) hatte das `b_realHead`-System originally. Konzept:
- Body-AABB bleibt für Movement/Block-Tests
- Head-Hit nutzt bone-position des Helms
- Optional: Body-Multi-Region per bone (REALHEAD_BONEHITS) — selten in Public-Servern aktiviert wegen CPU-Cost zu der Zeit

**RtCW SP / RtCW Coop:**
- Verwenden gleichen MDX-Code (id Tech 3 Wolfenstein engine)
- `mdx_hit_test`-API ist 1:1 portiert von dort
- Hit-Areas-Files (`*.hits`) sind im RtCW-Tooling üblich

**Urban Terror / Quake Arena Mods:**
- Nutzen meist eigenes Q3-AABB-System mit Capsule-Hitboxen
- Andere Engine-Variante (kein direkter Code-Reuse)

**True Combat Elite (TCE):**
- Q3-basierter Hardcore-Tactical-Mod, Multi-Region-Hitboxen waren ein Markenzeichen
- Source nicht öffentlich aber Konzept dokumentiert: head/torso/limbs als separate trace-targets pro shot

### 4.3 ETLegacy-Issue-/PR-Hinweise

`grep -rn "hitbox\|headbox\|bodybox" src/` zeigt:
- HITBOXBIT_*-Verwendung beschränkt auf Visualisierungs-Pfade
- Keine PR-Spuren von "multi-region damage" in dieser Codebase
- `mdx_hit_test` wirkt wie zur Vollständigkeit-mit-importiert-aber-nicht-final-verdrahtet

### 4.4 Sektion-4-Konklusion

**Wir wissen jetzt:**
- Multi-Box ist konzeptionell von ETPro übernommen aber dort verstaubt
- Das MDX-Tooling kommt aus der RtCW-Lineage und ist field-tested (zumindest für Single-Player damage)
- VanguardMod wäre der erste konkurrierende ET-Mod der diesen Pfad **als kompetitives Feature** voll fertig macht

**Architektur-Implikation:**
- Wir können `g_realHead`-Bitmask als bestehende Schalter-Infrastruktur nutzen — `REALHEAD_BONEHITS = 128` als der "Multi-Box ON"-Modus, plus eigenes Vanguard-Cvar für Tuning-Profile
- Bei der `*.hits`-Datei: existing RtCW-Tooling nicht direkt nutzbar (anderes Modell-Format vermutlich), aber Format-Spec ist klar genug zum Hand-Schreiben

---

## Sektion 5 — VanguardMod Tier-1-Status

### 5.1 `src/game/g_vanguard.{c,h}` — Server-Side

**g_vanguard.c** ist aktuell **nur das Dev-Mode-Subsystem** (vanguard_dev cvar, sv_cheats lifecycle, public-server-banner). Keine Hitbox-Logik darin.

**g_vanguard.h** reserviert die Namespace-Präfixe:
```c
/*
 * Subsystem prefixes in use / reserved:
 *   vg_DevMode_*    server-controlled developer/debug visualisation
 *   vg_Hitbox_*     (future) hitbox geometry tuning
 *   vg_Movement_*   (future) movement / strafe behaviour tweaks
 */
```

→ **`vg_Hitbox_*`-Namespace existiert als Reservation, ist aber nicht implementiert.** Sauberer Anker für Phase-6-Code.

### 5.2 `src/cgame/cg_vanguard_dev.{c,h}` — Client-Renderer

Aus Phase 5.1 Refactor:
- Single Entry: `CG_VanguardDev_DrawHitboxes()` (cg_view.c hook nach `CG_AddPacketEntities`)
- Rendert pro Spieler im Sicht-Feld **drei** axis-aligned Boxes (body / head / legs-when-prone) via `CG_AddLineToScene` (12 Edges/Box).
- **Verwendet absichtlich die no-MDX-Fallback-Math** für Head/Legs, weil cgame keinen einfachen Bone-Lookup für andere Spieler hat (nur Local-Player über `cg.predictedPlayerState`).
- Body: AABB aus `cent->lerpOrigin` + `vg_BodyMins/Maxs` (gespiegelt aus playerMins/Maxs).
- Head: Math-Port von G_BuildHead's Fallback-Pfad (no-MDX).
- Legs: Math-Port von G_BuildLeg's Fallback-Pfad (no-MDX, prone only).

**Bekannte Limitation aus Phase 5.6 docs/DEV_MODE.md:**
> Head box is an approximation, not the damage trace. The dev-mode renderer draws the head box from G_BuildHead's no-MDX fallback math. The server's actual headshot trace runs through mdx_head_position whenever FEATURE_SERVERMDX=ON.

→ Der Renderer **lügt schon jetzt** über die Head-Box-Position auf Servern mit MDX. Bei Multi-Box-Wiring muss der Renderer aktualisiert werden, sonst sind die User-Screenshots dauerhaft inkorrekt.

### 5.3 Tier 1 Bounds — Vanilla vs Tier 1 vs angestrebtes Tuning

| Stance | Vanilla ETLegacy | Tier 1 (VanguardMod aktuell) | Modell-Footprint (geschätzt) | Optimal (User-Befund) |
|---|---|---|---|---|
| Standing XY | ±18 (36×36) | **±16 (32×32)** | ~22×22 | ±12-13 (24-26 wide) |
| Standing Z | -24 / +48 (72) | -24 / +48 (72) | ~70 | ~70 (passt) |
| Crouching top-Z | 24 | 24 (geerbt) | ~50 | ~45 (etwas zu hoch laut User-Befund 3) |
| Prone (vertikal-AABB-Stub) | 16 | 16 (geerbt) | flach ~20 hoch, ~70 lang | **Brauch echte horizontale Box** |
| Prone-Legs (separat) | ±13.5 / -24..-14.4 | ±13.5 (geerbt) | passt grob | ok |
| Prone-Head (separat) | ±6 / -12..0 | ±6 (geerbt) | passt | ok |

**Bezug auf alle 6 User-Befunde:**
| # | Befund | Was Tier 1 leistet | Was Multi-Box (Stufe 2/3) leistet |
|---|---|---|---|
| 1 | Box zu groß (40% Luft) | Tier 1 schon 11% kleiner als vanilla, aber immer noch ~45% breiter als Modell | Multi-Box mit pro-Stance Capsule + bone-tracking → eng am Modell |
| 2 | Strafe-Lag | Animation-Desync, single-AABB folgt Server-Origin → halt nicht Modell-Render-Position | Multi-Box mit bone-tracking folgt Render-Pose, weil Antilag bone-state historisch speichert → kein Lag |
| 3 | Crouch-Offset | Tier 1: Z stimmt grob, Position bei manchen Frames falsch | Bone-tracked Torso/Legs-Capsule fixt das |
| 4 | Prone bleibt vertikal | **Tier 1 fixt das nicht** — Stub-AABB bleibt | Multi-Box: Torso-Capsule rotiert mit Animation (bone-anchored) |
| 5 | Jump-Pose falsch | Single-AABB bleibt vertikal-zentriert, Beine ragen unten raus | Per-Bone-Capsule folgt Beine-Animation |
| 6 | AABB-Rotation-Limit | Inhärent — AABB rotiert nie | Capsule oder bone-anchored Box rotiert mit |

→ **Tier 1 (XY ±16) hat Befunde 1+3 partiell verbessert; Befunde 2/4/5/6 sind architektur-gebunden und brauchen Multi-Box.**

### 5.4 Sektion-5-Konklusion

**Wir wissen jetzt:**
- Tier 1 ist ein Number-Tweak ohne System-Refactor — was es nicht lösen kann (Rotation, Bone-Tracking, Per-Pose-Geometrie) liegt außerhalb seines Scope
- VanguardMod hat bereits saubere Namespace-Präparation (`vg_Hitbox_*`) für ein Multi-Box-Subsystem
- cg_vanguard_dev.c muss bei Multi-Box-Aktivierung aktualisiert werden (Bone-Position-Lookup für korrekten Render, oder explizite Doku dass Renderer = Approximation, Damage = Source-of-Truth)

**Architektur-Implikation:**
- Tier 2 = Multi-Box-Wiring (mdx_hit_test in G_Damage, .hits-File schreiben, vg_Hitbox_* subsystem als Container)
- cg_vanguard_dev.c wird in Tier 2 entweder erweitert (echte bone-Position-Render) oder explizit als "Visualisierung der Body-Hull" gelabelt mit getrennter "Diagnostic"-Box-Render-Mode

---

## Gesamt-Konklusion: 3-Stufen-Konzept

### Stufe 1 — Number-Tuning (1-3 Tage, niedrig Risiko)

**Was:** Pro-Stance-AABB-Tuning. Stehender XY auf ±13, Crouch-Top-Z von 24 auf 20, Prone-Stub-Z von 16 auf 20 (mit re-tuning der prone-Legs-Box-Position um Lücken zu schließen).

**Files:** `g_client.c` (playerMins/Maxs als state-array statt globals), `bg_misc.c` (prone-bounds), `bg_pmove.c` (CROUCH_BODYHEIGHT optional), neuer kleiner Code in `g_vanguard.c` für vg_Hitbox-tuning-cvars.

**Adresse User-Befunde:** 1 (besser), 3 (besser), 4 partiell (Prone-Lücken kleiner aber nicht horizontal). 2/5/6 unverändert.

**Risiko:** Niedrig wenn man nur Numbers ändert. Antilag erbt automatisch (siehe Phase 5 Tier 1 Erfahrung).

### Stufe 2 — Multi-Box-Wiring (1-2 Wochen, mittel Risiko)

**Was:**
1. Eigenes `vanguard.hits`-File schreiben (10 Hit-Areas mit bone-tag Anchors für Head/Chest/Gut/Groin/SHL/SHR/KNL/KNR/LegsL/LegsR), via `mdx_LoadHitsFile` laden
2. In G_Damage / G_LocationalDamage die `IsHeadShot`/`IsLegShot`/`IsArmShot`-Aufrufe ersetzen durch ein einzelnes `mdx_hit_test`-Call das einen `animScriptImpactPoint_t` returnt; mappen auf Damage-Multiplier-Tabelle
3. `vg_Hitbox_*`-Subsystem in g_vanguard.{c,h} mit Cvars (`vanguard_hitbox_mode 0/1/2/3` für off/legacy/multi-box/multi-box+capsule)
4. cg_vanguard_dev.c entweder erweitern (mdx-bones via cgame-side trap_R_LerpTag oder via separate transmitted bone-points) oder explizit Approximate-Mode behalten + Multi-Box-Diagnostic-Mode hinzufügen

**Adresse User-Befunde:** 1, 2, 3, 5, 6 alle gefixt durch bone-tracked capsules. Befund 4 wenn Capsule rotation-anchored ist.

**Risiko:** Mittel. Damage-Pfad-Änderung braucht extensive Playtest. Antilag-Compatibility ist gesichert durch existierende Animation-State-Replication. Hit-Detection-Performance je nach Animation-Skeleton-Size; CPU-Profile vorher messen.

### Stufe 3 — Vollausbau (2-4 Wochen)

**Was:**
- Per-Klasse Hit-Region-Tuning (Heavy = leicht größere Capsules etc.)
- Capsule-statt-Box-Primitives wo's klar besser passt (Beine, Arme); Box behalten für Torso
- Pro-Pose Tuning-File (jump.hits, prone.hits ergänzend zu base.hits) mit Selection per `legsAnim`
- Multi-Region-Damage-Multipliers (nicht nur Head 2× — Chest 1.2×, Limb 0.7× etc.) — gameplay-feature
- cg_vanguard_dev.c voll integriert mit Server-Bone-Stream oder cgame-side MDX-Compute
- "Hit-Visualisierungs-Server"-Modus mit detaillierten Per-Region-Hits in der Console für Tuning-Sessions

**Adresse:** Alle User-Befunde plus Spielerbalance-Tuning ist möglich.

**Risiko:** Hoch, weil Game-Balance-Impact. Braucht echte Playtest-Sessions mit mehreren Spielern, nicht Bot-Dummys.

### Empfehlung für die Architektur-Diskussion

- **Stufe 1 als Quick-Win** ist OK wenn wir Befunde 1+3 schnell adressieren wollen, aber löst die fundamentalen Probleme (Befunde 2/4/5/6) nicht.
- **Stufe 2 ist die richtige Investition** — die Engine-Pieces existieren, das ist primär Wiring + ein Asset-File. ROI ist hoch (5 von 6 Befunden gelöst, kompetitive Differenzierung von vanilla ETLegacy). Antilag-RAM-Footprint-Risk ist nicht real (siehe Sektion 3). CPU-Profil sollte aber gemessen werden, kein blinder Vertrauensvorschuss.
- **Stufe 3 ist Future-Work** — erst nach Stufe-2-Live-Erfahrung sinnvoll, dann gezielt einzelne Sub-Features herausziehen.

### Offene Fragen vor Plan-Yes

1. **`*.hits`-Format**: Existiert eine Spec im upstream? Wenn nicht, müssen wir es per `mdx_LoadHitsFile`-Reverse-Engineering rekonstruieren. → **Action**: bei Stufe-2-Plan-Phase mdx_LoadHitsFile-Code lesen.
2. **Antilag CPU-Cost-Profil**: Wir vermuten <5% CPU-Overhead — aber ungemessen. → **Action**: vor produktivem Stufe-2-Live-Einsatz Mikro-Benchmark schreiben (1000 mdx_hit_test calls in tight loop).
3. **Cgame-Visualisierungs-Strategie**: Renderer-erweitern oder Approximate-Mode-explizit-belassen? Beides okay, aber muss Phase-6-Plan vorgeben.
4. **`g_realHead` vs `vanguard_hitbox_mode`**: Reuse upstream cvar-Bitmask oder eigenes Vanguard-Cvar? Empfehlung: Eigenes Vanguard-Cvar zu Saubere-Resync-Diff, der Schalter funktional unter Vanguard-Kontrolle.
5. **Damage-Tabelle**: Aktuell nur Head ×2 Multiplier. Bei 10-Region-Output: lassen wir 9 davon als "body" + 1 "head" zusammenfassen, oder differenzieren wir? Erste Iteration: alle 10 Regionen ↔ 4 alte HR_*-Klassen mappen (HEAD, ARMS=Shoulders, BODY=Chest/Gut/Groin, LEGS=Knees/Legs).

---

*Audit-Ende. Nächster Schritt: Architektur-Plan-Diskussion basierend auf den Findings dieser Sektionen.*
