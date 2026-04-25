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
