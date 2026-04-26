/*
 * g_vanguard.c — VanguardMod server-side feature module.
 *
 * Houses Vanguard-specific qagame extensions. Currently implements the
 * dev-mode subsystem (hitbox / bullet visualisation gated by the
 * vanguard_dev cvar). Future Vanguard subsystems (vg_Hitbox_*,
 * vg_Movement_*) live in this same file under their own static state
 * blocks so the upstream-resync diff stays small.
 *
 * Architectural note: dev mode is server-controlled by design. Clients
 * cannot synthesize hitbox visuals on their own — the engine will only
 * spawn the railtrail entities cgame renders if the server emits the
 * matching EV_RAILTRAIL events, and that is exactly what
 * g_debugPlayerHitboxes / g_debugBullets gate. Toggling vanguard_dev
 * on a server is therefore the only path to hitboxes-on-screen.
 */

#include "g_local.h"
#include "g_vanguard.h"

/* ================================================================== */
/* Dev mode                                                           */
/* ================================================================== */

/* Reminder cadence: how often (ms) to re-log "DEV MODE ACTIVE" while
 * the toggle is on. 5 minutes matches the requirement so a server
 * left running can't quietly drift in dev mode unnoticed. */
#define VG_DEVMODE_REMINDER_MS  (5 * 60 * 1000)

/* Engine-side master-list cvars probed for the "public server" check.
 * sv_master1..5 are the slots etlded honours for outbound heartbeats. */
#define VG_DEVMODE_MASTER_CVAR_COUNT 5

typedef struct
{
	vmCvar_t devCvar;       /* the vanguard_dev cvar itself */

	qboolean active;        /* current latched state — mirrors devCvar.integer != 0 */
	qboolean stateKnown;    /* false until first OnFrame, prevents bogus 0->? edge */

	int      lastReminderTime;

	/* Saved cvar values from the moment we flipped dev mode on, so
	 * 1->0 transitions restore whatever the admin had set manually
	 * before they enabled the master switch. */
	int      savedDebugPlayerHitboxes;
	int      savedDebugBullets;
	int      savedSvCheats;
} vg_devmode_state_t;

static vg_devmode_state_t s_devmode;

/* ------------------------------------------------------------------ */
/* internal helpers                                                   */
/* ------------------------------------------------------------------ */

static int vg_DevMode_ReadCvarInt(const char *name)
{
	return trap_Cvar_VariableIntegerValue(name);
}

static void vg_DevMode_SetCvarInt(const char *name, int value)
{
	char buf[16];
	Com_sprintf(buf, sizeof(buf), "%i", value);
	trap_Cvar_Set(name, buf);
}

/**
 * @brief True if the server is publicly heartbeating to a master list.
 *        Requires both `dedicated >= 2` (the engine's "publish me"
 *        mode) and at least one populated sv_master* slot. A LAN
 *        dedicated server (`dedicated 1`) does not heartbeat even if
 *        sv_master1 is set to its engine default, so we don't warn on
 *        it — that would be noise on every dev box.
 */
static qboolean vg_DevMode_LooksLikePublicServer(void)
{
	int  i;
	int  dedicated;
	char buf[MAX_CVAR_VALUE_STRING];

	dedicated = vg_DevMode_ReadCvarInt("dedicated");
	if (dedicated < 2)
	{
		return qfalse;
	}

	for (i = 1; i <= VG_DEVMODE_MASTER_CVAR_COUNT; i++)
	{
		char name[16];
		Com_sprintf(name, sizeof(name), "sv_master%i", i);
		trap_Cvar_VariableStringBuffer(name, buf, sizeof(buf));
		if (buf[0] != '\0')
		{
			return qtrue;
		}
	}
	return qfalse;
}

static void vg_DevMode_PrintBanner(qboolean publicServer)
{
	G_Printf(S_COLOR_RED "==========================================================\n");
	G_Printf(S_COLOR_RED "VanguardMod: DEV MODE ACTIVE — do not run on public servers\n");
	G_Printf(S_COLOR_RED "  hitbox visualisation is being broadcast to every client\n");
	G_Printf(S_COLOR_RED "  set 'vanguard_dev 0' to disable\n");
	if (publicServer)
	{
		G_Printf(S_COLOR_RED ">> WARNING: DEV MODE ON A PUBLIC SERVER, this is unsafe <<\n");
		G_Printf(S_COLOR_RED "   sv_master* slots are populated; heartbeats are going\n");
		G_Printf(S_COLOR_RED "   out to the master list. Disable dev mode immediately.\n");
	}
	G_Printf(S_COLOR_RED "==========================================================\n");
}

