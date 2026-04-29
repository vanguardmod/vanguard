# `.hit` File Format

Format-Spec für die per-bone collision-test description files, rekonstruiert
aus `src/game/g_mdx.c` Loader-Code (`hit_load`, `hit_parse_tag`, `hit_parse_hit`).
Zweck: Datengrundlage zum Hand-Schreiben von `vanguard`'s eigener `.hit`-Datei
in Phase 6.0 Tag 2.

Diese Spec ist code-getrieben — jede Behauptung ist mit `g_mdx.c:<line>` belegt.

> **Korrekturen ggü. PHASE_6_PLAN.md** (Code-Reading hat zwei Annahmen
> widerlegt, die der Plan vorab gemacht hatte):
>
> 1. **Datei-Extension ist `.hit`, nicht `.hits`** (g_mdx.c:1337-1341 schreibt
>    `Q_strncpyz(sep, ".hit", ...)`). Plan-Sektion 2.1 sagte
>    `etmain/animations/vanguard.hits` — korrekt wäre `.hit` (singular).
> 2. **Es gibt 9 brauchbare Impact-Point-Regionen, nicht 10**
>    (`animScriptImpactPoint_t` enthält IMPACTPOINT_UNUSED + 9 echte:
>    HEAD, CHEST, GUT, GROIN, SHOULDER_RIGHT, SHOULDER_LEFT, KNEE_RIGHT,
>    KNEE_LEFT, LEGS). Plan-Sektion 2.2 listete `vanguard_dmg_legs_l` und
>    `vanguard_dmg_legs_r` separat — das Enum kennt aber nur ein
>    `IMPACTPOINT_LEGS` ohne L/R-Split.
>
> Beide Punkte müssen in `PHASE_6_PLAN.md` korrigiert werden bevor
> Phase 6.0 Tag 2 startet (vanguard.hit schreiben). Vorschlag steht in
> Sektion 8 dieses Doks.

---

## Sektion 1 — Übersicht

Eine `.hit`-Datei beschreibt **per-bone Hit-Areas** für ein Player-Modell.
Jede Hit-Area ist ein collision-primitive (sphere/cylinder/box) das an einen
oder zwei Bones im Skeleton gebunden ist und mit einem Hit-Type
(MDX_HEAD/MDX_TORSO/MDX_ARM_L/...) und einem Impact-Point
(IMPACTPOINT_HEAD/CHEST/GUT/...) markiert ist.

Wenn ein Schuss-Trace-Pfad mit `mdx_hit_test()` (g_mdx.c:2745) gegen einen
Spieler getestet wird, iteriert der Code durch alle Hit-Areas der
zugehörigen `.hit`-Datei und gibt das nächst-getroffene zurück.

**Zwei Klassifikations-Achsen pro Hit-Area:**

1. `hit_type` — gröbere Kategorie für Animation-Pain-Logik.
   8 Werte (g_mdx.h:365-376, names in g_mdx.c:39-49):
   `none`, `gun`, `head`, `body`, `arm_L`, `arm_R`, `leg_L`, `leg_R`
2. `impactpoint` — feingranulare Region für Damage-Multiplier.
   9 brauchbare Werte (`animScriptImpactPoint_t` in bg_public.h:2494-2508):
   `IMPACTPOINT_HEAD`, `_CHEST`, `_GUT`, `_GROIN`,
   `_SHOULDER_RIGHT`, `_SHOULDER_LEFT`, `_KNEE_RIGHT`, `_KNEE_LEFT`,
   `_LEGS`. Plus `_UNUSED` als Default/Bail-Out.

**Das Mapping ist 1-zu-N**: ein `hit_type body` kann mehrere
Hit-Areas mit unterschiedlichen `impactpoint`s haben (CHEST + GUT + GROIN
beispielsweise). Mehrere Areas pro Region sind explicit erlaubt.

---

## Sektion 2 — Datei-Discovery

**Pfad-Konstruktion** (g_mdx.c:1331-1343, in `mdx_LoadHitsFile`):

```c
char hitsfile[MAX_QPATH], *sep;
Q_strncpyz(hitsfile, animationGroup, sizeof(hitsfile) - 4);
if ((sep = strrchr(hitsfile, '.')))
    Q_strncpyz(sep, ".hit", sizeof(hitsfile) - (sep - sizeof(hitsfile)));
else
    Q_strcat(hitsfile, ".hit", sizeof(hitsfile));
mdx_RegisterHits(animModelInfo, hitsfile);
```

Der `animationGroup`-Parameter ist der Wert aus dem `.char`-File des
Player-Models, Feld `animationgroup` (siehe
`etmain/characters/temperate/allied/soldier.char`):

```
animationgroup    "animations/human_base.anim"
```

Der Algorithmus ersetzt die letzte Datei-Extension durch `.hit`. Aus
`animations/human_base.anim` wird `animations/human_base.hit`. Falls
keine Extension da: `.hit` wird angehängt.

→ **Für VanguardMod relevant**: alle ETLegacy-Vanilla-Player (Soldier,
Medic, Engineer, Field-Ops, Cvops bei beiden Teams) verwenden den
gleichen `animationgroup` `"animations/human_base.anim"`. **Eine einzige
`.hit`-Datei `etmain/animations/human_base.hit` deckt alle Klassen und
Teams ab**.

