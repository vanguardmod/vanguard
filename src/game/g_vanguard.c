/*
 * g_vanguard.c — VanguardMod server-side feature module.
 *
 * Houses Vanguard-specific qagame extensions. Currently implements the
 * dev-mode subsystem (hitbox / bullet visualisation gated by the
 * vanguard_dev cvar). Future Vanguard subsystems (vg_Hitbox_*,
 * vg_Movement_*) live in this same file under their own static state
 * blocks so the upstream-resync diff stays small.
 *
 * Architectural note: dev mode is server-controlled by design — the
 * 'vanguard_dev' cvar is CVAR_SERVERINFO so cgame learns of it via
 * the configstring, and clients cannot synthesize the authority on
 * their own. The actual hitbox geometry is rendered locally by
 * cgame (src/cgame/cg_vanguard_dev.c) from existing snapshot data;
 * this module only manages the cvar, the sv_cheats lifecycle and
 * the operator banner. No EV_RAILTRAIL or other broadcast traffic
 * is emitted, so dev mode is network-cost free.
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

	/* Saved sv_cheats from the moment we flipped dev mode on, so a
	 * 1->0 transition restores whatever the admin had set manually
	 * before they enabled the master switch. */
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
	G_Printf(S_COLOR_RED "  every client is authorised to render hitbox overlays\n");
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
 * @brief Capture sv_cheats and unlock it. Called only on the 0->1 edge
 *        of vanguard_dev. Hitbox visualisation itself is rendered
 *        client-side (cg_vanguard_dev.c) from snapshot data and
 *        requires no server cvars to be flipped — the only thing dev
 *        mode unlocks server-side is sv_cheats, so the engine's
 *        CVAR_CHEAT client tools (noclip, cg_thirdperson, give, ...)
 *        become available for inspecting player models from any
 *        angle. The PUBLIC SERVER warning in the banner covers the
 *        abuse surface.
 */
static void vg_DevMode_Enable(void)
{
	s_devmode.savedSvCheats = vg_DevMode_ReadCvarInt("sv_cheats");

	vg_DevMode_SetCvarInt("sv_cheats", 1);

	s_devmode.active            = qtrue;
	s_devmode.lastReminderTime  = level.time;

	vg_DevMode_PrintBanner(vg_DevMode_LooksLikePublicServer());
}

/**
 * @brief Restore sv_cheats to its pre-toggle value. Called on the
 *        1->0 edge. A no-op restore is safe on hosts that lock the
 *        cvar (Pterodactyl) — the engine just refuses the set.
 */
