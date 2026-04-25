# Bootstrap Guide

This file describes how to take this scaffold and produce a first working
build of VanguardMod.

What you have right now is the **shell**: build system, WolfGuard public
interface, documentation, and conventions. The actual mod source tree
(`src/cgame`, `src/game`, `src/ui`, `src/qcommon`) is **not** yet present —
that comes from the ETLegacy Mod SDK and is integrated in Step 1.

---

## Prerequisites

On Debian / Ubuntu / WSL Debian:

    sudo apt update
    sudo apt install -y build-essential cmake git

The ETLegacy Mod SDK ships its own QVM toolchain (a vendored `lcc` /
`q3asm`) so you do not need to install one separately.

---

## Step 1 — Import the ETLegacy Mod SDK

The cleanest path is to overlay the SDK source on top of this scaffold.
Run from the repo root:

    ./scripts/bootstrap.sh

That script:
  1. Clones `https://github.com/etlegacy/etlegacy-mod-sdk.git` to a
     temporary directory.
  2. Copies the `src/` tree into this repo.
  3. Copies any vendored QVM toolchain into `tools/`.
  4. Cleans up the temporary clone.

If you prefer to do it manually:

    git clone https://github.com/etlegacy/etlegacy-mod-sdk.git _sdk-import
    cp -r _sdk-import/src/.   ./src/
    cp -r _sdk-import/tools/. ./tools/    # if the upstream ships one
    rm -rf _sdk-import

---

## Step 2 — Sanity check the community build

    cmake -B build -DVANGUARD_WITH_WOLFGUARD=OFF
    cmake --build build -j

Expected output in `build/vanguard/`:

  - `cgame.qvm`
  - `ui.qvm`
  - `qagame.qvm`

At this point you have a buildable, runnable but **vanilla** mod — it does
exactly what the upstream SDK does. Every Vanguard-specific change goes on
top of this baseline from here on.

---

## Step 3 — Wire WolfGuard hooks into qagame

This is a one-time integration step. Open `src/game/g_main.c` (or the
equivalent SDK entry point) and add the WolfGuard lifecycle calls:

    #include "../../wolfguard/wolfguard.h"

    // In G_InitGame:
    WG_Init(sv_serverid->string, NULL);

    // In G_ShutdownGame:
    WG_Shutdown();

    // In ClientConnect / ClientDisconnect / ClientCommand / G_RunFrame:
    WG_OnClientConnect(...), WG_OnClientDisconnect(...), etc.

In the community build these calls land in the null provider and do
nothing. In the protected build they go to the real WolfGuard impl.

---

## Step 4 (core devs only) — Add the protected build

If you have access to the private WolfGuard repo:

    git clone <private-url> wolfguard/private
    cmake -B build-protected -DVANGUARD_WITH_WOLFGUARD=ON
    cmake --build build-protected -j

This produces a native `qagame_*.so` (Linux) or `qagame_mp_*.dll` (Windows)
linked against WolfGuard. The QVM artefacts are still produced for clients.

The `wolfguard/private/` directory is in `.gitignore` and `wolfguard/.gitignore`
so an accidental `git add` cannot leak it into the public repo.

---

## Step 5 — First commit

    git init -b main
    git add .
    git commit -m "Initial VanguardMod scaffold on ETLegacy Mod SDK"
    git remote add origin git@github.com:<your-org>/vanguardmod.git
    git push -u origin main

---

## What goes where afterwards

| Change type                              | Lives in                       |
|------------------------------------------|--------------------------------|
| Gameplay tweaks (movement, hitboxes)     | `src/game/`, `src/cgame/`      |
| HUD / UI                                 | `src/cgame/`, `src/ui/`        |
| Menu definitions                         | `assets/ui/`                   |
| Default cvars / configs                  | `assets/cfg/`                  |
| Map rotation, mode scripts               | `assets/scripts/`              |
| AC hook surface (public)                 | `wolfguard/wolfguard.h`        |
| AC null provider                         | `wolfguard/wolfguard_null.c`   |
| AC real implementation (private)         | `wolfguard/private/`           |
| Build options, toolchain glue            | `cmake/`                       |

See `docs/ARCHITECTURE.md` for the full reasoning behind this split.