**Aufruf-Zeitpunkt** (g_character.c:246):

```c
if (!G_CheckForExistingAnimModelInfo(...)) {
    if (!G_ParseAnimationFiles(...)) {
        return qfalse;
    }
#ifdef FEATURE_SERVERMDX
    mdx_LoadHitsFile(characterDef.animationGroup, character->animModelInfo);
#endif
}
```

Pro Animation-Group genau einmal (`G_CheckForExistingAnimModelInfo`
macht den Dedup gegen die globale `animModelInfoPool`). Da alle ETLegacy
Klassen den gleichen `animationgroup` teilen, **wird die `.hit`-Datei
beim Spawn der ersten Klasse einmal geparst und für alle weiteren
Klassen aus dem `hits[]`-Array wiederverwendet**.

**Aktivierungs-Bedingungen:**
- `BONE_HITTESTS` muss definiert sein. Upstream hat den Define **in
  einem `/* */`-Block-Comment auskommentiert** (`g_mdx.h:34-39` mit
  TODO "figured out how the fuck it works") — die ganze Pipeline ist
  upstream compile-out. VanguardMod aktiviert ihn unconditionally
  (Phase 6.0) via zwei Einträge:
    - `g_mdx.h`: VANGUARDMOD-Block oberhalb des Upstream-Comments
      mit `#ifndef BONE_HITTESTS / #define BONE_HITTESTS 1 / #endif`
    - `cmake/ETLBuildMod.cmake`: `target_compile_definitions(qagame
      PRIVATE BONE_HITTESTS=1)` — propagiert den Define zu allen
      qagame-Compile-Units inkl. `q_math.c` (das `quat_from_axis`
      enthält, ebenfalls auf BONE_HITTESTS gegated, aber `q_math.c`
      inkludiert `g_mdx.h` nicht).
- Vier Upstream-Compile-Bugs blockierten zusätzlich die Aktivierung;
  alle vier sind in einem separaten Commit gefixt (g_mdx.c
  pointer-arithmetik typo, Q_strcat arg-order, ETLEGACY_DEBUG-gating
  von `legacy_AddDebugLine`, sowie der CMake-Define-Propagation für
  q_math.c). Siehe `git log --grep BONE_HITTESTS`.
- `FEATURE_SERVERMDX` muss enabled sein (ist es, default-on per
  CMakeLists.txt:84).

---

## Sektion 3 — Token-für-Token Syntax

### 3.0 — Single-Line Constraint (parser-verified)

**Alle** Argument-Token-Reads in `hit_parse_tag` (g_mdx.c:740-907)
und `hit_parse_hit` (g_mdx.c:917-1165) verwenden
`COM_ParseExt(ptr, qfalse)` — das `qfalse` ist
`allowLineBreaks=false`, der Tokenizer **stoppt am Newline**. Der
äußere Loader-Loop (`hit_load`, g_mdx.c:1207-1235) nutzt dagegen
`COM_Parse(&ptr)` welches Newlines konsumiert.

**Konsequenz:** TAG- und HIT-Blöcke müssen **jeweils auf einer einzigen
Zeile** stehen. Sobald der innere Parser an ein Newline kommt, gibt er
das leere Token zurück, der innere Loop bricht ab, und der äußere
Loop liest das nächste Token als Top-Level — was bei einem
Argument-Wort wie `radius` als "Unexpected token" Parse-Error endet.

Comments zwischen Blöcken (`//` und `/* */`) sind OK — der Tokenizer
überspringt sie als Whitespace.

**Beispiel — FALSCH (multi-line):**

```
HIT head _vg_head
    radius 6
    impactpoint head
```
→ Output: `Unexpected token: radius`. Block bricht ab nach
`_vg_head`, äußerer Loop sieht `radius` als unbekanntes Top-Level-Keyword.

**Beispiel — KORREKT (single-line):**

```
HIT head _vg_head radius 6 impactpoint head
```

Diese Erkenntnis ist live verifiziert via VG_HITDUMP-Test
(2026-04-27). Vor der Verifikation wurde im Doc fälschlich von
multi-line-Blocks ausgegangen; siehe Sektion 8 Q7 für den
Korrektur-Pfad.

### 3.1 — Allgemeine Tokenizer-Regeln

Der Parser nutzt `COM_Parse` / `COM_ParseExt` aus `q_shared.c` — der
Standard-Q3-Token-Parser. Heißt:

- **Whitespace-getrennt** (Tabs, Leerzeichen, Newlines äquivalent)
- **Kommentare:** `//` bis Zeilenende, `/* ... */` block-comments (Q3-Standard)
- **Strings in Quotes** für Pfade mit Leerzeichen, sonst Bare-Tokens
- **Bone-Namen mit Leerzeichen MÜSSEN gequoted werden.** Das
  Standard-3DS-Max-Biped-Skeleton verwendet `Bip01`-Namen mit
  Leerzeichen (z.B. `Bip01 L Calf`, `Bip01 Head`). Der COM_Parse-
  Tokenizer (q_shared.c:877-907) behandelt double-quoted strings
  als atomares Token und entfernt die Quotes vor der Auslieferung
  ans Caller-Code. Ohne Quotes würde der Parser `Bip01` und `L`
  und `Calf` als drei separate Tokens sehen → "Unexpected token"
  Parse-Error. Verifiziert anhand des body.mdx-Bone-Dumps
  (Phase 6.0 Tag 1.5, 53 bones, alle mit Spaces).
