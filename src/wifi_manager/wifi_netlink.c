/*!
    \file    wifi_netlink.c
    \brief   Operations of wifi netlink for GD32VW55x SDK.

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

#include "wifi_vif.h"
#ifdef CONFIG_WPA_SUPPLICANT
#include "common/ieee802_11_defs.h"
#endif
#include "wifi_management.h"
#include "wifi_export.h"
#include "wifi_net_ip.h"
#include "wifi_wpa.h"
#include "wifi_init.h"
#include "debug_print.h"
#include "util.h"
#include "dhcpd.h"
#include "nvds_flash.h"
#include "gd32vw55x_platform.h"

// If WiFi has been closed or not
uint8_t wifi_work_status = WIFI_RUNNING;
const char *wifi_closed_warn = "WiFi has been closed. Please open WiFi first.\r\n";
const char *wifi_closing_warn = "WiFi is closing. Please wait for a minute.\r\n";

#ifdef CONFIG_WPA_SUPPLICANT
#define macif_cmd_send           macif_cntrl_cmd_send_cli
#define macif_control_start      macif_cntrl_start
#else
#define macif_cmd_send           macif_ctl_cmd_execute
#define macif_control_start      macif_ctl_start
#endif
extern void wpa_debug_level_set(uint32_t level);
/*!
    \brief      Close wifi debug log
    \param[in]  none
    \param[out] none
    \retval     0.
*/
int wifi_netlink_dbg_close(void)
{
    /* level: [0-7]- NONE|CRT|ERR|WRN|NTC|STAT|INF|VRB */
    /* module: Bit[0-11]- KE|DBG|IPC|DMA|MM|TX|RX|PHY|PS|WDG|RC|ME */
    macif_dbg_filter_set(0, 0);

#ifdef CONFIG_WPA_SUPPLICANT
    /* level: [0-5]- EXCESSIVE|MSGDUMP|DEBUG|INFO|WARNING|ERROR */
    wpa_debug_level_set(5);
#else
    wpas_info_print_close();
#endif

    global_debug_level = NONE;

    return 0;
}

/*!
    \brief      Open wifi debug log
    \param[in]  none
    \param[out] none
    \retval     0.
*/
int wifi_netlink_dbg_open(void)
{
    /* level: [0-7]- NONE|CRT|ERR|WRN|NTC|STAT|INF|VRB */
    /* module: Bit[0-11]- KE|DBG|IPC|DMA|MM|TX|RX|PHY|PS|WDG|RC|ME */
    macif_dbg_filter_set(4, 0xFFF);

#ifdef CONFIG_WPA_SUPPLICANT
    /* level: [0-5]- EXCESSIVE|MSGDUMP|DEBUG|INFO|WARNING|ERROR */
    wpa_debug_level_set(3);
#else
    wpas_info_print_open();
#endif

    global_debug_level = NOTICE;

    return 0;
}

/*!
    \brief      Show the name of wifi wireless mode
    \param[in]  wireless_mode: wifi wireless mode
    \param[out] none
    \retval     none.
*/
void wifi_netlink_wireless_mode_print(uint32_t wireless_mode)
{
    if (wireless_mode == WIRELESS_MODE_UNKNOWN)
        printf("Unknown\n");
    else if (wireless_mode == WIRELESS_MODE_11BGN_AX)
        printf("11bgn/ax\n");
    else if (wireless_mode == WIRELESS_MODE_11GN_AX)
        printf("11gn/ax\n");
    else if (wireless_mode == WIRELESS_MODE_11BGN)
        printf("11bgn\n");
    else if (wireless_mode == WIRELESS_MODE_11GN)
        printf("11gn\n");
    else if (wireless_mode == WIRELESS_MODE_11N)
        printf("11n\n");
    else if (wireless_mode == WIRELESS_MODE_11BG)
        printf("11bg\n");
    else if (wireless_mode == WIRELESS_MODE_11G)
        printf("11g\n");
    else if (wireless_mode == WIRELESS_MODE_11B)
        printf("11b\n");
    else
        printf("Error\n");
}

/*!
    \brief      Print wifi status
    \param[in]  none
    \param[out] none
    \retval     0.
*/
int wifi_netlink_status_print(void)
{
    struct wifi_vif_tag *wvif;
    enum wifi_wireless_mode wireless_mode;
    int i, j;

    printf("WIFI Status:\r\n");
    printf("==============================\r\n");
    for (i = 0; i < CFG_VIF_NUM; i++) {
        wvif =  &wifi_vif_tab[i];
        if (wvif->mac_vif == NULL) {
            #ifdef CFG_WIFI_CONCURRENT
            if (wifi_management_concurrent_get())
                printf("WiFi VIF[%d]: INACTIVE\r\n", i);
            #endif
            continue;
        }
        printf("WiFi VIF[%d]: "MAC_FMT"\r\n", i, MAC_ARG(wvif->mac_addr.array));
        if (wvif->wvif_type == WVIF_STA) {
            printf("\tSTA\r\n");
            printf("\t Status: ");
            if (wvif->sta.state <= WIFI_STA_STATE_SCAN)
                printf("Disconnected\r\n");
            else if (wvif->sta.state <= WIFI_STA_STATE_IP_GETTING)
                printf("Connecting\r\n");
            else if (wvif->sta.state == WIFI_STA_STATE_CONNECTED)
                printf("Connected\r\n");
            else
                printf("Unknown\r\n");
            if (wvif->sta.state >= WIFI_STA_STATE_CONNECT) {
                printf("\t SSID: %s\r\n", wvif->sta.cfg.ssid);
                printf("\t BSSID: "MAC_FMT"\r\n", MAC_ARG((uint16_t *)wvif->sta.cfg.bssid));
                printf("\t Channel: %d\r\n", wvif->sta.cfg.channel);
                printf("\t Bandwidth: ");
                if (wvif->sta.cfg.bw == PHY_CHNL_BW_20)
                    printf("20MHz\r\n");
                else if (wvif->sta.cfg.bw == PHY_CHNL_BW_40)
                    printf("40MHz\r\n");
                else
                    printf("Unknown\r\n");
                printf("\t Security: ");
                if (wvif->sta.cfg.akm & CO_BIT(MAC_AKM_SAE))
                    printf("WPA3\t\n");
                else if (wvif->sta.cfg.akm == CO_BIT(MAC_AKM_PRE_RSN))
                    printf("WEP\t\n");
                else if (wvif->sta.cfg.akm == (CO_BIT(MAC_AKM_PSK) | CO_BIT(MAC_AKM_PRE_RSN)))
                    printf("WPA\t\n");
                else if (wvif->sta.cfg.akm == CO_BIT(MAC_AKM_PSK))
                    printf("WPA2\t\n");
                else if (wvif->sta.cfg.akm == CO_BIT(MAC_AKM_PSK_SHA256))
                    printf("WPA2_SHA256\t\n");
                else if (wvif->sta.cfg.akm == CO_BIT(MAC_AKM_NONE))
                    printf("OPEN\t\n");
                else if (wvif->sta.cfg.akm == CO_BIT(MAC_AKM_8021X))
                    printf("WPA-EAP\t\n");
                else if (wvif->sta.cfg.akm == CO_BIT(MAC_AKM_8021X_SHA256))
                    printf("WPA-EAP-SHA256\t\n");
                else if (wvif->sta.cfg.akm == CO_BIT(MAC_AKM_8021X_SUITE_B))
                    printf("WPA-EAP-SUITE-B\t\n");
                else if (wvif->sta.cfg.akm == CO_BIT(MAC_AKM_8021X_SUITE_B_192))
                    printf("WPA-EAP-SUITE-B-192\t\n");
                else if (wvif->sta.cfg.akm == CO_BIT(MAC_AKM_OWE))
                    printf("OWE\t\n");
                else if (wvif->sta.cfg.akm == (CO_BIT(MAC_AKM_PSK) | CO_BIT(MAC_AKM_FT_PSK)))
                    printf("WPA2_FT\t\n");
                else
                    printf("Unknown\t\n");
                wireless_mode = macif_vif_wireless_mode_get(i);
                printf("\t Mode: ");
                wifi_netlink_wireless_mode_print(wireless_mode);
                printf("\t RSSI: %d\r\n", macif_vif_sta_rssi_get(i));
            }
            if (wvif->sta.state >= WIFI_STA_STATE_IP_GETTING) {
                struct wifi_ip_addr_cfg ip_cfg;
                wifi_get_vif_ip(i, &ip_cfg);
                printf("\t IP: "IP_FMT"\r\n", IP_ARG(ip_cfg.ipv4.addr));
                printf("\t GW: "IP_FMT"\r\n", IP_ARG(ip_cfg.ipv4.gw));
                #ifdef CONFIG_IPV6_SUPPORT
                {
                    char ip6_local[IPV6_ADDR_STRING_LENGTH_MAX] = {0};
                    char ip6_unique[IPV6_ADDR_STRING_LENGTH_MAX] = {0};
                    if (!wifi_get_vif_ip6(i, ip6_local, ip6_unique)) {
                        printf("\t IP6_local: [%s]\r\n", ip6_local);
                        printf("\t IP6_uniqe: [%s]\r\n", ip6_unique);
                    }
                }
                #endif
            }
        } else if (wvif->wvif_type == WVIF_AP) {
            struct wifi_ip_addr_cfg ip_cfg;
            struct mac_addr cli_mac[CFG_STA_NUM];
            int cli_num;

            printf("\tSoftAP\r\n");
            printf("\t Status: ");
            if (wvif->ap.ap_state == WIFI_AP_STATE_INIT)
                printf("Not Started\r\n");
            else if (wvif->ap.ap_state == WIFI_AP_STATE_STARTED)
                printf("Started\r\n");
            /*else if (wvif->ap.ap_state == WIFI_AP_STATE_BEFORE_CHANNEL_SWITCH)
                printf("Before Channel Switch\r\n");*/
            else
                printf("Unknown\r\n");
            if (wvif->ap.ap_state != WIFI_AP_STATE_STARTED) {
                continue;
            }
            printf("\t SSID: %s\r\n", wvif->ap.cfg.ssid);
            printf("\t Channel: %d\r\n", wvif->ap.cfg.channel);
            printf("\t Security: ");
            if (wvif->ap.cfg.akm == CO_BIT(MAC_AKM_NONE))
                printf("OPEN\t\n");
            else if (wvif->ap.cfg.akm == (CO_BIT(MAC_AKM_PSK) | CO_BIT(MAC_AKM_PRE_RSN)))
                printf("WPA\t\n");
            else if (wvif->ap.cfg.akm == CO_BIT(MAC_AKM_PSK))
                printf("WPA2\t\n");
            else if (wvif->ap.cfg.akm == CO_BIT(MAC_AKM_SAE))
                printf("WPA3\t\n");
            else if (wvif->ap.cfg.akm == (CO_BIT(MAC_AKM_PSK) | CO_BIT(MAC_AKM_SAE)))
                printf("WPA2/WPA3\t\n");
            else
                printf("Unknown\t\n");
            printf("\t Mode: ");
            if (wvif->ap.cfg.he_disabled)
                wireless_mode = WIRELESS_MODE_11BGN;
            else
                wireless_mode = WIRELESS_MODE_11BGN_AX;
            wifi_netlink_wireless_mode_print(wireless_mode);
            wifi_get_vif_ip(i, &ip_cfg);
            printf("\t IP: "IP_FMT"\r\n", IP_ARG(ip_cfg.ipv4.addr));
            printf("\t GW: "IP_FMT"\r\n", IP_ARG(ip_cfg.ipv4.gw));
            #ifdef CONFIG_IPV6_SUPPORT
            {
                char ip6_local[IPV6_ADDR_STRING_LENGTH_MAX] = {0};
                char ip6_unique[IPV6_ADDR_STRING_LENGTH_MAX] = {0};
                if (!wifi_get_vif_ip6(i, ip6_local, ip6_unique)) {
                    printf("\t IP6_local: [%s]\r\n", ip6_local);
                    printf("\t IP6_uniqe: [%s]\r\n", ip6_unique);
                }
            }
            #endif

            cli_num = macif_vif_ap_assoc_info_get(i, (uint16_t *)&cli_mac);
            for (j = 0; j < cli_num; j++) {
                uint32_t cli_ipaddr = dhcpd_find_ipaddr_by_macaddr((uint8_t *)cli_mac[j].array);
                printf("\t Client[%d]: "MAC_FMT"   ", j, MAC_ARG(cli_mac[j].array));
                if (cli_ipaddr == 0) {
                    printf("unknown.\r\n");
                } else {
                    printf(IP_FMT"\r\n", IP_ARG(cli_ipaddr));
                }
            }

        } else if (wvif->wvif_type == WVIF_MONITOR) {
            printf("\tMonitor\r\n");
        } else if (wvif->wvif_type == WVIF_UNKNOWN) {
            printf("\tUnknown\r\n");
        }
    }

    printf("\r\n");

    return 0;
}

