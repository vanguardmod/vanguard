/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 wahke <info@wahke.lu> (https://wahke.lu)
 * SPDX-FileCopyrightText: 2026 VanguardMod Project Contributors
 *
 * This file is part of VanguardMod.
 *
 * VanguardMod is built on ETLegacy (https://www.etlegacy.com),
 * which is licensed under GPL-3.0-or-later.
 *
 * VanguardMod is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * VanguardMod is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with VanguardMod. If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * g_xp_saver_stub.c — Empty stubs for XP-Saver functions.
 *
 * The original g_xp_saver.c is disabled in this build (see
 * g_xp_saver.c.disabled) because upstream marks it as needing rework
 * and it pulls in a SDK-internal sqlite layer (level.database.*) that
 * is not part of the public API.
 *
 * These no-op stubs let qagame link cleanly while preserving the
 * unmodified call sites in g_main.c, g_client.c and g_svcmds.c.
 *
 * When VanguardMod's own persistence layer is in place (likely via
 * the WolfGuard / vanguardmod.com backend rather than local sqlite),
 * either restore g_xp_saver.c or replace these stubs with real
 * implementations.
 */

#include "g_local.h"

void G_XPSaver_Load(gclient_t *cl)  { (void)cl; }
void G_XPSaver_Store(gclient_t *cl) { (void)cl; }
int  G_XPSaver_Clear(void)          { return 0; }
void G_XPSaver_Convert(void)        { }