- **Case-insensitive** Keyword-Matching durchgängig (`Q_stricmp`
  everywhere) — aber **case-sensitive Bone-Lookup** (siehe
  Sektion 5.1).

**Top-Level (g_mdx.c:1203-1230)**: zwei Block-Keywords akzeptiert:

```
TAG <name> <bone> [<weight|offset|axis|headangles|<bone>>...]
HIT <hit_type> [<tag/bone>] [scale|radius|axis|impactpoint|box|headangles ...]
```

Alles andere → `Unexpected token` Parse-Error, Datei wird abgebrochen,
`hit_count = 0`. Kein partielles Laden.

### 3.2 `TAG`-Block (g_mdx.c:736-904)

Definiert einen **internal tag** (zusätzlicher logischer Anchor-Punkt
relativ zum Skeleton, nutzbar in `HIT`-Blöcken statt eines echten
Bone-Namens). Optional, nur nötig wenn die Standard-Bones nicht reichen.

**Syntax:**

```
TAG <new-tag-name> <existing-bone-name> [optional-keywords]
TAG <new-tag-name> <bone1> <bone2> [optional-keywords]   // 2-bone variant
```

**Pflicht-Felder:**
- 1. Token nach `TAG`: neuer Tag-Name (string, ≤63 chars)
- 2. Token: existierender Bone-Name oder bereits-cached-Tag-Name
  (Lookup via `mdx_bone_lookup`, g_mdx.c:487-499; bei Miss
  `cachetag_cache(token) | INTERNTAG_TAG`)

**Optional-Felder (in beliebiger Reihenfolge nach den Bones):**

| Keyword | Argumente | Bedeutung |
|---|---|---|
| `weight` | float | Tag-Anteil bei multi-bone-Mischung (g_mdx.c:771-779) |
| `offset` | x y z (3 floats) | Translation relativ zum Bone-Origin (g_mdx.c:780-802) |
| `axis` | 9 floats (fwd, left, up als 3D-Vektoren) | Rotation-Matrix (g_mdx.c:803-866) |
| `headangles` | (kein Argument) | Tag nutzt Head-Animation-Angles (g_mdx.c:868-871) |
| `<bone-name>` | (token) | Zweiter Bone — nur einmal erlaubt, definiert Mid-Point/Axis-Anchor (g_mdx.c:872-893) |

Wenn ein zweiter Bone-Name auftaucht, wird intern ein zweites
`interntag_t`-Eintrag angelegt mit auto-generiertem Namen `_<orig>_m`
(g_mdx.c:890), der "merge"-tag wird dem ersten als `merged`-Reference
gesetzt.

### 3.3 `HIT`-Block (g_mdx.c:913-1161)

Definiert eine collision-primitive area.

**RESOLUTION RULE — verifiziert (g_mdx.c:1143):**
`hit_parse_hit` ruft pro Tag-/Bone-Name-Token nur `cachetag_cache(token)`
— **kein** `mdx_bone_lookup`. Die echte Auflösung passiert später via
`mdm_tag_lookup` (g_mdx.c:512), das in zwei Listen sucht:

1. `model->tags[]` — die in der `.mdm`-Binär eingebetteten Built-in-Tags
   (typisch `tag_head`, `tag_torso`, `tag_chest`, `tag_back`,
   `tag_weapon`, `tag_weapon2`, `tag_footleft`, `tag_footright`).
2. `interntags[]` — die mit `TAG`-Blöcken in `.hit`-Files definierten
   internal-tags. Match via `Q_stricmp`.

**Konsequenz:** Bone-Namen aus dem `.mdx`-Skeleton (z.B. `Bip01 Head`)
können **NICHT direkt in HIT-Blöcken** referenziert werden — die
würden unter `cachetag_cache` landen aber nie gegen ein Bone-Lookup
laufen, und am Ende prints
`MDX WARNING: Unable to find tag <bone-name> in model <body.mdm>`.
Korrekt: Bone-Namen über einen `TAG`-Block bridgen, der internal-tag
dann im HIT referenzieren:

```
TAG _vg_head "Bip01 Head"
HIT head _vg_head radius 6 impactpoint head
```

`mdx_bone_lookup` (case-sensitiv `strcmp`) wird **nur in `hit_parse_tag`**
am 2. Token (`bone`) aufgerufen — das ist der einzige Pfad in dem ein
.mdx-Skeleton-Bone direkt auflösbar ist.

**Syntax (single-line, alle Tokens auf einer Zeile):**

```
HIT <hit_type> <tag1> [scale <x> <y> <z>] [radius <r>] [headangles] \
    [<tag2> [scale <x> <y> <z>] [radius <r>] [headangles]] \
    [axis <9 floats>] [impactpoint <name-or-int>] [box]
```

(Backslash zur Lesbarkeit; in der echten `.hit`-Datei muss alles auf
einer physischen Zeile stehen — siehe Sektion 3.0.)

**Pflicht-Felder:**