/*!
    \brief      Print one result of wifi scan
    \param[in]  idx: index of the scanned AP
    \param[in]  result: pointer to the result of wifi scan
    \param[out] none
    \retval     none
*/
void wifi_netlink_scan_result_print(int idx, struct mac_scan_result *result)
{
    char sep __MAYBE_UNUSED;
    uint16_t mfp, group_cipher;
    uint8_t *bssid __MAYBE_UNUSED;
    int i;

    bssid = (uint8_t *)result->bssid.array;
    if (result->ssid.length < sizeof(result->ssid.array))
        result->ssid.array[result->ssid.length] = '\0';
    else
        result->ssid.array[result->ssid.length - 1] = '\0';

    netlink_printf("[%d] (%d dBm) CH=%3d BSSID=%02x:%02x:%02x:%02x:%02x:%02x SSID=%s ",
                idx,
                result->rssi,
                wifi_freq_to_channel(result->chan->freq),
                bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
                (char *)result->ssid.array);

    if (result->akm & CO_BIT(MAC_AKM_NONE))
    {
        netlink_printf("[OPEN]\n");
        return;
    }

    if (result->akm == CO_BIT(MAC_AKM_PRE_RSN))
    {
        netlink_printf("[WEP]\n");
        return;
    }

    if (result->akm & CO_BIT(MAC_AKM_WAPI_CERT) ||
        result->akm & CO_BIT(MAC_AKM_WAPI_PSK))
    {
        bool cert __MAYBE_UNUSED = result->akm & CO_BIT(MAC_AKM_WAPI_CERT);
        bool psk __MAYBE_UNUSED = result->akm & CO_BIT(MAC_AKM_WAPI_PSK);
        netlink_printf("[WAPI:%s%s%s %s/%s]\n",
                    cert ? "CERT" : "",
                    cert && psk ? "-" : "",
                    psk ? "PSK" : "",
                    result->group_cipher == CO_BIT(MAC_CIPHER_WPI_SMS4) ? "SMS4" : "?",
                    result->pairwise_cipher == CO_BIT(MAC_CIPHER_WPI_SMS4) ?
                    "SMS4" : "?");
        return;
    }

    if (result->akm & CO_BIT(MAC_AKM_PRE_RSN))
        netlink_printf(" [WPA");
    else
        netlink_printf(" [RSN");

    sep = ':';
    for (i = MAC_AKM_8021X; i <= MAC_AKM_DPP; i++)
    {
        if (result->akm & CO_BIT(i))
        {
            netlink_printf("%c%s", sep, wpa_akm_str[i]);
            sep = ',';
        }
    }

    sep = ' ';
    for (i = MAC_CIPHER_WEP40; i <= MAC_CIPHER_BIP_CMAC_256; i++)
    {
        if (result->pairwise_cipher & CO_BIT(i))
        {
            netlink_printf("%c%s", sep, wpa_cipher_str[i]);
            sep = ',';
        }
    }
    netlink_printf("/");

    group_cipher = result->group_cipher;
    mfp = (CO_BIT(MAC_CIPHER_BIP_CMAC_128) | CO_BIT(MAC_CIPHER_BIP_GMAC_128) |
           CO_BIT(MAC_CIPHER_BIP_GMAC_256) | CO_BIT(MAC_CIPHER_BIP_CMAC_256));
    mfp = group_cipher & mfp;
    group_cipher &= ~(mfp);

    if (group_cipher) {
        i = 31 - co_clz(group_cipher);
        if (i < MAC_CIPHER_BIP_CMAC_256)
            netlink_printf("%s", wpa_cipher_str[i]);
    } else
        netlink_printf("?");

    if (mfp) {
        netlink_printf("][MFP");
        sep = ':';
        for (i = MAC_CIPHER_BIP_CMAC_128; i <= MAC_CIPHER_BIP_CMAC_256; i++) {
            if (mfp & CO_BIT(i)) {
                netlink_printf("%c%s", sep, wpa_cipher_str[i]);
                sep = ',';
            }
        }
    }
    netlink_printf("]\n");
}

/*!
    \brief      Print all results of wifi scan
    \param[in]  vif_idx: index of the wifi vif
    \param[in]  callback: callback func to print result of wifi scan
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_scan_results_print(int vif_idx, void (*callback)(int, struct mac_scan_result *))
{
    uint32_t result_cnt = 0;
    struct macif_scan_results *results;
    int idx;

    results = (struct macif_scan_results *)sys_malloc(sizeof(struct macif_scan_results));
    if (NULL == results)
        return -1;

    if (wifi_netlink_scan_results_get(vif_idx, results)) {
        sys_mfree(results);
        return -2;
    }

    result_cnt = results->result_cnt;
    for (idx = 0; idx < result_cnt; idx++) {
        if (callback)
            callback(idx, &results->result[idx]);
    }

    sys_mfree(results);
    return 0;
}

/*!
    \brief      Find the specified AP in the results of wifi scan
    \param[in]  vif_idx: index of the wifi vif
    \param[in]  bssid: pointer to the bssid of the AP
    \param[in]  ssid: pointer to the ssid of the AP
    \param[in]  candidate: pointer to the result of wifi scan
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_candidate_ap_find(int vif_idx, uint8_t *bssid, char *ssid, struct mac_scan_result *candidate)
{
    struct macif_scan_results *results = NULL;
    struct mac_scan_result *result;
    int idx, ret = 0;
    int bssid_valid = 0;
    int max_rssi = -255;
    int found = 0;
    uint32_t result_cnt = 0;

    if (vif_idx >= CFG_VIF_NUM) {
        return -1;
    }

    if (bssid)
        bssid_valid = 1;
    if (!bssid_valid && (NULL == ssid)) {
        return -2;
    }

    results = (struct macif_scan_results *) sys_zalloc(sizeof(struct macif_scan_results));
    if (NULL == results) {
        return -3;
    }
    if (wifi_netlink_scan_results_get(vif_idx, results)) {
        ret = -4;
        goto Exit;
    }

    result_cnt = results->result_cnt;
    for (idx = 0; idx < result_cnt; idx++) {
        result = &results->result[idx];
        if (bssid_valid) {
            if (0 == sys_memcmp(bssid, (uint8_t *)result->bssid.array, WIFI_ALEN)) {
                if (result->rssi > max_rssi) {
                    found++;
                    sys_memcpy(candidate, result, sizeof(*candidate));
                    max_rssi = result->rssi;
                }
            }
        } else {
            if ((strlen(ssid) == result->ssid.length)
                && (strncmp(ssid, (char *)result->ssid.array, result->ssid.length) == 0)) {
                if (result->rssi > max_rssi) {
                    found++;
                    sys_memcpy(candidate, result, sizeof(*candidate));
                    max_rssi = result->rssi;
                }
            }
        }
    }

    if (found == 0) {
        if (bssid_valid)
            netlink_printf("Can not found candidate AP: "MAC_FMT".\r\n", MAC_ARG_UINT8(bssid));
        else
            netlink_printf("Can not found candidate AP: %s.\r\n", ssid);
        ret = -5;
    }

Exit:
    sys_mfree(results);

    return ret;
}

/*!
    \brief      Set whether to enable auto connection
    \param[in]  auto_conn_enable: 0: disble; 1: enable
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_auto_conn_set(uint8_t auto_conn_enable)
{
    return nvds_data_put(NULL, NVDS_NS_WIFI_INFO, WIFI_AUTO_CONN_EN,
                        (uint8_t *)(&auto_conn_enable), sizeof(uint8_t));
}

/*!
    \brief      Get whether to enable auto connection
    \param[in]  none
    \param[out] none
    \retval     0: disble; 1: enable.
*/
uint8_t wifi_netlink_auto_conn_get(void)
{
    uint8_t auto_conn_enable;
    uint8_t flash_data_len = sizeof(uint8_t);

    int ret = nvds_data_get(NULL, NVDS_NS_WIFI_INFO, WIFI_AUTO_CONN_EN,
                            (uint8_t *)(&auto_conn_enable), (uint32_t *)(&flash_data_len));
    if (ret != 0) {
        auto_conn_enable = 0;
    }

    return auto_conn_enable;
}

/*!
    \brief      Store infomation of connected AP if enable auto connection
    \param[in]  cfg: pointer to the information of AP
    \param[in]  ip: IP address
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_joined_ap_store(struct sta_cfg *cfg, uint32_t ip)
{
    struct auto_conn_info info;
    uint32_t total_len;

    //ip
    info.ip_addr = ip;

    //channel
    info.channel = cfg->channel;

    //ssid
    info.ssid.length = cfg->ssid_len;
    sys_memcpy(info.ssid.array, (uint8_t *)cfg->ssid, info.ssid.length);

    //key
    info.key.length = cfg->passphrase_len;
    sys_memcpy(info.key.array, cfg->passphrase, info.key.length);

    /* only store the valid key length */
    total_len = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(info.ssid) + info.key.length + 1;

    netlink_printf("Store ssid = %s passphrase = %s channel = %d ip = "IP_FMT"\r\n",
                    cfg->ssid, cfg->passphrase, cfg->channel, IP_ARG(info.ip_addr));

    return nvds_data_put(NULL, NVDS_NS_WIFI_INFO, WIFI_AUTO_CONN_AP_INFO, (uint8_t *)(&info), total_len);
}

