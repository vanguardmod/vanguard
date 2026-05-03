// SPDX-FileCopyrightText: 2026 wahke <info@wahke.lu> (https://wahke.lu)
// SPDX-FileCopyrightText: 2026 VanguardMod Project Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * wg_banner.c — Startup banner printer.
 *
 * Reads WG_IsAvailable / WG_Version (defined by either wg_stub.c
 * or the private provider) and emits a fixed-width banner via
 * G_Printf. Called once from G_InitGame and on demand by the
 * `wg_status` server console command.
 *
 * Banner content is part of the v0.6.0 spec — line widths, color
 * codes, and label spacing are pixel-fixed; do not reformat.
 */

#include "../g_local.h"           /* G_Printf */
#include "../g_vanguard.h"        /* vg_Fun_IsActive (v0.7.0) */
#include "wg_interface.h"
#include "wg_banner.h"
#include "version_generated.h"    /* ETL_BUILD_VERSION */

/* The v0.6.0 spec refers to this string as VANGUARD_VERSION. The
 * underlying value is the existing ETL_BUILD_VERSION macro that
 * cmake substitutes from the git tag (see
 * cmake/version_generated.h.in). The "v" prefix is already part
 * of the macro string ("v0.6.0"), so the banner format below uses
 * a bare %s rather than `v%s` to avoid double-prefixing. */
#define VANGUARD_VERSION ETL_BUILD_VERSION

void WG_PrintStartupBanner(void)
{
	G_Printf("^7========================================================\n");
	G_Printf("^7  ^8VANGUARD^7MOD ^3%s\n", VANGUARD_VERSION);
	G_Printf("^7--------------------------------------------------------\n");

	if (WG_IsAvailable)
	{
		G_Printf("^7  WolfGuard:    ^2[ ACTIVE ]  ^9v%s\n", WG_Version);
		G_Printf("^7  Protection:   ^2enabled\n");
		G_Printf("^7  Backend:      ^5api.vanguardmod.com\n");
		G_Printf("^7  Mode:         ^%s%s\n",
		         vg_Fun_IsActive() ? "3" : "5",
		         vg_Fun_IsActive() ? "fun-public" : "cup-orthodox");
		G_Printf("^7  Build mode:   ^5protected\n");
	}
	else
	{
		G_Printf("^7  WolfGuard:    ^1[ NOT INCLUDED ]\n");
		G_Printf("^7  Protection:   ^1disabled\n");
		G_Printf("^7  Info:         ^5https://vanguardmod.com\n");
		G_Printf("^7  Mode:         ^%s%s\n",
		         vg_Fun_IsActive() ? "3" : "5",
		         vg_Fun_IsActive() ? "fun-public" : "cup-orthodox");
		G_Printf("^7  Build mode:   ^3community\n");
	}

	G_Printf("^7========================================================\n");
}