static void vg_DevMode_Disable(void)
{
	vg_DevMode_SetCvarInt("sv_cheats", s_devmode.savedSvCheats);

	s_devmode.active = qfalse;

	G_Printf(S_COLOR_GREEN "VanguardMod: dev mode disabled (sv_cheats restored)\n");
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

/* ================================================================== */
/* Multi-box hitbox                                                   */
/* ================================================================== */

/* Cvar storage. vmCvar_t is the engine-side handle the trap layer
 * keeps in sync with the underlying string cvar. Stored statically:
 * one instance per cvar, alive for the module lifetime. */
typedef struct
{
	vmCvar_t mode;          /* vanguard_hitbox_mode (0=off, 1=on) */

	/* 9 per-region damage multipliers; mapped 1:1 to the .hit-file
	 * impactpoints (head/chest/gut/groin, shoulder L+R, knee L+R,
	 * legs). IMPACTPOINT_LEGS has no L/R split. */
	vmCvar_t dmg_head;
	vmCvar_t dmg_chest;
	vmCvar_t dmg_gut;
	vmCvar_t dmg_groin;
	vmCvar_t dmg_shoulder_l;
	vmCvar_t dmg_shoulder_r;
	vmCvar_t dmg_knee_l;
	vmCvar_t dmg_knee_r;
	vmCvar_t dmg_legs;

	/* Fallback multiplier for IMPACTPOINT_UNUSED / out-of-range hits
	 * (e.g. trace landed on a hit-area that was registered without an
	 * impactpoint or on a region the .hit file doesn't cover yet). */
	vmCvar_t dmg_default;
} vg_hitbox_state_t;

static vg_hitbox_state_t s_hitbox;

void vg_Hitbox_Init(void)
{
	memset(&s_hitbox, 0, sizeof(s_hitbox));

	/* mode is CVAR_LATCH so a server can't drift between competitive
	 * and casual mid-match; CVAR_ARCHIVE so admin choice persists in
	 * etconfig_server.cfg; CVAR_SERVERINFO so cgame eventually learns
	 * the active mode for HUD/disclaimer purposes (Phase 6.2). */
	trap_Cvar_Register(&s_hitbox.mode, "vanguard_hitbox_mode", "1",
	                   CVAR_SERVERINFO | CVAR_LATCH | CVAR_ARCHIVE);

	/* Per-region multipliers. CVAR_ARCHIVE only — admins routinely
	 * tune these mid-match while balancing on a shared scrim server,
	 * a LATCH would be hostile UX here. Defaults are conservative
	 * starting points; final balancing happens in Phase 6.2. */
	trap_Cvar_Register(&s_hitbox.dmg_head,       "vanguard_dmg_head",       "2.0", CVAR_ARCHIVE);
	trap_Cvar_Register(&s_hitbox.dmg_chest,      "vanguard_dmg_chest",      "1.3", CVAR_ARCHIVE);
	trap_Cvar_Register(&s_hitbox.dmg_gut,        "vanguard_dmg_gut",        "1.1", CVAR_ARCHIVE);
	trap_Cvar_Register(&s_hitbox.dmg_groin,      "vanguard_dmg_groin",      "1.2", CVAR_ARCHIVE);
	trap_Cvar_Register(&s_hitbox.dmg_shoulder_l, "vanguard_dmg_shoulder_l", "0.8", CVAR_ARCHIVE);
	trap_Cvar_Register(&s_hitbox.dmg_shoulder_r, "vanguard_dmg_shoulder_r", "0.8", CVAR_ARCHIVE);
	trap_Cvar_Register(&s_hitbox.dmg_knee_l,     "vanguard_dmg_knee_l",     "0.6", CVAR_ARCHIVE);
	trap_Cvar_Register(&s_hitbox.dmg_knee_r,     "vanguard_dmg_knee_r",     "0.6", CVAR_ARCHIVE);
	trap_Cvar_Register(&s_hitbox.dmg_legs,       "vanguard_dmg_legs",       "0.7", CVAR_ARCHIVE);
	trap_Cvar_Register(&s_hitbox.dmg_default,    "vanguard_dmg_default",    "1.0", CVAR_ARCHIVE);

	G_Printf("VG_Hitbox: initialized (mode=%d)\n", s_hitbox.mode.integer);
}

void vg_Hitbox_Shutdown(void)
{
	/* No-op currently — vmCvars are static, lifetime is module
	 * lifetime. Function exists for symmetry with Init and as a
	 * future hook for any teardown (e.g. memory pools, hit_count
	 * tracking, stats flush). */
	memset(&s_hitbox, 0, sizeof(s_hitbox));
}

float vg_Hitbox_DamageMultiplierFor(animScriptImpactPoint_t impactpoint)
{
	/* TODO Phase 6.1.2 — switch on impactpoint, return matching
	 * cvar.value, fall back to s_hitbox.dmg_default.value. */
	(void)impactpoint;
	return 1.0f;
}

int vg_Hitbox_RegionFor(animScriptImpactPoint_t impactpoint)
{
	/* TODO Phase 6.1.2 — bucket impactpoints into a coarser
	 * HR_HEAD/HR_BODY/HR_LIMB/HR_NONE for stats aggregation. */
	(void)impactpoint;
	return 0;
}

const char *vg_Hitbox_RegionName(animScriptImpactPoint_t impactpoint)
{
	/* TODO Phase 6.1.2 — return "head"/"chest"/.../"legs" for
	 * stats output and admin log lines. */
	(void)impactpoint;
	return "unknown";
}
