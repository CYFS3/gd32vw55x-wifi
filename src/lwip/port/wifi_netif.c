/*!
    \file    wifi_netif.c
    \brief   Implementation of WiFi network interface layer.

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

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "wifi_netif.h"
#include "lwip/tcpip.h"
#include "lwip/etharp.h"
#include "lwip/netifapi.h"
#include "lwip/sockets.h"
#include "lwip/netbuf.h"
#include "lwip/api.h"
#include "lwip/dns.h"
#include "lwip/inet_chksum.h"
#include "lwip/netif.h"
#include "netif/ethernet.h"
#include "co_math.h"

#include "app_cfg.h"
#include "wrapper_os.h"
#include "dhcpd.h"
#include "macif_api.h"
#include "dbg_print.h"
#include "wifi_init.h"

#if LWIP_IPV6
#include "lwip/ethip6.h"
#include "lwip/mld6.h"
#if LWIP_IPV6_DHCP6
#include "lwip/dhcp6.h"
#endif
#endif

#define WIFI_NB_L2_FILTER 2

// Ethernet MTU
#define ETHERNET_MTU 1500

struct l2_filter_tag
{
    struct netif *net_if;
    int sock;
    struct netconn *conn;
    uint16_t ethertype;
};

static struct l2_filter_tag l2_filter[WIFI_NB_L2_FILTER];
static os_sema_t l2_semaphore;
static os_mutex_t l2_mutex;
static uint8_t ap_dhcpd_started = 0;
static bool netif_static_ip = 0;
#if LWIP_IPV6
static uint8_t ap_ip6_server_started = 0;
#endif

#if PBUF_LINK_ENCAPSULATION_HLEN < NET_AL_TX_HEADROOM
#error "PBUF_LINK_ENCAPSULATION_HLEN must be at least NET_AL_TX_HEADROOM"
#endif

/*
 * FUNCTIONS
 ****************************************************************************************
 */
// Fake function used to detected too small link encapsulation header length
void p_buf_link_encapsulation_hlen_too_small(void);

#if defined(NET_UDP_PBUF_REALLOC) && (NET_UDP_PBUF_REALLOC == 1)
static int net_buf_need_realloc(struct pbuf *pbuf)
{
    uint16_t eth_type;
    uint8_t proto;
    uint16_t port;

    if (pbuf->flags & PBUF_FLAG_IS_CUSTOM)
        return 0;

    eth_type = ((struct eth_hdr *)pbuf->payload)->type;
    // printf("eth_type = %x\r\n", eth_type);
#if LWIP_IPV6
    if (eth_type != PP_HTONS(ETHTYPE_IP) && eth_type != PP_HTONS(ETHTYPE_IPV6))
#else
    if (eth_type != PP_HTONS(ETHTYPE_IP))
#endif
        return 0;

#if LWIP_IPV6
    if (eth_type == PP_HTONS(ETHTYPE_IPV6))
        proto = IP6H_NEXTH((struct ip6_hdr *)((uint8_t *)pbuf->payload + SIZEOF_ETH_HDR));
    else
#endif
        proto = IPH_PROTO((struct ip_hdr *)((uint8_t *)pbuf->payload + SIZEOF_ETH_HDR));
    // printf("proto = %x\r\n", proto);
    if (proto != IP_PROTO_UDP)
        return 0;

#if LWIP_IPV6
    if (eth_type == PP_HTONS(ETHTYPE_IPV6))
        port = ((struct udp_hdr *)((uint8_t *)pbuf->payload + SIZEOF_ETH_HDR + IP6_HLEN))->src;
    else
#endif
        port = ((struct udp_hdr *)((uint8_t *)pbuf->payload + SIZEOF_ETH_HDR + IP_HLEN))->src;
    // printf("port = %x\r\n", lwip_ntohs(port));
    if (lwip_ntohs(port) == 0x43 || lwip_ntohs(port) == 0x44)  /* DHCP */
        return 0;

    return 1;
}
#endif /* NET_UDP_REALLOC */

/*!
    \brief      Callback used by the networking stack to push a buffer for transmission by the
                WiFi interface.
    \param[in]  net_if: Pointer to the network interface on which the TX is done
    \param[in]  p_buf: Pointer to the buffer to transmit
    \param[out] none
    \retval     ERR_OK upon successful pushing of the buffer, ERR_BUF otherwise
*/
static err_t net_if_output(struct netif *net_if, struct pbuf *p_buf)
{
    err_t status = ERR_BUF;

#if defined(NET_UDP_PBUF_REALLOC) && (NET_UDP_PBUF_REALLOC == 1)
    struct pbuf *pbuf_head_new, *pbuf_new, *p;

    if (net_buf_need_realloc(p_buf))
    {
        if (!netif_is_up(net_if))
            return (status);

        // Allocation of the new pbufs
        pbuf_head_new = pbuf_alloc(PBUF_RAW_TX, p_buf->len, PBUF_RAM);
        if (pbuf_head_new != NULL)
        {
            sys_memcpy(pbuf_head_new->payload, (uint8_t *)(p_buf->payload), p_buf->len);
            p = p_buf;
            while (p->next != NULL)
            {
                p = p->next;
                pbuf_new = pbuf_alloc(PBUF_RAW_TX, p->len, PBUF_RAM);
                if (pbuf_new == NULL)
                {
                    pbuf_free(pbuf_head_new);
                    return (status);
                }
                sys_memcpy(pbuf_new->payload, (uint8_t *)(p->payload), p->len);
                pbuf_cat(pbuf_head_new, pbuf_new);
            }
        }
        else
        {
            return (status);
        }

        if (macif_tx_start(net_if, pbuf_head_new, NULL, NULL) != 0)
        {
            pbuf_free(pbuf_head_new);
            return (status);
        }
        status = ERR_OK;
    }
    else
#endif /* NET_UDP_REALLOC */
    {
        // Increase the ref count so that the buffer is not freed by the networking stack
        // until it is actually sent over the WiFi interface
        pbuf_ref(p_buf);

        // Push the buffer and verify the status
        if (netif_is_up(net_if) && macif_tx_start(net_if, p_buf, NULL, NULL) == 0)
        {
            status = ERR_OK;
        }
        else
        {
            // Failed to push message to TX task, call pbuf_free only to decrease ref count
            pbuf_free(p_buf);
        }

    }

    return (status);
}