/*!
    \brief      Load infomation of connected AP which saved previously if enable auto connection
    \param[in]  vif_idx: index of the wifi vif
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_joined_ap_load(int vif_idx)
{
    struct wifi_vif_tag *wvif = &wifi_vif_tab[vif_idx];
    struct sta_cfg *cfg = &wvif->sta.cfg;
    struct auto_conn_info info = {0};
    uint32_t flash_data_len = sizeof(struct auto_conn_info);
    int ret;

    ret = nvds_data_get(NULL, NVDS_NS_WIFI_INFO, WIFI_AUTO_CONN_AP_INFO,
                        (uint8_t *)(&info), &flash_data_len);
    if (ret != 0) {
        return ret;
    }

    if ((info.ssid.length == 0) || (info.ssid.length > MAC_SSID_LEN) ||
                    (info.key.length > WPA_MAX_PSK_LEN) ||
                    ((info.key.length < WPA_MIN_PSK_LEN) && (info.key.length != 0) && (info.key.length != WPAS_WEP40_ASCII_LEN))) {
        return -1;
    }

    cfg->ssid_len = info.ssid.length;
    sys_memcpy(cfg->ssid, info.ssid.array, cfg->ssid_len);
    cfg->passphrase_len = info.key.length;
    sys_memcpy(cfg->passphrase, info.key.array, cfg->passphrase_len);
    cfg->channel = info.channel;

    wvif->sta.history_ip = info.ip_addr;
    netlink_printf("Load ssid = %s passphrase = %s channel = %d ip = "IP_FMT"\r\n",
                    cfg->ssid, cfg->passphrase, cfg->channel, IP_ARG(info.ip_addr));

    return 0;
}

/*!
    \brief      Config and start wifi scan
    \param[in]  vif_idx: index of the wifi vif
    \param[in]  channel: channel to be scanned
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_scan_set(int vif_idx, uint8_t channel)
{
    return wifi_netlink_scan_set_with_extraie(vif_idx, channel, NULL, 0);
}

/*!
    \brief      Config and start wifi scan, but only scan the specified AP
    \param[in]  vif_idx: index of the wifi vif
    \param[in]  ssid: pointer to the ssid of the AP
    \param[in]  channel: channel to be scanned
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_scan_set_with_ssid(int vif_idx, char *ssid, uint8_t channel)
{
    struct macif_cmd_scan cmd;
    struct macif_cmd_resp resp;
    struct macif_scan_ssid scan_ssid = {NULL, 0};
    char str_ssid[MAC_SSID_MAX_LEN + 1];
    int freq;

    if ((NULL == ssid) || strlen(ssid) > MAC_SSID_MAX_LEN)
        return -1;

    if ((channel < 1) || ((channel > 14) && (channel != 0xFF)))
        return -2;

    sys_memset((void *)&cmd, 0, sizeof(cmd));

    scan_ssid.len = strlen(ssid);
    strcpy(str_ssid, ssid);
    scan_ssid.ssid = str_ssid;
    cmd.ssids = &scan_ssid;
    cmd.ssid_cnt = 1;

    cmd.hdr.len = sizeof(cmd);
    cmd.hdr.id = MACIF_SCAN_CMD;
    cmd.vif_idx = vif_idx;
    if (0xFF == channel) {
        /* all channels */
        cmd.freqs = NULL;
    } else {
        /* specified channel */
        freq = wifi_channel_to_freq(channel);
        cmd.freqs = &freq;
    }
    cmd.extra_ies = NULL;
    cmd.bssid = NULL;
    cmd.extra_ies_len = 0;
    cmd.no_cck = 0;
    cmd.duration = 0;
    cmd.passive = false;
    cmd.sock = -1;

    if (macif_cmd_send(&cmd.hdr, &resp.hdr) ||
        (resp.status != MACIF_STATUS_SUCCESS))
        return -3;
    return 0;
}

/*!
    \brief      Config and start wifi scan
    \param[in]  vif_idx: index of the wifi vif
    \param[in]  channel: channel to be scanned
    \param[in]  extra_ie: extra ie to be put in probe request
    \param[in]  extra_ie_len: the length of extra ie
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_scan_set_with_extraie(int vif_idx, uint8_t channel,
                                  uint8_t *extra_ie, uint32_t extra_ie_len)
{
    struct macif_cmd_scan cmd;
    struct macif_cmd_resp resp;
    struct macif_scan_ssid ssid = {NULL, 0};
    int freq;

    if ((channel < 1) || ((channel > 14) && (channel != 0xFF))) {
        netlink_printf("%s: channel (%d) is illegal\r\n", __func__, channel);
        return -1;
    }

    sys_memset((void *)&cmd, 0, sizeof(cmd));
    cmd.hdr.len = sizeof(cmd);
    cmd.hdr.id = MACIF_SCAN_CMD;
    cmd.vif_idx = vif_idx;
    if (channel == 0xFF) {
        /* all channels */
        cmd.freqs = NULL;
    } else {
        /* specified channel */
        freq = wifi_channel_to_freq(channel);
        cmd.freqs = &freq;
    }
    cmd.extra_ies = extra_ie;
    cmd.bssid = NULL;
    cmd.ssids = &ssid;
    cmd.extra_ies_len = extra_ie_len;
    cmd.no_cck = 0;
    cmd.ssid_cnt = 1;
    cmd.duration = 0;
    cmd.passive = false;
    cmd.sock = -1;

    if (macif_cmd_send(&cmd.hdr, &resp.hdr) ||
        (resp.status != MACIF_STATUS_SUCCESS))
        return -2;
    return 0;
}

/*!
    \brief      Get results of wifi scan
    \param[in]  vif_idx: index of the wifi vif
    \param[out] results: pointer to the result of wifi scan
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_scan_results_get(int vif_idx, struct macif_scan_results *results)
{
    struct macif_cmd_scan_results cmd;
    struct macif_scan_results_resp resp;

    if (vif_idx >= CFG_VIF_NUM) {
        return -1;
    }

    if (NULL == results) {
        return -2;
    }

    cmd.hdr.len = sizeof(cmd);
    cmd.hdr.id = MACIF_GET_SCAN_RESULTS_CMD;
    cmd.vif_idx = vif_idx;
    resp.results = results;

    if (macif_cmd_send(&cmd.hdr, &resp.hdr)) {
        return -3;
    }

    return 0;
}

#ifdef CFG_SOFTAP
/*!
    \brief      Start softap
    \param[in]  vif_idx: index of the wifi vif
    \param[in]  cfg: pointer to the config of softap
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_ap_start(int vif_idx, struct ap_cfg *cfg)
{
    struct wifi_vif_tag *wvif;
    struct ap_cfg *ap_cfg;
    struct wifi_ip_addr_cfg ip_cfg;
    int res = 0;

    if (vif_idx >= CFG_VIF_NUM)
        return -1;

    wvif =  &wifi_vif_tab[vif_idx];
    if (wvif->wvif_type != WVIF_AP)
        return -2;

    if (VIF_AP != macif_vif_type_get(vif_idx)) {
        return -3;
    }

    /* 1. copy cfg to wifi vif */
    ap_cfg = &wvif->ap.cfg;
    if (cfg) {
        if (cfg->ssid_len == 0 || cfg->channel == 0)
            return -4;

        sys_memcpy(ap_cfg, cfg, sizeof(struct ap_cfg));

        if (ap_cfg->akm & CO_BIT(MAC_AKM_SAE)) {
            if (ap_cfg->akm & CO_BIT(MAC_AKM_PSK)) {
                // MFP is optional in WPA2/WP3 transition mode
                ap_cfg->mfp = 1;
            } else {
                // MFP is mandatory in SAE
                ap_cfg->mfp = 2;
            }
        } else {
            ap_cfg->mfp = 0;
        }
        ap_cfg->he_disabled = 1;
        ap_cfg->bcn_interval = 100;  // 113
        ap_cfg->dtim_period = 1;
    }

    /* 2. inform wpas to start softap */
    res = wifi_wpa_ap_sm_step(vif_idx, WIFI_MGMT_EVENT_START_AP_CMD, (uint8_t *)ap_cfg, sizeof(struct ap_cfg));
    if (res) {
        netlink_printf("%s: ap start failed, res %d\r\n", __func__, res);
        return -4;
    }

    /* 3. Set IP */
    net_if_up(&wvif->net_if);
    sys_memset(&ip_cfg, 0, sizeof(ip_cfg));
    ip_cfg.mode = IP_ADDR_DHCP_SERVER;
#ifdef CONFIG_IPV6_SUPPORT
    ip_cfg.ip6_mode = IP6_ADDR_SERVER;
#endif
    ip_cfg.default_output = true;
    ip_cfg.ipv4.addr = PP_HTONL(LWIP_MAKEU32(192, 168, 237, 1));
    ip_cfg.ipv4.mask = PP_HTONL(LWIP_MAKEU32(255, 255, 255, 0));
    ip_cfg.ipv4.gw = PP_HTONL(LWIP_MAKEU32(192, 168, 237, 1));
    res = wifi_set_vif_ip(vif_idx, &ip_cfg);
    if (res) {
        net_if_down(&wvif->net_if);
        wifi_wpa_ap_sm_step(vif_idx, WIFI_MGMT_EVENT_STOP_AP_CMD, NULL, 0);
        netlink_printf("%s: dhcp server start failed, res %d\r\n", __func__, res);
        return -5;
    }

    /* 4. Set mac vif state */
    macif_vif_ap_state_set(vif_idx, AP_OPEN);
    macif_vif_ap_isolation_set(vif_idx, false);

    netlink_printf("IP: "IP_FMT"/24\r\n", IP_ARG(ip_cfg.ipv4.addr));
    netlink_printf("GW: "IP_FMT"/24\r\n", IP_ARG(ip_cfg.ipv4.gw));
    return 0;
}

/*!
    \brief      Stop softap
    \param[in]  vif_idx: index of the wifi vif
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_ap_stop(int vif_idx)
{
    struct wifi_vif_tag *wvif;
    struct wifi_ip_addr_cfg ip_cfg;

    if (vif_idx >= CFG_VIF_NUM)
        return -1;

    wvif = vif_idx_to_wvif(vif_idx);
    if (wvif->wvif_type != WVIF_AP || macif_vif_type_get(vif_idx) != VIF_AP)
        return 0;

    if (wvif->ap.ap_state != WIFI_AP_STATE_STARTED)
        return 0;

    /* 1. stop wpas softap */
    wifi_wpa_ap_sm_step(vif_idx, WIFI_MGMT_EVENT_STOP_AP_CMD, NULL, 0);

    /* 2. stop dhcpd and reset ip */
    ip_cfg.mode = IP_ADDR_NONE;
#ifdef CONFIG_IPV6_SUPPORT
    ip_cfg.ip6_mode = IP6_ADDR_NONE;
#endif
    wifi_set_vif_ip(vif_idx, &ip_cfg);
    net_if_down(&wvif->net_if);

    /* 3. stop mac softap */
    macif_vif_ap_state_set(vif_idx, AP_CLOSE);

    return 0;
}
#endif /* CFG_SOFTAP */

