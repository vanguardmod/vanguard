/*
 * wolfguard_null.c — Null WolfGuard provider
 *
 * Used in the community build when wolfguard/private/ is absent or
 * VANGUARD_WITH_WOLFGUARD=OFF. All hooks succeed without performing any
 * detection. This file is intentionally trivial: it documents the API
 * surface every provider must satisfy.
 */

#include "wolfguard.h"
#include <stddef.h>

static const wg_info_t s_info = {
    WG_API_VERSION,
    WG_BUILD_COMMUNITY,
    "null",
    "1.0.0"
};

wg_result_t WG_Init(const char *server_id, const char *config_path)
{
    (void)server_id;
    (void)config_path;
    return WG_OK;
}

void WG_Shutdown(void)
{
}

const wg_info_t *WG_GetInfo(void)
{
    return &s_info;
}

wg_result_t WG_OnClientConnect(int client_num, const char *guid, const char *ip)
{
    (void)client_num;
    (void)guid;
    (void)ip;
    return WG_OK;
}

wg_result_t WG_OnClientDisconnect(int client_num)
{
    (void)client_num;
    return WG_OK;
}

wg_result_t WG_OnClientCommand(int client_num, const char *cmd)
{
    (void)client_num;
    (void)cmd;
    return WG_OK;
}

wg_result_t WG_OnFrame(int level_time)
{
    (void)level_time;
    return WG_OK;
}

wg_result_t WG_OnSnapshotSend(int client_num)
{
    (void)client_num;
    return WG_OK;
}

wg_result_t WG_QueryClientStatus(int client_num, wg_ban_tier_t *out_tier)
{
    (void)client_num;
    if (out_tier) {
        *out_tier = WG_TIER_OBSERVED;
    }
    return WG_OK;
}