| Field | g_mdx.c | Description |
|---|---|---|
| `<hit_type>` | 933-948 | One of `none/gun/head/body/arm_L/arm_R/leg_L/leg_R`. Maps to MDX_NONE/MDX_GUN/MDX_HEAD/MDX_TORSO/MDX_ARM_L/MDX_ARM_R/MDX_LEG_L/MDX_LEG_R. |
| `<tag1>` (Pflicht) | 1139-1149 | Mindestens ein internal-tag (oder built-in `.mdm`-Tag). Bone-Namen via `TAG`-Bridge erforderlich (siehe oben). Max 2 Tags pro HIT (sonst `Too many tags for hit`). |

**Optional-Felder (per-tag oder per-hit, unterschiedlich):**

| Keyword | Position | Argumente | Effekt | g_mdx.c |
|---|---|---|---|---|
| `scale` | per-tag (after tag name) | x y z | Multiplies `hit->scale[tagidx]` (init = 1,1,1) | 962-986 |
| `radius` | per-tag | float | Sets all 3 axes uniformly | 987-1003 |
| `headangles` | per-tag | (none) | Sets `hit->ishead[tagidx] = qtrue` | 1004-1008 |
| `axis` | per-hit | 9 floats (fwd, left, up) | Rotation matrix `hit->axis[3]` | 1011-1077 |
| `impactpoint` (or `impact`) | per-hit | name or int | Sets `hit->impactpoint` | 1078-1132 |
| `box` | per-hit | (none) | Sets `hit->isbox = qtrue` (else cylinder/sphere) | 1133-1137 |

**Default-Werte beim Block-Beginn** (g_mdx.c:923-931, in `for tagidx 0..1` loop):

| Field | Default |
|---|---|
| `hit_type` | -1 (must be set first) |
| `impactpoint` | `NUM_ANIM_COND_IMPACTPOINT` (= unused) |
| `tag[i]` | -1 |
| `scale[i]` | (1.0, 1.0, 1.0) |
| `axis` | identity (`axisDefault`) |
| `ishead[i]` | qfalse |
| `isbox` | qfalse (= cylinder/sphere) |

**Impact-Point-Namen (g_mdx.c:1086-1129):**

| Name (case-insensitive) | Mapped to | Notes |
|---|---|---|
| `head` | IMPACTPOINT_HEAD | |
| `chest` | IMPACTPOINT_CHEST | |
| `gut` | IMPACTPOINT_GUT | |
| `groin` | IMPACTPOINT_GROIN | |
| `shoulder_right` | IMPACTPOINT_SHOULDER_RIGHT | **Note**: only `shoulder_right`, not `shoulder_R` |
| `shoulder_left` | IMPACTPOINT_SHOULDER_LEFT | |
| `knee_right` | IMPACTPOINT_KNEE_RIGHT | |
| `knee_left` | IMPACTPOINT_KNEE_LEFT | |
| `legs` | IMPACTPOINT_LEGS | **Single** — no L/R split for legs |
| `<integer>` | direct cast to enum | Fallback; out-of-range → `NUM_ANIM_COND_IMPACTPOINT` |

→ **9 named impact-points + integer-fallback. KEIN `legs_left` / `legs_right`** — die Beine sind im Damage-Modell ein einzelner Bucket.

---

## Sektion 4 — Primitive-Typen

Selektion erfolgt zur Trace-Zeit in `mdx_hit_test` basierend auf
`hit->isbox` und Anzahl Tags:

| Tags | `isbox` | Primitive | Func | Scale-Semantik |
|---|---|---|---|---|
| 1 | false | **Sphere** | `mdx_hit_test_sphere` (g_mdx.c:2697) | `scale[0]` als (radius_x, radius_y, radius_z) |
| 1 | true | **Box (axis-aligned in tag-space)** | `mdx_hit_test_box` (g_mdx.c:2714) | `scale[0]` als half-extents |
| 2 | false | **Cylinder** (along axis from tag1 to tag2) | `mdx_hit_test_cylinder` (g_mdx.c:2612) | `scale[0]` (start radius), `scale[1]` (end radius) |
| 2 | true | **Box** (between tag1 and tag2 with rotated axis) | `mdx_hit_test_box2` (g_mdx.c:2655) | `scale[0]` (half-extents at tag1), `scale[1]` (half-extents at tag2) |

**Achse:** Bei 2-Tag-Primitives wird die Achse implizit von `tag1 → tag2`
genommen (siehe mdx_hit_test:2790-2802). Die explizite `axis`-Direktive
gibt einen **Rotation-Offset relativ zur impliziten Tag-zu-Tag-Achse** —
typisch `identity` (default), aber nutzbar wenn z.B. ein Capsule um die
Knochen-Achse rotiert sein soll.

**Capsule** (Cylinder mit Halbkugel-Endkappen) ist KEIN dediziertes
Primitive — wird via Cylinder mit großen Endpunkten approximiert oder
durch zwei separate Hit-Areas modelliert.

---

## Sektion 5 — Tag-Anforderungen

### 5.1 Bone- und Tag-Lookup — die zwei Resolver-Pfade

Es gibt zwei distinkte Auflösungs-Pfade mit unterschiedlicher
Case-Sensitivity. Wer `.hit`-Files schreibt muss beide kennen:

**Pfad A — Bone-Lookup im `TAG`-Block** (`mdx_bone_lookup`,
g_mdx.c:491-503):

