/*!
    \file    wifi_management.c
    \brief   WiFi management for GD32VW55x SDK.

    \version 2023-07-20, V1.0.0, firmware for GD32VW55x
*/

/*
    Copyright (c) 2023, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software without
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/

/*============================ INCLUDES ======================================*/
#include "wifi_vif.h"
#include "wifi_management.h"
#include "wifi_export.h"
#include "wifi_wpa.h"
#include "state_machine.h"
#include "lwip/netifapi.h"
#include "lwip/dhcp.h"
#include "lwip/netifapi.h"
#include "lwip/ip_addr.h"
#include "wifi_net_ip.h"
#include "wifi_init.h"
#include "dbg_print.h"

/*============================ MACROS ========================================*/
#define STATE_MACHINE_DATA struct wifi_management_sm_data
#define STATE_MACHINE_DEBUG_PREFIX "WIFI_MGMT"

/*============================ MACRO FUNCTIONS ===============================*/
#define GET_SM_STATE(machine)  sm->machine ## _state

/*============================ TYPES =========================================*/
/*============================ GLOBAL VARIABLES ==============================*/
wifi_management_sm_data_t wifi_sm_data[CFG_VIF_NUM];
os_task_t wifi_mgmt_task_tcb;
os_queue_t mgmt_wait_queue;
#ifdef CFG_WIFI_CONCURRENT
uint8_t wifi_concurrent_mode = 0;
#endif
/*============================ LOCAL VARIABLES ===============================*/

/*============================ PROTOTYPES ====================================*/
SM_STATE(MAINTAIN_CONNECTION, IDLE);
SM_STATE(MAINTAIN_CONNECTION, SCAN);
#ifdef CFG_SOFTAP
SM_STATE(MAINTAIN_SOFTAP, INIT);
#endif
SM_STATE(MAINTAIN_MONITOR, INIT);
/*============================ IMPLEMENTATION ================================*/
/*!
    \brief      Fetch event from queue mgmt_wait_queue
    \param[in]  vif_idx: index of the wifi vif
    \param[in]  evt: event which to be fetched
    \param[in]  timeout_ms: timeout time
    \param[out] none
    \retval     reason of event on success and 0xFFFF if error occured.
*/
static uint16_t mgmt_wait_queue_fetch(uint8_t vif_idx, uint8_t evt, uint32_t timeout_ms)
{
    mgmt_wait_evt_t wmsg;
    int res;

    do {
        res = sys_queue_fetch(&mgmt_wait_queue, &wmsg, timeout_ms, 1);
        if (OS_TIMEOUT == res) {
            wifi_sm_printf(WIFI_SM_INFO, "Wait timeout.\r\n");
            wmsg.reason = 0xFFFF;
            break;
        }
        if ((vif_idx == wmsg.vif) && (evt == wmsg.evt)) {
            break;
        }
    } while (1);

    return wmsg.reason;
}

/*!
    \brief      Post event MGMT_WAIT_EVT_CONN_DONE to queue mgmt_wait_queue
    \param[in]  vif_idx: index of the wifi vif
    \param[in]  reason: reason of event
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
static int mgmt_post_conn_done(uint8_t vif_idx, uint16_t reason)
{
    mgmt_wait_evt_t wmsg;
    struct wifi_sta *config_sta = &wifi_vif_tab[vif_idx].sta;

    if (config_sta->cfg.conn_blocked == 0)
        return 0;
    config_sta->cfg.conn_blocked = 0;

    wmsg.vif = vif_idx;
    wmsg.evt = MGMT_WAIT_EVT_CONN_DONE;
    wmsg.reason = reason;

    return sys_queue_post(&mgmt_wait_queue, &wmsg);
}

/*!
    \brief      Post event MGMT_WAIT_EVT_DISCONN_DONE to queue mgmt_wait_queue
    \param[in]  vif_idx: index of the wifi vif
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
static int mgmt_post_disconn_done(uint8_t vif_idx)
{
    mgmt_wait_evt_t wmsg;

    wmsg.vif = vif_idx;
    wmsg.evt = MGMT_WAIT_EVT_DISCONN_DONE;
    wmsg.reason = 0;

    return sys_queue_post(&mgmt_wait_queue, &wmsg);
}

/*!
    \brief      Post event MGMT_WAIT_EVT_SCAN_DONE to queue mgmt_wait_queue
    \param[in]  vif_idx: index of the wifi vif
    \param[in]  reason: reason of event
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
static int mgmt_post_scan_done(uint8_t vif_idx, uint16_t reason)
{
    mgmt_wait_evt_t wmsg;

    if (wifi_sm_data[vif_idx].scan_blocked == 0)
        return 0;
    wifi_sm_data[vif_idx].scan_blocked = 0;

    wmsg.vif = vif_idx;
    wmsg.evt = MGMT_WAIT_EVT_SCAN_DONE;
    wmsg.reason = reason;

    return sys_queue_post(&mgmt_wait_queue, &wmsg);
}

/*!
    \brief      Post event MGMT_WAIT_EVT_AP_START_DONE to queue mgmt_wait_queue
    \param[in]  vif_idx: index of the wifi vif
    \param[in]  reason: reason of event
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
static int mgmt_post_ap_start_done(uint8_t vif_idx, uint16_t reason)
{
    mgmt_wait_evt_t wmsg;

    wmsg.vif = vif_idx;
    wmsg.evt = MGMT_WAIT_EVT_AP_START_DONE;
    wmsg.reason = reason;

    return sys_queue_post(&mgmt_wait_queue, &wmsg);
}

/*!
    \brief      Post event MGMT_WAIT_EVT_MONITOR_START_DONE to queue mgmt_wait_queue
    \param[in]  vif_idx: index of the wifi vif
    \param[in]  reason: reason of event
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
static int mgmt_post_monitor_start_done(uint8_t vif_idx, uint16_t reason)
{
    mgmt_wait_evt_t wmsg;

    wmsg.vif = vif_idx;
    wmsg.evt = MGMT_WAIT_EVT_MONITOR_START_DONE;
    wmsg.reason = reason;

    return sys_queue_post(&mgmt_wait_queue, &wmsg);
}

/*!
    \brief      Flush queue mgmt_wait_queue
    \param[in]  none
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
static int mgmt_wait_queue_flush(void)
{
    mgmt_wait_evt_t wmsg;
    int res = 0;

    if (NULL == mgmt_wait_queue) {
        return -1;
    }
    while (!sys_queue_is_empty(&mgmt_wait_queue)) {
        res = sys_queue_fetch(&mgmt_wait_queue, &wmsg, 0, 0);
    };

    return res;
}

/************************ WiFi Management Timeouts ****************************/
/*!
    \brief      Callback function for dhcp polling
    \param[in]  eloop_data: pointer to the eloop data
    \param[in]  user_ctx: pointer to the user parameters
    \param[out] none
    \retval     none
*/
static void mgmt_dhcp_polling(void *eloop_data, void *user_ctx)
{
    wifi_management_sm_data_t *sm = eloop_data;
    struct netif *net_if = vif_idx_to_net_if(sm->vif_idx);
    struct wifi_ip_addr_cfg cfg;

    if (net_dhcp_address_obtained(net_if) || net_if_is_static_ip()) {
        net_if_get_ip(net_if, &(cfg.ipv4.addr), &(cfg.ipv4.mask), &(cfg.ipv4.gw));
        net_get_dns(&cfg.ipv4.dns);

        wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": IPv4 addr got " IP_FMT "\r\n", IP_ARG(cfg.ipv4.addr));

        net_if_set_default(net_if);
        net_if_send_gratuitous_arp(net_if);
        eloop_event_send(sm->vif_idx, WIFI_MGMT_EVENT_DHCP_SUCCESS);
        return;
    }

    if (--sm->dhcp_polling_count) {
        if ((sm->dhcp_polling_count & 0xF) == 0) {
            wifi_sm_printf(WIFI_SM_INFO, STATE_MACHINE_DEBUG_PREFIX ": waiting for DHCP done\r\n");
        }
        eloop_timeout_register(WIFI_MGMT_DHCP_POLLING_INTERVAL, mgmt_dhcp_polling, sm, NULL);
    } else {
        wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": DHCP: IP request timeout!\r\n");
        sm->reason = WIFI_MGMT_CONN_DHCP_FAIL;
        eloop_event_send(sm->vif_idx, WIFI_MGMT_EVENT_DHCP_FAIL);
    }
}

#ifdef CONFIG_IPV6_SUPPORT
/*!
    \brief      Callback function for ipv6 addr polling
    \param[in]  eloop_data: pointer to the eloop data
    \param[in]  user_ctx: pointer to the user parameters
    \param[out] none
    \retval     none
*/
static void mgmt_ipv6_polling(void *eloop_data, void *user_ctx)
{
    wifi_management_sm_data_t *sm = eloop_data;
    struct netif *net_if = vif_idx_to_net_if(sm->vif_idx);

    if (wifi_ipv6_is_got(sm->vif_idx)) {
        wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": IPv6 addr got %s\r\n", ip6addr_ntoa(ip_2_ip6(&net_if->ip6_addr[1])));
    } else if (net_if->rs_count) {
        eloop_timeout_register(WIFI_MGMT_IPV6_POLLING_INTERVAL, mgmt_ipv6_polling, sm, NULL);
    } else {
        wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": IPv6 addr got timeout!\r\n");
        wifi_ip6_unique_addr_set_invalid(net_if);
    }
}
#endif /* CONFIG_IPV6_SUPPORT */

#if 0
/*!
    \brief      Callback function for link status polling
    \param[in]  eloop_data: pointer to the eloop data
    \param[in]  user_ctx: pointer to the user parameters
    \param[out] none
    \retval     none
*/
static void mgmt_link_status_polling(void *eloop_data, void *user_ctx)
{
    wifi_management_sm_data_t *sm = eloop_data;
    int rssi, ret;

    wifi_sm_printf(WIFI_SM_DEBUG, STATE_MACHINE_DEBUG_PREFIX ": polling link status\r\n");

    if (sm->roaming_en) {
        rssi = macif_vif_sta_rssi_get(sm->vif_idx);
        if (rssi < sm->rssi_th && (sm->polling_scan_count--)) {
            wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": rssi(%d) < rssi_th(%d), start polling scan\r\n",
                        rssi, sm->rssi_th);
            ret = wifi_netlink_scan_set(sm->vif_idx, 0xFF);
            if (ret) {
                wifi_sm_printf(WIFI_SM_WARNING, STATE_MACHINE_DEBUG_PREFIX ": start scan failed %d\r\n", ret);
            }
            sm->polling_scan = true;

            if (sm->polling_scan_count == 0)
                sm->polling_scan_count = WIFI_MGMT_POLLING_SCAN_LONG_LIMIT;
        }
    }
    eloop_timeout_register(WIFI_MGMT_LINK_POLLING_INTERVAL, mgmt_link_status_polling, sm, NULL);
}
#else
/*!
    \brief      Callback function for link status polling
    \param[in]  eloop_data: pointer to the eloop data
    \param[in]  user_ctx: pointer to the user parameters
    \param[out] none
    \retval     none
*/
static void mgmt_link_status_polling(void *eloop_data, void *user_ctx)
{
    wifi_management_sm_data_t *sm = eloop_data;
    int ret = 0;

    if (sm->preroam_enable && sm->preroam_start) {
        int8_t rssi = macif_vif_sta_rssi_get(sm->vif_idx);
        int8_t rssi_thresh = macif_vif_roaming_rssi_get(sm->vif_idx);

        if (rssi > rssi_thresh + VIF_RSSI_HYSTERESIS) {
            sm->preroam_start = 0;
            sm->polling_scan_count = 0;
            sys_memset(sm->preroam_bssid_bk, 0, sizeof(sm->preroam_bssid_bk));
            return;
        }

        if (sm->polling_scan_count < 10
                || (sm->polling_scan_count % WIFI_MGMT_POLLING_SCAN_TRIGGER_POINT == 0)) {
            wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": Start polling scan [%u]\r\n", sm->polling_scan_count);
#ifdef CFG_80211R
            if (sm->param) {
                ret = wifi_netlink_scan_set_with_ssid(sm->vif_idx, (char *)sm->param, 0xFF);
            } else {
                ret = wifi_netlink_scan_set(sm->vif_idx, 0xFF);
            }
#else
            ret = wifi_netlink_scan_set(sm->vif_idx, 0xFF);
#endif

            if (ret) {
                wifi_sm_printf(WIFI_SM_WARNING, STATE_MACHINE_DEBUG_PREFIX ": start scan failed %d\r\n", ret);
            }
            sm->polling_scan = true;
        }

        sm->polling_scan_count++;

        eloop_timeout_register(WIFI_MGMT_LINK_POLLING_INTERVAL, mgmt_link_status_polling, sm, NULL);
    }

}
#endif

