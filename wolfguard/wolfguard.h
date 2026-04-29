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
 * wolfguard.h — Public WolfGuard interface
 *
 * This header defines the entire public hook surface that VanguardMod uses to
 * integrate with anti-cheat providers. The closed-source detection logic
 * lives in a separate, private repository that is cloned into
 * wolfguard/private/ at build time. When that repo is absent, the null
 * provider in wolfguard_null.c is used instead, yielding a clean community
 * build.
 *
 * Stability: this header is the contract between the public mod code and
 * any provider. Bumping WG_API_VERSION is a breaking change for private
 * implementations and must be coordinated.
 *
 * Concurrency: all WG_* functions are called from the main game thread.
 * Providers must not assume any other thread context.
 */

#ifndef VANGUARD_WOLFGUARD_H
#define VANGUARD_WOLFGUARD_H

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Versioning and build identity                                      */
/* ------------------------------------------------------------------ */

#define WG_API_VERSION       1

#define WG_BUILD_COMMUNITY   0
#define WG_BUILD_PROTECTED   1

/* ------------------------------------------------------------------ */
/* Result and tier enums                                              */
/* ------------------------------------------------------------------ */

typedef enum {
    WG_OK                    =  0,
    WG_ERR_GENERIC           = -1,
    WG_ERR_INIT              = -2,
    WG_ERR_PROVIDER_MISSING  = -3,
    WG_ERR_NOT_REGISTERED    = -4,
    WG_ERR_API_MISMATCH      = -5,
    WG_VIOLATION_DETECTED    =  1,
    WG_VIOLATION_BANNED      =  2
} wg_result_t;

typedef enum {
    WG_TIER_OBSERVED   = 0,   /* logged only, no action                  */
    WG_TIER_KICK       = 1,   /* kicked from this match                  */
    WG_TIER_TEMP_BAN   = 2,   /* time-limited server ban                 */
    WG_TIER_GLOBAL_BAN = 3    /* global ban via vanguardmod.com backend  */
} wg_ban_tier_t;

/* ------------------------------------------------------------------ */
/* Provider info struct                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    int         api_version;     /* must equal WG_API_VERSION           */
    int         build_type;      /* WG_BUILD_COMMUNITY | WG_BUILD_PROTECTED */
    const char *provider_name;   /* "null", "wolfguard", ...            */
    const char *provider_version;
} wg_info_t;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

/*
 * Called once during G_InitGame after the server identity is known.
 * server_id should be the cvar value of sv_serverid (or any stable
 * identifier this server registered on vanguardmod.com).
 * config_path may be NULL; if non-NULL, points at a provider-specific
 * configuration file (absolute path).
 */
wg_result_t WG_Init(const char *server_id, const char *config_path);

/* Called from G_ShutdownGame. Idempotent. */
void WG_Shutdown(void);

/* Returns a pointer to a static struct describing the linked provider. */
const wg_info_t *WG_GetInfo(void);

/* ------------------------------------------------------------------ */
/* Per-client hooks                                                   */
/* ------------------------------------------------------------------ */

wg_result_t WG_OnClientConnect(int client_num, const char *guid, const char *ip);
wg_result_t WG_OnClientDisconnect(int client_num);
wg_result_t WG_OnClientCommand(int client_num, const char *cmd);

/* ------------------------------------------------------------------ */
/* Per-frame / network                                                */
/* ------------------------------------------------------------------ */

/* Called from G_RunFrame, level_time is current level.time (ms). */
wg_result_t WG_OnFrame(int level_time);

/* Optional hook called immediately before a snapshot is sent to a client. */
wg_result_t WG_OnSnapshotSend(int client_num);

/* ------------------------------------------------------------------ */
/* Status query                                                       */
/* ------------------------------------------------------------------ */

/*
 * Returns the current ban tier the provider holds for client_num.
 * out_tier is written on WG_OK; otherwise unchanged.
 */
wg_result_t WG_QueryClientStatus(int client_num, wg_ban_tier_t *out_tier);

#ifdef __cplusplus
}
#endif

#endif /* VANGUARD_WOLFGUARD_H */
