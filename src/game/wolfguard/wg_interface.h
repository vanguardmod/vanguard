// SPDX-FileCopyrightText: 2026 wahke <info@wahke.lu> (https://wahke.lu)
// SPDX-FileCopyrightText: 2026 VanguardMod Project Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * wg_interface.h — VanguardMod WolfGuard plugin interface (v0.6.0).
 *
 * Function-pointer dispatch surface that decouples the public mod
 * code from any concrete WolfGuard implementation. The community
 * build links wg_stub.c (no-op); the protected build links a
 * provider from src/game/wolfguard/private/ that is cloned at
 * build time from the private vanguardmod/wolfguard repo.
 *
 * Return convention: every int-returning hook returns 0 on
 * success / no-action and non-zero when the caller should react
 * (e.g. reject the client connect with a kick).
 *
 * Concurrency: all WG_* hooks run on the main game thread. The
 * provider must not assume any other thread context.
 *
 * Stability: v0.6.0 introduces this API and supersedes the earlier
 * direct-function scaffold (src/game/wolfguard.h). Future field
 * or signature additions go onto the wg_interface_t struct (with a
 * preserved layout for older private builds) rather than into new
 * extern symbols.
 */

#ifndef VANGUARD_WG_INTERFACE_H
#define VANGUARD_WG_INTERFACE_H

#include "../q_shared.h"   /* qboolean */

typedef struct
{
	int  (*init)(void);
	void (*shutdown)(void);
	int  (*client_connect)(int clientNum, const char *userinfo);
	void (*client_disconnect)(int clientNum);
	void (*frame)(int levelTime);
} wg_interface_t;

/* The active provider. Always non-NULL: the stub provides a no-op
 * dispatch table when FEATURE_WOLFGUARD=OFF, and the protected
 * build replaces it from inside the private subdirectory. Callers
 * may invoke WG_Active->init() etc. without a NULL check. */
extern wg_interface_t *WG_Active;

/* qtrue only when the protected build linked a real provider.
 * Read by WG_PrintStartupBanner to choose between the [ ACTIVE ]
 * and [ NOT INCLUDED ] banner variants. */
extern qboolean WG_IsAvailable;

/* Provider version string. "n/a" in the stub; the protected build
 * provides an actual version (e.g. "1.0.0"). Banner prepends "v"
 * when displayed. */
extern const char *WG_Version;

#endif /* VANGUARD_WG_INTERFACE_H */