/*!
    \brief      Callback used by the networking stack to setup the network interface.
                This function should be passed as a parameter to netifapi_netif_add().
    \param[in]  net_if: Pointer to the network interface to setup
    \param[in]  p_buf: Pointer to the buffer to transmit
    \param[out] none
    \retval     ERR_OK upon successful setup of the interface, other status otherwise
*/
static err_t net_if_init(struct netif *net_if)
{
    err_t status = ERR_OK;

    #if LWIP_NETIF_HOSTNAME
    {
        /* Initialize interface hostname */
        net_if->hostname = "wlan";
    }
    #endif /* LWIP_NETIF_HOSTNAME */

    net_if->name[ 0 ] = 'w';
    net_if->name[ 1 ] = 'l';

    net_if->output = etharp_output;
    net_if->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

    #if LWIP_IGMP
    net_if->flags |= NETIF_FLAG_IGMP;
    #endif
    net_if->hwaddr_len = ETHARP_HWADDR_LEN;
    // hwaddr is updated in net_if_add
    net_if->mtu = ETHERNET_MTU;
    net_if->linkoutput = net_if_output;
#if LWIP_IPV6
    #if LWIP_IPV6_MLD
    net_if->flags |= NETIF_FLAG_MLD6;
    #endif
    net_if->output_ip6 = ethip6_output;
#endif
    return status;
}

/*!
    \brief      Call the checksum computation function of the TCP/IP stack
    \param[in]  dataptr: Pointer to the data buffer on which the checksum is computed
    \param[in]  len: Length of the data buffer
    \param[out] none
    \retval     The computed checksum
*/
uint16_t net_ip_chksum(const void *dataptr, int len)
{
    // Simply call the LwIP function
    return lwip_standard_chksum(dataptr, len);
}

/*!
    \brief      Add a network interface.
                This function must initialize the provided struct netif structure.
                The private VIF structure must be the one returned by @ref net_if_vif_info
    \param[in]  net_if: Pointer to the net_if structure to add
    \param[in]  mac_addr: MAC address of the interface
    \param[in]  ipaddr: IPv4 address of the interface (NULL if not available)
    \param[in]  netmask: Net mask of the interface (NULL if not available)
    \param[in]  gw: Gateway address of the interface (NULL if not available)
    \param[in]  vif_priv: Pointer to the VIF private structure
    \param[out] none
    \retval     0 on success and != 0 if error occurred
*/
int net_if_add(void *net_if,
               const uint8_t *mac_addr,
               const uint32_t *ipaddr,
               const uint32_t *netmask,
               const uint32_t *gw,
               void *vif)
{
    err_t status;
    struct netif *netif = (struct netif *)net_if;

    status = netifapi_netif_add(netif,
                               (const ip4_addr_t *)ipaddr,
                               (const ip4_addr_t *)netmask,
                               (const ip4_addr_t *)gw,
                               vif,
                               net_if_init,
                               tcpip_input);

    // Init MAC addr here as we can't do it in net_if_init (without dereferencing vif)
    sys_memcpy(netif->hwaddr, mac_addr, ETHARP_HWADDR_LEN);
#if LWIP_IPV6
    netif_create_ip6_linklocal_address(netif, 1);
    #if LWIP_IPV6_DHCP6
    dhcp6_enable_stateless(netif);
    #endif
#endif
    return (status == ERR_OK ? 0 : -1);
}

/*!
    \brief      Remove a network interface
    \param[in]  net_if: Pointer to the net_if structure to remove
    \param[out] none
    \retval     0 on success and != 0 if error occurred
*/
int net_if_remove(void *net_if)
{
    err_t status;
    struct netif *netif = (struct netif *)net_if;

    dhcp_cleanup(netif);

    status = netifapi_netif_remove(netif);

    return (status == ERR_OK ? 0 : -1);
}

/*!
    \brief      Get network interface MAC address
    \param[in]  net_if: Pointer to the net_if structure
    \param[out] none
    \retval     Pointer to MAC address
*/
const uint8_t *net_if_get_mac_addr(void *net_if)
{
    struct netif *netif = (struct netif *)net_if;

    return netif->hwaddr;
}