/*!
    \brief      Retry to connect
    \param[in]  eloop_data: pointer to the eloop data
    \param[in]  user_ctx: pointer to the user parameters
    \param[out] none
    \retval     none
*/
static void mgmt_connect_retry(void *eloop_data, void *user_ctx)
{
    wifi_management_sm_data_t *sm = eloop_data;

    wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": retry to connect, remaining times %u\r\n",
                   sm->retry_count);

    sm->retry_count--;
    sm->delayed_connect_retry = 0;

    SM_ENTER(MAINTAIN_CONNECTION, SCAN);
}

/************************ WiFi Management Callbacks ***************************/
#ifdef CFG_80211R
static bool mgmt_is_ft_roaming(struct mac_scan_result* candidate, struct wifi_sta *sta)
{
    if (!candidate)
        return false;
#ifdef CONFIG_WPA_SUPPLICANT
    if (candidate->akm & CO_BIT(MAC_AKM_FT_PSK))
        return true;
#else
    if (!sta || !sta->ft)
        return false;

    if ((candidate->akm & CO_BIT(MAC_AKM_FT_PSK)) &&
        (sys_memcmp(candidate->md_ie, sta->ft->key_mobility_domain, MAC_INFOELT_MDE_MDID_LEN) == 0))
        return true;
#endif

    return false;
}
#endif /* CFG_80211R */

/*!
    \brief      Indicate that scan done in connection process
    \param[in]  sm: pointer to the state machine parameters
    \param[out] none
    \retval     none
*/
static void mgmt_connected_scan_done(wifi_management_sm_data_t *sm)
{
    struct sta_cfg *sta_cfg = &wifi_vif_tab[sm->vif_idx].sta.cfg;
    struct mac_scan_result candidate;
    int rssi, ret;
#ifdef CFG_80211R
    bool ft_roaming;
#endif /* CFG_80211R */

    wifi_sm_printf(WIFI_SM_INFO, STATE_MACHINE_DEBUG_PREFIX ": polling scan done\r\n");

    sm->polling_scan = false;

    ret = wifi_netlink_candidate_ap_find(sm->vif_idx, NULL, sta_cfg->ssid, &candidate);
    if (ret) {
        /* Not find any ap with the same ssid. Do nothing. */
        return;
    }

#ifdef CFG_80211R
    ft_roaming = mgmt_is_ft_roaming(&candidate, &wifi_vif_tab[sm->vif_idx].sta);
#endif /* CFG_80211R */

    if (sys_memcmp((uint8_t *)candidate.bssid.array, sta_cfg->bssid, WIFI_ALEN) == 0) {
        /* The current AP has the best signal strength. Do nothing. */
        wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": the current AP has the best rssi or no others\r\n");
        return;
    }

#ifdef CFG_80211R
    if(ft_roaming) {
        wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": try FT roaming to a better AP\r\n");
        sm->polling_scan_count = 0;
        eloop_timeout_cancel(mgmt_link_status_polling, ELOOP_ALL_CTX, ELOOP_ALL_CTX);
#ifdef CONFIG_WPA_SUPPLICANT
        char buffer[32];
        co_snprintf(buffer, sizeof(buffer), MAC_FMT, MAC_ARG(candidate.bssid.array));
        wifi_wpa_roaming_start(sm->vif_idx, buffer);
#else
        rssi = macif_vif_sta_rssi_get(sm->vif_idx);
        if (candidate.rssi >= rssi + WIFI_MGMT_ROAMING_RSSI_RELATIVE_GAIN) {
            wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": try roaming to a better AP\r\n");
            sta_cfg->channel = wifi_freq_to_channel(candidate.chan->freq);
            struct mac_scan_result *target_ap = (struct mac_scan_result *)sys_malloc(sizeof(struct mac_scan_result));
            if (target_ap == NULL) {
                wifi_sm_printf(WIFI_SM_ERROR, STATE_MACHINE_DEBUG_PREFIX ": Failed to allocate memory for target AP\r\n");
                return;
            }
            sys_memcpy(target_ap, &candidate, sizeof(struct mac_scan_result));
            wifi_sm_printf(WIFI_SM_INFO, STATE_MACHINE_DEBUG_PREFIX ": target AP found ("MAC_FMT")\r\n", MAC_ARG(candidate.bssid.array));
            eloop_message_send(sm->vif_idx, WIFI_MGMT_EVENT_FT_ROAMING_CMD, 0,
                    (uint8_t *)target_ap, sizeof(struct mac_scan_result));
            sys_mfree(target_ap);
        } else {
            wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": the targe ap isn't good enough(%d - %d < %d)\r\n",
                candidate.rssi, rssi, WIFI_MGMT_ROAMING_RSSI_RELATIVE_GAIN);
        }
#endif
    } else
#endif /* CFG_80211R */
    {
        wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": try roaming to a better AP\r\n");
        rssi = macif_vif_sta_rssi_get(sm->vif_idx);
        if (candidate.rssi >= rssi + WIFI_MGMT_ROAMING_RSSI_RELATIVE_GAIN) {
            wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": try roaming to a better AP\r\n");
            sta_cfg->channel = wifi_freq_to_channel(candidate.chan->freq);
            eloop_event_send(sm->vif_idx, WIFI_MGMT_EVENT_CONNECT_CMD);
        } else {
            wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": the targe ap isn't good enough(%d - %d < %d)\r\n",
                candidate.rssi, rssi, WIFI_MGMT_ROAMING_RSSI_RELATIVE_GAIN);
        }
    }
}

static void mgmt_preroam_rollback(wifi_management_sm_data_t *sm)
{
    struct sta_cfg *sta_cfg = &wifi_vif_tab[sm->vif_idx].sta.cfg;
    uint8_t zero_addr[] = {0, 0, 0, 0, 0, 0};

    if (sm->preroam_start && sys_memcmp(sm->preroam_bssid_bk, zero_addr, WIFI_ALEN)) {
        sm->preroam_start = 0;
        sm->polling_scan_count = 0;
        sta_cfg->channel = 0xFF;
        sys_memcpy(sta_cfg->bssid, sm->preroam_bssid_bk, WIFI_ALEN);
        sys_memset(sm->preroam_bssid_bk, 0, sizeof(sm->preroam_bssid_bk));
        sta_cfg->conn_with_bssid = true;
        wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": preroam rollback to link with original ap ("MAC_FMT")\r\n", MAC_ARG_UINT8(sta_cfg->bssid));
        eloop_event_send(sm->vif_idx, WIFI_MGMT_EVENT_CONNECT_CMD);
    }
}
/*!
    \brief      Set parameters for connection retry
    \param[in]  sm: pointer to the state machine parameters
    \param[in]  roaming_required: whether roaming is required
    \param[out] none
    \retval     none
*/
static void mgmt_connect_retry_param_set(wifi_management_sm_data_t *sm, uint8_t roaming_required)
{
    if (roaming_required == 1) {
        if (WIFI_MGMT_UNLIMITED_ROAMING_RETRY()) {
            sm->retry_count = WIFI_MGMT_ROAMING_RETRY_UNLIMITED - 1;
            sm->retry_limit = WIFI_MGMT_ROAMING_RETRY_UNLIMITED;
        } else {
            sm->retry_count = WIFI_MGMT_ROAMING_RETRY_LIMIT - 1;
            sm->retry_limit = WIFI_MGMT_ROAMING_RETRY_LIMIT;
        }
        sm->retry_interval = WIFI_MGMT_ROAMING_RETRY_INTERVAL;
    } else {
        sm->retry_count = WIFI_MGMT_CONNECT_RETRY_LIMIT - 1;
        sm->retry_limit = WIFI_MGMT_CONNECT_RETRY_LIMIT;
        sm->retry_interval = WIFI_MGMT_CONNECT_RETRY_INTERVAL;
    }
}

static void mgmt_register_delayed_connect_retry(wifi_management_sm_data_t *sm, void *param)
{
    uint32_t retry_interval;

    if ((sm->retry_limit - sm->retry_count) > 30)
        retry_interval = sm->retry_interval * (sm->retry_limit - sm->retry_count - 30);
    else
        retry_interval = sm->retry_interval;
    if (retry_interval > WIFI_MGMT_MAX_RETRY_INTERVAL)
        retry_interval = WIFI_MGMT_MAX_RETRY_INTERVAL;

    if (!eloop_timeout_is_registered(mgmt_connect_retry, sm, param))
        eloop_timeout_register(retry_interval, mgmt_connect_retry, sm, param);
}

static void mgmt_link_is_ongoing(wifi_management_sm_data_t *sm)
{
    struct sta_cfg *sta_cfg = &wifi_vif_tab[sm->vif_idx].sta.cfg;

    wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX \
                    ": Link is ongoing... \r\n");
    wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX \
                    ": Please wait for a minute or input \"wifi_disconnect\".\r\n");

    sta_cfg->conn_blocked = 1;
    mgmt_post_conn_done(sm->vif_idx, MAC_ST_FAILURE);
}

/*!
    \brief      Switch wifi vif mode
    \param[in]  sm: pointer to the state machine parameters
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
static int mgmt_switch_mode(wifi_management_sm_data_t *sm)
{
    struct wifi_vif_tag *wvif = &wifi_vif_tab[sm->vif_idx];
    enum wifi_vif_type prev_type = WVIF_UNKNOWN;
    struct wifi_ip_addr_cfg ip_cfg;
    int vif_idx = sm->vif_idx;
    int wvif_type = sm->reason;
    int global = 0;

    // Do nothing if interface type is already the requested one
    prev_type = wvif->wvif_type;
    if (prev_type == wvif_type)
        return 0;

    wifi_sm_printf(WIFI_SM_DEBUG, STATE_MACHINE_DEBUG_PREFIX
               ": vif %u switch mode from %d to %d.\r\n", vif_idx, prev_type, wvif_type);

    /* 1. Close current connection (if any) */
    ip_cfg.mode = IP_ADDR_NONE;
#ifdef CONFIG_IPV6_SUPPORT
    ip_cfg.ip6_mode = IP6_ADDR_NONE;
#endif
    wifi_set_vif_ip(vif_idx, &ip_cfg);
    net_if_down(&wvif->net_if);

    /* 2. Reset original state machine */
    switch (prev_type) {
    case WVIF_STA:
        SM_ENTER(MAINTAIN_CONNECTION, IDLE);
        wifi_wpa_sta_pmksa_cache_flush(vif_idx, 1);
        break;
#ifdef CFG_SOFTAP
    case WVIF_AP:
        SM_ENTER(MAINTAIN_SOFTAP, INIT);
        break;
#endif
    case WVIF_MONITOR:
        SM_ENTER(MAINTAIN_MONITOR, INIT);
        break;
    default:
        break;
    }
    //eloop_timeout_all_cancel();  //For test

    /* 3. Reset wvif cfg */
    wifi_vif_reset(vif_idx, prev_type);

    /* 4. Set new wvif type */
    wvif->wvif_type = wvif_type;
    if (wifi_vif_type_set(vif_idx, WVIF_UNKNOWN)
        || wifi_vif_type_set(vif_idx, wvif_type)) {
        return -2;
    }

    /* 5. Init new state machine */
//    sys_memset(sm, 0, sizeof(*sm));
    sm->event = 0;
    sm->reason = 0;
    sm->init = true;

    switch (wvif_type) {
    case WVIF_STA:
        // Reset STA related params
        do {
            sm->polling_scan = false;
            mgmt_connect_retry_param_set(sm, 0);
        } while(0);
        SM_ENTRY(MAINTAIN_CONNECTION, IDLE);
        wifi_netlink_ps_mode_set(WIFI_VIF_INDEX_DEFAULT, WIFI_STA_PS_MODE_OFF);

        wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX
               ": vif%d switch to station mode at %d\r\n", vif_idx, sys_os_now(0));
        break;
#ifdef CFG_SOFTAP
    case WVIF_AP:
        SM_ENTRY(MAINTAIN_SOFTAP, INIT);
        break;
#endif
    case WVIF_MONITOR:
        SM_ENTRY(MAINTAIN_MONITOR, INIT);
        break;
#ifdef CFG_WIFI_CONCURRENT
    case WVIF_UNKNOWN:
        if (wifi_concurrent_mode) {
            if (prev_type == WVIF_AP) {
                wifi_netlink_enable_vif_ps(WIFI_VIF_INDEX_STA_MODE);
            }
        }
        break;
#endif
    default:
        break;
    }

    return 0;
}

/***************************** WiFi Management State Machine ******************/
SM_STATE(MAINTAIN_CONNECTION, IDLE)
{
    struct wifi_vif_tag *wvif = &wifi_vif_tab[sm->vif_idx];
    int ret;

    SM_ENTRY(MAINTAIN_CONNECTION, IDLE);
#ifdef CONFIG_WPS
    sm->wps_state = WPS_STATE_INIT;
#endif
    sm->delayed_connect_retry = 0;

    eloop_timeout_cancel(mgmt_dhcp_polling, ELOOP_ALL_CTX, ELOOP_ALL_CTX);
    eloop_timeout_cancel(mgmt_link_status_polling, ELOOP_ALL_CTX, ELOOP_ALL_CTX);
    eloop_timeout_cancel(mgmt_connect_retry, ELOOP_ALL_CTX, ELOOP_ALL_CTX);

    ret = wifi_netlink_disconnect_req(sm->vif_idx);
    if (ret) {
        wifi_sm_printf(WIFI_SM_ERROR, STATE_MACHINE_DEBUG_PREFIX": disconnect req return %d\r\n", ret);
    }
    mgmt_post_disconn_done(sm->vif_idx);

    /*
     * it has to be put after disconnect done,
     * because in wpas_eapol_stop, the state is used
     * to clear keys.
    */
    wvif->sta.state = WIFI_STA_STATE_IDLE;
}

