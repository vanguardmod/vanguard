// SPDX-FileCopyrightText: 2026 wahke <info@wahke.lu> (https://wahke.lu)
// SPDX-FileCopyrightText: 2026 VanguardMod Project Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * ui_vg_branding.c — VanguardMod mod-list branding lookup table
 * (Phase Branding 2, v0.7.0.1).
 *
 * The display strings here are VanguardMod's own design choices,
 * informed by each listed mod's known community brand colors. They
 * are NOT derived from any other mod's binary or source code. See
 * docs/VG_BRANDING.md for the full attribution + contribution
 * policy (community PRs adding new mods are welcome).
 */

#include "ui_local.h"
#include "ui_vg_branding.h"

typedef struct
{
	const char *dir_name;     /* mod directory name, lookup key (case-insensitive match) */
	const char *display_name; /* color-coded display string */
} vg_mod_brand_t;

/*
 * Baseline table. Each entry is one mod we recognise; unknown mods
 * cascade through TIER 2 (description.txt) and TIER 3 (raw dir name)
 * in the caller. Order doesn't matter — lookup is linear scan.
 */
static const vg_mod_brand_t vg_mod_brands[] =
{
	{ "vanguard",  "^8Vanguard^7Mod"   }, /* VanguardMod own branding */
	{ "legacy",    "^1ET^7:Legacy"     }, /* ETLegacy upstream */
	{ "jaymod",    "^8Jay^4mod"        }, /* Jaymod */
	{ "nitmod",    "^7N^1!^7tmod"      }, /* Nitmod */
	{ "noquarter", "^1No Quarter"      }, /* NoQuarter */
	{ "etpro",     "^7ETPro"           }, /* ETPro (cup) */
	{ "silent",    "^7silEnT"          }, /* silEnT */
	{ "etpub",     "^7ETPub"           }, /* ETPub */
	{ "compet",    "^7Comp^1ET"        }, /* CompET (cup) */
	{ "xmod",      "^7xmod"            }, /* xmod */
	{ "etmain",    "^7Wolfenstein: ET" }, /* W:ET base game (etmain fallback) */
	{ NULL,        NULL                }  /* sentinel */
};

const char *VG_Branding_GetModDisplay(const char *dir_name)
{
	int i;

	if (!dir_name || !*dir_name)
	{
		return NULL;
	}

	for (i = 0; vg_mod_brands[i].dir_name != NULL; i++)
	{
		if (Q_stricmp(dir_name, vg_mod_brands[i].dir_name) == 0)
		{
			return vg_mod_brands[i].display_name;
		}
	}

	return NULL; /* not in table — caller falls back to TIER 2 / TIER 3 */
}

void VG_Branding_PrintTable(void)
{
	int i;

	Com_Printf("VG_Branding lookup table (%s):\n", "v0.7.0.1");
	for (i = 0; vg_mod_brands[i].dir_name != NULL; i++)
	{
		Com_Printf("  %-12s -> %s^7\n",
		           vg_mod_brands[i].dir_name,
		           vg_mod_brands[i].display_name);
	}
}