/*!
    \brief      Get pointer to network interface from its name
    \param[in]  name: Name of the interface
    \param[out] none
    \retval     pointer to the net_if structure and NULL if such interface doesn't exist.
*/
void *net_if_find_from_name(const char *name)
{
    return netif_find(name);
}

/*!
    \brief      Get name of network interface
                Copy the name on the interface (including a terminating a null byte) in the given
                buffer. If buffer is not big enough then the interface name is truncated and no
                null byte is written in the buffer.
    \param[in]  net_if: Pointer to the net_if structure
    \param[in]  buf: Buffer to write the interface name
    \param[in]  len: Length of the buffer.
    \param[out] none
    \retval     < 0 if error occurred, otherwise the number of characters (excluding the
                terminating null byte) needed to write the interface name. If return value is greater
                or equal to @p len, it means that the interface name has been truncated
*/
int net_if_get_name(void *net_if, char *buf, int len)
{
    struct netif *netif = (struct netif *)net_if;

    if (len > 0)
        buf[0] = netif->name[0];
    if (len > 1)
        buf[1] = netif->name[1];
    if (len > 2)
        buf[2] = netif->num + '0';
    if ( len > 3)
        buf[3] = '\0';

    return 3;
}

/*!
    \brief      Indicate that the network interface is now up (i.e. able to do traffic)
    \param[in]  net_if: Pointer to the net_if structure
    \param[out] none
    \retval     none
*/
void net_if_up(void *net_if)
{
    struct netif *netif = (struct netif *)net_if;

    netifapi_netif_set_up(netif);
}

/*!
    \brief      Indicate that the network interface is now down
    \param[in]  net_if: Pointer to the net_if structure
    \param[out] none
    \retval     none
*/
void net_if_down(void *net_if)
{
    struct netif *netif = (struct netif *)net_if;

    netifapi_netif_set_down(netif);
}

/*!
    \brief      Set a network interface as the default output interface
                If IP routing failed to select an output interface solely based on
                interface and destination addresses then this interface will be selected.
                Use a NULL parameter to reset the default interface.
    \param[in]  net_if: Pointer to the net_if structure
    \param[out] none
    \retval     none
*/
void net_if_set_default(void *net_if)
{
    struct netif *netif = (struct netif *)net_if;

    netifapi_netif_set_default(netif);
}

/*!
    \brief      send "gratuitous ARP" on a given interface.
    \param[in]  net_if: Pointer to the net_if structure
    \param[out] none
    \retval     none
*/
void net_if_send_gratuitous_arp(void *net_if)
{
    struct netif *netif = (struct netif *)net_if;

    etharp_gratuitous(netif);
}

/*!
    \brief      Set IPv4 address of an interface
                It is assumed that only one address can be configured on the interface
                and then setting a new address can be used to replace/delete the current
                address.
    \param[in]  net_if: Pointer to the net_if structure
    \param[in]  ip: IPv4 address
    \param[in]  mask: IPv4 network mask
    \param[in]  gw: IPv4 gateway address
    \param[out] none
    \retval     none
*/
void net_if_set_ip(void *net_if, uint32_t ip, uint32_t mask, uint32_t gw)
{
    struct netif *netif = (struct netif *)net_if;

    if (!netif)
        return;
    netif_set_addr(netif, (const ip4_addr_t *)&ip, (const ip4_addr_t *)&mask,
                   (const ip4_addr_t *)&gw);
}

/*!
    \brief      Get IPv4 address of an interface
                Set to NULL parameter you're not interested in.
    \param[in]  net_if: Pointer to the net_if structure
    \param[in]  ip: IPv4 address
    \param[in]  mask: IPv4 network mask
    \param[in]  gw: IPv4 gateway address
    \param[out] none
    \retval     0 if requested parameters have been updated successfully and !=0 otherwise.
*/
int net_if_get_ip(void *net_if, uint32_t *ip, uint32_t *mask, uint32_t *gw)
{
    struct netif *netif = (struct netif *)net_if;

    if (!netif)
        return -1;

    if (ip)
        *ip = netif_ip4_addr(netif)->addr;
    if (mask)
        *mask = netif_ip4_netmask(netif)->addr;
    if (gw)
        *gw = netif_ip4_gw(netif)->addr;

    return 0;
}

/*!
    \brief      Call the networking stack input function.
                This function is supposed to link the payload data and length to the RX
                buffer structure passed as parameter. The free_fn function shall be called
                when the networking stack is not using the buffer anymore.
    \param[in]  buf: Pointer to the RX buffer structure
    \param[in]  net_if: Pointer to the net_if structure that receives the packet
    \param[in]  addr: Pointer to the payload data
    \param[in]  len: Length of the data available at payload address
    \param[in]  free_fn: Pointer to buffer freeing function to be called after use
    \param[out] none
    \retval     0 on success and != 0 if packet is not accepted
*/
int net_if_input(net_buf_rx_t *buf, void *net_if, void *addr, uint16_t len, net_buf_free_fn free_fn)
{
    struct pbuf* p;
    struct netif *netif = (struct netif *)net_if;

    buf->custom_free_function = (pbuf_free_custom_fn)free_fn;
    p = pbuf_alloced_custom(PBUF_RAW, len, PBUF_REF, buf, addr, len);
    if (p == NULL)
    {
        dbg_print(ERR, "pbuf_alloced_custom NULL\r\n");
        free_fn(buf);
        return -1;
    }

    if (netif->input(p, netif))
    {
        free_fn(buf);
        return -1;
    }

    return 0;
}