#ifdef CFG_WPS
SM_STATE(MAINTAIN_CONNECTION, WPS)
{
    struct wifi_vif_tag *wvif = &wifi_vif_tab[sm->vif_idx];
    struct wps_config_t *wps_cfg = (struct wps_config_t *)sm->param;
    int ret;

    SM_ENTRY(MAINTAIN_CONNECTION, WPS);
    wvif->sta.state = WIFI_STA_STATE_WPS;
    wvif->sta.cfg.conn_blocked = 1;
    sm->wps_state = WPS_STATE_SCAN;

    sys_memcpy(&(wvif->sta.cfg.wps_cfg), wps_cfg, sizeof(struct wps_config_t));
    ret = wifi_wpa_wps_start(sm->vif_idx);
    if (ret) {
        eloop_message_send(sm->vif_idx, WIFI_MGMT_EVENT_WPS_FAIL, ret, NULL, 0);
    }
}
#endif

SM_STATE(MAINTAIN_CONNECTION, SCAN)
{
    struct wifi_sta *config_sta = &wifi_vif_tab[sm->vif_idx].sta;
    int ret;
    uint8_t target_channel;

    SM_ENTRY(MAINTAIN_CONNECTION, SCAN);
    config_sta->state = WIFI_STA_STATE_SCAN;

    eloop_timeout_cancel(mgmt_dhcp_polling, ELOOP_ALL_CTX, ELOOP_ALL_CTX);
    eloop_timeout_cancel(mgmt_link_status_polling, ELOOP_ALL_CTX, ELOOP_ALL_CTX);

    if (sm->delayed_connect_retry) // delay the connect
        return;

    if (config_sta->last_reason == WIFI_MGMT_CONN_NO_AP ||
                config_sta->last_reason == WIFI_MGMT_DISCON_NO_BEACON ||
                config_sta->last_reason == WIFI_MGMT_DISCON_RECV_DEAUTH ||
                config_sta->last_reason == WIFI_MGMT_DISCON_UNSPECIFIED)
        target_channel = 0xFF;
    else
        target_channel = config_sta->cfg.channel;

    if (config_sta->cfg.conn_with_bssid)
        ret = wifi_netlink_scan_set(sm->vif_idx, target_channel);
    else
        ret = wifi_netlink_scan_set_with_ssid(sm->vif_idx, config_sta->cfg.ssid, target_channel);

    if (ret) {
        wifi_sm_printf(WIFI_SM_ERROR, STATE_MACHINE_DEBUG_PREFIX": start scan failed %d\r\n", ret);
        eloop_event_send(sm->vif_idx, WIFI_MGMT_EVENT_SCAN_FAIL);
    }
}


SM_STATE(MAINTAIN_CONNECTION, CONNECT)
{
    struct wifi_sta *config_sta = &wifi_vif_tab[sm->vif_idx].sta;
    int reason;

    SM_ENTRY(MAINTAIN_CONNECTION, CONNECT);
    config_sta->state = WIFI_STA_STATE_CONNECT;

    /* connect request */
    reason = wifi_netlink_connect_req(sm->vif_idx, &config_sta->cfg);
    if (reason) {
        eloop_message_send(sm->vif_idx, WIFI_MGMT_EVENT_CONNECT_FAIL, reason, NULL, 0);
        return;
    }

#ifdef CFG_WIFI_CONCURRENT
    if (wifi_concurrent_mode && wifi_vif_tab[WIFI_VIF_INDEX_SOFTAP_MODE].wvif_type == WVIF_AP) {
        if (WIFI_AP_STATE_STARTED == wifi_vif_tab[WIFI_VIF_INDEX_SOFTAP_MODE].ap.ap_state) {
            uint8_t softap_channel = 0;
            macif_vif_current_chan_get(WIFI_VIF_INDEX_SOFTAP_MODE, &softap_channel);
            if ((softap_channel > 0) && (softap_channel != config_sta->cfg.channel)) {
                /* The operation channel of the concurrent softap  must be synchronized with STA */
                eloop_message_send(WIFI_VIF_INDEX_SOFTAP_MODE, WIFI_MGMT_EVENT_AP_SWITCH_CHNL_CMD,
                                   config_sta->cfg.channel, NULL, 0);
            }
        }
    }
#endif
}

SM_STATE(MAINTAIN_CONNECTION, HANDSHAKE)
{
    struct wifi_vif_tag *wvif = &wifi_vif_tab[sm->vif_idx];

    SM_ENTRY(MAINTAIN_CONNECTION, HANDSHAKE);
    wvif->sta.state = WIFI_STA_STATE_HANDSHAKE;

    wifi_netlink_associate_done(sm->vif_idx, sm->param);
}


SM_STATE(MAINTAIN_CONNECTION, DHCP)
{
    struct wifi_ip_addr_cfg ip_cfg;
    struct wifi_vif_tag *wvif = &wifi_vif_tab[sm->vif_idx];
    struct netif *net_if = &wvif->net_if;

    SM_ENTRY(MAINTAIN_CONNECTION, DHCP);
    wvif->sta.state = WIFI_STA_STATE_IP_GETTING;

    if (!net_if)
        return;

    if (!net_if_is_static_ip()) {
        if (net_dhcp_address_obtained(net_if)) {
#if 0 /* Marked here to continue iperf after reconnect  */
            /* if ip has been get before, clear it and get a new one */
            ip_cfg.mode = IP_ADDR_NONE;
            wifi_set_vif_ip(sm->vif_idx, &ip_cfg);
#endif
        }

        sm->dhcp_polling_count = WIFI_MGMT_DHCP_POLLING_LIMIT;
        ip_cfg.mode = IP_ADDR_DHCP_CLIENT;
        ip_cfg.default_output = true;
        ip_cfg.dhcp.to_ms = 0;
        wifi_set_vif_ip(sm->vif_idx, &ip_cfg);
    }
    wifi_sm_printf(WIFI_SM_INFO, STATE_MACHINE_DEBUG_PREFIX ": start polling DHCP status\r\n");
    eloop_timeout_register(WIFI_MGMT_DHCP_POLLING_INTERVAL, mgmt_dhcp_polling, sm, NULL);
}

SM_STATE(MAINTAIN_CONNECTION, CONNECTED)
{
    struct wifi_vif_tag *wvif = &wifi_vif_tab[sm->vif_idx];
    struct wifi_sta *sta = &wifi_vif_tab[sm->vif_idx].sta;
    uint32_t ip;

    SM_ENTRY(MAINTAIN_CONNECTION, CONNECTED);
    wvif->sta.state = WIFI_STA_STATE_CONNECTED;
    wvif->sta.last_reason = 0;

    if (sta->cfg.conn_blocked == 0)
        eloop_event_send(sm->vif_idx, WIFI_MGMT_EVENT_CONNECT_SUCCESS);
    else
        mgmt_post_conn_done(sm->vif_idx, MAC_ST_SUCCESSFUL);

#ifdef CONFIG_IPV6_SUPPORT
    if (wifi_ipv6_is_got(sm->vif_idx)) {
        wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": DHCP got ip6 %s\r\n", ip6addr_ntoa(ip_2_ip6(&wvif->net_if.ip6_addr[1])));
    } else {
        eloop_timeout_register(WIFI_MGMT_IPV6_POLLING_INTERVAL, mgmt_ipv6_polling, sm, NULL);
    }
#endif /* CONFIG_IPV6_SUPPORT */

    wifi_sm_printf(WIFI_SM_INFO, STATE_MACHINE_DEBUG_PREFIX ": start polling link status\r\n");

    sm->preroam_start = 0;
    sm->polling_scan_count = 0;
    sys_memset(sm->preroam_bssid_bk, 0, sizeof(sm->preroam_bssid_bk));
    sta->cfg.conn_with_bssid = false;  // clear here to find the same ssid with higher rssi after disconnect

    net_if_get_ip(&wvif->net_if, &ip, NULL, NULL);
#ifdef CONFIG_FAST_RECONNECT
    wvif->sta.history_ip = ip;
#endif
    if (wifi_netlink_auto_conn_get()) {
        wifi_netlink_joined_ap_store(&wvif->sta.cfg, ip);
    }
}

SM_STEP(MAINTAIN_CONNECTION)
{
    if (!sm->init || sm->event == WIFI_MGMT_EVENT_INIT) {
        sys_memset(sm, 0, sizeof(*sm));
        sm->init = true;
        sm->preroam_enable = 1;
        mgmt_connect_retry_param_set(sm, 0);

        SM_ENTER(MAINTAIN_CONNECTION, IDLE);
    } else if (GET_SM_STATE(MAINTAIN_CONNECTION) == MAINTAIN_CONNECTION_IDLE) {
        switch (sm->event) {
        case WIFI_MGMT_EVENT_SCAN_CMD:
            if (sm->param) {
                if (wifi_netlink_scan_set_with_ssid(sm->vif_idx, (char *)sm->param, 0xFF)) {
                    wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": start scan failed\r\n");
                    mgmt_post_scan_done(sm->vif_idx, WIFI_MGMT_SCAN_START_FAIL);
                }
            } else {
                if (wifi_netlink_scan_set(sm->vif_idx, 0xFF)) {
                    wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": start scan failed\r\n");
                    mgmt_post_scan_done(sm->vif_idx, WIFI_MGMT_SCAN_START_FAIL);
                }
            }
            break;
        case WIFI_MGMT_EVENT_CONNECT_CMD:
            if (sm->param)
                sys_memcpy(&wifi_vif_tab[sm->vif_idx].sta.cfg, sm->param, sm->param_len);
            mgmt_connect_retry_param_set(sm, 0);
            SM_ENTER(MAINTAIN_CONNECTION, SCAN);
            break;
        case WIFI_MGMT_EVENT_AUTO_CONNECT_CMD:
            if (wifi_netlink_joined_ap_load(sm->vif_idx) != 0) {
                SM_ENTER(MAINTAIN_CONNECTION, IDLE);
            } else {
                /// retry as roaming
                mgmt_connect_retry_param_set(sm, 1);
                SM_ENTER(MAINTAIN_CONNECTION, SCAN);
            }
            break;
        case WIFI_MGMT_EVENT_SCAN_DONE:
            mgmt_post_scan_done(sm->vif_idx, WIFI_MGMT_SCAN_SUCCESS);
            break;
        case WIFI_MGMT_EVENT_DISCONNECT_CMD:
            mgmt_post_disconn_done(sm->vif_idx);
            break;
#ifdef CFG_WPS
        case WIFI_MGMT_EVENT_WPS_CMD:
            SM_ENTER(MAINTAIN_CONNECTION, WPS);
            break;
#endif
        case WIFI_MGMT_EVENT_SCAN_FAIL:
        case WIFI_MGMT_EVENT_CONNECT_FAIL:
        case WIFI_MGMT_EVENT_DISCONNECT:
            break;
        default:
            goto unexpected_events;
        }