static void monitor_cb_default(struct wifi_frame_info *info, void *arg)
{
    if (info->payload == NULL)
    {
        netlink_printf("Unsupported frame: length = %d\r\n", info->length);
    }
    else
    {
#if 0
        struct mac_hdr *hdr __MAYBE_UNUSED = (struct mac_hdr *)info->payload;
        netlink_printf(MAC_FMT" "MAC_FMT" "MAC_FMT" FCTL %x SN:%d length = %d\r\n", MAC_ARG(hdr->addr1.array),
                  MAC_ARG(hdr->addr2.array), MAC_ARG(hdr->addr3.array),
                  hdr->fctl, hdr->seq >> 4, info->length);
#endif
    }
}

/*!
    \brief      Start monitor
    \param[in]  vif_idx: index of the wifi vif
    \param[in]  cfg: pointer to the config of monitor
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_monitor_start(int vif_idx, struct wifi_monitor *cfg)
{
    struct wifi_vif_tag *wvif;
    struct macif_cmd_monitor_cfg cmd = {0};
    struct macif_cmd_resp resp;
    struct mac_chan_def *chan;
    struct wifi_monitor *mon = NULL;

    if (vif_idx >= CFG_VIF_NUM)
        return -1;

    // do nothing, use default monitor configuration
    if (cfg == NULL)
        return 0;

    wvif = vif_idx_to_wvif(vif_idx);
    if (wvif->wvif_type != WVIF_MONITOR || macif_vif_type_get(vif_idx) != VIF_MONITOR)
        return -2;

    mon = &wifi_vif_tab[vif_idx].monitor;
    if (mon != cfg) {
        mon->cb = cfg->cb;
        mon->cb_arg = cfg->cb_arg;
        mon->channel = cfg->channel;
        mon->uf = cfg->uf;
    }

#ifdef CONFIG_WPA_SUPPLICANT
    wifi_wpa_link_monitor(vif_idx, 1);  //TODO??
    //wifi_wpa_link_monitor(vif_idx, 0);
#endif

    cmd.hdr.len = sizeof(cmd);
    cmd.hdr.id = MACIF_MONITOR_CFG_CMD;
    cmd.vif_idx = vif_idx;

    cmd.chan.prim20_freq = wifi_channel_to_freq(cfg->channel);
    // by default 20Mhz bandwidth
    cmd.chan.type = PHY_CHNL_BW_20;
    cmd.chan.center1_freq = cmd.chan.prim20_freq;
    cmd.chan.center2_freq = 0;

    cmd.uf = cfg->uf;
    if (cfg->cb)
        cmd.cb = cfg->cb;
    else
        cmd.cb = monitor_cb_default;
    cmd.cb_arg = cfg->cb_arg;

    chan = (struct mac_chan_def *)macif_wifi_chan_get(cmd.chan.prim20_freq);
    if (!chan)
        return -3;

    cmd.chan.tx_power = chan->tx_power;
    cmd.chan.band = chan->band;

    if (macif_cmd_send(&cmd.hdr, &resp.hdr) ||
        (resp.status != MACIF_STATUS_SUCCESS))
        return -4;

    return 0;
}

/*!
    \brief      Set wifi channel
    \param[in]  channel: wifi channel
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_channel_set(uint32_t channel)
{
    struct macif_cmd_set_channel cmd;
    struct macif_set_channel_resp resp;

    cmd.hdr.len = sizeof(cmd);
    cmd.hdr.id = MACIF_SET_CHANNEL_CMD;
    cmd.chan_idx = channel;

    if (macif_cmd_send(&cmd.hdr, &resp.hdr) || (resp.status != MACIF_STATUS_SUCCESS))
        return -1;

    return 0;
}

/*!
    \brief      Set power save mode
    \param[in]  vif_idx: index of the wifi vif
    \param[in]  ps_mode: power save mode
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_ps_mode_set(int vif_idx, uint8_t psmode)
{
    struct macif_cmd_set_ps_mode cmd;
    struct macif_cmd_resp resp;
    struct wifi_vif_tag *wvif = vif_idx_to_wvif(vif_idx);

    if (!wvif) {
        return -1;
    }

    if (psmode > WIFI_STA_PS_MODE_BASED_ON_TD) {
        return -1;
    }

    wvif->sta.psmode = psmode;

    if (psmode == WIFI_STA_PS_MODE_ALW_ON) {
        cmd.ps_on = 1;
        cmd.ps_mode = PS_MODE_ON;
    } else if (psmode == WIFI_STA_PS_MODE_BASED_ON_TD) {
        cmd.ps_on = 1;
        cmd.ps_mode = PS_MODE_ON_DYN;
    } else {
        cmd.ps_on = 0;
        cmd.ps_mode = PS_MODE_OFF;
    }

    cmd.hdr.len = sizeof(cmd);
    cmd.hdr.id = MACIF_SET_PS_MODE_CMD;
    cmd.vif_idx = vif_idx;

    if (macif_cmd_send(&cmd.hdr, &resp.hdr))
        return -1;

    return 0;
}

int wifi_netlink_enable_vif_ps(int vif_idx)
{
    struct macif_cmd_enable_vif_ps cmd;
    struct macif_cmd_resp resp;
    struct wifi_vif_tag *wvif = vif_idx_to_wvif(vif_idx);

    if ((wvif->wvif_type == WVIF_STA) && (wvif->sta.state == WIFI_STA_STATE_CONNECTED)) {
        cmd.hdr.len = sizeof(cmd);
        cmd.hdr.id = MACIF_ENABLE_VIF_PS_CMD;
        cmd.vif_idx = vif_idx;

        if (macif_cmd_send(&cmd.hdr, &resp.hdr) || (resp.status != MACIF_STATUS_SUCCESS))
            return -1;
    }

    return 0;
}

int wifi_netlink_priv_req(uint32_t type, uint32_t param1, uint32_t param2, uint32_t *result)
{
    struct macif_cmd_do_priv_req cmd;
    struct macif_do_priv_resp resp;

    cmd.hdr.len = sizeof(cmd);
    cmd.hdr.id = MACIF_DO_PRIV_REQ_CMD;

    cmd.req_type = (WIFI_PRIV_REQ_E)type;
    cmd.param1 = param1;
    cmd.param2 = param2;
    cmd.result = result;

    if (macif_cmd_send(&cmd.hdr, &resp.hdr) ||
        (resp.status != MACIF_STATUS_SUCCESS))
        return MACIF_STATUS_ERROR;

    return MACIF_STATUS_SUCCESS;
}

int wifi_netlink_ext_priv_req(uint32_t type, uint32_t param1, uint32_t param2, uint32_t param3,
                                        uint32_t param4, uint32_t *result)
{
    struct macif_cmd_do_priv_req cmd;
    struct macif_do_priv_resp resp;

    cmd.hdr.len = sizeof(cmd);
    cmd.hdr.id = MACIF_DO_PRIV_REQ_CMD;

    cmd.req_type = (WIFI_PRIV_REQ_E)type;
    cmd.param1 = param1;
    cmd.param2 = param2;
    cmd.param3 = param3;
    cmd.param4 = param4;
    cmd.result = result;

    if (macif_cmd_send(&cmd.hdr, &resp.hdr) ||
        (resp.status != MACIF_STATUS_SUCCESS))
        return MACIF_STATUS_ERROR;

    return MACIF_STATUS_SUCCESS;
}

/*!
    \brief      Set power save listen beacon interval
    \param[in]  interval: 0, listen beacon by dtim, other, the interval of listen beacon
    \param[in]  fixed_rate_idx: index of the fixed rate
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/

int wifi_netlink_listen_interval_set(uint8_t interval)
{
    return wifi_netlink_priv_req(WIFI_PRIV_LISTEN_INTERVAL, interval, 0, NULL);
}

/*!
    \brief      Set fixed rate
    \param[in]  sta_idx: index of the station
    \param[in]  fixed_rate_idx: index of the fixed rate
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_fix_rate_set(int sta_idx, int fixed_rate_idx)
{
    struct macif_cmd_rc_set_rate cmd;
    struct macif_cmd_resp resp;
    uint32_t rate_config_value;
    char buf[LINE_MAX_SZ];

    // Convert rate index into rate configuration
    if ((fixed_rate_idx < 0) || (fixed_rate_idx >= TOT_RATES)) {
        // disable fixed rate
        rate_config_value = (uint32_t)-1;
    } else if (macif_setting_rate_cfg_get(fixed_rate_idx, false, &rate_config_value, NULL)) {
        return MACIF_STATUS_ERROR;
    }
    if (fixed_rate_idx >= 0) {
        #ifdef CFG_MAC_DBG
        wifi_rc_print_rate(buf, LINE_MAX_SZ, rate_config_value, 0, NULL);
        netlink_printf("%s\r\n", buf);
        #endif
    } else {
        netlink_printf("Disable fixed rate.\r\n");
    }
    // prepare RC_SET_RATE_CMD to send
    cmd.hdr.len = sizeof(cmd);
    cmd.hdr.id = MACIF_RC_SET_RATE_CMD;

    cmd.sta_idx = sta_idx;
    cmd.fixed_rate_cfg = (uint16_t)rate_config_value;

    // Send RC_SET_RATE_CMD
    if (macif_cmd_send(&cmd.hdr, &resp.hdr) ||
        (resp.status != MACIF_STATUS_SUCCESS)) {
        return MACIF_STATUS_ERROR;
    }
    return MACIF_STATUS_SUCCESS;
}

/*!
    \brief      get wifi doze stats
    \param[in/out]  doze_time: doze time in statistic window time
    \param[in/out]  stats_time: statistic window time
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_sys_stats_get(uint32_t *doze_time, uint32_t *stats_time)
{
#ifdef CFG_STATS
    struct macif_cmd cmd;
    struct macif_sys_stats_resp rsp;
    struct dbg_get_sys_stat_cfm *stats = &rsp.stats;

    cmd.hdr.len = sizeof(cmd);
    cmd.hdr.id = MACIF_SYS_STATS_CMD;

    sys_memset(&rsp, 0, sizeof(rsp));
    if (macif_cmd_send(&cmd.hdr, &rsp.hdr)
        || (rsp.status != MACIF_STATUS_SUCCESS))
        return -1;

    if (doze_time)
        *doze_time = stats->doze_time;
    if (stats_time)
        *stats_time = stats->stats_time;
#endif
    return 0;
}

#ifdef CFG_TWT
/*!
    \brief      Config and start twt connection
    \param[in]  vif_idx: index of the wifi vif
    \param[in]  param: pointer to the config of twt
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_twt_setup(int vif_idx, struct macif_twt_setup_t *param)
{
    struct macif_cmd_twt_setup_req cmd;
    struct macif_cmd_resp resp;

    cmd.hdr.len = sizeof(cmd);
    cmd.hdr.id = MACIF_TWT_SETUP_REQ_CMD;
    cmd.vif_idx = vif_idx;
    sys_memcpy(&cmd.param, param, sizeof(struct macif_twt_setup_t));

    if (macif_cmd_send(&cmd.hdr, &resp.hdr) ||
        (resp.status != MACIF_STATUS_SUCCESS)) {
        return -1;
    }
    return 0;
}

/*!
    \brief      Config and start twt connection
    \param[in]  vif_idx: index of the wifi vif
    \param[in]  id: index of the twt connection
    \param[in]  neg_type: type of twt negotiation
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_twt_teardown(int vif_idx, uint8_t id, uint8_t neg_type)
{
    struct macif_cmd_twt_teardown_req cmd;
    struct macif_cmd_resp resp;

    cmd.hdr.len = sizeof(cmd);
    cmd.hdr.id = MACIF_TWT_TEARDOWN_REQ_CMD;

    cmd.vif_idx = vif_idx;
    cmd.all_twt = 0;
    cmd.id = id;
    cmd.neg_type = neg_type;

    if (macif_cmd_send(&cmd.hdr, &resp.hdr) ||
        (resp.status != MACIF_STATUS_SUCCESS)) {
        return -1;
    }
    return 0;
}
#endif /* CFG_TWT */