/*!
    \brief      Get the pointer to the VIF private structure attached to a net interface.
    \param[in]  net_if: Pointer to the net_if structure
    \param[out] none
    \retval     The pointer to the VIF private structure attached to the net interface
                when it has been initialized (@ref net_if_add)
*/
void *net_if_vif_info(void *net_if)
{
    struct netif *netif = (struct netif *)net_if;

    return netif->state;
}

/*!
    \brief      Allocate a buffer for TX.
                This function is used to transmit buffer that do not originate from the
                Network Stack. (e.g. a management frame sent by wpa)
                This function allocates a buffer for transmission. The buffer must still
                reserve @ref NET_AL_TX_HEADROOM headroom space like for regular TX buffers.
    \param[in]  length: Size, in bytes, of the payload
    \param[out] none
    \retval     The pointer to the allocated TX buffer and NULL if allocation failed
*/
net_buf_tx_t *net_buf_tx_alloc(uint32_t length)
{
    struct pbuf *pbuf;

    pbuf = pbuf_alloc(PBUF_RAW_TX, length, PBUF_RAM);
    if (pbuf == NULL)
        return NULL;

    return pbuf;
}

/*!
    \brief      Allocate a buffer for TX with a reference to a payload.
                This function allocates a buffer for transmission. It only allocates
                memory for the TX buffer structure. The pointer to the payload
                is set to NULL and should be updated.
    \param[in]  length: Size, in bytes, of the payload
    \param[out] none
    \retval     The pointer to the allocated TX buffer and NULL if allocation failed
*/
net_buf_tx_t *net_buf_tx_alloc_ref(uint32_t length)
{
    struct pbuf *pbuf;

    pbuf = pbuf_alloc(PBUF_RAW_TX, length, PBUF_REF);
    if (pbuf == NULL)
        return NULL;

    return pbuf;
}

/*!
    \brief      Free a TX buffer.
    \param[in]  buf: Pointer to the TX buffer structure
    \param[out] none
    \retval     none
*/
void net_buf_tx_pbuf_free(net_buf_tx_t *buf)
{
    if (buf) {
        pbuf_free((struct pbuf *)buf);
    }
}

/*!
    \brief      Provides information on a TX buffer.
                This function is used by the WIFI module before queuing the buffer for
                transmission. This function must returns information (pointer and length)
                on all data segments that compose the buffer. Each buffer must at least
                have one data segment. It must also return a pointer to the headroom
                (of size @ref NET_AL_TX_HEADROOM) which must be reserved for each TX
                buffer on their first data segment.
    \param[in]      buf: Pointer to the TX buffer structure
    \param[in]      tot_len: Total size in bytes on the buffer (includes size of all data segment)
    \param[in,out]  seg_cnt: Contains the maximum number of data segment supported (i.e.
                             the size of @p seg_addr and @p seg_len parameter) and must be
                             updated with the actual number of segment in this buffer.
    \param[out]     seg_addr: Table to retrieve the address of each segment.
    \param[out]     seg_len: Table to retrieve the length, in bytes, of each segment.
    \retval     The pointer to the headroom reserved at the beginning of the first data segment
*/
void *net_buf_tx_info(net_buf_tx_t *buf, uint16_t *tot_len, int *seg_cnt,
                      uint32_t seg_addr[], uint16_t seg_len[])
{
    int idx, seg_cnt_max = *seg_cnt;
    uint16_t length = buf->tot_len;
    void *headroom;

    *tot_len = length;

    // Sanity check - the payload shall be in shared RAM
    // GD32VW55X_TODO need check why?
    // ASSERT_ERR(!TST_SHRAM_PTR(buf->payload));
    seg_addr[0] = (uint32_t)buf->payload;
    seg_len[0] = buf->len;
    length -= buf->len;

    // Get pointer to reserved headroom
    if (pbuf_header(buf, PBUF_LINK_ENCAPSULATION_HLEN))
    {
        // Sanity check - we shall have enough space in the buffer
        dbg_print(ERR, "pbuf_header() failed\r\n");
        return NULL;
    }
    headroom = (void *)CO_ALIGN4_HI((uint32_t)buf->payload);

    // Get info of extra segments if any
    buf = buf->next;
    idx = 1;
    while (length && buf && (idx < seg_cnt_max))
    {
        // Sanity check - the payload shall be in shared RAM
        // GD32VW55X_TODO why?
        // ASSERT_ERR(!TST_SHRAM_PTR(buf->payload));

        seg_addr[idx] = (uint32_t)buf->payload;
        seg_len[idx] = buf->len;
        length -= buf->len;
        idx++;
        buf = buf->next;
    }

    *seg_cnt = idx;
    if (length != 0)
    {
        // The complete buffer must be included in all the segments
        dbg_print(ERR, "remaining length != 0\r\n");
        return NULL;
    }

    return headroom;
}