#ifdef CFG_WPS
    } else if (GET_SM_STATE(MAINTAIN_CONNECTION) == MAINTAIN_CONNECTION_WPS) {
        switch (sm->event) {
        case WIFI_MGMT_EVENT_SCAN_RESULT:
            if (sm->wps_state == WPS_STATE_SCAN) {
                int ret = wifi_wpa_wps_ssid_bss_match(sm->vif_idx, sm->param, sm->param_len);
                if (ret == 1 && (NULL == sm->wps_bcn)) { //find ap with wps active
                    eloop_timeout_cancel(wifi_wpa_wps_scan_timer, (void *)sm->vif_idx, NULL);
                    sm->wps_bcn = sm->param;
                    sm->wps_bcn_len = sm->param_len;
                    sm->param = NULL;
                }
            }
            break;
        case WIFI_MGMT_EVENT_SCAN_DONE:
            if (sm->wps_state == WPS_STATE_SCAN) {
                int reason;
                if (sm->wps_bcn && sm->wps_bcn_len) {
                    sm->wps_state = WPS_STATE_CONNECT;
                    reason = wifi_wpa_wps_associate(sm->vif_idx, sm->wps_bcn, sm->wps_bcn_len);
                    if (reason) {
                        eloop_message_send(sm->vif_idx, WIFI_MGMT_EVENT_CONNECT_FAIL, reason, NULL, 0);
                    }
                    sys_mfree(sm->wps_bcn);
                    sm->wps_bcn = NULL;
                    sm->wps_bcn_len = 0;
                } else {
                    if (!eloop_timeout_is_registered(wifi_wpa_wps_scan_timer, (void *)sm->vif_idx, NULL))
                        wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": start wps scanning.\r\n");
                        eloop_timeout_register(2000, wifi_wpa_wps_scan_timer, (void *)sm->vif_idx, NULL);
                }
            }
            break;
        case WIFI_MGMT_EVENT_ASSOC_SUCCESS:
            if (sm->wps_state == WPS_STATE_CONNECT) {
                sm->wps_state = WPS_STATE_EAP_HANDSHAKE;
                wifi_wpa_wps_associate_done(sm->vif_idx, sm->param);
            }
            break;
        case WIFI_MGMT_EVENT_RX_EAPOL:
            wifi_wpa_sta_sm_step(sm->vif_idx, WIFI_MGMT_EVENT_RX_EAPOL, sm->param, sm->param_len, WIFI_STA_SM_EAPOL);
            break;
        case WIFI_MGMT_EVENT_DISCONNECT_CMD:
            wifi_wpa_wps_stop(sm->vif_idx);
            SM_ENTER(MAINTAIN_CONNECTION, IDLE);
            break;
        case WIFI_MGMT_EVENT_WPS_SUCCESS:
            sm->wps_state = WPS_STATE_DONE;
            break;
        case WIFI_MGMT_EVENT_DISCONNECT:
            if (sm->wps_state <= WPS_STATE_SCAN)
                break;
            wifi_wpa_wps_stop(sm->vif_idx);
            if (sm->wps_cred && sm->wps_state == WPS_STATE_DONE) {
                struct sta_cfg *cfg = &wifi_vif_tab[sm->vif_idx].sta.cfg;
                struct wps_cred_t *cred = sm->wps_cred;
                if (cred->ssid_len) {
                    sys_memset(cfg->ssid, 0, sizeof(cfg->ssid));
                    if (cred->ssid_len <= sizeof(cfg->ssid))
                        cfg->ssid_len = cred->ssid_len;
                    else
                        cfg->ssid_len = sizeof(cfg->ssid);
                    sys_memcpy(cfg->ssid, cred->ssid, cfg->ssid_len);
                }
                if (cred->passphrase_len) {
                    sys_memset(cfg->passphrase, 0, sizeof(cfg->passphrase));
                    if (cred->passphrase_len <= sizeof(cfg->passphrase))
                        cfg->passphrase_len = cred->passphrase_len;
                    else
                        cfg->passphrase_len = sizeof(cfg->passphrase);
                    sys_memcpy(cfg->passphrase, cred->passphrase, cfg->passphrase_len);
                }
                cfg->channel = 0xFF;
                sys_mfree(sm->wps_cred);
                sm->wps_cred = NULL;
                mgmt_connect_retry_param_set(sm, 0);
                SM_ENTER(MAINTAIN_CONNECTION, SCAN);
            } else {
                mgmt_post_conn_done(sm->vif_idx, WIFI_MGMT_CONN_WPS_FAIL);
                SM_ENTER(MAINTAIN_CONNECTION, IDLE);
            }
            break;
        case WIFI_MGMT_EVENT_WPS_CRED:
            if (sm->param) {
                sm->wps_cred = (struct wps_cred_t *)sm->param;
                sm->param = NULL;
            }
            break;
        case WIFI_MGMT_EVENT_SCAN_FAIL:
        case WIFI_MGMT_EVENT_CONNECT_FAIL:
            case WIFI_MGMT_EVENT_WPS_FAIL:
            wifi_wpa_wps_stop(sm->vif_idx);
            mgmt_post_conn_done(sm->vif_idx, WIFI_MGMT_CONN_WPS_FAIL);
            SM_ENTER(MAINTAIN_CONNECTION, IDLE);
            break;
        default:
            goto unexpected_events;
        }
#endif /* CFG_WPS */
    } else if (GET_SM_STATE(MAINTAIN_CONNECTION) == MAINTAIN_CONNECTION_SCAN) {
        switch (sm->event) {
        case WIFI_MGMT_EVENT_SCAN_CMD:
        case WIFI_MGMT_EVENT_CONNECT_CMD:
            mgmt_link_is_ongoing(sm);
            break;
        case WIFI_MGMT_EVENT_DISCONNECT_CMD:
        case WIFI_MGMT_EVENT_CONNECT_FAIL:
            SM_ENTER(MAINTAIN_CONNECTION, IDLE);
            break;
        case WIFI_MGMT_EVENT_SCAN_DONE:
            SM_ENTER(MAINTAIN_CONNECTION, CONNECT);
            break;
        case WIFI_MGMT_EVENT_SCAN_FAIL:
            if (sm->retry_count > 0) {
                mgmt_register_delayed_connect_retry(sm, NULL);
            } else {
                wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": all connecting retries failed\r\n");
                mgmt_preroam_rollback(sm);
                mgmt_post_conn_done(sm->vif_idx, sm->reason);
                SM_ENTER(MAINTAIN_CONNECTION, IDLE);
            }
            break;
        default:
            goto unexpected_events;
        }
    } else if (GET_SM_STATE(MAINTAIN_CONNECTION) == MAINTAIN_CONNECTION_CONNECT) {
        switch (sm->event) {
        case WIFI_MGMT_EVENT_SCAN_CMD:
        case WIFI_MGMT_EVENT_CONNECT_CMD:
            mgmt_link_is_ongoing(sm);
            break;
        case WIFI_MGMT_EVENT_SCAN_DONE:
        case WIFI_MGMT_EVENT_SCAN_FAIL:
            break;
        case WIFI_MGMT_EVENT_SCAN_RESULT:
            wifi_wpa_sta_sm_step(sm->vif_idx, sm->event, sm->param, sm->param_len, WIFI_STA_SM_EAPOL);
            break;
        case WIFI_MGMT_EVENT_DISCONNECT_CMD:
            SM_ENTER(MAINTAIN_CONNECTION, IDLE);
            break;
        case WIFI_MGMT_EVENT_EXTERNAL_AUTH_REQUIRED:
            wifi_wpa_sta_sm_step(sm->vif_idx, WIFI_MGMT_EVENT_EXTERNAL_AUTH_REQUIRED, NULL, 0, WIFI_STA_SM_SAE);
            break;
        case WIFI_MGMT_EVENT_RX_MGMT:
            wifi_wpa_sta_sm_step(sm->vif_idx, WIFI_MGMT_EVENT_RX_MGMT, sm->param, sm->param_len, WIFI_STA_SM_SAE);
            break;
        case WIFI_MGMT_EVENT_ASSOC_SUCCESS:
            SM_ENTER(MAINTAIN_CONNECTION, HANDSHAKE);
            break;
        case WIFI_MGMT_EVENT_CONNECT_FAIL:
        case WIFI_MGMT_EVENT_DISCONNECT:
        {
            struct wifi_sta *config_sta = &wifi_vif_tab[sm->vif_idx].sta;

            config_sta->last_reason = sm->reason;
            wifi_wpa_sta_sm_step(sm->vif_idx, WIFI_MGMT_EVENT_DISCONNECT, NULL, 0, WIFI_STA_SM_SAE);
            if (sm->retry_count > 0) {
                sm->delayed_connect_retry = 1;
                SM_ENTER(MAINTAIN_CONNECTION, SCAN);
                mgmt_register_delayed_connect_retry(sm, NULL);
            } else {
                wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": all connecting retries failed\r\n");
                mgmt_preroam_rollback(sm);
                mgmt_post_conn_done(sm->vif_idx, sm->reason);
                SM_ENTER(MAINTAIN_CONNECTION, IDLE);
            }
            break;
        }
        default:
            goto unexpected_events;
        }
    } else if (GET_SM_STATE(MAINTAIN_CONNECTION) == MAINTAIN_CONNECTION_HANDSHAKE) {
        switch (sm->event) {
        case WIFI_MGMT_EVENT_SCAN_CMD:
        case WIFI_MGMT_EVENT_CONNECT_CMD:
            mgmt_link_is_ongoing(sm);
            break;
        case WIFI_MGMT_EVENT_SCAN_DONE:
        case WIFI_MGMT_EVENT_SCAN_FAIL:
        case WIFI_MGMT_EVENT_ASSOC_SUCCESS:
            break;
        case WIFI_MGMT_EVENT_RX_EAPOL:
            wifi_wpa_sta_sm_step(sm->vif_idx, WIFI_MGMT_EVENT_RX_EAPOL, sm->param, sm->param_len, WIFI_STA_SM_EAPOL);
            break;;
        case WIFI_MGMT_EVENT_EAP_SUCCESS:
            wifi_wpa_sta_sm_step(sm->vif_idx, WIFI_MGMT_EVENT_EAP_SUCCESS, NULL, 0, WIFI_STA_SM_EAPOL);
            break;
        case WIFI_MGMT_EVENT_DISCONNECT_CMD:
            SM_ENTER(MAINTAIN_CONNECTION, IDLE);
            break;
        case WIFI_MGMT_EVENT_CONNECT_FAIL:
            wifi_netlink_disconnect_req(sm->vif_idx);
        case WIFI_MGMT_EVENT_DISCONNECT:
        {
            struct wifi_sta *config_sta = &wifi_vif_tab[sm->vif_idx].sta;

            config_sta->last_reason = sm->reason;
            wifi_wpa_sta_sm_step(sm->vif_idx, WIFI_MGMT_EVENT_DISCONNECT, NULL, 0, WIFI_STA_SM_SAE);
            wifi_wpa_sta_sm_step(sm->vif_idx, WIFI_MGMT_EVENT_DISCONNECT, NULL, 0, WIFI_STA_SM_EAPOL);
            if (sm->retry_count > 0) {
                sm->delayed_connect_retry = 1;

                SM_ENTER(MAINTAIN_CONNECTION, SCAN);
                mgmt_register_delayed_connect_retry(sm, NULL);
            } else {
                wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": all connecting retries failed\r\n");
                mgmt_preroam_rollback(sm);
                mgmt_post_conn_done(sm->vif_idx, sm->reason);
                SM_ENTER(MAINTAIN_CONNECTION, IDLE);
            }
            break;
        }
        case WIFI_MGMT_EVENT_DHCP_START:
            SM_ENTER(MAINTAIN_CONNECTION, DHCP);
            break;
        default:
            goto unexpected_events;
        }
    } else if (GET_SM_STATE(MAINTAIN_CONNECTION) == MAINTAIN_CONNECTION_DHCP) {
        switch (sm->event) {
        case WIFI_MGMT_EVENT_SCAN_CMD:
        case WIFI_MGMT_EVENT_CONNECT_CMD:
            mgmt_link_is_ongoing(sm);
            break;
        case WIFI_MGMT_EVENT_SCAN_DONE:
        case WIFI_MGMT_EVENT_SCAN_FAIL:
            break;
        case WIFI_MGMT_EVENT_DISCONNECT_CMD:
            SM_ENTER(MAINTAIN_CONNECTION, IDLE);
            break;
        case WIFI_MGMT_EVENT_RX_EAPOL:
            wifi_wpa_sta_sm_step(sm->vif_idx, WIFI_MGMT_EVENT_RX_EAPOL, sm->param, sm->param_len, WIFI_STA_SM_EAPOL);
            break;
        case WIFI_MGMT_EVENT_DHCP_SUCCESS:
            wifi_netlink_dhcp_done(sm->vif_idx);
            SM_ENTER(MAINTAIN_CONNECTION, CONNECTED);
            eloop_timeout_cancel(mgmt_connect_retry, ELOOP_ALL_CTX, ELOOP_ALL_CTX);
            break;
        case WIFI_MGMT_EVENT_DHCP_FAIL:
        case WIFI_MGMT_EVENT_CONNECT_FAIL:
            wifi_netlink_disconnect_req(sm->vif_idx);
        case WIFI_MGMT_EVENT_DISCONNECT:
        {
            struct wifi_sta *config_sta = &wifi_vif_tab[sm->vif_idx].sta;

            config_sta->last_reason = sm->reason;
            wifi_wpa_sta_sm_step(sm->vif_idx, WIFI_MGMT_EVENT_DISCONNECT, NULL, 0, WIFI_STA_SM_SAE);
            wifi_wpa_sta_sm_step(sm->vif_idx, WIFI_MGMT_EVENT_DISCONNECT, NULL, 0, WIFI_STA_SM_EAPOL);
            if (sm->retry_count > 0) {
                sm->delayed_connect_retry = 1;
                SM_ENTER(MAINTAIN_CONNECTION, SCAN);
                mgmt_register_delayed_connect_retry(sm, NULL);
            } else {
                wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": all connecting retries failed\r\n");
                mgmt_preroam_rollback(sm);
                mgmt_post_conn_done(sm->vif_idx, sm->reason);
                SM_ENTER(MAINTAIN_CONNECTION, IDLE);
            }
            break;
        }
        default:
            goto unexpected_events;
        }
    } else if (GET_SM_STATE(MAINTAIN_CONNECTION) == MAINTAIN_CONNECTION_CONNECTED) {
        switch (sm->event) {
        case WIFI_MGMT_EVENT_SCAN_CMD:
            if (sm->param) {
                if (wifi_netlink_scan_set_with_ssid(sm->vif_idx, (char *)sm->param, 0xFF)) {
                    wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": start scan failed\r\n");
                    mgmt_post_scan_done(sm->vif_idx, WIFI_MGMT_SCAN_START_FAIL);
                }
            } else {
                if (wifi_netlink_scan_set(sm->vif_idx, 0xFF)) {
                    wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": start scan failed\r\n");
                    mgmt_post_scan_done(sm->vif_idx, WIFI_MGMT_SCAN_START_FAIL);
                }
            }
            break;
        case WIFI_MGMT_EVENT_CONNECT_CMD:
            sys_memcpy(&wifi_vif_tab[sm->vif_idx].sta.cfg, sm->param, sm->param_len);
            SM_ENTER(MAINTAIN_CONNECTION, IDLE);
            mgmt_connect_retry_param_set(sm, 0);
            SM_ENTER(MAINTAIN_CONNECTION, SCAN);
            break;
