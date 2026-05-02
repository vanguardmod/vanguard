// SPDX-FileCopyrightText: 2026 wahke <info@wahke.lu> (https://wahke.lu)
// SPDX-FileCopyrightText: 2026 VanguardMod Project Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * wg_banner.h — Public entry to the WolfGuard startup banner.
 *
 * Compiled in BOTH community and protected builds. The function
 * inspects WG_IsAvailable and WG_Version (declared in
 * wg_interface.h) to choose between the [ ACTIVE ] and
 * [ NOT INCLUDED ] banner variants. Output goes through G_Printf
 * so the lines land in the server console + log just like every
 * other startup line.
 */

#ifndef VANGUARD_WG_BANNER_H
#define VANGUARD_WG_BANNER_H

void WG_PrintStartupBanner(void);

#endif /* VANGUARD_WG_BANNER_H */
