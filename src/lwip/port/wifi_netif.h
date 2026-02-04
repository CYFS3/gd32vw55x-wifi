/*!
    \file    wifi_netif.h
    \brief   Header file of declaration of WiFi network interface layer.

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

#ifndef WIFI_NETIF_H_
#define WIFI_NETIF_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "lwip/pbuf.h"
#include "macif_api.h"

// Maximum size of a interface name (including null character)
#define NET_AL_MAX_IFNAME 4

// Minimum headroom to include in all TX buffer
// #define NET_AL_TX_HEADROOM 348

// Prototype for a function to free a network buffer */
// typedef void (*net_buf_free_fn)(void *net_buf);

/*
 * FUNCTIONS
 ****************************************************************************************
 */
uint16_t net_ip_chksum(const void *dataptr, int len);
int net_if_add(void *net_if, const uint8_t *mac_addr, const uint32_t *ipaddr,
               const uint32_t *netmask, const uint32_t *gw, void *vif_priv);
int net_if_remove(void *net_if);
const uint8_t *net_if_get_mac_addr(void *net_if);
void *net_if_find_from_name(const char *name);
int net_if_get_name(void *net_if, char *buf, int len);
void net_if_up(void *net_if);
void net_if_down(void *net_if);
int net_if_input(net_buf_rx_t *buf, void *net_if, void *addr, uint16_t len,
                 net_buf_free_fn free_fn);
void *net_if_vif_info(void *net_if);

net_buf_tx_t *net_buf_tx_alloc(uint32_t length);
net_buf_tx_t *net_buf_tx_alloc_ref(uint32_t length);
void net_buf_tx_pbuf_free(net_buf_tx_t *buf);
void *net_buf_tx_info(net_buf_tx_t *buf, uint16_t *tot_len, int *seg_cnt,
                      uint32_t seg_addr[], uint16_t seg_len[]);
void net_buf_tx_free(net_buf_tx_t *buf);

int net_init(void);
void net_deinit(void);

int net_l2_send(void *net_if, const uint8_t *data, int data_len, uint16_t ethertype,
                const uint8_t *dst_addr, bool *ack);
int net_l2_socket_create(void *net_if, uint16_t ethertype);
int net_l2_socket_delete(int sock);

void net_if_set_default(void *net_if);
void net_if_send_gratuitous_arp(void *net_if);
void net_if_set_ip(void *net_if, uint32_t ip, uint32_t mask, uint32_t gw);
int net_if_get_ip(void *net_if, uint32_t *ip, uint32_t *mask, uint32_t *gw);

int net_dhcp_start(void *net_if);
void net_dhcp_stop(void *net_if);
int net_dhcp_release(void *net_if);
bool net_dhcp_address_obtained(void *net_if);

int net_dhcpd_start(void *net_if);
void net_dhcpd_stop(void *net_if);

#if LWIP_IPV6
void net_ip6_server_start(void *net_if);
void net_ip6_server_stop(void *net_if);
#endif

int net_set_dns(uint32_t dns_server);
int net_get_dns(uint32_t *dns_server);

void net_buf_tx_cat(net_buf_tx_t *buf1, net_buf_tx_t *buf2);
err_t net_eth_receive(struct pbuf *pbuf, void *net_if);
int net_compat_check(size_t netif_size);

int net_lpbk_socket_create(int protocol);
int net_lpbk_socket_bind(int sock_recv, uint32_t port);
int net_lpbk_socket_connect(int sock_send, uint32_t port);
void net_if_use_static_ip(bool static_ip);
bool net_if_is_static_ip(void);

#ifdef __cplusplus
 }
#endif

#endif // WIFI_NETIF_H_