```c
for (i = 0; i < mdxModel->bone_count; i++) {
    if (!strcmp(mdxModel->bones[i].name, name)) return i;
}
```

→ **`strcmp`, case-sensitiv.** Aufgerufen ausschließlich in
`hit_parse_tag` (g_mdx.c:760, 883) für das 2. Argument eines
`TAG`-Blocks. Bei Miss: `cachetag_cache(name) | INTERNTAG_TAG` als
Fallback, was einen Forward-Reference auf einen späteren TAG/Built-in
erzeugt.

**Konsequenz:** Bone-Namen aus dem `.mdx`-Skeleton müssen exakt
geschrieben werden — `"Bip01 Head"` ≠ `"bip01 head"` ≠ `"BIP01 HEAD"`.
Aus dem Bone-Dump (`docs/notes/bone_dump_2026-04-27.txt`, Phase 6.0
Task 1.5) sind die echten Namen alle in `"Bip01 X Y"`-Form mit großem
B in `Bip01` und Single-Spaces als Trenner.

**Pfad B — Tag-Lookup für Cachetags** (`mdm_tag_lookup`,
g_mdx.c:512-533):

```c
for (i = 0; i < model->tag_count; i++)
    if (!Q_stricmp(model->tags[i].name, tagName)) return i;
#ifdef BONE_HITTESTS
for (i = 0; i < interntag_count; i++)
    if (!Q_stricmp(interntags[i].tag.name, tagName))
        return (i | TAG_INTERNAL);
#endif
return -1;
```

→ **`Q_stricmp`, case-INsensitiv.** Aufgerufen indirekt aus
`mdm_cachetag_resize` (g_mdx.c:421) für jeden Namen den
`cachetag_cache` registriert hat. Sucht zuerst in den `.mdm`-Built-in-
Tags (`tag_head`, `tag_torso`, ...), dann in den interntags
(via `TAG`-Blöcke definiert). Bei Miss: `MDX WARNING: Unable to find
tag X in model Y` Print + Fallback auf Tag-Index 0.

**Konsequenz:** Internal-Tag-Namen wie `_vg_head` und HIT-Block-
Referenzen sind **case-INsensitiv** — `_vg_head` und `_VG_HEAD`
auflösen auf den gleichen interntag.

**Zusammengefasst:**

| Token-Position | Resolver | Case |
|---|---|---|
| `TAG <new-name> <bone-or-tag>` (2. Token) | mdx_bone_lookup → fallback cachetag | `strcmp`, sensitiv |
| `HIT <type> <tag>` (Tag-Tokens) | cachetag_cache → später mdm_tag_lookup | `Q_stricmp`, insensitiv |

### 5.2 Verfügbare Bones im Standard-Player-Skeleton

**OPEN**: Nicht direkt aus dem Code-Reading bestimmbar. Der Bone-Name-Liste
kommt aus dem .mdx-Datei-Inhalt (binary file format `mdx_load`,
g_mdx.c:1298). Wir kennen aus der ETLegacy-Codebase nur die
**Tag-Namen**, nicht direkt Bone-Namen:

| Tag (aus cgame/-Refs) | Vermutlich auch Bone-Name |
|---|---|
| `tag_head` | likely yes (Head-Bone) |
| `tag_torso`, `tag_chest`, `tag_back` | likely (Torso-Anchors) |
| `tag_weapon`, `tag_weapon2` | weapon-mount, NICHT Body-Hitbox-tauglich |
| `tag_footleft`, `tag_footright` | yes (per `mdx_t`-Struct g_mdx.h:282) |

**Action für Tag-2 (vanguard.hit schreiben):**

Wir müssen **die echten Bone-Namen aus einer geladenen .mdx-Datei
extrahieren**. Optionen:

1. Dump-Tool schreiben das `mdx_models[].bones[i].name` ausliest und
   logged (cleanest)
2. Trial-and-error: vermutete Namen wie `Bip01 Spine`, `Bip01 Head`,
   `Bip01 R Calf` etc. (typisch für RtCW/ETLegacy-Bone-Naming —
   3DS-Max-Biped-Standard)
3. Bei Server-Start einen Debug-Print in `mdx_load` einbauen der alle
   Bones loggt (entfernen nach Discovery)

**Empfehlung:** Variante 3 — temporärer Debug-Print, einmal Server
starten, Output speichern, Print entfernen. Saubereste Discovery-Methode
ohne Tooling-Aufwand.

### 5.3 Knee-/Shoulder-Verfügbarkeit

Bei RtCW-Skeleton-Konvention (3DS-Max-Biped) existieren typischerweise
bones wie `Bip01 R Calf`, `Bip01 L Calf` (Knee-Region), `Bip01 R UpperArm`,
`Bip01 L UpperArm` (Shoulder-Region). Aber das ist **nicht code-verifiziert**
in dieser Codebase ohne Bone-Dump.

**OPEN**: Bone-Namen für Knee und Shoulder. Auflösung in Tag-2.

**Fallback-Strategie** falls dedicated Knee/Shoulder-Bones nicht
existieren:

- **Knee:** Cylinder zwischen `Bip01 R Thigh` und `Bip01 R Calf` (oder
  ähnlich). Mid-point der Cylinder = Knee-Region. Mit kleiner
  Z-extents, kann eine separate Knie-Hit-Area sein.
