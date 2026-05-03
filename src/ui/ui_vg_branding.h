// SPDX-FileCopyrightText: 2026 wahke <info@wahke.lu> (https://wahke.lu)
// SPDX-FileCopyrightText: 2026 VanguardMod Project Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UI_VG_BRANDING_H
#define UI_VG_BRANDING_H

/*
 * VanguardMod mod-list branding — Phase Branding 2 (v0.7.0.1).
 *
 * Provides color-coded display strings for known ET mods in the
 * UI Mods menu (FEEDER_MODS dispatch in ui_main.c). Falls through
 * to ETLegacy default behaviour (description.txt -> raw dir name)
 * for unknown mods.
 *
 * Hook point: ui_main.c::UI_FeederItemText case FEEDER_MODS
 * (TIER 1 lookup); see docs/notes/PHASE_BRANDING_2_AUDIT.md §3
 * for the architectural rationale.
 *
 * License: clean-room re-implementation of a pattern observed in
 * Nitmod's UI binary; no Nitmod code copied. See docs/VG_BRANDING.md
 * for the attribution.
 */

/*
 * Returns the color-coded display string for a known mod directory,
 * or NULL if the directory name is not in the lookup table. Caller
 * (UI_FeederItemText) falls back to description.txt (TIER 2) and
 * raw dir name (TIER 3) when this returns NULL.
 *
 * Lookup is case-insensitive (Q_stricmp). dir_name MAY be NULL or
 * empty; both yield a NULL return.
 */
const char *VG_Branding_GetModDisplay(const char *dir_name);

/*
 * Diagnostic: prints the lookup table contents to console. Called
 * from the vanguard_diag_branding cvar gate in UI_LoadMods so admins
 * can verify which mods are recognised vs falling through to TIER 2/3.
 */
void VG_Branding_PrintTable(void);

#endif // UI_VG_BRANDING_H