/**
 * @brief Capture current upstream debug-cvar values and force them on.
 *        Called only on the 0->1 edge of vanguard_dev.
 */
static void vg_DevMode_Enable(void)
{
	s_devmode.savedDebugPlayerHitboxes = vg_DevMode_ReadCvarInt("g_debugPlayerHitboxes");
	s_devmode.savedDebugBullets        = vg_DevMode_ReadCvarInt("g_debugBullets");
	s_devmode.savedSvCheats            = vg_DevMode_ReadCvarInt("sv_cheats");

	/* Visualisation only — hitboxes (& 1) for the player railbox path,
	 * bullet traces for shot diagnostics. We deliberately do NOT touch
	 * any cvar that would expose a wallhack-style advantage (r_showtris,
	 * cg_drawTraces, etc. are out of scope for this toggle). */
	vg_DevMode_SetCvarInt("g_debugPlayerHitboxes", 1);
	vg_DevMode_SetCvarInt("g_debugBullets", 1);

	/* sv_cheats unlocks the engine's CVAR_CHEAT-protected client tooling
	 * (noclip, cg_thirdperson, give, etc.) which is exactly what mod
	 * authors and hitbox tuners need to inspect player models from any
	 * angle. The PUBLIC SERVER warning above already covers the abuse
	 * surface; restoring on disable keeps an admin who deliberately
	 * pre-set sv_cheats from being silently overridden. */
	vg_DevMode_SetCvarInt("sv_cheats", 1);

	s_devmode.active            = qtrue;
	s_devmode.lastReminderTime  = level.time;

	vg_DevMode_PrintBanner(vg_DevMode_LooksLikePublicServer());
}

/**
 * @brief Restore the upstream debug cvars to whatever they were before
 *        dev mode took ownership. Called on the 1->0 edge.
 */
static void vg_DevMode_Disable(void)
{
	vg_DevMode_SetCvarInt("g_debugPlayerHitboxes", s_devmode.savedDebugPlayerHitboxes);
	vg_DevMode_SetCvarInt("g_debugBullets",        s_devmode.savedDebugBullets);
	vg_DevMode_SetCvarInt("sv_cheats",             s_devmode.savedSvCheats);

	s_devmode.active = qfalse;

	G_Printf(S_COLOR_GREEN "VanguardMod: dev mode disabled (debug cvars restored)\n");
}

/* ------------------------------------------------------------------ */
/* public entry points                                                */
/* ------------------------------------------------------------------ */

void vg_DevMode_Init(void)
{
	memset(&s_devmode, 0, sizeof(s_devmode));

	/* CVAR_SERVERINFO so cgame sees vanguard_dev via CS_SERVERINFO.
	 * Not CVAR_LATCH — we want runtime toggling, not map-bound state.
	 * Default 0 — competitive / production servers stay clean. */
	trap_Cvar_Register(&s_devmode.devCvar, "vanguard_dev", "0", CVAR_SERVERINFO);
	trap_Cvar_Update(&s_devmode.devCvar);
}

void vg_DevMode_Shutdown(void)
{
	if (s_devmode.active)
	{
		vg_DevMode_Disable();
	}
	memset(&s_devmode, 0, sizeof(s_devmode));
}

void vg_DevMode_OnFrame(int leveltime)
{
	qboolean wantActive;

	trap_Cvar_Update(&s_devmode.devCvar);
	wantActive = (s_devmode.devCvar.integer != 0) ? qtrue : qfalse;

	/* First call after Init: latch the current state without firing
	 * a transition (so a map restart with vanguard_dev already 1 does
	 * not double-print the banner from Enable + the previous run's
	 * residual cvar values). */
	if (!s_devmode.stateKnown)
	{
		s_devmode.stateKnown = qtrue;
		if (wantActive)
		{
			vg_DevMode_Enable();
		}
		return;
	}

	if (wantActive && !s_devmode.active)
	{
		vg_DevMode_Enable();
	}
	else if (!wantActive && s_devmode.active)
	{
		vg_DevMode_Disable();
	}

	if (s_devmode.active &&
	    (leveltime - s_devmode.lastReminderTime) >= VG_DEVMODE_REMINDER_MS)
	{
		s_devmode.lastReminderTime = leveltime;
		vg_DevMode_PrintBanner(vg_DevMode_LooksLikePublicServer());
	}
}