/*!
    \brief      Free a TX buffer that was involved in a transmission.
    \param[in]  buf: Pointer to the TX buffer structure
    \param[out] none
    \retval     none
*/
void net_buf_tx_free(net_buf_tx_t *buf)
{
    // Remove the link encapsulation header
    pbuf_header(buf, -PBUF_LINK_ENCAPSULATION_HLEN);

    // Free the buffer
    pbuf_free(buf);
}

/*!
    \brief      Concatenate 2 Tx buffers
    \param[in]  buf1: Pointer to the TX buffer structure 1
    \param[in]  buf2: Pointer to the TX buffer structure 2
    \param[out] none
    \retval     none
*/
void net_buf_tx_cat(net_buf_tx_t *net_buf_tx_1, net_buf_tx_t *net_buf_tx_2)
{
    pbuf_cat(net_buf_tx_1, net_buf_tx_2);
}

#if 0
static void net_buf_rx_free(net_buf_rx_t *buf)
{
    // Free the buffer
    pbuf_free(&buf->pbuf);
}
#endif

#if 0
/**
 ****************************************************************************************
 * @brief Callback when lwip init is done
 *
 * @param[in] arg Not used
 ****************************************************************************************
 */
static void net_init_done(void *arg)
{
    wifi_task_ready(IP_TASK);
}
#endif

/*!
    \brief      Initialize some resources for L2
    \param[in]  none
    \param[out] none
    \retval     0 on success and != 0 if error occured
*/
int net_init(void)
{
    int i;

    for (i = 0; i < WIFI_NB_L2_FILTER; i++)
    {
        l2_filter[i].net_if = NULL;
    }

    if (sys_sema_init_ext(&l2_semaphore, 1, 0))
    {
        dbg_print(ERR, "sys sema init failed\r\n");
        return -1;
    }

    sys_mutex_init(&l2_mutex);
    if (l2_mutex == NULL)
    {
        dbg_print(ERR, "sys sema init failed\r\n");
        return -1;
    }

    // Initialize the TCP/IP stack
    //tcpip_init(net_init_done, NULL);

    return 0;
}

/*!
    \brief      Release the resources for L2
    \param[in]  none
    \param[out] none
    \retval     none
*/
void net_deinit(void)
{
    if (l2_semaphore) {
        sys_sem_free(&l2_semaphore);
        l2_semaphore = NULL;
    }
    if (l2_mutex) {
        sys_mutex_free(&l2_mutex);
        l2_mutex = NULL;
    }
}

static void net_l2_send_cfm(uint32_t frame_id, bool acknowledged, void *arg)
{
    if (arg)
        *((bool *)arg) = acknowledged;
    sys_sema_up(&l2_semaphore);
}

/*!
    \brief      Send a L2 (aka ethernet) packet
                Send data on the link layer (L2). If destination address is not NULL, Ethernet header
                will be added (using ethertype parameter) and MAC address of the sending interface is
                used as source address. If destination address is NULL, it means that ethernet header
                is already present and frame should be send as is.
                The data buffer will be copied by this function, and must then be freed by the caller.

                The primary purpose of this function is to allow the supplicant sending EAPOL frames.
                As these frames are often followed by addition/deletion of crypto keys, that
                can cause encryption to be enabled/disabled in the MAC, it is required to ensure that
                the packet transmission is completed before proceeding to the key setting.
                This function shall therefore be blocking until the frame has been transmitted by the
                MAC.
    \param[in]  net_if: Pointer to the net_if structure.
    \param[in]  data: Data buffer to send.
    \param[in]  data_len: Buffer size, in bytes.
    \param[in]  ethertype: Ethernet type to set in the ethernet header. (in host endianess)
    \param[in]  dst_addr: Ethernet address of the destination. If NULL then it means that ethernet header
                          is already present in the frame (and in this case ethertype should be ignored)
    \param[out] ack: Optional to get transmission status. If not NULL, the value pointed is set to true
                     if peer acknowledged the transmission and false in all other cases.
    \retval     0 on success and != 0 if packet hasn't been sentegment
*/
int net_l2_send(void *net_if, const uint8_t *data, int data_len, uint16_t ethertype,
                const uint8_t *dst_addr, bool *ack)
{
    struct pbuf *pbuf;
    int res;
    struct netif *netif = (struct netif *)net_if;

    if (netif == NULL || data == NULL || data_len >= netif->mtu || !netif_is_up(netif))
        return -1;

    pbuf = pbuf_alloc(PBUF_LINK, data_len, PBUF_RAM);
    if (pbuf == NULL)
        return -2;

    sys_memcpy(pbuf->payload, data, data_len);

    if (dst_addr)
    {
        // Need to add ethernet header as macif_tx_start is called directly
        struct eth_hdr* ethhdr;
        if (pbuf_header(pbuf, SIZEOF_ETH_HDR))
        {
            pbuf_free(pbuf);
            return -3;
        }
        ethhdr = (struct eth_hdr*)pbuf->payload;
        ethhdr->type = htons(ethertype);
        sys_memcpy(&ethhdr->dest, dst_addr, sizeof(struct eth_addr));
        sys_memcpy(&ethhdr->src, netif->hwaddr, sizeof(struct eth_addr));
    }

    // Ensure no other thread will program a L2 transmission while this one is waiting
    // for its confirmation
    sys_mutex_get(&l2_mutex);

    // In order to implement this function as blocking until the completion of the frame
    // transmission, directly call macif_tx_start with a confirmation callback.
    res = macif_tx_start(netif, pbuf, net_l2_send_cfm, ack);

    // Wait for the transmission completion
    sys_sema_down(&l2_semaphore, 0);

    // Now new L2 transmissions are possible
    sys_mutex_put(&l2_mutex);

    return res;
}