#ifdef CFG_WPS
        case WIFI_MGMT_EVENT_WPS_CMD:
            SM_ENTER(MAINTAIN_CONNECTION, IDLE);
            mgmt_connect_retry_param_set(sm, 0);
            SM_ENTER(MAINTAIN_CONNECTION, WPS);
            break;
#endif
        case WIFI_MGMT_EVENT_DISCONNECT_CMD:
            SM_ENTER(MAINTAIN_CONNECTION, IDLE);
            break;
        case WIFI_MGMT_EVENT_SCAN_DONE:
            if (sm->polling_scan)
                mgmt_connected_scan_done(sm);
            mgmt_post_scan_done(sm->vif_idx, WIFI_MGMT_SCAN_SUCCESS);
        case WIFI_MGMT_EVENT_SCAN_FAIL:
            mgmt_post_scan_done(sm->vif_idx, WIFI_MGMT_SCAN_FAIL);
            break;
        case WIFI_MGMT_EVENT_RX_EAPOL:
            wifi_wpa_sta_sm_step(sm->vif_idx, WIFI_MGMT_EVENT_RX_EAPOL, sm->param, sm->param_len, WIFI_STA_SM_EAPOL);
            break;
        case WIFI_MGMT_EVENT_DISCONNECT:
        {
            struct wifi_sta *config_sta = &wifi_vif_tab[sm->vif_idx].sta;

            wifi_wpa_sta_sm_step(sm->vif_idx, WIFI_MGMT_EVENT_DISCONNECT, NULL, 0, WIFI_STA_SM_SAE);
            wifi_wpa_sta_sm_step(sm->vif_idx, WIFI_MGMT_EVENT_DISCONNECT, NULL, 0, WIFI_STA_SM_EAPOL);
            config_sta->last_reason = sm->reason;
            if (sm->reason == WIFI_MGMT_DISCON_SA_QUERY_FAIL) {
                // disconnect indication from wpas lib and need to notify macsw
                wifi_netlink_disconnect_req(sm->vif_idx);
            }
            if (sm->reason == WIFI_MGMT_DISCON_NO_BEACON ||
                    sm->reason == WIFI_MGMT_DISCON_UNSPECIFIED ||
                    sm->reason == WIFI_MGMT_DISCON_RECV_DEAUTH ||
                    sm->reason == WIFI_MGMT_DISCON_SA_QUERY_FAIL) {
                mgmt_connect_retry_param_set(sm, 1);
                SM_ENTER(MAINTAIN_CONNECTION, SCAN);
            } else {
                SM_ENTER(MAINTAIN_CONNECTION, IDLE);
            }
            break;
        }
        case WIFI_MGMT_EVENT_ROAMING_START:
            if (sm->preroam_enable) {
                struct wifi_sta *config_sta = &wifi_vif_tab[sm->vif_idx].sta;
                sm->preroam_start = 1;
                sys_memcpy(sm->preroam_bssid_bk, config_sta->cfg.bssid, WIFI_ALEN);
                eloop_timeout_register(1, mgmt_link_status_polling, sm, NULL);
            }
            break;
#ifndef CONFIG_WPA_SUPPLICANT
        case WIFI_MGMT_EVENT_RX_UNPROT_DEAUTH:
            wpas_unprot_disconnect(sm->vif_idx, sm->param, sm->param_len);
            break;
        case WIFI_MGMT_EVENT_RX_ACTION:
            wpas_action_receive(sm->vif_idx, sm->param, sm->param_len);
            break;
#ifdef CFG_80211R
        case WIFI_MGMT_EVENT_FT_AUTH:
            wifi_wpa_ft_auth_rsp(sm->vif_idx, sm->param);
            break;
        case WIFI_MGMT_EVENT_FT_ROAMING_CMD:
            wifi_wpa_sta_sm_step(sm->vif_idx, WIFI_MGMT_EVENT_FT_ROAMING_CMD, sm->param, sm->param_len, WIFI_STA_SM_EAPOL);
            break;
        case WIFI_MGMT_EVENT_ASSOC_SUCCESS:
        {
            // FT roaming reasscociation response is received
            wifi_wpa_ft_reassociate_done(sm->vif_idx, sm->param);
            break;
        }
        case WIFI_MGMT_EVENT_DHCP_START:
        {
            struct netif *net_if = vif_idx_to_net_if(sm->vif_idx);
            struct wifi_ip_addr_cfg cfg;

            net_if_get_ip(net_if, &(cfg.ipv4.addr), &(cfg.ipv4.mask), &(cfg.ipv4.gw));
            net_get_dns(&cfg.ipv4.dns);

            wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": IPv4 addr got " IP_FMT "\r\n", IP_ARG(cfg.ipv4.addr));

            net_if_set_default(net_if);
            net_if_send_gratuitous_arp(net_if);
            wifi_netlink_dhcp_done(sm->vif_idx);
            break;
        }
#endif /* CFG_80211R */
#endif
        default:
            goto unexpected_events;
        }
    }

    wifi_sm_printf(WIFI_SM_DEBUG, STATE_MACHINE_DEBUG_PREFIX
                   ": vif %d STA stay in the state %u upon the event (%u:%u)\r\n",
                   sm->vif_idx, GET_SM_STATE(MAINTAIN_CONNECTION), sm->event, sm->reason);

    return;

unexpected_events:
    wifi_sm_printf(WIFI_SM_INFO, STATE_MACHINE_DEBUG_PREFIX
                   ": vif %d STA unexpected event (%u:%u) in the state %u\r\n",
                   sm->vif_idx, sm->event, sm->reason, GET_SM_STATE(MAINTAIN_CONNECTION));
}

#ifdef CFG_SOFTAP
SM_STATE(MAINTAIN_SOFTAP, INIT)
{
    struct wifi_vif_tag *wvif = &wifi_vif_tab[sm->vif_idx];

    SM_ENTRY(MAINTAIN_SOFTAP, INIT);

    wifi_netlink_ap_stop(sm->vif_idx);

    wvif->ap.ap_state = WIFI_AP_STATE_INIT;
}

SM_STATE(MAINTAIN_SOFTAP, STARTED)
{
    struct wifi_vif_tag *wvif = &wifi_vif_tab[sm->vif_idx];

    SM_ENTRY(MAINTAIN_SOFTAP, STARTED);

    wvif->ap.ap_state = WIFI_AP_STATE_STARTED;
}

SM_STEP(MAINTAIN_SOFTAP)
{
    int ret = 0;

    if (!sm->init || sm->event == WIFI_MGMT_EVENT_INIT) {
        sys_memset(sm, 0, sizeof(*sm));
        sm->init = true;
        SM_ENTER(MAINTAIN_SOFTAP, INIT);
    } else if (GET_SM_STATE(MAINTAIN_SOFTAP) == MAINTAIN_SOFTAP_INIT) {
        switch (sm->event) {
        case WIFI_MGMT_EVENT_START_AP_CMD:
            ret = wifi_netlink_ap_start(sm->vif_idx, (struct ap_cfg *)sm->param);
            if (!ret) {
                SM_ENTER(MAINTAIN_SOFTAP, STARTED);
                mgmt_post_ap_start_done(sm->vif_idx, 0);
            } else {
                SM_ENTER(MAINTAIN_SOFTAP, INIT);
                mgmt_post_ap_start_done(sm->vif_idx, 1);
            }
            break;
        case WIFI_MGMT_EVENT_RX_MGMT:
            break;
        case WIFI_MGMT_EVENT_SCAN_CMD:
            wifi_sm_printf(WIFI_SM_INFO, STATE_MACHINE_DEBUG_PREFIX ": SoftAP is configuring... Please wait for a minute.\r\n");
            break;
        default:
            goto unexpected_events;
        }
    } else if (GET_SM_STATE(MAINTAIN_SOFTAP) == MAINTAIN_SOFTAP_STARTED) {
        switch (sm->event) {
        case WIFI_MGMT_EVENT_STOP_AP_CMD:
            SM_ENTER(MAINTAIN_SOFTAP, INIT);
            break;
        case WIFI_MGMT_EVENT_RX_MGMT:
            wifi_wpa_ap_sm_step(sm->vif_idx, WIFI_MGMT_EVENT_RX_MGMT, sm->param, sm->param_len);
            break;
        case WIFI_MGMT_EVENT_TX_MGMT_DONE:
            wifi_wpa_ap_sm_step(sm->vif_idx, WIFI_MGMT_EVENT_TX_MGMT_DONE, sm->param, sm->param_len);
            break;
         case WIFI_MGMT_EVENT_RX_EAPOL:
            wifi_wpa_ap_sm_step(sm->vif_idx, WIFI_MGMT_EVENT_RX_EAPOL, sm->param, sm->param_len);
            break;
        case WIFI_MGMT_EVENT_AP_SWITCH_CHNL_CMD:
        {
            struct wifi_vif_tag *wvif = &wifi_vif_tab[sm->vif_idx];
            uint8_t new_channel = sm->reason;

            wifi_netlink_ap_stop(sm->vif_idx);
            wvif->ap.ap_state = WIFI_AP_STATE_INIT;

            wifi_vif_tab[sm->vif_idx].ap.cfg.channel = new_channel;

            wifi_netlink_ap_start(sm->vif_idx, NULL);
            wvif->ap.ap_state = WIFI_AP_STATE_STARTED;
            break;
        }
        case WIFI_MGMT_EVENT_CLIENT_ADDED:
            // add user callback here
            wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": Add client "MAC_FMT"\r\n", MAC_ARG_UINT8(sm->param));
            break;
        case WIFI_MGMT_EVENT_CLIENT_REMOVED:
            // add user callback here
            wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": Delete client "MAC_FMT"\r\n", MAC_ARG_UINT8(sm->param));
            break;
        case WIFI_MGMT_EVENT_SCAN_CMD:
            if (sm->param) {
                if (wifi_netlink_scan_set_with_ssid(sm->vif_idx, (char *)sm->param, 0xFF)) {
                    wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": start scan failed\r\n");
                    mgmt_post_scan_done(sm->vif_idx, WIFI_MGMT_SCAN_START_FAIL);
                }
            } else {
                if (wifi_netlink_scan_set(sm->vif_idx, 0xFF)) {
                    wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": start scan failed\r\n");
                    mgmt_post_scan_done(sm->vif_idx, WIFI_MGMT_SCAN_START_FAIL);
                }
            }
            break;
        case WIFI_MGMT_EVENT_SCAN_DONE:
            mgmt_post_scan_done(sm->vif_idx, WIFI_MGMT_SCAN_SUCCESS);
            break;
        case WIFI_MGMT_EVENT_SCAN_FAIL:
            mgmt_post_scan_done(sm->vif_idx, WIFI_MGMT_SCAN_FAIL);
        default:
            goto unexpected_events;
        }
    }

    if (sm->event != WIFI_MGMT_EVENT_RX_MGMT)
        wifi_sm_printf(WIFI_SM_DEBUG, STATE_MACHINE_DEBUG_PREFIX
                   ": vif %u SoftAP stay in the state %u upon the event (%u:%u)\r\n",
                   sm->vif_idx, GET_SM_STATE(MAINTAIN_SOFTAP), sm->event, sm->reason);

    return;

unexpected_events:
    wifi_sm_printf(WIFI_SM_INFO, STATE_MACHINE_DEBUG_PREFIX
                   ": vif %u SoftAP unexpected event (%u:%u) in the state %u\r\n",
                   sm->vif_idx, sm->event, sm->reason, GET_SM_STATE(MAINTAIN_SOFTAP));
}
#endif /* CFG_SOFTAP */

SM_STATE(MAINTAIN_MONITOR, INIT)
{
    SM_ENTRY(MAINTAIN_MONITOR, INIT);
}

SM_STATE(MAINTAIN_MONITOR, STARTED)
{
    SM_ENTRY(MAINTAIN_MONITOR, STARTED);
}

