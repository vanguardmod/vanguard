// SPDX-FileCopyrightText: 2026 wahke <info@wahke.lu> (https://wahke.lu)
// SPDX-FileCopyrightText: 2026 VanguardMod Project Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * wg_stub.c — Community-build no-op WolfGuard provider.
 *
 * Compiled only when FEATURE_WOLFGUARD=OFF. Provides the minimal
 * symbol set wg_interface.h advertises so the public mod links and
 * runs identically to the protected build minus any anti-cheat
 * detection. Every hook returns 0 / does nothing.
 *
 * The protected build instead pulls in
 * src/game/wolfguard/private/<provider>.c, which defines its own
 * wg_interface_t, sets WG_Active to point at it, and sets
 * WG_IsAvailable = qtrue + WG_Version to the real provider version.
 */

#include "wg_interface.h"

static int wg_stub_init(void)
{
	return 0;
}

static void wg_stub_shutdown(void)
{
}

static int wg_stub_client_connect(int clientNum, const char *userinfo)
{
	(void)clientNum;
	(void)userinfo;
	return 0;
}

static void wg_stub_client_disconnect(int clientNum)
{
	(void)clientNum;
}

static void wg_stub_frame(int levelTime)
{
	(void)levelTime;
}

static wg_interface_t wg_stub_iface = {
	wg_stub_init,
	wg_stub_shutdown,
	wg_stub_client_connect,
	wg_stub_client_disconnect,
	wg_stub_frame
};

wg_interface_t *WG_Active      = &wg_stub_iface;
qboolean        WG_IsAvailable = qfalse;
const char     *WG_Version     = "n/a";