#ifdef CONFIG_WPA_SUPPLICANT
/*!
    \brief      Create a L2 (aka ethernet) socket for specific packet
                Create a L2 socket that will receive specified frames: a given ethertype
                on a given interface.
                It is expected to fail if a L2 socket for the same ethertype/interface
                couple already exists.

                As L2 sockets are not specified in POSIX standard, the implementation
                of such function may be impossible in some network stack.
    \param[in]  net_if: Pointer to the net_if structure.
    \param[in]  ethertype: Ethernet type to filter. (in host endianess)
    \param[out] none
    \retval     <0 if error occurred and the socket descriptor otherwise.
*/
int net_l2_socket_create(void *net_if, uint16_t ethertype)
{
    struct l2_filter_tag *filter = NULL;
    socklen_t len = sizeof(filter->conn);
    int i;
    struct netif *netif = (struct netif *)net_if;

    /* First find free filter and check that socket for this ethertype/net_if couple
       doesn't already exists */
    for (i = 0; i < WIFI_NB_L2_FILTER; i++)
    {
        if ((l2_filter[i].net_if == netif) &&
            (l2_filter[i].ethertype == ethertype))
        {
            return -1;
        }
        else if ((filter == NULL) && (l2_filter[i].net_if == NULL))
        {
            filter = &l2_filter[i];
        }
    }

    if (!filter)
        return -1;

    /* Note: we create DGRAM socket here but in practice we don't care, net_eth_receive
       will use the socket as a L2 raw socket */
    filter->sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (filter->sock < 0)
        return -1;

    if (getsockopt(filter->sock, SOL_SOCKET, SO_CONNINFO, &(filter->conn), &len))
    {
        close(filter->sock);
        return -1;
    }

    filter->net_if = netif;
    filter->ethertype = ethertype;

    return filter->sock;
}

/*!
    \brief      Delete a L2 (aka ethernet) socket
    \param[in]  sock: Socket descriptor returned by @ref net_l2_socket_create
    \param[in]  none
    \param[out] none
    \retval     0 on success and != 0 if error occurred.
*/
int net_l2_socket_delete(int sock)
{
    int i;
    for (i = 0; i < WIFI_NB_L2_FILTER; i++)
    {
        if ((l2_filter[i].net_if != NULL) &&
            (l2_filter[i].sock == sock))
        {
            l2_filter[i].net_if = NULL;
            close(l2_filter[i].sock);
            l2_filter[i].sock = -1;
            return 0;
        }
    }

    return -1;
}

/*!
    \brief      LWIP hook to process ethernet packet not supported.
                Check if a socket has been created for this (netif, ethertype) couple.
                If so push the buffer in the socket buffer list. (no buffer copy is done)
    \param[in]  pbuf: Buffer containing the ethernet frame
    \param[in]  net_if: Pointer to the network interface that received the frame
    \param[out] none
    \retval     ERR_OK if a L2 socket exist for this frame and buffer has been successfully
                pushed, ERR_MEM/ERR_VAL otherwise.
*/
err_t net_eth_receive(struct pbuf *pbuf, void *net_if)
{
    struct l2_filter_tag *filter = NULL;
    struct eth_hdr* ethhdr = pbuf->payload;
    uint16_t ethertype = ntohs(ethhdr->type);
    struct netconn *conn;
    struct netbuf *buf;
    int i;
    struct netif *netif = (struct netif *)net_if;

    for (i = 0; i < WIFI_NB_L2_FILTER; i++)
    {
        if ((l2_filter[i].net_if == netif) &&
            (l2_filter[i].ethertype == ethertype))
        {
            filter = &l2_filter[i];
            break;
        }
    }

    if (!filter)
        return ERR_VAL;

    buf = (struct netbuf *)memp_malloc(MEMP_NETBUF);
    if (buf == NULL)
    {
        return ERR_MEM;
    }

    buf->p = pbuf;
    buf->ptr = pbuf;
    conn = filter->conn;

    if (sys_mbox_trypost(&conn->recvmbox, buf) != ERR_OK)
    {
        netbuf_delete(buf);
        return ERR_OK;
    }
    else
    {
        #if LWIP_SO_RCVBUF
        SYS_ARCH_INC(conn->recv_avail, pbuf->tot_len);
        #endif /* LWIP_SO_RCVBUF */
        /* Register event with callback */
        API_EVENT(conn, NETCONN_EVT_RCVPLUS, pbuf->tot_len);
    }

    return ERR_OK;
}
#else /* CONFIG_WPA_SUPPLICANT */
extern int wifi_wpa_rx_eapol_event(void *wvif, uint16_t type, uint8_t *data, uint32_t len);
/*!
    \brief      LWIP hook to process ethernet packet not supported.
                Check if a socket has been created for this (netif, ethertype) couple.
                If so push the buffer in the socket buffer list. (no buffer copy is done)
    \param[in]  pbuf: Buffer containing the ethernet frame
    \param[in]  net_if: Pointer to the network interface that received the frame
    \param[out] none
    \retval     ERR_OK if a L2 socket exist for this frame and buffer has been successfully
                pushed, ERR_MEM/ERR_VAL otherwise.
*/
err_t net_eth_receive(struct pbuf *pbuf, void *net_if)
{
    struct eth_hdr* ethhdr = pbuf->payload;
    void *wvif = ((struct netif *)net_if)->state;

    if (NULL == wvif) {
        goto Exit;
    }
    wifi_wpa_rx_eapol_event(wvif, ntohs(ethhdr->type), pbuf->payload, pbuf->tot_len);
Exit:
    pbuf_free(pbuf);
    return ERR_OK;
}
#endif /* CONFIG_WPA_SUPPLICANT */