SM_STEP(MAINTAIN_MONITOR)
{
    int ret = 0;

    if (!sm->init || sm->event == WIFI_MGMT_EVENT_INIT) {
        sys_memset(sm, 0, sizeof(*sm));
        sm->init = true;
        SM_ENTER(MAINTAIN_MONITOR, INIT);
    } else if (GET_SM_STATE(MAINTAIN_MONITOR) == MAINTAIN_MONITOR_INIT) {
        switch (sm->event) {
        case WIFI_MGMT_EVENT_MONITOR_START_CMD:
            ret = wifi_netlink_monitor_start(sm->vif_idx, (struct wifi_monitor *)(sm->param));
            if (!ret) {
                SM_ENTER(MAINTAIN_MONITOR, STARTED);
                mgmt_post_monitor_start_done(sm->vif_idx, 0);
            } else {
                SM_ENTER(MAINTAIN_MONITOR, INIT);
                wifi_sm_printf(WIFI_SM_ERROR, STATE_MACHINE_DEBUG_PREFIX
                            ": start monitor mode failed\r\n");
                mgmt_post_monitor_start_done(sm->vif_idx, 1);
            }
            break;
        case WIFI_MGMT_EVENT_SCAN_CMD:
            wifi_sm_printf(WIFI_SM_INFO, STATE_MACHINE_DEBUG_PREFIX ": Monitor is configuring... Please wait for a minute.\r\n");
            break;
        default:
            goto unexpected_events;
        }
    } else if (GET_SM_STATE(MAINTAIN_MONITOR) == MAINTAIN_MONITOR_STARTED) {
        switch (sm->event) {
        case WIFI_MGMT_EVENT_SCAN_CMD:
            if (sm->param) {
                if (wifi_netlink_scan_set_with_ssid(sm->vif_idx, (char *)sm->param, 0xFF)) {
                    wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": start scan failed\r\n");
                    mgmt_post_scan_done(sm->vif_idx, WIFI_MGMT_SCAN_START_FAIL);
                }
            } else {
                if (wifi_netlink_scan_set(sm->vif_idx, 0xFF)) {
                    wifi_sm_printf(WIFI_SM_NOTICE, STATE_MACHINE_DEBUG_PREFIX ": start scan failed\r\n");
                    mgmt_post_scan_done(sm->vif_idx, WIFI_MGMT_SCAN_START_FAIL);
                }
            }
            break;
        case WIFI_MGMT_EVENT_SCAN_DONE:
            mgmt_post_scan_done(sm->vif_idx, WIFI_MGMT_SCAN_SUCCESS);
            break;
        case WIFI_MGMT_EVENT_SCAN_FAIL:
            mgmt_post_scan_done(sm->vif_idx, WIFI_MGMT_SCAN_FAIL);
            break;
        default:
            goto unexpected_events;
        }
    }

    wifi_sm_printf(WIFI_SM_DEBUG, STATE_MACHINE_DEBUG_PREFIX
               ": vif %u Monitor stay in the state %u upon the event (%u:%u)\r\n",
               sm->vif_idx, GET_SM_STATE(MAINTAIN_MONITOR), sm->event, sm->reason);

    return;

unexpected_events:
    wifi_sm_printf(WIFI_SM_INFO, STATE_MACHINE_DEBUG_PREFIX
                   ": vif %u Monitor unexpected event (%u:%u) in the state %u\r\n",
                   sm->vif_idx, sm->event, sm->reason, GET_SM_STATE(MAINTAIN_MONITOR));
}

/*!
    \brief      Run state machine
    \param[in]  eloop_data: pointer to the eloop data
    \param[in]  user_ctx: pointer to the user parameters
    \param[out] none
    \retval     none
*/
void wifi_mgmt_cb_run_state_machine(void *eloop_data, void *user_ctx)
{
    eloop_message_t *message = (eloop_message_t *)user_ctx;
    uint16_t event = ELOOP_EVENT_GET_EV(message->event_id);
    uint8_t vif_idx = ELOOP_EVENT_GET_VIF(message->event_id);
    struct wifi_vif_tag *wvif = NULL;
    wifi_management_sm_data_t *sm = NULL;

    if (vif_idx >=  CFG_VIF_NUM)
        return;

    if (event < WIFI_MGMT_EVENT_INIT)
        return;

    wvif = vif_idx_to_wvif(vif_idx);
    sm = &wifi_sm_data[vif_idx];
    sm->vif_idx = vif_idx;
    sm->event = (wifi_management_event_t)event;
    sm->reason = message->reason;
    sm->param = message->param;
    sm->param_len = message->param_len;

    if (event == WIFI_MGMT_EVENT_SWITCH_MODE_CMD) {
        wifi_sm_printf(WIFI_SM_DEBUG, STATE_MACHINE_DEBUG_PREFIX
                       ": vif %u switch mode, received message: (%u:%u:%u:%p:%u)\r\n",
                       vif_idx, sm->MAINTAIN_CONNECTION_state, event, message->reason, message->param, message->param_len);
        if (mgmt_switch_mode(sm)) {
            wifi_sm_printf(WIFI_SM_DEBUG, STATE_MACHINE_DEBUG_PREFIX
                       ": vif %u switch to mode %d failed.\r\n", vif_idx, message->reason);
        }
        goto exit;
    }

    if (wvif->wvif_type == WVIF_STA) {
        wifi_sm_printf(WIFI_SM_DEBUG, STATE_MACHINE_DEBUG_PREFIX
                       ": vif %u STA received message: (%u:%u:%u:%p:%u)\r\n",
                       vif_idx, sm->MAINTAIN_CONNECTION_state, event, message->reason, message->param, message->param_len);
        SM_STEP_RUN(MAINTAIN_CONNECTION);
#ifdef CFG_SOFTAP
    } else if (wvif->wvif_type == WVIF_AP) {
        if (event != WIFI_MGMT_EVENT_RX_MGMT)
            wifi_sm_printf(WIFI_SM_DEBUG, STATE_MACHINE_DEBUG_PREFIX
                       ": vif %u SoftAP received message: (%u:%u:%u:%p:%u)\r\n",
                       vif_idx, sm->MAINTAIN_SOFTAP_state, event, message->reason, (uint32_t)message->param, message->param_len);
        SM_STEP_RUN(MAINTAIN_SOFTAP);
#endif
    } else if (wvif->wvif_type == WVIF_MONITOR) {
        wifi_sm_printf(WIFI_SM_DEBUG, STATE_MACHINE_DEBUG_PREFIX
                       ": vif %u Monitor received message: (%u:%u:%u:%p:%u)\r\n",
                       vif_idx, sm->MAINTAIN_MONITOR_state, event, message->reason, message->param, message->param_len);
        SM_STEP_RUN(MAINTAIN_MONITOR);
    }

exit:
    if (sm->param) {
        sys_mfree(sm->param);
        sm->param = NULL;
        sm->param_len = 0;
    }
}

/************************ WiFi Management Interfaces **************************/
/*!
    \brief      Set whether to enable wifi concurrent mode
    \param[in]  enable: 0: disble; 1: enable
    \param[out] none
    \retval     0.
*/
int wifi_management_concurrent_set(uint8_t enable)
{
    WIFI_CLOSED_CHECK(1);

#ifdef CFG_WIFI_CONCURRENT
    if (enable) {
        wifi_concurrent_mode = 1;
    } else {
        wifi_management_ap_stop();
        wifi_concurrent_mode = 0;
    }
#else
    netlink_printf("Please define CFG_WIFI_CONCURRENT first.");
#endif
    return 0;
}

/*!
    \brief      Get whether to enable wifi concurrent mode
    \param[in]  none
    \param[out] none
    \retval     0: disble; 1: enable.
*/
int wifi_management_concurrent_get(void)
{
#ifdef CFG_WIFI_CONCURRENT
    return wifi_concurrent_mode;
#endif
    return 0;
}

/*!
    \brief      Set whether to enable wifi roaming mechanism
    \param[in]  enable: 0: disble; 1: enable
    \param[in]  rssi_th: the rssi threshold to trigger roaming
    \param[out] none
    \retval     0.
*/
int wifi_management_roaming_set(uint8_t enable, int8_t rssi_th)
{
    if (enable) {
        wifi_sm_data[WIFI_VIF_INDEX_DEFAULT].preroam_enable = 1;
        if (rssi_th)
            wifi_netlink_roaming_rssi_set(WIFI_VIF_INDEX_DEFAULT, rssi_th);
    } else {
        wifi_sm_data[WIFI_VIF_INDEX_DEFAULT].preroam_enable = 0;
    }
    return 0;
}

/*!
    \brief      Get whether to enable wifi roaming mechanism
    \param[in]  none
    \param[out] rssi_th: the rssi threshold to trigger roaming
    \retval     0: disble; 1: enable.
*/
int wifi_management_roaming_get(int8_t *rssi_th)
{
    if (rssi_th)
        *rssi_th = wifi_netlink_roaming_rssi_get(WIFI_VIF_INDEX_DEFAULT);

    return wifi_sm_data[WIFI_VIF_INDEX_DEFAULT].preroam_enable;
}

/*!
    \brief      Start wifi scan
    \param[in]  blocked: whether block
    \param[in]  ssid: pointer to the ssid of the AP or NULL
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_management_scan(uint8_t blocked, const char *ssid)
{
    uint8_t vif_idx = WIFI_VIF_INDEX_DEFAULT;
    int reason = 0;
    char *scan_ssid = NULL;
    uint8_t ssid_len = ssid ? strlen(ssid) : 0;

    WIFI_CLOSED_CHECK(1);

    if (ssid_len != 0) {
        scan_ssid = sys_malloc(ssid_len + 1);
        if (!scan_ssid)
            return -1;
        strncpy(scan_ssid, ssid, ssid_len);
        scan_ssid[ssid_len] = '\0';
        ssid_len += 1;
    }
    if (eloop_message_send(vif_idx, WIFI_MGMT_EVENT_SCAN_CMD, 0, (uint8_t *)scan_ssid, ssid_len)) {
        netlink_printf("MGMT: SCAN_CMD, eloop event queue full\r\n");
        if (scan_ssid)
            sys_mfree(scan_ssid);
        return -2;
    }

    mgmt_wait_queue_flush();
    wifi_sm_data[vif_idx].scan_blocked = blocked;

    if (blocked) {
        reason = mgmt_wait_queue_fetch(vif_idx, MGMT_WAIT_EVT_SCAN_DONE, 2500);
        if (reason != WIFI_MGMT_SCAN_SUCCESS)
            netlink_printf("MGMT: wait scan done timeout, reason %d\r\n", reason);
    }
    return reason;
}

/*!
    \brief      Connect to an AP
    \param[in]  ssid: pointer to the ssid of the AP
    \param[in]  password: pointer to the password of the AP
    \param[in]  blocked: whether block
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_management_connect(char *ssid, char *password, uint8_t blocked)
{
    uint8_t vif_idx = WIFI_VIF_INDEX_DEFAULT;
    struct sta_cfg *sta_cfg = NULL;
    struct wifi_sta *sta = &(wifi_vif_tab[vif_idx].sta);
    struct sta_cfg *sta_cfg_prev = &sta->cfg;
    int reason = 0;

    WIFI_CLOSED_CHECK(1);

    if ((ssid == NULL) || (strlen((const char *)ssid) > WIFI_SSID_MAX_LEN)) {
        netlink_printf("MGMT: ssid is null or too long (>%d)\r\n", WIFI_SSID_MAX_LEN);
        return -1;
    }

    if (password && (strlen(password) == 0)) {
        password = NULL;
    }

    if (password &&
        ((strlen(password) > WPAS_MAX_PASSPHRASE_LEN) ||
         ((strlen(password) < WPAS_MIN_PASSPHRASE_LEN) &&
         (strlen(password) != WPAS_WEP40_ASCII_LEN)))) {
        netlink_printf("MGMT: password's length should not be greater than %d or less than %d!\r\n",
                    WPAS_MAX_PASSPHRASE_LEN, WPAS_MIN_PASSPHRASE_LEN);
        return -2;
    }

    /* switch wvif to STA mode */
    if (eloop_message_send(vif_idx, WIFI_MGMT_EVENT_SWITCH_MODE_CMD, WVIF_STA, NULL, 0) < 0) {
        netlink_printf("MGMT: SWITCH_MODE_CMD, eloop event queue full\r\n");
        return -3;
    }

    /* save connect info */
    sta_cfg = sys_zalloc(sizeof(struct sta_cfg));
    if (NULL == sta_cfg) {
        netlink_printf("MGMT: alloc sta_cfg failed\r\n");
        return -4;
    }
    sta_cfg->ssid_len = strlen(ssid);
    sys_memcpy(sta_cfg->ssid, ssid, sta_cfg->ssid_len);
    if (password) {
        sta_cfg->passphrase_len = strlen(password);
        sys_memcpy(sta_cfg->passphrase, password, sta_cfg->passphrase_len);
    } else {
        sta_cfg->passphrase_len = 0;
    }
    sta_cfg->channel = 0xFF;
    sta_cfg->conn_with_bssid = 0;
    sta_cfg->conn_blocked = blocked;

    /* Clear history IP if AP changed */
    if ((sta_cfg->ssid_len != sta->cfg.ssid_len)
        || strncmp(sta_cfg->ssid, sta->cfg.ssid, sta->cfg.ssid_len)) {
        sta->history_ip = 0;
    }

    /* Flush SAE PMK Cache if pwd or ssid changes */
    if ((sta_cfg->ssid_len != sta_cfg_prev->ssid_len) || (sta_cfg->passphrase_len != sta_cfg_prev->passphrase_len) ||
            strncmp(sta_cfg->ssid, sta_cfg_prev->ssid, sta_cfg->ssid_len) ||
            strncmp(sta_cfg->passphrase, sta_cfg_prev->passphrase, sta_cfg->passphrase_len)) {
        sta_cfg->flush_cache_req = 1;
    }

    /* Flush wait queue */
    mgmt_wait_queue_flush();

    if (eloop_message_send(vif_idx, WIFI_MGMT_EVENT_CONNECT_CMD, 0, (uint8_t *)sta_cfg, sizeof(struct sta_cfg)) < 0) {
        sys_mfree(sta_cfg);
        netlink_printf("MGMT: CONNECT_CMD, eloop event queue full\r\n");
        return -5;
    }

    if (blocked) {
        /* Block here until CONNECT related event received */
        reason = mgmt_wait_queue_fetch(vif_idx, MGMT_WAIT_EVT_CONN_DONE, WIFI_MGMT_CONNECT_BLOCK_TIME);
        sta->last_reason = reason;
        if (reason == 0xFFFF) {
            netlink_printf("MGMT: eloop wait timeout\r\n");
            wifi_management_disconnect();
        }
        // netlink_printf("MGMT: connect complete with reason %d\r\n", reason);
    }

    return reason;
}