/*!
    \brief      indicate dhcp is successful
    \param[in]  vif_idx: index of the wifi vif
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_dhcp_done(int vif_idx)
{
    struct macif_cmd_dhcp_done cmd;
    struct macif_cmd_resp resp;

    /* Ctrl port open */
    cmd.hdr.len = sizeof(cmd);
    cmd.hdr.id = MACIF_DHCP_DONE_CMD;
    cmd.vif_idx = vif_idx;

    if (macif_cmd_send(&cmd.hdr, &resp.hdr)
        || (resp.status != MACIF_STATUS_SUCCESS))
        return -1;

    return 0;
}

/*!
    \brief      set roaming rssi threshold
    \param[in]  vif_idx: index of the wifi vif
    \param[in]  rssi_thresh: the rssi threshold for roaming
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_roaming_rssi_set(int vif_idx, int8_t rssi_thresh)
{
    struct macif_cmd_roaming_rssi cmd;
    struct macif_cmd_resp rsp;

    cmd.hdr.len = sizeof(cmd);
    cmd.hdr.id = MACIF_ROAMING_RSSI_CMD;
    cmd.vif_idx = vif_idx;
    cmd.rssi_threshold = rssi_thresh;
    cmd.rssi_hysteresis = VIF_RSSI_HYSTERESIS;

    sys_memset(&rsp, 0, sizeof(rsp));
    if (macif_cmd_send(&cmd.hdr, &rsp.hdr)
        || (rsp.status != MACIF_STATUS_SUCCESS))
        return -1;

    return 0;
}

/*!
    \brief      get roaming rssi threshold
    \param[in]  vif_idx: index of the wifi vif
    \param[out]  rssi_thresh: the rssi threshold for roaming
    \retval     rssi threshold on success and 0 if error occured.
*/
int8_t wifi_netlink_roaming_rssi_get(int vif_idx)
{
    return macif_vif_roaming_rssi_get(vif_idx);
}

#ifndef CONFIG_WPA_SUPPLICANT
/*!
    \brief      Connect to an AP
    \param[in]  vif_idx: index of the wifi vif
    \param[in]  cfg: pointer to the information of AP
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_connect_req(int vif_idx, struct sta_cfg *cfg)
{
    struct wifi_vif_tag *wvif;
    struct mac_scan_result candidate;
    struct macif_cmd_connect cmd;
    struct sta_cfg *sta_cfg;
    struct macif_cmd_resp resp;
    uint8_t wep_pwd[WPAS_WEP104_HEX_LEN];
    int ret = -1, res;
    uint16_t mfp;

    if ((vif_idx >= CFG_VIF_NUM) || (NULL == cfg)) {
        return WIFI_MGMT_CONN_UNSPECIFIED;
    }

    if (VIF_STA != macif_vif_type_get(vif_idx)) {
        return WIFI_MGMT_CONN_UNSPECIFIED;
    }

    wvif =  &wifi_vif_tab[vif_idx];
    sta_cfg = &wvif->sta.cfg;

    /* find candidate ap from scan results */
    sys_memset(&candidate, 0, sizeof(struct mac_scan_result));
    if (cfg->conn_with_bssid)
        ret = wifi_netlink_candidate_ap_find(vif_idx, cfg->bssid, NULL, &candidate);
    else
        ret = wifi_netlink_candidate_ap_find(vif_idx, NULL, cfg->ssid, &candidate);
    if (ret) {
        wvif->sta.last_reason = WIFI_MGMT_CONN_NO_AP;
        return WIFI_MGMT_CONN_NO_AP;
    }
    wifi_netlink_scan_result_print(vif_idx, &candidate);

    /* Check if crypto matched */
    if ((cfg->passphrase_len != 0 && candidate.akm == CO_BIT(MAC_AKM_NONE))
        || (cfg->passphrase_len == 0 && !(candidate.akm & (CO_BIT(MAC_AKM_NONE) | CO_BIT(MAC_AKM_OWE)
                                        | CO_BIT(MAC_AKM_8021X) | CO_BIT(MAC_AKM_8021X_SHA256)
                                        | CO_BIT(MAC_AKM_8021X_SUITE_B) | CO_BIT(MAC_AKM_8021X_SUITE_B_192))))) {
        return WIFI_MGMT_CONN_NO_AP;
    }

    if (candidate.akm == CO_BIT(MAC_AKM_PRE_RSN)) {
        if ((cfg->passphrase_len != WPAS_WEP40_ASCII_LEN) && (cfg->passphrase_len != WPAS_WEP40_HEX_LEN)
            && (cfg->passphrase_len != WPAS_WEP104_ASCII_LEN) && (cfg->passphrase_len != WPAS_WEP104_HEX_LEN)) {
            netlink_printf("%s: WEP passphrase len %d error\r\n", __func__, cfg->passphrase_len);
            return WIFI_MGMT_CONN_UNSPECIFIED;
        }
        if (cfg->passphrase_len == WPAS_WEP40_HEX_LEN) {
            ret = util_hexstr2bin(cfg->passphrase, wep_pwd, WPAS_WEP40_ASCII_LEN);
            if (ret == -1) {
                netlink_printf("%s: WEP passphrase hex format error\r\n");
                return WIFI_MGMT_CONN_UNSPECIFIED;
            }
            wep_pwd[WPAS_WEP40_ASCII_LEN] = '\0';
            sys_memcpy(cfg->passphrase, wep_pwd, (WPAS_WEP40_ASCII_LEN + 1));
            cfg->passphrase_len = WPAS_WEP40_ASCII_LEN;
        }
        if (cfg->passphrase_len == WPAS_WEP104_HEX_LEN) {
            ret = util_hexstr2bin(cfg->passphrase, wep_pwd, WPAS_WEP104_ASCII_LEN);
            if (ret == -1) {
                netlink_printf("%s: WEP passphrase hex format error\r\n");
                return WIFI_MGMT_CONN_UNSPECIFIED;
            }
            wep_pwd[WPAS_WEP104_ASCII_LEN] = '\0';
            sys_memcpy(cfg->passphrase, wep_pwd, (WPAS_WEP104_ASCII_LEN + 1));
            cfg->passphrase_len = WPAS_WEP104_ASCII_LEN;
        }
    }

    /* Complete connect info */
    if (sta_cfg != cfg) {
#ifdef CONFIG_WPA3_PMK_CACHE_ENABLE
        /* Flush SAE PMK Cache if sta config changes */
        if (candidate.akm & (CO_BIT(MAC_AKM_SAE) | CO_BIT(MAC_AKM_FT_OVER_SAE)
            | CO_BIT(MAC_AKM_8021X) | CO_BIT(MAC_AKM_8021X_SHA256)
            | CO_BIT(MAC_AKM_8021X_SUITE_B) | CO_BIT(MAC_AKM_8021X_SUITE_B_192))) {
            if ((sta_cfg->ssid_len != cfg->ssid_len) || strcmp(sta_cfg->ssid, cfg->ssid) ||
                (sta_cfg->passphrase_len != cfg->passphrase_len) ||
                (sta_cfg->passphrase != NULL && strcmp(sta_cfg->passphrase, cfg->passphrase)) ||
                (sta_cfg->akm != cfg->akm) || (sta_cfg->p_cipher != cfg->p_cipher) || (sta_cfg->g_cipher != cfg->g_cipher)) {
                sta_cfg->flush_cache_req = 1;
            }
        }
#endif

        sta_cfg->ssid_len = cfg->ssid_len;
        if (cfg->ssid_len)
            sys_memcpy(sta_cfg->ssid, cfg->ssid, cfg->ssid_len);
        sta_cfg->passphrase_len = cfg->passphrase_len;
        if (cfg->passphrase_len)
            sys_memcpy(sta_cfg->passphrase, cfg->passphrase, cfg->passphrase_len);
        sta_cfg->conn_with_bssid = cfg->conn_with_bssid;
        sys_memcpy(sta_cfg->bssid, cfg->bssid, WIFI_ALEN);
    }

    if (sta_cfg->flush_cache_req) {
        wifi_wpa_sta_pmksa_cache_flush(vif_idx, 0);
        sta_cfg->flush_cache_req = 0;
    }

    sta_cfg->akm = candidate.akm;

#ifndef CONFIG_WPA3_SAE
    if ((sta_cfg->akm & CO_BIT(MAC_AKM_SAE)) || (sta_cfg->akm & CO_BIT(MAC_AKM_FT_OVER_SAE)) || (candidate.akm & CO_BIT(MAC_AKM_OWE))) {
        sta_cfg->akm &= ~(CO_BIT(MAC_AKM_SAE) | CO_BIT(MAC_AKM_FT_OVER_SAE) | CO_BIT(MAC_AKM_OWE));
    }
#endif

    sta_cfg->g_cipher = candidate.group_cipher;
    sta_cfg->p_cipher = candidate.pairwise_cipher;
    sta_cfg->channel = wifi_freq_to_channel(candidate.chan->freq);
    if (sta_cfg->conn_with_bssid) {
        sys_memcpy(sta_cfg->ssid, candidate.ssid.array, candidate.ssid.length);
        sta_cfg->ssid_len = candidate.ssid.length;
    } else {
        sys_memcpy(sta_cfg->bssid, (uint8_t *)candidate.bssid.array, WIFI_ALEN);
    }

#ifdef CONFIG_WPA3_PMK_CACHE_ENABLE
    /* Check if pmksa cached */
    if (((candidate.akm & CO_BIT(MAC_AKM_SAE))
            && pmksa_cache_get(&wvif->sta.cache, (uint8_t *)candidate.bssid.array, NULL, WPA_KEY_MGMT_SAE))
        || ((candidate.akm & CO_BIT(MAC_AKM_OWE))
            && pmksa_cache_get(&wvif->sta.cache, (uint8_t *)candidate.bssid.array, NULL, WPA_KEY_MGMT_OWE))
        ) {
        candidate.akm = CO_BIT(MAC_AKM_NONE);
    }
#endif

#ifdef CFG_8021x_EAP_TLS
    if (sta_cfg->eap_cfg.conn_with_enterprise) {
        wifi_wpa_eap_init(vif_idx);
    }
#endif