/*!
    \brief      Start DHCP procedure on a given interface
    \param[in]  net_if: Pointer to the interface on which DHCP must be started
    \param[out] none
    \retval     0 if DHCP procedure successfully started and != 0 if an error occurred
*/
int net_dhcp_start(void *net_if)
{
    #if LWIP_IPV4 && LWIP_DHCP
    struct netif *netif = (struct netif *)net_if;
    if (netifapi_dhcp_start(netif) ==  ERR_OK)
        return 0;
    #endif //LWIP_IPV4 && LWIP_DHCP
    return -1;
}

/*!
    \brief      Stop DHCP procedure on a given interface
    \param[in]  net_if: Pointer to the interface on which DHCP must be stopped
    \param[out] none
    \retval     none
*/
void net_dhcp_stop(void *net_if)
{
    #if LWIP_IPV4 && LWIP_DHCP
    struct netif *netif = (struct netif *)net_if;
    netifapi_dhcp_stop(netif);
    #endif //LWIP_IPV4 && LWIP_DHCP
}

/*!
    \brief      Release DHCP lease on a given interface
    \param[in]  net_if: Pointer to the interface on which DHCP must be released
    \param[out] none
    \retval     0 if DHCP lease has been released and != 0 if an error occurred
*/
int net_dhcp_release(void *net_if)
{
    #if LWIP_IPV4 && LWIP_DHCP
    struct netif *netif = (struct netif *)net_if;
    if (netifapi_dhcp_release(netif) ==  ERR_OK)
        return 0;
    #endif //LWIP_IPV4 && LWIP_DHCP
    return -1;
}

/*!
    \brief      Check if an IP has been assigned with DHCP
    \param[in]  net_if: Pointer to the interface to test
    \param[out] none
    \retval     1 if ip address assigned to the interface has been obtained via DHCP and 0 otherwise
*/
bool net_dhcp_address_obtained(void *net_if)
{
    #if LWIP_IPV4 && LWIP_DHCP
    struct netif *netif = (struct netif *)net_if;
    if (dhcp_supplied_address(netif))
        return true;
    #endif //LWIP_IPV4 && LWIP_DHCP
    return false;
}

/*!
    \brief      Start DHCPD procedure on a given interface
    \param[in]  net_if: Pointer to the interface on which DHCPD must be started
    \param[out] none
    \retval     0 if DHCPD procedure successfully started and != 0 if an error occurred
*/
int net_dhcpd_start(void *net_if)
{
    struct netif *netif = (struct netif *)net_if;

    if (!netif)
        return -1;

    #if LWIP_IPV4 && LWIP_DHCPD
    if (!ap_dhcpd_started) {
        dhcpd_daemon(netif);
        ap_dhcpd_started = 1;
    }
    return 0;
    #endif //LWIP_IPV4 && LWIP_DHCPD

    return -1;
}

/*!
    \brief      Stop DHCPD procedure on a given interface
    \param[in]  net_if: Pointer to the interface on which DHCPD must be stopped
    \param[out] none
    \retval     none
*/
void net_dhcpd_stop(void *net_if)
{
    struct netif *netif = (struct netif *)net_if;

    if (!netif)
        return;

    #if LWIP_IPV4 && LWIP_DHCPD
    if (ap_dhcpd_started) {
        if (stop_dhcpd_daemon(netif) == 0)
            ap_dhcpd_started = 0;
    }
    #endif //LWIP_IPV4 && LWIP_DHCPD
}

#if LWIP_IPV6
/*!
    \brief      Start SLAAC server procedure on a given interface
    \param[in]  net_if: Pointer to the interface on which SLAAC server must be started
    \param[out] none
    \retval     none
*/
void net_ip6_server_start(void *net_if)
{
    ip6_addr_t ipaddr;
    struct netif *netif = (struct netif *)net_if;

    if (!netif || ap_ip6_server_started)
        return;

#if LWIP_IPV6_DHCP6
    // only use SLAAC
    dhcp6_disable(netif);
#endif
    IP6_ADDR(&ipaddr, PP_HTONL(UIP6_DEFAULT_PREFIX_1), PP_HTONL(UIP6_DEFAULT_PREFIX_2),
                    PP_HTONL(0), PP_HTONL(0x1));
    netif_add_ip6_address(netif, &ipaddr, NULL);

    ip6_addr_set_allnodes_linklocal(&ipaddr);
    mld6_joingroup_netif(netif, &ipaddr);
    ip6_addr_set_allrouters_linklocal(&ipaddr);
    mld6_joingroup_netif(netif, &ipaddr);
    ip6_addr_set_solicitednode(&ipaddr, PP_HTONL(0x00000001UL));
    mld6_joingroup_netif(netif, &ipaddr);

    ap_ip6_server_started = 1;
}