/*!
    \brief      Connect to an AP
    \param[in]  bssid: pointer to the bssid of the AP
    \param[in]  password: pointer to the password of the AP
    \param[in]  blocked: whether block
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_management_connect_with_bssid(uint8_t *bssid, char *password, uint8_t blocked)
{
    uint8_t vif_idx = WIFI_VIF_INDEX_DEFAULT;
    struct sta_cfg *sta_cfg = NULL;
    struct wifi_sta *sta = &(wifi_vif_tab[vif_idx].sta);
    struct sta_cfg *sta_cfg_prev = &sta->cfg;
    int reason = 0;

    WIFI_CLOSED_CHECK(1);

    if (bssid == NULL) {
        netlink_printf("MGMT: bssid is null\r\n");
        return -1;
    }

    if (password && (strlen(password) == 0)) {
        password = NULL;
    }

    if (password &&
        ((strlen(password) > WPAS_MAX_PASSPHRASE_LEN) ||
         (strlen(password) < WPAS_MIN_PASSPHRASE_LEN))) {
        netlink_printf("MGMT: password's length should not be greater than %d or less than %d!\r\n",
                    WPAS_MAX_PASSPHRASE_LEN, WPAS_MIN_PASSPHRASE_LEN);
        return -2;
    }

    netlink_printf("MGMT: connect to "MAC_FMT" with pwd \"%s\"\r\n", MAC_ARG_UINT8(bssid), (password == NULL) ? "\0":password);

    /* switch wvif to STA mode */
    if (eloop_message_send(vif_idx, WIFI_MGMT_EVENT_SWITCH_MODE_CMD, WVIF_STA, NULL, 0) < 0) {
        netlink_printf("MGMT: SWITCH_MODE_CMD, eloop event queue full\r\n");
        return -3;
    }

    /* save connect info */
    sta_cfg = sys_zalloc(sizeof(struct sta_cfg));
    if (NULL == sta_cfg) {
        netlink_printf("MGMT: alloc sta_cfg failed\r\n");
        return -4;
    }

    sys_memcpy(sta_cfg->bssid, bssid, 6);
    sta_cfg->conn_with_bssid = 1;
    if (password) {
        sta_cfg->passphrase_len = strlen(password);
        sys_memcpy(sta_cfg->passphrase, password, sta_cfg->passphrase_len);
    } else {
        sta_cfg->passphrase_len = 0;
    }
    sta_cfg->channel = 0xFF;
    sta_cfg->conn_blocked = blocked;

    /* Clear history IP if AP changed */
    if (sys_memcmp(sta_cfg->bssid, sta->cfg.bssid, 6)) {
        sta->history_ip = 0;
    }

    /* Flush SAE PMK Cache if pwd changes */
    if ((sta_cfg->passphrase_len != sta_cfg_prev->passphrase_len) ||
            strncmp(sta_cfg->passphrase, sta_cfg_prev->passphrase, sta_cfg->passphrase_len)) {
        sta_cfg->flush_cache_req = 1;
    }

    /* Flush wait queue */
    mgmt_wait_queue_flush();

    if (eloop_message_send(vif_idx, WIFI_MGMT_EVENT_CONNECT_CMD, 0, (uint8_t *)sta_cfg, sizeof(struct sta_cfg)) < 0) {
        sys_mfree(sta_cfg);
        netlink_printf("MGMT: CONNECT_CMD, eloop event queue full\r\n");
        return -5;
    }

    if (blocked) {
        /* Block here until CONNECT related event received */
        reason = mgmt_wait_queue_fetch(vif_idx, MGMT_WAIT_EVT_CONN_DONE, WIFI_MGMT_CONNECT_BLOCK_TIME);
        if (reason == 0xFFFF) {
            netlink_printf("MGMT: eloop wait timeout\r\n");
        }
        // netlink_printf("MGMT: connect complete with reason %d\r\n", reason);
    }

    return reason;
}

#ifdef CFG_8021x_EAP_TLS
/*!
    \brief      Connect to an AP
    \param[in]  ssid: pointer to the ssid of the AP
    \param[in]  identity: pointer to user identity
    \param[in]  ca_cert: pointer to ca cert
    \param[in]  client_key: pointer to client key
    \param[in]  client_cert: pointer to client cert
    \param[in]  client_key_password: pointer to client cert password
    \param[in]  blocked: whether block
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_management_connect_with_eap_tls(char *ssid, const char *identity, const char *ca_cert,
                                    const char *client_key, const char *client_cert,
                                    const char *client_key_password, const char *phase1,
                                    uint8_t blocked)
{
    uint8_t vif_idx = WIFI_VIF_INDEX_DEFAULT;
    struct sta_cfg *sta_cfg = NULL;
    struct wifi_sta *sta = &(wifi_vif_tab[vif_idx].sta);
    int reason = 0;

    WIFI_CLOSED_CHECK(1);

    if ((ssid == NULL) || (strlen((const char *)ssid) > WIFI_SSID_MAX_LEN)) {
        netlink_printf("MGMT: ssid is null or too long (>%d)\r\n", WIFI_SSID_MAX_LEN);
        return -1;
    }

    if (!((ca_cert && (strlen(ca_cert) != 0))           \
          && (client_key && (strlen(client_key) != 0))  \
          && (identity && (strlen(identity) != 0))      \
          && (client_cert && (strlen(client_cert) != 0)))) {
        netlink_printf("MGMT: identity, ca_cert, client_key and client_cert must be configured for EAP-TLS connect\r\n");
        return -2;
    }

    /* switch wvif to STA mode */
    if (eloop_message_send(vif_idx, WIFI_MGMT_EVENT_SWITCH_MODE_CMD, WVIF_STA, NULL, 0) < 0) {
        netlink_printf("MGMT: SWITCH_MODE_CMD, eloop event queue full\r\n");
        return -3;
    }

    /* save connect info */
    sta_cfg = sys_zalloc(sizeof(struct sta_cfg));
    if (NULL == sta_cfg) {
        netlink_printf("MGMT: alloc sta_cfg failed\r\n");
        return -4;
    }

    if ((client_key_password != NULL) && (strlen(client_key_password) != 0)) {
        sta_cfg->eap_cfg.client_key_password = client_key_password;
    }

    sta_cfg->ssid_len = strlen(ssid);
    sys_memcpy(sta_cfg->ssid, ssid, sta_cfg->ssid_len);

    sta_cfg->passphrase_len = 0;
    sta_cfg->channel = 0xFF;
    sta_cfg->conn_with_bssid = 0;
    sta_cfg->conn_blocked = blocked;
    sta_cfg->eap_cfg.conn_with_enterprise = 1;
    sta_cfg->eap_cfg.identity = identity;
    sta_cfg->eap_cfg.identity_len = strlen(identity);
    sta_cfg->eap_cfg.ca_cert = ca_cert;
    sta_cfg->eap_cfg.client_cert = client_cert;
    sta_cfg->eap_cfg.client_key = client_key;
    sta_cfg->eap_cfg.phase1 = phase1;

    /* Clear history IP if AP changed */
    if ((sta_cfg->ssid_len != sta->cfg.ssid_len)
        || strncmp(sta_cfg->ssid, sta->cfg.ssid, sta->cfg.ssid_len)) {
        sta->history_ip = 0;
    }

    /* Flush wait queue */
    mgmt_wait_queue_flush();

    if (eloop_message_send(vif_idx, WIFI_MGMT_EVENT_CONNECT_CMD, 0, (uint8_t *)sta_cfg, sizeof(struct sta_cfg)) < 0) {
        sys_mfree(sta_cfg);
        netlink_printf("MGMT: CONNECT_CMD, eloop event queue full\r\n");
        return -6;
    }

    if (blocked) {
        /* Block here until CONNECT related event received */
        reason = mgmt_wait_queue_fetch(vif_idx, MGMT_WAIT_EVT_CONN_DONE, WIFI_MGMT_CONNECT_BLOCK_TIME);
        sta->last_reason = reason;
        if (reason == 0xFFFF) {
            netlink_printf("MGMT: eloop wait timeout\r\n");
            wifi_management_disconnect();
        }
    }

    return reason;
}
#endif

/*!
    \brief      Disconnect with AP
    \param[in]  none
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_management_disconnect(void)
{
    uint8_t vif_idx = WIFI_VIF_INDEX_DEFAULT;
    struct wifi_vif_tag *wvif = &wifi_vif_tab[vif_idx];
    struct wifi_sta *sta = &(wvif->sta);
    int ret = 0;

    WIFI_CLOSED_CHECK(1);

    if (wvif->wvif_type == WVIF_STA) {
        /* Flush wait queue */
        mgmt_wait_queue_flush();

        if (eloop_event_send(vif_idx, WIFI_MGMT_EVENT_DISCONNECT_CMD)) {
            netlink_printf("MGMT: DISCONNECT_CMD, eloop event queue full\r\n");
            return -1;
        }

        ret = mgmt_wait_queue_fetch(vif_idx, MGMT_WAIT_EVT_DISCONN_DONE, 10000);
        if (ret != 0)
            netlink_printf("MGMT: wait disconnect done timeout, ret %d\r\n", ret);
        else
            netlink_printf("MGMT: disconnect complete\r\n");
        sta->last_reason = 0;
    } else {
        netlink_printf("MGMT: not station mode, do nothing\r\n");
    }

    return ret;
}

/*!
    \brief      Start softap
    \param[in]  bssid: pointer to the bssid of the softap
    \param[in]  password: pointer to the password of the softap
    \param[in]  channel: channel of the softap
    \param[in]  auth_mode: auth_mode of the softap
    \param[in]  hidden: whether hidden
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_management_ap_start(char *ssid, char *passwd, uint32_t channel, wifi_ap_auth_mode_t auth_mode, uint32_t hidden)
{
    int vif_idx = WIFI_VIF_INDEX_DEFAULT;
    struct ap_cfg *ap_cfg = NULL;
    uint16_t reason = 0;

    WIFI_CLOSED_CHECK(1);

    if ((ssid == NULL) || (strlen((const char *)ssid) > WIFI_SSID_MAX_LEN)) {
        netlink_printf("MGMT: ssid is null or too long (>%d)\r\n", WIFI_SSID_MAX_LEN);
        return -1;
    }

    if (passwd && (strlen(passwd) == 0)) {
        passwd = NULL;
    }
    if (passwd == NULL) {
        auth_mode = AUTH_MODE_OPEN;
    }

    if (passwd &&
        ((strlen((const char *)passwd) > WPAS_MAX_PASSPHRASE_LEN) ||
         (strlen((const char *)passwd) < WPAS_MIN_PASSPHRASE_LEN))) {
        netlink_printf("MGMT: password's length should not be greater than %d or less than %d!\r\n",
                    WPAS_MAX_PASSPHRASE_LEN, WPAS_MIN_PASSPHRASE_LEN);
        return -2;
    }

    if ((channel > 14) || (channel < 1)) {
        netlink_printf("MGMT: channel must be 1-14\r\n");
        return -3;
    }

    /* check if the softap is opened */
    wifi_management_ap_stop();

#ifdef CFG_WIFI_CONCURRENT
    if (wifi_concurrent_mode) {
        vif_idx = WIFI_VIF_INDEX_SOFTAP_MODE;

        /* Check if the STA interface is connected and with the same channel */
        if (wifi_vif_tab[WIFI_VIF_INDEX_STA_MODE].wvif_type == WVIF_STA
            && wifi_vif_tab[WIFI_VIF_INDEX_STA_MODE].sta.state > WIFI_STA_STATE_SCAN) {
            uint8_t cur_channel;
            macif_vif_current_chan_get(WIFI_VIF_INDEX_STA_MODE, &cur_channel);
            if (cur_channel == 0) {
                netlink_printf("MGMT ERROR: vif %d current channel is zero!\r\n", WIFI_VIF_INDEX_STA_MODE);
                return -4;
            }
            if (channel != cur_channel) {
                netlink_printf("MGMT WARNING: The STA vif is linked in channel %d. "
                                "The softap must work under the same channel.\r\n",
                                cur_channel);
                channel = cur_channel;
            }
        } else {
            if (eloop_message_send(WIFI_VIF_INDEX_STA_MODE, WIFI_MGMT_EVENT_SWITCH_MODE_CMD, WVIF_STA, NULL, 0)) {
                netlink_printf("MGMT: vif %d SWITCH_MODE_CMD, eloop event queue full\r\n", WIFI_VIF_INDEX_STA_MODE);
                return -5;
            }
        }
    }