#ifdef CFG_80211R
    if (candidate.akm & CO_BIT(MAC_AKM_FT_PSK)) {
        wvif->sta.ft = sys_zalloc(sizeof(struct ft_params));
        if (wvif->sta.ft == NULL) {
            netlink_printf("wifi_netlink_connect_req: allocate ft_info failed.\r\n");
            return WIFI_MGMT_CONN_UNSPECIFIED;
        }
        /* mdie */
        sys_memcpy(sta_cfg->md.mobility_domain, candidate.md_ie, MAC_INFOELT_MDE_MDID_LEN);
        sta_cfg->md.ft_capab = (candidate.md_ie[MAC_MDE_ELMT_LEN - 1] & 0xFE);
    }
#endif /* CFG_80211R */

    sys_memset((void *)&cmd, 0, sizeof(cmd));
    cmd.hdr.len = sizeof(cmd);
    cmd.hdr.id = MACIF_CONNECT_CMD;
    cmd.vif_idx = vif_idx;
    cmd.chan.freq = candidate.chan->freq;
    cmd.chan.band = PHY_BAND_2G4;
    cmd.chan.flags = 0;
    cmd.chan.tx_power = 0;
    cmd.ssid.len = candidate.ssid.length;
    cmd.ssid.ssid = (const char *)candidate.ssid.array;
    cmd.bssid = (uint8_t *)candidate.bssid.array;
    cmd.uapsd = 0xFFFF;
    cmd.flags = CONTROL_PORT_HOST;

    mfp = (CO_BIT(MAC_CIPHER_BIP_CMAC_128) | CO_BIT(MAC_CIPHER_BIP_GMAC_128) |
           CO_BIT(MAC_CIPHER_BIP_GMAC_256) | CO_BIT(MAC_CIPHER_BIP_CMAC_256));

    if (candidate.group_cipher & mfp) {
        cmd.flags |= MFP_IN_USE;
    }

    if (cfg->akm & CO_BIT(MAC_AKM_NONE)) {
        cmd.ie_len = 0;
        cmd.ie = NULL;
    } else if (cfg->akm == CO_BIT(MAC_AKM_PRE_RSN)) { // FOR WEP
        cmd.ie_len = 0;
        cmd.ie = NULL;
        wpas_set_wep_key(vif_idx, 0, (uint8_t *)sta_cfg->passphrase, sta_cfg->passphrase_len);
    }
    else {
        res = wifi_wpa_gen_wpa_or_rsn_ie(vif_idx);
        if (res) {
            ret = WIFI_MGMT_CONN_UNSPECIFIED;
            netlink_printf("wifi netlink generate wpa/rsn ie failed.\r\n");
            goto fail;
        } else {
            cmd.ie = wvif->sta.w_eapol.assoc_wpa_ie;
            cmd.ie_len = wvif->sta.w_eapol.assoc_wpa_ie_len
#ifdef CONFIG_OWE
                + wvif->sta.w_eapol.assoc_owe_ie_len
#endif /* CONFIG_OWE */
                ;

            cmd.flags |= USE_PAIRWISE_KEY;
        }
    }

#ifdef CONFIG_WPA3_SAE
    if ((candidate.akm & CO_BIT(MAC_AKM_SAE)) || (candidate.akm & CO_BIT(MAC_AKM_FT_OVER_SAE))) {
        cmd.auth_alg = MAC_AUTH_ALGO_SAE;
    } else
#endif
    if (candidate.akm == CO_BIT(MAC_AKM_PRE_RSN)) {
        if (wvif->sta.status_code == WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG)
            cmd.auth_alg = MAC_AUTH_ALGO_OPEN;
        else
            cmd.auth_alg = MAC_AUTH_ALGO_SHARED;
    } else{
        cmd.auth_alg = MAC_AUTH_ALGO_OPEN;
    }

    cmd.ctrl_port_ethertype = htons(ETH_P_PAE);

    if (candidate.akm != CO_BIT(MAC_AKM_NONE)) {
        cmd.flags |= USE_PRIVACY;
    }

    if ((candidate.pairwise_cipher == CO_BIT(MAC_CIPHER_WEP40)) ||
        (candidate.pairwise_cipher == CO_BIT(MAC_CIPHER_TKIP)) ||
        (candidate.pairwise_cipher == CO_BIT(MAC_CIPHER_WEP104))) {
        cmd.flags |= DISABLE_HT;
    }

    if (macif_ctl_cmd_execute(&cmd.hdr, &resp.hdr)
        || resp.status != MACIF_STATUS_SUCCESS) {
        ret = WIFI_MGMT_CONN_ASSOC_FAIL;
        goto fail;
    }

fail:
    return ret;
}

/*!
    \brief      indicate that association is completed
    \param[in]  vif_idx: index of the wifi vif
    \param[in]  ind_param: pointer to the information of association
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_associate_done(int vif_idx, void *ind_param)
{
    struct macif_connect_ind *ind_info = (struct macif_connect_ind *)ind_param;
    struct wifi_vif_tag *wvif = &wifi_vif_tab[vif_idx];
    struct wifi_sta *config_sta = &wvif->sta;
    config_sta->ap_id = ind_info->ap_idx;
    config_sta->aid = ind_info->aid;
    #ifdef CFG_WIFI_RX_STATS
    macif_alloc_rx_rates(config_sta->ap_id);
    #endif
    macif_tx_sta_add(config_sta->ap_id, 0);
    net_if_up(&wvif->net_if);

#ifdef CONFIG_EAP_TLS
    if (config_sta->cfg.akm & (CO_BIT(MAC_AKM_8021X) | CO_BIT(MAC_AKM_8021X_SHA256)
                                | CO_BIT(MAC_AKM_8021X_SUITE_B)
                                | CO_BIT(MAC_AKM_8021X_SUITE_B_192))) {
        /* get wpa/rsn ie from assoc resp */
        if (wpas_set_wpa_rsn_ie(&wvif->sta.w_eapol, ((uint8_t *)ind_info->assoc_ie_buf + ind_info->assoc_req_ie_len),
                        ind_info->assoc_rsp_ie_len)) {
            wpa_printf("EAPOL: not get wpa/rsn ie from assoc resp\r\n");
            return -1;
        }
        wpas_eap_start(wvif->sta.esm);
    } else
#endif
    if ((config_sta->cfg.akm != CO_BIT(MAC_AKM_NONE))
        && (config_sta->cfg.akm != CO_BIT(MAC_AKM_PRE_RSN))) {
        // netlink_printf("Start WPA handshaking\r\n");
        wifi_wpa_sta_sm_step(vif_idx, WIFI_MGMT_EVENT_ASSOC_SUCCESS,
                        ((uint8_t *)ind_info->assoc_ie_buf + ind_info->assoc_req_ie_len),
                        ind_info->assoc_rsp_ie_len, WIFI_STA_SM_EAPOL);
    } else {
        // netlink_printf("Connect Successful, Aid %d\r\n", ind_info->aid);
        wpas_set_mac_ctrl_port(vif_idx, NULL, 1);
    }

    return 0;
}

/*!
    \brief      Disconnect with AP
    \param[in]  vif_idx: index of the wifi vif
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_disconnect_req(int vif_idx)
{
    struct wifi_vif_tag *wvif;
    struct macif_cmd_disconnect dc_cmd;
    struct wifi_ip_addr_cfg ip_cfg;
    struct macif_cmd_resp resp;

    if (vif_idx >= CFG_VIF_NUM) {
        return -1;
    }
    wvif =  &wifi_vif_tab[vif_idx];

    sys_memset(&ip_cfg, 0, sizeof(ip_cfg));
    ip_cfg.mode = IP_ADDR_NONE;
#ifdef CONFIG_IPV6_SUPPORT
    ip_cfg.ip6_mode = IP6_ADDR_NONE;
#endif
    wifi_set_vif_ip(vif_idx, &ip_cfg);
    net_if_down(&wvif->net_if);

    /* execute disconnect cmd */
    sys_memset((void *)&dc_cmd, 0, sizeof(dc_cmd));
    dc_cmd.hdr.len = sizeof(dc_cmd);
    dc_cmd.hdr.id = MACIF_DISCONNECT_CMD;
    dc_cmd.vif_idx = vif_idx;
    macif_ctl_cmd_execute(&dc_cmd.hdr, &resp.hdr);

    wifi_wpa_sta_sm_step(vif_idx, WIFI_MGMT_EVENT_DISCONNECT, NULL, 0, WIFI_STA_SM_SAE);
    wifi_wpa_sta_sm_step(vif_idx, WIFI_MGMT_EVENT_DISCONNECT, NULL, 0, WIFI_STA_SM_EAPOL);

#if 0
    if (wvif->sta.ap_id < CFG_STA_NUM) {
        #ifdef CFG_WIFI_RX_STATS
        macif_free_rx_rates(wvif->sta.ap_id);
        #endif
        macif_tx_sta_del(wvif->sta.ap_id);
        wvif->sta.ap_id = 0xFF;
    }
#endif

    return 0;
}

