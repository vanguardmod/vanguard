# Coding style

Short, opinionated, and not optional inside Vanguard-specific code.
Code inherited from the ETLegacy Mod SDK keeps its original style; do
not reformat it just to match this guide. New files and Vanguard-specific
additions follow the rules below.

## Language

  - **QVM-bound code** (anything that ends up in `cgame.qvm`, `ui.qvm`,
    or `qagame.qvm`): C89 only. The QVM toolchain's `lcc` does not
    accept C99 niceties like designated initialisers, `//` comments
    inside macros, or variable-length arrays. Stay portable.
  - **Native-only code** (protected `qagame` and the WolfGuard private
    impl): C99 is fine. C11 is acceptable where it pays off.

When in doubt, write C89. It always works.

## Naming

| Construct                     | Convention                |
|-------------------------------|---------------------------|
| New public functions          | `vg_DoTheThing` (PascalCase, `vg_` prefix) |
| New static / file-local funcs | `vg_doTheThing` or `do_the_thing` |
| Types (structs, enums)        | `vg_thing_t`              |
| Macros                        | `VG_THING`                |
| WolfGuard-facing API          | `WG_*`, `wg_*` (already established) |

The `vg_` prefix is mandatory on anything Vanguard adds. It makes
diffs against the upstream SDK trivial to audit and prevents
collisions when merging upstream changes.

## Formatting

  - Tabs for indentation in `.c` / `.h`. Tab width 4 for display.
  - One opening brace on the same line as `if` / `for` / `while`,
    on a new line for function definitions:

        void vg_DoTheThing(int x)
        {
            if (x > 0) {
                ...
            }
        }

  - Always use braces, even for single-line bodies.
  - 100-column soft limit. Break at logical points, not mid-expression.
  - One blank line between functions. Two between major sections.

## Headers

  - Header guards in screaming-snake-case matching the path:

        #ifndef VANGUARD_GAME_VG_FOO_H
        #define VANGUARD_GAME_VG_FOO_H
        ...
        #endif

  - Forward-declare structs in headers; include only what the header
    itself needs. Implementation `#include`s go in the `.c` file.
  - Every new file starts with a short comment block: filename, one
    line of purpose, license note if non-trivial.

## Comments

  - `/* ... */` only. `//` is C99 and breaks QVM builds in some
    contexts.
  - Comment **why**, not what. The code says what.
  - Doxygen-style is welcome on public APIs; not required elsewhere.

## Error handling

  - Return non-zero on success, zero on failure — *unless* you are
    matching an existing SDK convention in the same module, in which
    case follow that. Consistency within a file beats consistency with
    this doc.
  - Use `wg_result_t`-style enums where there are more than two
    outcomes worth distinguishing.
  - Never silently swallow an error from the engine API. Log it via
    `G_Printf` / `Com_Printf` at minimum.

## Don't

  - Don't use `goto` for anything other than the classic single
    cleanup label at function bottom (and even there, prefer
    restructuring).
  - Don't `#define` constants that should be `enum` or `static const`.
  - Don't add new global variables. If you really need persistent
    state, attach it to `level` or `g_entities` like the SDK does.
  - Don't include WolfGuard headers from `cgame` or `ui`. WolfGuard is
    server-side only.


---

**Copyright Notice**

Copyright (c) 2026 wahke <info@wahke.lu> (https://wahke.lu)  
Copyright (c) 2026 VanguardMod Project Contributors

Licensed under GPL-3.0-or-later. Part of VanguardMod project.
Built on ETLegacy (https://www.etlegacy.com).