- **Shoulder:** Cylinder zwischen `Bip01 R Clavicle` und `Bip01 R UpperArm`.

Falls auch diese nicht existieren: `tag_torso` als Anchor, mit manuellem
`offset` und `scale` auf Modell-Schultern (gemessen anhand
Standing-Pose-Screenshots).

---

## Sektion 6 — Validierung & Fehlerverhalten

### 6.1 Datei nicht da (g_mdx.c:1185-1192)

```c
len = trap_FS_FOpenFile(filename, &fh, FS_READ);
if (len <= 0) {
#if ETLEGACY_DEBUG
    G_Printf(S_COLOR_YELLOW "MDX WARNING: Missing %s ...\n", filename);
#endif
    return qfalse;
}
```

→ **Silent fail in Release-Build** (kein Debug). `mdx_RegisterHits`
returnt 0, `mdx_hit_test` findet `hit_count == 0` und returnt `qfalse`
(g_mdx.c:2767-2773), Damage-Pfad fällt durch. Kein Crash.

→ **Implication für Phase 6:** Wenn unsere `human_base.hit` aus
irgendeinem Grund nicht im pk3 ist, schaltet Multi-Box stillschweigend
auf "kein Treffer registriert" zurück. Wir brauchen einen
**expliziten Print** in unserem `vg_Hitbox_Init` der nach dem ersten
Map-Spawn prüft ob `hits[].hit_count > 0` ist und sonst eine
**LOUD WARNING** ausgibt.

### 6.2 Parse-Errors (g_mdx.c)

Jeder Parse-Error (`COM_ParseError`) führt zu:
- Print der Fehler-Message mit Datei+Line
- `goto err` → cleanup + `return qfalse`
- Ganze Datei wird verworfen, `hit_count = 0` (g_mdx.c:1241-1244)
- Kein partielles Laden

Beispiel-Fehler-Messages:
- `"Expected hit type"` — HIT ohne folgendes Type-Token
- `"Invalid hit type: <token>"` — unbekannter hit_type-Name
- `"Unexpected token: <token>"` — irgend was anderes als TAG/HIT/known-keyword
- `"Too many tags for hit"` — > 2 Tag-Namen in einem HIT
- `"Expected X scale"` / `"Expected radius"` etc. — fehlendes
  Argument nach Keyword

### 6.3 Wie das Ergebnis ans MDX-System geht

`mdx_RegisterHits` → `hit_load` → bei Erfolg ist `hits[i].hits`
ein Array von `struct hit_area`, `hits[i].animModelInfo` zeigt auf den
Character. Pro Schuss wird in `mdx_hit_test` (g_mdx.c:2745-2870):

1. Animation-Group des Targets aus `BG_GetCharacter` holen
2. Im globalen `hits[]`-Array die passende `animModelInfo` finden
3. Über alle `hits[i].hits[j]` iterieren
4. Pro Hit-Area: `mdx_tag_orientation` für jeden tag, dann das Primitive
   gegen den shot-trace testen
5. Bestes (= kleinster `fraction`) zurückgeben

→ **Schreiben einer .hit-Datei reicht aus, um Multi-Box zu aktivieren** —
sobald die Datei am richtigen Pfad liegt und parsed, läuft die Engine.
Kein zusätzlicher cgame/qagame-Code-Touch nötig für die *Detektion*. Aber
Detektion ohne Wiring ins Damage-Modell hat noch keinen Effekt — das ist
Phase 6.1.

---

## Sektion 7 — Beispiel-Snippet

Parser-verifiziertes Format (Phase 6.0, Live-Test 2026-04-27):
TAG-Bridge-Pattern + single-line HIT-Blöcke. Identisch zum
shipping `etmain/animations/human_base.hit`:

```
// VanguardMod human_base.hit — multi-box hit-region
// definitions for the standard human animation skeleton.

// ---- TAG bridges (internal-tag → bone) ----
// Each TAG declares an internal tag that wraps a real
// skeleton bone. mdx_bone_lookup is case-sensitive (strcmp),
// so the bone-name spelling must match the .mdx exactly.

TAG _vg_head      "Bip01 Head"
TAG _vg_spine_lo  "Bip01 Spine"
TAG _vg_spine_mid "Bip01 Spine1"
TAG _vg_spine_up  "Bip01 Spine2"
TAG _vg_spine_top "Bip01 Spine3"
TAG _vg_pelvis    "Bip01 Pelvis"
TAG _vg_clav_l    "Bip01 L Clavicle"
TAG _vg_uarm_l    "Bip01 L UpperArm"
TAG _vg_clav_r    "Bip01 R Clavicle"
TAG _vg_uarm_r    "Bip01 R UpperArm"
TAG _vg_thigh_l   "Bip01 L Thigh"
TAG _vg_calf_l    "Bip01 L Calf"
TAG _vg_foot_l    "Bip01 L Foot"
TAG _vg_thigh_r   "Bip01 R Thigh"
TAG _vg_calf_r    "Bip01 R Calf"
TAG _vg_foot_r    "Bip01 R Foot"

// ---- HIT areas (10 blocks, 9 distinct impact-points) ----

HIT head _vg_head radius 6 impactpoint head
HIT body _vg_spine_up _vg_spine_top scale 8 6 4 scale 8 6 4 impactpoint chest box
HIT body _vg_spine_lo _vg_spine_mid scale 8 6 4 scale 8 6 4 impactpoint gut box
HIT body _vg_pelvis radius 5 impactpoint groin
HIT arm_L _vg_clav_l _vg_uarm_l radius 3 radius 4 impactpoint shoulder_left
HIT arm_R _vg_clav_r _vg_uarm_r radius 3 radius 4 impactpoint shoulder_right
HIT leg_L _vg_thigh_l _vg_calf_l radius 3 radius 3 impactpoint knee_left
HIT leg_R _vg_thigh_r _vg_calf_r radius 3 radius 3 impactpoint knee_right
HIT leg_L _vg_calf_l _vg_foot_l radius 3 radius 2 impactpoint legs
HIT leg_R _vg_calf_r _vg_foot_r radius 3 radius 2 impactpoint legs
```