/*!
    \brief      wifi netlink forward message from wpa supplicant or macif control task
    \param[in]  vif_idx: index of the wifi vif
    \param[in]  msg: pointer to the wpa message or macif message
    \param[in]  from_wpa: 1 from wpa supplicant and 0 from macif control task
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_msg_forward(int vif_idx, void *msg, bool from_wpa)
{
    uint16_t mgmt_event;
    struct macif_msg_hdr *hdr = (struct macif_msg_hdr *)msg;
    int reason = 0;
    uint32_t len = 0;
    uint8_t *param = NULL;
    struct wifi_vif_tag *wvif = vif_idx_to_wvif(vif_idx);

    WIFI_CLOSED_CHECK(0);

    if (msg == NULL || wvif == NULL)
        return -1;

    if (from_wpa) {
        return -2;
    } else {
        // netlink_printf("===> %s: id = %d\r\n", __func__, hdr->id);
        switch (hdr->id) {
        case MACIF_SCAN_RESULT_EVENT:
        {
            struct macif_scan_result_event *evt = (struct macif_scan_result_event *)msg;
            mgmt_event = WIFI_MGMT_EVENT_SCAN_RESULT;
            reason = 0;
            param = evt->payload;
            len = evt->length;
            break;
        }
        case MACIF_SCAN_DONE_EVENT:
            reason = ((struct macif_scan_completed_event *)msg)->status;
            if (reason == MACIF_STATUS_SUCCESS)
                mgmt_event = WIFI_MGMT_EVENT_SCAN_DONE;
            else
                mgmt_event = WIFI_MGMT_EVENT_SCAN_FAIL;
            break;
        case MACIF_CONNECT_EVENT:
        {
            struct macif_connect_ind *ind = (struct macif_connect_ind *)msg;

            if (ind->status_code == MACIF_STATUS_SUCCESS) {
                mgmt_event = WIFI_MGMT_EVENT_ASSOC_SUCCESS;
                wvif->sta.status_code = WLAN_STATUS_SUCCESS;
            } else {
                if ((ind->status_code == WLAN_STATUS_INVALID_PMKID)
                    || (ind->status_code == WLAN_STATUS_INVALID_IE)
                    || (ind->status_code == WLAN_STATUS_ASSOC_DENIED_UNSPEC)) {
                    /* When assoc rsp status code is 53, we should flush PMK */
                    /* exception : AP MikroTik send status code 12 in assoc rsp,
                                   AP HUAWEI AX2 Pro send status code 40 */
                    wvif->sta.cfg.flush_cache_req = 1;
                    reason = WIFI_MGMT_CONN_ASSOC_FAIL;
                } else if ((ind->status_code  >= WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG) && (ind->status_code  <= WLAN_STATUS_AUTH_TIMEOUT)) {
                    reason = WIFI_MGMT_CONN_AUTH_FAIL;
                } else if (ind->status_code  == WLAN_STATUS_UNSPECIFIED_FAILURE) {
                    reason = WIFI_MGMT_CONN_UNSPECIFIED;
                }
                else {
                    netlink_printf("Connect fail status code %d!\r\n", ind->status_code);
                    reason = WIFI_MGMT_CONN_ASSOC_FAIL;
                }
                wvif->sta.status_code = ind->status_code;
                mgmt_event = WIFI_MGMT_EVENT_CONNECT_FAIL;
            }
            param = msg;
            len = sizeof(*ind) + ind->assoc_req_ie_len + ind->assoc_rsp_ie_len;
            break;
        }
        case MACIF_DISCONNECT_EVENT:
            mgmt_event = WIFI_MGMT_EVENT_DISCONNECT;
            reason = ((struct macif_disconnect_event *)msg)->reason_code;

            wvif->sta.reason_code = ((struct macif_disconnect_event *)msg)->reason_code;

            if (reason == WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA ||
                reason == WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA ||
                reason == WLAN_REASON_DISASSOC_STA_HAS_LEFT ||
                reason == WLAN_REASON_DEAUTH_LEAVING ||
                reason == WLAN_REASON_DISASSOC_DUE_TO_INACTIVITY) {
                reason = WIFI_MGMT_DISCON_RECV_DEAUTH;
            } else if (reason == WLAN_REASON_UNSPECIFIED) { // generated by macsw
                reason = WIFI_MGMT_DISCON_NO_BEACON;
            } else if (reason == WLAN_REASON_MICHAEL_MIC_FAILURE) {
                reason = WIFI_MGMT_DISCON_MIC_FAIL;
            } else if (reason == MAC_RS_RESERVED) { // disconnect from host
                reason = WIFI_MGMT_DISCON_FROM_UI;
            } else if (reason == WLAN_REASON_4WAY_HANDSHAKE_TIMEOUT) {
                reason = WIFI_MGMT_CONN_HANDSHAKE_FAIL;
            } else if (reason == WLAN_REASON_PREV_AUTH_NOT_VALID) {
                /* Previous authentication no longer valid, we should flush PMK */
                wvif->sta.cfg.flush_cache_req = 1;
                reason = WIFI_MGMT_DISCON_RECV_DEAUTH;
            } else {
                netlink_printf("Disconnect reason %d!\r\n", reason);
                reason = WIFI_MGMT_DISCON_UNSPECIFIED;
            }
            break;
        case MACIF_ROAMING_EVENT:
            mgmt_event = WIFI_MGMT_EVENT_ROAMING_START;
#ifdef CFG_80211R
            param = (uint8_t *)wvif->sta.cfg.ssid;
            len = wvif->sta.cfg.ssid_len;
#endif /* CFG_80211R */
            break;
        case MACIF_DHCP_START_EVENT:
            mgmt_event = WIFI_MGMT_EVENT_DHCP_START;
            break;
        case MACIF_EXTERNAL_AUTH_EVENT:
            mgmt_event = WIFI_MGMT_EVENT_EXTERNAL_AUTH_REQUIRED;
            break;
#ifdef CFG_80211R
        case MACIF_FT_AUTH_EVENT:
        {
            struct macif_ft_auth_ind *ind = (struct macif_ft_auth_ind *)msg;
            mgmt_event = WIFI_MGMT_EVENT_FT_AUTH;
            param = msg;
            len = sizeof(*ind) + ind->auth_ie_len;
            break;
        }
#endif /* CFG_80211R */
        default:
            netlink_printf("Unknown event(%d) from wlan lib!\r\n", hdr->id);
            return -3;
        }

        return eloop_message_send(vif_idx, mgmt_event, reason, param, len);
    }
    return 0;
}

#else  /* CONFIG_WPA_SUPPLICANT */
/*!
    \brief      Connect to an AP
    \param[in]  vif_idx: index of the wifi vif
    \param[in]  cfg: pointer to the information of AP
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_connect_req(int vif_idx, struct sta_cfg *cfg)
{
    struct wifi_vif_tag *wvif;
    struct sta_cfg *sta_cfg;
    struct mac_scan_result candidate;
    int res = -1;
    int network_change = 0;

    if ((vif_idx >= CFG_VIF_NUM) || (NULL == cfg)) {
        return WIFI_MGMT_CONN_UNSPECIFIED;
    }

    if (VIF_STA != macif_vif_type_get(vif_idx)) {
        return WIFI_MGMT_CONN_UNSPECIFIED;
    }

    wvif =  &wifi_vif_tab[vif_idx];
    sta_cfg = &wvif->sta.cfg;

    /* find candidate ap from scan results */
    sys_memset(&candidate, 0, sizeof(struct mac_scan_result));
    if (cfg->conn_with_bssid)
        res = wifi_netlink_candidate_ap_find(vif_idx, cfg->bssid, NULL, &candidate);
    else
        res = wifi_netlink_candidate_ap_find(vif_idx, NULL, cfg->ssid, &candidate);
    if (res ) {
        return WIFI_MGMT_CONN_NO_AP;
    }
    wifi_netlink_scan_result_print(0, &candidate);

    /* Check if crypto matched */
    if ((cfg->passphrase_len != 0 && candidate.akm == CO_BIT(MAC_AKM_NONE))
        || (cfg->passphrase_len == 0 && !(candidate.akm & (CO_BIT(MAC_AKM_NONE)
                                        | CO_BIT(MAC_AKM_OWE)
                                        | CO_BIT(MAC_AKM_8021X) | CO_BIT(MAC_AKM_8021X_SHA256)
                                        | CO_BIT(MAC_AKM_8021X_SUITE_B) | CO_BIT(MAC_AKM_8021X_SUITE_B_192))))) {
        return WIFI_MGMT_CONN_NO_AP;
    }

    /* Complete connect info */
    if (sta_cfg != cfg) {
        sta_cfg->ssid_len = cfg->ssid_len;
        if (cfg->ssid_len)
            sys_memcpy(sta_cfg->ssid, cfg->ssid, cfg->ssid_len);
        sta_cfg->passphrase_len = cfg->passphrase_len;
        if (cfg->passphrase_len)
            sys_memcpy(sta_cfg->passphrase, cfg->passphrase, cfg->passphrase_len);
        sta_cfg->conn_with_bssid = cfg->conn_with_bssid;
        sys_memcpy(sta_cfg->bssid, cfg->bssid, WIFI_ALEN);
    }
    sta_cfg->akm = candidate.akm;
    sta_cfg->g_cipher = candidate.group_cipher;
    sta_cfg->p_cipher = candidate.pairwise_cipher;
    sta_cfg->channel = wifi_freq_to_channel(candidate.chan->freq);
    if (sta_cfg->conn_with_bssid) {
        sys_memcpy(sta_cfg->ssid, candidate.ssid.array, candidate.ssid.length);
        sta_cfg->ssid_len = candidate.ssid.length;
    } else {
        sys_memcpy(sta_cfg->bssid, (uint8_t *)candidate.bssid.array, WIFI_ALEN);
    }

    //check. if there is a wpa interface existed
    if (WIFI_WPA_STATE_STOPPED != wifi_wpa_get_state(vif_idx) && WVIF_STA == wvif->wvif_type) {
        network_change = wifi_wpa_check_network(vif_idx, &wvif->sta);
        // network not changed, at least same ssid and password(can not judge bssid here)
        if (!network_change) {
            if (WIFI_WPA_STATE_NOT_CONNECTED == wifi_wpa_get_state(vif_idx)) {
                // network has been disabled, reenable it
                if (!wifi_wpa_enable_network(vif_idx)) {
                    goto success;
                } else {
                    netlink_printf("reenable network failed, continue\r\n");
                    res = WIFI_MGMT_CONN_UNSPECIFIED;
                    goto end;
                }
            } else {
                // network is connected or under connecting, disable it then reenable
                if ((!wifi_wpa_disable_network(vif_idx)) &&
                    (!wifi_wpa_enable_network(vif_idx))) {
                        goto success;
                } else {
                    netlink_printf("disable or reenable network failed, continue\r\n");
                    res = WIFI_MGMT_CONN_UNSPECIFIED;
                    goto end;
                }
            }
        }
    }

    wifi_wpa_remove_vif(vif_idx);

    if (wifi_wpa_sta_cfg(vif_idx, sta_cfg)) {
        res = WIFI_MGMT_CONN_ASSOC_FAIL;
        goto end;
    }

success:
    res = 0;
end:
    return res;
}

int wifi_netlink_associate_done(int vif_idx, void *ind_param)
{

    return 0;
}

/*!
    \brief      Disconnect with AP
    \param[in]  vif_idx: index of the wifi vif
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_disconnect_req(int vif_idx)
{
    struct wifi_ip_addr_cfg ip_cfg;

    wifi_wpa_link_monitor(vif_idx, 0);

    sys_memset(&ip_cfg, 0, sizeof(ip_cfg));
    ip_cfg.mode = IP_ADDR_NONE;
#ifdef CONFIG_IPV6_SUPPORT
    ip_cfg.ip6_mode = IP6_ADDR_NONE;
#endif
    if (wifi_set_vif_ip(vif_idx, &ip_cfg))
        return -1;

    if (wifi_wpa_disable_network(vif_idx))
        return -2;

    return 0;
}

#ifdef CFG_FTM_INIT
/*!
    \brief      Start ftm
    \param[in]  vif_idx: index of the wifi vif
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_ftm_start(int vif_idx)
{
    struct mac_ftm_results res = {0};
    int i;

    if (vif_idx > CFG_STA_NUM)
        return -1;

    if (macif_cntrl_start_ftm(vif_idx, &res))
        return -1;

    netlink_printf("FTM Response Number: %u\r\n", res.nb_ftm_rsp);
    for (i = 0; i < res.nb_ftm_rsp; i++) {
        netlink_printf("[%d]: "MAC_FMT" rtt %u\r\n",
                        i, MAC_ARG(res.meas[i].addr.array), res.meas[i].rtt);
    }
    netlink_printf("\r\n");

    return 0;
}
#endif /* CFG_FTM_INIT */