/*!
    \brief      Stop SLAAC server procedure on a given interface
    \param[in]  net_if: Pointer to the interface on which SLAAC server must be stopped
    \param[out] none
    \retval     none
*/
void net_ip6_server_stop(void *net_if)
{
    ip6_addr_t ipaddr;
    struct netif *netif = (struct netif *)net_if;

    if (!netif || !ap_ip6_server_started)
        return;

    ip6_addr_set_allnodes_linklocal(&ipaddr);
    mld6_leavegroup_netif(netif, &ipaddr);
    ip6_addr_set_allrouters_linklocal(&ipaddr);
    mld6_leavegroup_netif(netif, &ipaddr);

#if LWIP_IPV6_DHCP6
    dhcp6_enable_stateless(netif);
#endif

    ap_ip6_server_started = 0;
}
#endif /* LWIP_IPV6 */

/*!
    \brief      Configure DNS server IP address
    \param[in]  dns_server: DNS server IPv4 address
    \param[out] none
    \retval     0 on success and != 0 if error occurred.
*/
int net_set_dns(uint32_t dns_server)
{
    #if LWIP_DNS
    ip_addr_t ip;
    #if LWIP_IPV6
    ip_addr_set_ip4_u32_val(ip, dns_server);
    #else /* LWIP_IPV6 */
    ip_addr_set_ip4_u32(&ip, dns_server);
    #endif /* LWIP_IPV6 */
    dns_setserver(0, &ip);
    return 0;
    #else
    return -1;
    #endif
}

/*!
    \brief      Get DNS server IP address
    \param[in]  none
    \param[out] dns_server: DNS server IPv4 address
    \retval     0 on success and != 0 if error occurred.
*/
int net_get_dns(uint32_t *dns_server)
{
    #if LWIP_DNS
    const ip_addr_t *ip;

    if (dns_server == NULL)
        return -1;

    ip = dns_getserver(0);
    *dns_server = ip_addr_get_ip4_u32(ip);
    return 0;
    #else
    return -1;
    #endif
}

/*!
    \brief      compatibility check of struct netif
    \param[in]  netif_size: size of struct netif which need to be checked
    \param[out] none
    \retval     0 on success and 1 if check fails.
*/
int net_compat_check(size_t netif_size)
{
    return (netif_size != sizeof(struct netif));
}

/*!
    \brief      get a loopback socket
    \param[in]  protocol: protocol type of socket
    \param[out] none
    \retval     socket descriptor on success and -1 if error occurred.
*/
int net_lpbk_socket_create(int protocol)
{
    int sock;

    sock = socket(PF_INET, SOCK_DGRAM, protocol);
    if (sock < 0)
        return -1;

    return sock;
}

/*!
    \brief      bind a loopback socket with network infomation
    \param[in]  sock_recv: socket descriptor
    \param[in]  port: port of network
    \param[out] none
    \retval     0 on success and -1 if error occurred.
*/
int net_lpbk_socket_bind(int sock_recv, uint32_t port)
{
    struct sockaddr_in recv_addr;

    sys_memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    recv_addr.sin_port = htons(port);
    if (bind(sock_recv, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) < 0)
        return -1;

    return 0;
}

/*!
    \brief      connect a loopback socket to remote network infomation
    \param[in]  sock_send: socket descriptor
    \param[in]  port: port of network
    \param[out] none
    \retval     0 on success and -1 if error occurred.
*/
int net_lpbk_socket_connect(int sock_send, uint32_t port)
{
    struct sockaddr_in send_addr;

    sys_memset(&send_addr, 0, sizeof(send_addr));
    send_addr.sin_family = AF_INET;
    send_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    send_addr.sin_port = htons(port);
    if (connect(sock_send, (struct sockaddr *)&send_addr, sizeof(send_addr)) < 0)
        return -1;

    return 0;
}

/*!
    \brief      indicate whether to use static ip
    \param[in]  static_ip: true if use static ip and false if not use.
    \param[out] none
    \retval     none
*/
void net_if_use_static_ip(bool static_ip)
{
    netif_static_ip = static_ip;
}

/*!
    \brief      check if static ip is used
    \param[in]  none
    \param[out] none
    \retval     true if static ip used and false if not use.
*/
bool net_if_is_static_ip(void)
{
    return netif_static_ip;
}

/*!
    \brief      check if static ip is conflict
    \param[in]  net_if: Pointer to the net_if structure
    \param[in]  addr: Pointer to the IPv4 addr to be compared
    \param[out] none
    \retval     none.
*/
void net_static_ip_check_conflict(struct netif *netif, const ip4_addr_t *addr)
{
#if LWIP_IPV4 && LWIP_ARP
    if (netif_static_ip == 0)
        return;
    if (ip4_addr_cmp(addr, netif_ip4_addr(netif)) == 1) {
        dbg_print(ERR, "There is an IP conflict with the current IP.\r\n");
    }
#endif
}