**Zähle:** 10 Hit-Area-Blocks, 9 distinct impact-points (head, chest,
gut, groin, shoulder_right, shoulder_left, knee_right, knee_left, legs
× 2 areas). Bone-Namen entstammen dem Live-Bone-Dump
`docs/notes/bone_dump_2026-04-27.txt`.

---

## Sektion 8 — Offene Fragen

### Q1 (aus PHASE_6_PLAN.md Sektion 6.1) — Bone-Tag-Verfügbarkeit

> **Welche bone-Tags hat das Vanilla-ETLegacy-Player-Model tatsächlich?**

**Antwort:** Code-Reading liefert nur die Tag-Namen aus den
**Cgame-Render-Aufrufen** (tag_head, tag_torso, tag_chest, tag_back,
tag_weapon[2], tag_footleft, tag_footright). Die **Bone-Namen** im
.mdx-Skeleton sind nicht aus der Source allein bestimmbar — sie kommen
aus dem .mdx Binary-Format-Inhalt.

**Auflösung in Phase 6.0 Tag 2:** Bone-Dump per temporärem Debug-Print
in `mdx_load` (g_mdx.c:1298 Pfad) oder `mdx_LoadHitsFile`. Server
einmal starten, Output capturen, Liste in `human_base.hit` einsetzen.

### Q2 (Plan 6.1) — Pfad-Discovery-Mechanismus

> **Hat `mdx_LoadHitsFile` einen Pfad-Discovery-Mechanismus?**

**Antwort:** Ja, automatisch. `animationGroup` aus `.char` →
Extension-Replace `.anim` → `.hit` → FS-Lookup. Wir müssen die Datei
**genau dort ablegen wo der Loader sie sucht**: für ETLegacy-Vanilla
ist das `etmain/animations/human_base.hit`. Kein eigener
Pfad-Discovery-Code nötig.

### Q3 (Plan 6.1) — animationGroup-Parameter und character.cfg

> **Was ist `animationGroup`? Müssen wir mehrere .hits-Files für
> verschiedene Klassen schreiben oder reicht eines?**

**Antwort:** EIN File reicht. **Alle ETLegacy-Vanilla-Klassen
(Soldier/Medic/Engineer/FieldOps/Cvops × Allies/Axis = 10 Charaktere)
verwenden den gleichen `animationgroup "animations/human_base.anim"`**
(verifiziert via `etmain/characters/temperate/allied/soldier.char`).
`mdx_LoadHitsFile` wird via `G_CheckForExistingAnimModelInfo`
de-dupliziert auf einen einzigen Aufruf für die `human_base`-Group.

→ Eine einzige `etmain/animations/human_base.hit` deckt **alle 10
Vanilla-Charaktere ab**. Brauchst keine pro-Klasse-Variante in Phase 6.

### Q4 (NEU, aus Code-Reading) — Plan-Korrekturen nötig

> **Status:** addressed in `PHASE_6_PLAN.md` correction commit
> (this commit). Filename auf `human_base.hit`, Cvar-Liste auf 9
> Einträge, Mode-2 entfernt, Task 1.5 (Bone-Discovery) ergänzt,
> Estimate 4-5 Tage.

Während des Code-Readings sind zwei Annahmen aus PHASE_6_PLAN.md
widerlegt worden:

**Q4a — Datei-Extension:** Plan-Sektion 2.1 nennt
`etmain/animations/vanguard.hits`. Korrekt ist **`.hit`** (singular,
g_mdx.c:1337-1341). Plus der Filename **muss `human_base.hit` heißen**,
nicht `vanguard.hit` (Auto-Discovery via animationGroup, siehe Q3).

**Q4b — Region-Count und Cvar-Liste:** Plan-Sektion 2.2 listet 10
Damage-Cvars inklusive `vanguard_dmg_legs_l` und `vanguard_dmg_legs_r`.
Das `animScriptImpactPoint_t` Enum kennt aber nur ein einzelnes
`IMPACTPOINT_LEGS` (kein L/R-Split). Korrekt sind **9 Cvars**:

```
vanguard_dmg_head           (default 2.0)
vanguard_dmg_chest          (default 1.3)
vanguard_dmg_gut            (default 1.1)
vanguard_dmg_groin          (default 1.2)
vanguard_dmg_shoulder_l     (default 0.8)
vanguard_dmg_shoulder_r     (default 0.8)
vanguard_dmg_knee_l         (default 0.6)
vanguard_dmg_knee_r         (default 0.6)
vanguard_dmg_legs           (default 0.7)
```