/*!
    \brief      wifi netlink forward message from wpa supplicant or macif control task
    \param[in]  vif_idx: wifi vif index
    \param[in]  msg: wpa message or macif message
    \param[in]  from_wpa: 1 from wpa supplicant and 0 from macif control task
    \retval     function run status
                  -1: error happen
                  0: run success
*/
int wifi_netlink_msg_forward(int vif_idx, void *msg, bool from_wpa)
{
    uint16_t mgmt_event = 0;
    int reason = 0;
    int event_param = 0;
    uint8_t *param = NULL;
    uint32_t len = 0;

    WIFI_CLOSED_CHECK(0);

    if (vif_idx >= CFG_VIF_NUM)
        return -1;

    if (from_wpa) {
        struct wifi_wpa_target_event *wpa_msg = (struct wifi_wpa_target_event *)msg;
        // netlink_printf("Netlink: rx event %d from wpa\r\n", wpa_msg->event);

        switch (wpa_msg->event) {
        case WIFI_WPA_CONNECTED:
            mgmt_event = WIFI_MGMT_EVENT_ASSOC_SUCCESS;
            reason = MACIF_STATUS_SUCCESS;
            break;
        case WIFI_WPA_PROCESS_ERROR:
            mgmt_event = WIFI_MGMT_EVENT_CONNECT_FAIL;
            event_param = (int)wpa_msg->event_param;

            if (event_param == WIFI_WPA_ERROR_OTHERS) // SAE password wrong
                reason = WIFI_MGMT_CONN_AUTH_FAIL;
            else if (event_param == WIFI_WPA_ERROR_WRONG_KEY) // password wrong
                reason = WIFI_MGMT_CONN_HANDSHAKE_FAIL;
            else if (event_param == WIFI_WPA_ERROR_DPP)
                reason = WIFI_MGMT_CONN_DPP_FAIL;
            else if (event_param == WIFI_WPA_ERROR_NO_AP)
                reason = WIFI_MGMT_CONN_NO_AP;
            else {
                reason = WIFI_MGMT_CONN_UNSPECIFIED;
                netlink_printf("Connect fail reason %d!\r\n", event_param);
            }
            break;
        case WIFI_WPA_DISCONNECTED:
            mgmt_event = WIFI_MGMT_EVENT_DISCONNECT;
            event_param = (int)wpa_msg->event_param;

            netlink_printf("Disconnect reason %d!\r\n", event_param);
            if (event_param == WLAN_REASON_CLASS2_FRAME_FROM_NONAUTH_STA ||
                event_param == WLAN_REASON_CLASS3_FRAME_FROM_NONASSOC_STA ||
                reason == WLAN_REASON_DISASSOC_STA_HAS_LEFT ||
                reason == WLAN_REASON_DEAUTH_LEAVING) {
                reason = WIFI_MGMT_DISCON_RECV_DEAUTH;
            } else if (event_param == WLAN_REASON_UNSPECIFIED) { // generated by macsw
                reason = WIFI_MGMT_DISCON_NO_BEACON;
            } else if (reason == WLAN_REASON_MICHAEL_MIC_FAILURE) {
                reason = WIFI_MGMT_DISCON_MIC_FAIL;
            } else {
                reason = WIFI_MGMT_DISCON_UNSPECIFIED;
            }
            break;
#ifdef CFG_WPS
        case WIFI_WPA_WPS_CRED:
            // netlink_printf("MSG FORWARD: WIFI_MGMT_EVENT_WPS_CRED\r\n");
            mgmt_event = WIFI_MGMT_EVENT_WPS_CRED;
            reason = 0;
            param = wpa_msg->event_param;
            len = wpa_msg->param_len;
            break;
        case WIFI_WPA_WPS_ERROR:
            // netlink_printf("MSG FORWARD: WIFI_WPA_WPS_ERROR\r\n");
            mgmt_event = WIFI_MGMT_EVENT_WPS_FAIL;
            reason = (int)wpa_msg->event_param;
            break;
        case WIFI_WPA_WPS_SUCCESS:
            // netlink_printf("MSG FORWARD: WIFI_WPA_WPS_SUCCESS\r\n");
            mgmt_event = WIFI_MGMT_EVENT_WPS_SUCCESS;
            reason = 0;
            break;
#endif
        default:
            return -1;
        }
    } else {
        struct macif_msg_hdr *msg_hdr = (struct macif_msg_hdr *)msg;
        // netlink_printf("Netlink: rx msg %d from macif\r\n", msg_hdr->id);

        switch (msg_hdr->id) {
        case MACIF_SCAN_DONE_EVENT:
        {
            struct macif_scan_completed_event *scan_res = (struct macif_scan_completed_event *)msg_hdr;

            if (scan_res->status != MACIF_STATUS_SUCCESS ||
                scan_res->vif_idx != WIFI_VIF_INDEX_DEFAULT) {
                mgmt_event = WIFI_MGMT_EVENT_SCAN_FAIL;
            } else {
                mgmt_event = WIFI_MGMT_EVENT_SCAN_DONE;
            }
            reason = scan_res->status;
            break;
        }
        case MACIF_DISCONNECT_EVENT:
            mgmt_event = WIFI_MGMT_EVENT_DISCONNECT;
            reason = ((struct macif_disconnect_event *)msg)->reason_code;
            break;
        case MACIF_ROAMING_EVENT:
            mgmt_event = WIFI_MGMT_EVENT_ROAMING_START;
#ifdef CFG_80211R
            param = (uint8_t *)wifi_vif_tab[vif_idx].sta.cfg.ssid;
            len = wifi_vif_tab[vif_idx].sta.cfg.ssid_len;
#endif
            break;
        case MACIF_DHCP_START_EVENT:
            wifi_wpa_link_monitor(vif_idx, 1);
            mgmt_event = WIFI_MGMT_EVENT_DHCP_START;
            break;
        case MACIF_EXTERNAL_AUTH_EVENT:
            mgmt_event = WIFI_MGMT_EVENT_EXTERNAL_AUTH_REQUIRED;
            break;
        default:
            netlink_printf("Unknown event(%d) from wlan lib!\r\n", msg_hdr->id);
            return 0;
        }
    }

    if (eloop_message_send(vif_idx, mgmt_event, reason, param, len)) {
        netlink_printf("%s: eloop_message_send failed\r\n", __func__);
        return -1;
    }

    return 0;
}
#endif  /* CONFIG_WPA_SUPPLICANT */

/*!
    \brief      Start wifi netlink
    \param[in]  none
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_start(void)
{
    struct mac_addr base_mac_addr;

#ifndef CONFIG_WPA_SUPPLICANT

    macif_ctl_base_addr_get(&base_mac_addr);

    if (wifi_vifs_init(&base_mac_addr))
        return -1;

    /* set rx management frame callback */
    macif_rx_set_mgmt_cb(wifi_wpa_rx_mgmt_cb, NULL);

#else /* CONFIG_WPA_SUPPLICANT */

    macif_cntrl_base_addr_get(&base_mac_addr);

    if (wifi_vifs_init(&base_mac_addr))
        return -1;

#endif /* CONFIG_WPA_SUPPLICANT */

#if defined(CONFIG_RF_TEST_SUPPORT) || defined(CONFIG_SIGNALING_TEST_SUPPORT)
    /* Default initialize VIF-0 as Monitor mode */
    wifi_vif_tab[WIFI_VIF_INDEX_DEFAULT].wvif_type = WVIF_MONITOR;
    if (macif_control_start(WIFI_VIF_INDEX_DEFAULT, VIF_MONITOR)) {
        netlink_printf("%s: macif control start failed!!!\r\n", __func__);
    }
    eloop_event_send(WIFI_VIF_INDEX_DEFAULT, WIFI_MGMT_EVENT_MONITOR_START_CMD);

#else /* CONFIG_RF_TEST_SUPPORT */

    /* Default initialize VIF-0 as STA mode */
    wifi_vif_tab[WIFI_VIF_INDEX_DEFAULT].wvif_type = WVIF_STA;
    if (macif_control_start(WIFI_VIF_INDEX_DEFAULT, VIF_STA)) {
        netlink_printf("%s: macif control start failed!!!\r\n", __func__);
    }
    wifi_vif_tab[WIFI_VIF_INDEX_DEFAULT].sta.psmode = WIFI_STA_PS_MODE_OFF;
    wifi_netlink_ps_mode_set(WIFI_VIF_INDEX_DEFAULT, WIFI_STA_PS_MODE_OFF);

#endif /* CONFIG_RF_TEST_SUPPORT */

#ifndef CONFIG_WPA_SUPPLICANT
    /* this filter is valid only when rx buf is close to full */
    macif_vif_wpa_rx_filter_set(WIFI_VIF_INDEX_DEFAULT, MAC_STA_MGMT_RX_FILTER);
#endif /* CONFIG_WPA_SUPPLICANT */

    return 0;
}

/*!
    \brief      Stop wifi netlink
    \param[in]  none
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
void wifi_netlink_stop(void)
{
    struct wifi_vif_tag *wvif;
    int vif_idx;

    macif_rx_set_mgmt_cb(NULL, NULL);

    /* deinitialize wifi virtue interfaces */
    for (vif_idx = 0; vif_idx < CFG_VIF_NUM; vif_idx++)
    {
        wvif = (struct wifi_vif_tag *)vif_idx_to_wvif(vif_idx);

        if ((wvif->wvif_type == WVIF_AP) || (wvif->wvif_type == WVIF_MONITOR)) {
            eloop_message_send(vif_idx, WIFI_MGMT_EVENT_SWITCH_MODE_CMD, WVIF_STA, NULL, 0);
        } else if (wvif->wvif_type == WVIF_STA) {
            eloop_event_send(vif_idx, WIFI_MGMT_EVENT_DISCONNECT_CMD);
        }
        wifi_wpa_sta_pmksa_cache_flush(vif_idx, 1);
    }

}

/*!
    \brief      Open wifi device
    \param[in]  none
    \param[out] none
    \retval     0 on success and != 0 if error occured.
*/
int wifi_netlink_wifi_open(void)
{
    int ret;

    if (wifi_work_status == WIFI_CLOSED) {
        /* WiFi PMU/RCC config */
        ret = wifi_power_on();
        if (ret)
            return ret;

        /* WiFi enable IRQ */
        wifi_irq_enable();

        /* Create WiFi related tasks and start */
        ret = wifi_sw_init();
        if (ret)
            return ret;

        /* Wait all tasks ready */
        wifi_wait_ready();

        netlink_printf("WiFi opened.\r\n");
        wifi_work_status = WIFI_RUNNING;
    } else {
        netlink_printf("WiFi is already running.\r\n");
    }
    return 0;
}

/*!
    \brief      Close wifi device
    \param[in]  none
    \param[out] none
    \retval     none
*/
void wifi_netlink_wifi_close(void)
{
    if (wifi_work_status == WIFI_RUNNING) {
        wifi_work_status = WIFI_CLOSING;

        /* AP/monitor stopped or STA disconnected */
        wifi_netlink_stop();

        /* Shut down wifi tasks and free related resources */
        wifi_sw_deinit();
        sys_ms_sleep(5);  /* Wait idle task to process list xTasksWaitingTermination */

        /* WiFi disable IRQ */
        wifi_irq_disable();

        /* WiFi RCC/PMU off */
        wifi_power_off();

        netlink_printf("WiFi closed.\r\n");
        wifi_work_status = WIFI_CLOSED;
    } else {
        netlink_printf("WiFi is already closed.\r\n");
    }
}

/*!
    \brief      Get wifi work status
    \param[in]  none
    \param[out] none
    \retval     wifi work status
*/
uint8_t wifi_netlink_status_get(void)
{
    return wifi_work_status;
}