#endif

    /* switch WVIF to AP mode */
    if (eloop_message_send(vif_idx, WIFI_MGMT_EVENT_SWITCH_MODE_CMD, WVIF_AP, NULL, 0)) {
        netlink_printf("MGMT: vif %d SWITCH_MODE_CMD, eloop event queue full\r\n", vif_idx);
        return -6;
    }

    /* save ap info */
    ap_cfg = sys_zalloc(sizeof(struct ap_cfg));
    if (NULL == ap_cfg) {
        netlink_printf("MGMT: alloc ap_cfg failed\r\n");
        return -7;
    }
    ap_cfg->ssid_len = strlen(ssid);
    sys_memcpy(ap_cfg->ssid, ssid, ap_cfg->ssid_len);
    ap_cfg->ssid[ap_cfg->ssid_len] = '\0';
    ap_cfg->akm = wifi_wpa_auth_mode_2_akm(auth_mode);
    sys_memset(ap_cfg->passphrase, 0, sizeof(ap_cfg->passphrase));
    if (auth_mode != AUTH_MODE_OPEN) {
        ap_cfg->passphrase_len = strlen(passwd);
        sys_memcpy(ap_cfg->passphrase, passwd, ap_cfg->passphrase_len);
    }
    ap_cfg->channel = channel;
    ap_cfg->hidden = hidden;
    sys_memcpy(ap_cfg->bssid, wifi_vif_mac_addr_get(vif_idx), WIFI_ALEN);

    /* Flush wait queue */
    mgmt_wait_queue_flush();

    /* start softap */
    if (eloop_message_send(vif_idx, WIFI_MGMT_EVENT_START_AP_CMD, 0, (uint8_t *)ap_cfg, sizeof(struct ap_cfg))) {
        sys_mfree(ap_cfg);
        netlink_printf("MGMT: vif %d START_AP_CMD, eloop event queue full\r\n", vif_idx);
        return -8;
    }

    /* Block here until ap started received */
    if ((reason = mgmt_wait_queue_fetch(vif_idx, MGMT_WAIT_EVT_AP_START_DONE, 10000))) {
        netlink_printf("MGMT: AP started failed (reason = %d)\r\n", reason);
        return -9;
    }

    return 0;
}

int wifi_management_ap_delete_client(uint8_t *client_mac_addr)
{
    int vif_idx = WIFI_VIF_INDEX_DEFAULT;
    int ret = 0;
#ifndef CONFIG_WPA_SUPPLICANT
    void *ap = NULL;
#endif

#ifdef CFG_WIFI_CONCURRENT
    if (wifi_concurrent_mode) {
        vif_idx = WIFI_VIF_INDEX_SOFTAP_MODE;
    }
#endif

#ifdef CONFIG_WPA_SUPPLICANT
    ret = wifi_wpa_ap_delete_client(vif_idx, client_mac_addr, 0x1); // WLAN_REASON_UNSPECIFIED
#else
    ap = wifi_wpa_w_ap_get(vif_idx);
    if (ap == NULL) {
        return -1;
    }

    ret = ap_send_deauth(ap, client_mac_addr, WLAN_REASON_UNSPECIFIED);
#endif
    return ret;
}

/*!
    \brief      Stop softap
    \param[in]  none
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_management_ap_stop(void)
{
    WIFI_CLOSED_CHECK(1);

#ifdef CFG_WIFI_CONCURRENT
    if (wifi_concurrent_mode) {
        if (eloop_message_send(WIFI_VIF_INDEX_SOFTAP_MODE, WIFI_MGMT_EVENT_SWITCH_MODE_CMD, WVIF_UNKNOWN, NULL, 0)) {
            netlink_printf("MGMT: vif %d SWITCH_MODE_CMD, eloop event queue full\r\n", WIFI_VIF_INDEX_SOFTAP_MODE);
            return -2;
        }
    } else
#endif
    {
        if (eloop_message_send(WIFI_VIF_INDEX_DEFAULT, WIFI_MGMT_EVENT_SWITCH_MODE_CMD, WVIF_STA, NULL, 0)) {
            netlink_printf("MGMT: vif %d SWITCH_MODE_CMD, eloop event queue full\r\n", WIFI_VIF_INDEX_DEFAULT);
            return -1;
        }
    }
    return 0;
}

/*!
    \brief      Enter station mode
    \param[in]  none
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_management_sta_start(void)
{
    WIFI_CLOSED_CHECK(1);

    if (eloop_message_send(WIFI_VIF_INDEX_DEFAULT, WIFI_MGMT_EVENT_SWITCH_MODE_CMD, WVIF_STA, NULL, 0)) {
        netlink_printf("MGMT: vif %d SWITCH_MODE_CMD, eloop event queue full\r\n", WIFI_VIF_INDEX_DEFAULT);
        return -1;
    }
    return 0;
}

/*!
    \brief      Start monitor
    \param[in]  channel: channel which to be monitored
    \param[in]  monitor_cb: callback function when receive packet
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_management_monitor_start(uint8_t channel, cb_macif_rx monitor_cb)
{
    struct wifi_monitor *cfg = NULL;
    struct wifi_vif_tag *wvif = &wifi_vif_tab[WIFI_VIF_INDEX_DEFAULT];
    uint16_t reason = 0;

    WIFI_CLOSED_CHECK(1);

    /* stop monitor first, if the monitor is already started */
    if (wvif->wvif_type == WVIF_MONITOR) {
        if (eloop_message_send(WIFI_VIF_INDEX_DEFAULT, WIFI_MGMT_EVENT_SWITCH_MODE_CMD, WVIF_STA, NULL, 0)) {
            netlink_printf("MGMT: vif %d SWITCH_MODE_CMD, eloop event queue full\r\n", WIFI_VIF_INDEX_DEFAULT);
            return -1;
        }
    }

#ifdef CFG_WIFI_CONCURRENT
    if (wifi_concurrent_mode) {
        if (eloop_message_send(WIFI_VIF_INDEX_SOFTAP_MODE, WIFI_MGMT_EVENT_SWITCH_MODE_CMD, WVIF_UNKNOWN, NULL, 0)) {
            netlink_printf("MGMT: vif %d SWITCH_MODE_CMD, eloop event queue full\r\n", WIFI_VIF_INDEX_SOFTAP_MODE);
            return -2;
        }
    }
#endif

    if (eloop_message_send(WIFI_VIF_INDEX_DEFAULT, WIFI_MGMT_EVENT_SWITCH_MODE_CMD, WVIF_MONITOR, NULL, 0)) {
        netlink_printf("MGMT: vif %d SWITCH_MODE_CMD, eloop event queue full\r\n", WIFI_VIF_INDEX_DEFAULT);
        return -3;
    }

    /* save monitor info */
    cfg = sys_zalloc(sizeof(struct wifi_monitor));
    if (NULL == cfg) {
        netlink_printf("MGMT: alloc monitor_cfg failed\r\n");
        return -4;
    }

    cfg->cb = monitor_cb;
    cfg->cb_arg = NULL;
    cfg->channel = channel;
    cfg->uf = true;

    if (eloop_message_send(WIFI_VIF_INDEX_DEFAULT, WIFI_MGMT_EVENT_MONITOR_START_CMD, WVIF_MONITOR, (uint8_t *)cfg, sizeof(struct wifi_monitor))) {
        netlink_printf("MGMT: vif %d SWITCH_MODE_CMD, eloop event queue full\r\n", WIFI_VIF_INDEX_DEFAULT);
        sys_mfree(cfg);
        return -5;
    }

    /* Flush wait queue */
    mgmt_wait_queue_flush();

    /* Block here until monitor started received */
    if ((reason = mgmt_wait_queue_fetch(WIFI_VIF_INDEX_DEFAULT, MGMT_WAIT_EVT_MONITOR_START_DONE, 10000))) {
        netlink_printf("MGMT: Monitor started failed (reason = %d)\r\n", reason);
        return -6;
    }

    return 0;
}

/*!
    \brief      Connect with AP using WPS mode
    \param[in]  is_pbc: true using PBC and false using PIN code
    \param[in]  pin: PIN code, valid when using WPS PIN mode
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_management_wps_start(bool is_pbc, char *pin, uint8_t blocked)
{
    int reason = 0;
#ifdef CFG_WPS
    uint8_t vif_idx = WIFI_VIF_INDEX_DEFAULT;
    struct wps_config_t *wps_cfg;

    WIFI_CLOSED_CHECK(1);

    if (is_pbc == false && NULL == pin) {
        netlink_printf("MGMT: pin code is null\r\n");
        return -1;
    }

    wps_cfg = sys_zalloc(sizeof(struct wps_config_t));
    if (!wps_cfg) {
        netlink_printf("MGMT: alloc wps_cfg failed\r\n");
        return -3;
    }

    /* switch wvif to STA mode */
    if (eloop_message_send(vif_idx, WIFI_MGMT_EVENT_SWITCH_MODE_CMD, WVIF_STA, NULL, 0) < 0) {
        netlink_printf("MGMT: SWITCH_MODE_CMD, eloop event queue full\r\n");
        return -4;
    }

    /* Flush wait queue */
    mgmt_wait_queue_flush();

    wps_cfg->pbc = is_pbc;
    if (pin)
        strncpy((char *)wps_cfg->pin, pin, 8);
    if (eloop_message_send(vif_idx, WIFI_MGMT_EVENT_WPS_CMD, (int)is_pbc, (uint8_t *)wps_cfg, sizeof(struct wps_config_t)) < 0) {
        netlink_printf("MGMT: wps start, eloop event queue full\r\n");
        return -5;
    }

    if (blocked) {
        /* Block here until CONNECT related event received */
        reason = mgmt_wait_queue_fetch(vif_idx, MGMT_WAIT_EVT_CONN_DONE, WIFI_MGMT_WPS_CONNECT_BLOCK_TIME);
        if (reason == 0xFFFF) {
            netlink_printf("MGMT: wps, eloop wait timeout\r\n");
            wifi_management_disconnect();
        }
    }
#else
    netlink_printf("Please define CFG_WPS to enable wps.\r\n");
#endif
    return reason;
}

static void wifi_management_task(void *arg)
{
    if (wifi_netlink_start()) {
        netlink_printf("MGMT: wifi_netlink_start failed!\r\n");
    } else {
        wifi_task_ready(WIFI_MGMT_TASK);
    }

    wifi_wait_ready();

    wifi_eloop_run();

    wifi_eloop_destroy();

    sys_queue_free(&mgmt_wait_queue);

    wifi_task_terminated(WIFI_MGMT_TASK);

    sys_task_delete(NULL);
}

/*!
    \brief      Initialize wifi management components
    \param[in]  none
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_management_init(void)
{
    COMPILE_TIME_ASSERT(WIFI_MGMT_EVENT_MAX <= 0xFFFFFF);

    sys_memset(wifi_sm_data, 0, sizeof(wifi_sm_data));
    if (sys_queue_init(&mgmt_wait_queue, MGMT_WAIT_QUEUE_MSG_SIZE, sizeof(mgmt_wait_evt_t))){
        return -1;
    }

    wifi_mgmt_task_tcb = (os_task_t)sys_task_create(NULL, (const uint8_t *)"wifi_mgmt", NULL,
                    MGMT_TASK_STACK_SIZE, MGMT_TASK_QUEUE_SIZE, MGMT_TASK_QUEUE_ITEM_SIZE,
                    MGMT_TASK_PRIORITY, (task_func_t)wifi_management_task, NULL);
    if (wifi_mgmt_task_tcb == NULL) {
        netlink_printf("Create wifi management task failed.\r\n");
        return -2;
    }

    wifi_eloop_init();

    /* Wifi management sm init */
    eloop_event_send(WIFI_VIF_INDEX_DEFAULT, WIFI_MGMT_EVENT_INIT);

    if (wifi_netlink_auto_conn_get()) {
        eloop_message_send(WIFI_VIF_INDEX_DEFAULT, WIFI_MGMT_EVENT_SWITCH_MODE_CMD, WVIF_STA, NULL, 0);
        eloop_event_send(WIFI_VIF_INDEX_DEFAULT, WIFI_MGMT_EVENT_AUTO_CONNECT_CMD);
    }
    return 0;
}

/*!
    \brief      Release all wifi management components
    \param[in]  none
    \param[out] none
    \retval     none
*/
void wifi_management_deinit(void)
{
    wifi_eloop_terminate();
    wifi_wait_terminated(WIFI_MGMT_TASK);
}