**Plus optional** ein `vanguard_dmg_default` (default 1.0) für Hits die
auf eine Hit-Area treffen mit `IMPACTPOINT_UNUSED` oder bei Hit-Areas
ohne explizit gesetzten impactpoint.

→ **Action:** PHASE_6_PLAN.md Sektion 2.1 (Filename) und 2.2 (Cvar-
Tabelle) vor Tag-2-Start aktualisieren. Klein, kein Refactor.

### Q5 (NEU) — Wirkung des `hit_type` (vs `impactpoint`)

`hit_type` (8 Werte: none/gun/head/body/arm_L/arm_R/leg_L/leg_R) ist
ein PFLICHT-Feld jedes HIT-Blocks. `impactpoint` (9 Werte) ist
optional. Die **gröbere `hit_type`-Kategorie** wird vom mdx_hit_test als
`*hit_type` Output zurückgegeben (g_mdx.c:2779).

**Frage:** Brauchen wir `hit_type` für Damage-Multiplier oder ist
`impactpoint` ausreichend?

**Antwort:** `impactpoint` ist die feinere Auflösung und reicht für
Per-Region-Damage-Multipliers. `hit_type` wird vom Animation-Pain-System
benutzt (welche Pain-Animation der Spieler abspielt). Wir brauchen
beide, aber:
- `hit_type` für jedes HIT korrekt setzen (pflicht im Parser)
- `impactpoint` als unsere Damage-Multiplier-Achse

In der `human_base.hit`-Datei werden beide Felder gesetzt (siehe
Sektion 7 Beispiel).

### Q6 (NEU) — Capsule-Primitive

Plan-Sektion 2.2 erwähnt "vanguard_hitbox_mode 2 = multi-box+capsule".
Das `.hit`-Format unterstützt **kein dediziertes Capsule-Primitive** —
nur Sphere/Box/Cylinder/Box2.

**Auflösung:** Cylinder mit angemessen großen Endpunkten approximiert
Capsule. Wenn Phase 6.x echte Capsule-Halbkugel-Endkappen will, müsste
das Primitive in `mdx_hit_test_*` ergänzt werden — eigene C-Code-
Erweiterung. Für Phase 6.0/6.1 reicht Cylinder-Approx.

→ Plan-Sektion 2.2 Cvar-Mode-Beschreibung anpassen: "mode 2 = vanguard_dmg_*
multipliers active und cylinder-as-capsule approximation" — keine
separate Primitive-Familie.

### Q7 (NEU, Phase 6.0 Aktivierungs-Live-Test) — Format-Annahmen aus Code-Reading

Beim ersten HITS_FORMAT.md-Schreiben (Phase 6.0 Tag 1) wurden zwei
Annahmen aus dem Loader-Code-Reading abgeleitet, die sich beim
Live-Test (nach Aktivierung von BONE_HITTESTS und Behebung der vier
Compile-Bugs) als falsch erwiesen.

**Q7a — Multi-Line vs. Single-Line:** Die initiale Spec (und das
ursprüngliche Tag-2-`human_base.hit`) ging davon aus, dass innerhalb
eines TAG/HIT-Blocks freie Newlines erlaubt sind. Real verwendet der
Parser durchgängig `COM_ParseExt(ptr, qfalse)` mit
`allowLineBreaks=false` — die Token-Reads stoppen am Newline. Multi-
line-Blocks führen zu "Unexpected token" Parse-Errors. Korrigiert in
Sektion 3.0.

**Q7b — Bone-Direkt-Reference in HIT vs. TAG-Bridge:** Die initiale
Spec listete `<tag-or-bone-name>` als gültiges 2. Token in HIT-Blöcken
und implizierte Direct-Resolution gegen das `.mdx`-Skeleton. Real
ruft `hit_parse_hit` ausschließlich `cachetag_cache(token)` — kein
`mdx_bone_lookup`. Die spätere Auflösung via `mdm_tag_lookup`
durchsucht nur `.mdm`-Built-in-Tags und `interntags` (TAG-Block-
Definitionen). Bone-Namen aus dem Skeleton können daher nicht direkt
in HIT verwendet werden — sie müssen über `TAG _bridge "Bone Name"`
gebridget werden. Korrigiert in Sektion 3.3 Resolution-Rule.

Beide Korrekturen verifiziert via VG_HITDUMP-Live-Test 2026-04-27.

---

*Spec-Ende. Format ist parser-verifiziert via Phase-6.0-Aktivierung
(BONE_HITTESTS=on, 4 upstream compile-bugs gefixt, .hit auto-loaded
und 10 hit-areas registriert). Tag 2's initiale Annahmen
(multi-line, bone-direkt) waren falsch und sind in Q7 dokumentiert.*


---

**Copyright Notice**

Copyright (c) 2026 wahke <info@wahke.lu> (https://wahke.lu)  
Copyright (c) 2026 VanguardMod Project Contributors

Licensed under GPL-3.0-or-later. Part of VanguardMod project.
Built on ETLegacy (https://www.etlegacy.com).
