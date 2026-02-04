/*!
    \file    dhcpd.c
    \brief   Definitions related to the dhcpd.

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

#include "arch/sys_arch.h"
#include "dhcpd.h"
#include "common_subr.h"
#include "lwip/api.h"
#include "dhcpd_conf.h"
#include "leases.h"
#include "lwip/dhcp.h"
#include "lwip/sockets.h"
#include "dbg_print.h"
#include "lwip/tcpip.h"
#include "lwip/etharp.h"

#if LWIP_DHCPD

ip_addr_t destAddr;
struct dhcpOfferedAddr leases[DHCPD_MAX_LEASES] = {0};
struct dhcpd payload_out;
struct server_config_t server_config;
struct udp_pcb *UdpPcb = NULL;
int    pass_wan_domain = 0;
#define DECLINE_IP_MAX    (CFG_STA_NUM / 2)
uint32_t decline_ip[DECLINE_IP_MAX] = {0};
#define DHCP_SERVER_PORT  67

//pickup what i want according to "code" in the packet, "dest" callback
static unsigned char *dhcpd_pickup_opt(struct dhcpd *packet, int code, int dest_len, void *dest)
{
    int i, length;
    unsigned char *ptr;
    int over = 0, done = 0, curr = OPTION_FIELD;
    unsigned char len;

    ptr = packet->options;
    i = 0;
    length = 308;
    while (!done) {
        if (i >= length) {
            //printf( "Option fields too long.");
            return 0;
        }

        if (ptr[i + OPT_CODE] == code) {
            if (i + 1 + ptr[i + OPT_LEN] >= length) {
                return 0;
            }

            if (dest) {
                len = ptr[i + OPT_LEN];
                if (len > dest_len) {
                    //printf( "Option fields too long to fit in dest.");
                    return 0;
                }

                sys_memcpy(dest, &ptr[i + OPT_DATA], (int)len);
            }

            return &ptr[i + OPT_DATA];
        }

        switch (ptr[i + OPT_CODE]) {
        case DHCP_PADDING:
            i++;
            break;

        case DHCP_OPTION_OVER:
            if (i + 1 + ptr[i + OPT_LEN] >= length) {
                return 0;
            }

            over = ptr[i + 3];
            i += ptr[OPT_LEN] + 2;
            break;

        case DHCP_END:
            if (curr == OPTION_FIELD && over & FILE_FIELD) {
                ptr = packet->file;
                i = 0;
                length = 128;
                curr = FILE_FIELD;
            } else if (curr == FILE_FIELD && over & SNAME_FIELD) {
                ptr = packet->sname;
                i = 0;
                length = 64;
                curr = SNAME_FIELD;
            } else {
                done = 1;
            }
            break;

        default:
            i += ptr[OPT_LEN + i] + 2;
        }
    }

    return NULL;
}

static int dhcpd_add_option(unsigned char *ptr, unsigned char code, unsigned char len, void *data)
{
    int end;

    // Search DHCP_END
    end = 0;
    while (ptr[end] != DHCP_END) {
        if (ptr[end] == DHCP_PADDING) {
            end++;
        } else {
            end += ptr[end + OPT_LEN] + 2; // 2 is opt_code and opt_len cost 2 bytes
        }
    }

    if (end + len + 2 + 1 >= 308) {
        //printf( "Option 0x%02x cannot not fit into the packet!", code);
        return 0;
    }

    ptr += end; //rebuild pointer

    ptr[OPT_CODE] = code;
    ptr[OPT_LEN] = len; // bytes number of data stored in option
    sys_memcpy(&ptr[OPT_DATA], data, len);

    // Reassign DHCP_END
    ptr += (len+2);
    *ptr = DHCP_END;
    return (len + 2); // return this operation costs option bytes number
}

static struct dhcpOfferedAddr *DHCPD_FindLeaseByYiaddr(struct in_addr yiaddr)
{
    unsigned int i;

    for (i = 0; i < server_config.max_leases; i++) {
        if (leases[i].yiaddr.s_addr == yiaddr.s_addr) {
            return &leases[i];
        }
    }

    return NULL;
}

static int dhcpd_find_decline_ip(uint32_t ipaddr)
{
    uint16_t i;

    for (i = 0; i < DECLINE_IP_MAX; i++) {
        if (ipaddr == decline_ip[i]) {
            return i;
        }
    }
    return -1;
}

static int dhcpd_check_ipaddr_in_arp(struct in_addr *addr)
{
    const ip4_addr_t *unused_ipaddr;
    struct eth_addr *unused_ethaddr;
    struct netif *net_if = NULL;
    ssize_t idx;

    if (UdpPcb->netif_idx != NETIF_NO_INDEX) {
        net_if = netif_get_by_index(UdpPcb->netif_idx);
        if (net_if) {
            idx = etharp_find_addr(net_if, (ip4_addr_t *)addr, &unused_ethaddr, &unused_ipaddr);
            if (idx != -1) {
                return 1;
            }
        }
    }
    return 0;
}

static struct in_addr DHCPD_FindAddress(void)
{
    uint32_t addr;
    struct in_addr ret;
    struct dhcpOfferedAddr *lease = 0;

    for( addr = ntohl(server_config.start.s_addr);
        addr <= ntohl(server_config.end.s_addr);
         addr++ ) {
        // ie, xx.xx.xx.0 or xx.xx.xx.255 or itself
        if ((addr & 0xFF) == 0 ||
        (addr & 0xFF) == 0xFF ||
        (addr == ntohl(server_config.server.s_addr))) {
            continue;
        }

        ret.s_addr = ntohl(addr);

        if (dhcpd_check_ipaddr_in_arp(&ret)) {
            continue;
        }

        lease = DHCPD_FindLeaseByYiaddr(ret);
        if (lease == 0) {
            return ret;
        }
    }
    ret.s_addr = 0;
    return ret;
}

static struct dhcpOfferedAddr *DHCPD_FindLeaseByChaddr(uint8_t *chaddr)
{
    unsigned int i;

    for (i = 0; i < server_config.max_leases; i++) {
        if (memcmp(leases[i].chaddr, chaddr, 6) == 0) {
            return &(leases[i]);
        }
    }

    return NULL;
}

uint32_t dhcpd_find_ipaddr_by_macaddr(uint8_t *mac_addr)
{
    struct dhcpOfferedAddr *dhcp_offered_addr = NULL;
    dhcp_offered_addr = DHCPD_FindLeaseByChaddr(mac_addr);
    if (dhcp_offered_addr && !(dhcp_offered_addr->flag & DELETED) &&
        (dhcpd_find_decline_ip(dhcp_offered_addr->yiaddr.s_addr) == -1)) {
        return dhcp_offered_addr->yiaddr.s_addr;
    } else {
        return 0;
    }
}

int dhcpd_ipaddr_is_valid(uint32_t ipaddr)
{
    int i;

    for (i = 0; i < server_config.max_leases; i++) {
        if ((leases[i].yiaddr.s_addr == ipaddr) && ((leases[i].flag & DELETED) == 0)) {
            return 1;
        }
    }

    return 0;
}

static void dhcpd_clean_arp(void)
{
    struct netif *net_if = NULL;

    if (UdpPcb->netif_idx != NETIF_NO_INDEX) {
        net_if = netif_get_by_index(UdpPcb->netif_idx);
        if (net_if) {
            etharp_cleanup_netif(net_if);
        }
    }
}

static void _dhcpd_delete_ipaddr_by_macaddr(uint8_t *mac_addr)
{
    struct dhcpOfferedAddr *lease = NULL;

    lease = DHCPD_FindLeaseByChaddr(mac_addr);
    if (lease != NULL) {
        lease->flag |= DELETED;
    }

    dhcpd_clean_arp();
}

void dhcpd_delete_ipaddr_by_macaddr(uint8_t *mac_addr)
{
    LOCK_TCPIP_CORE();
    _dhcpd_delete_ipaddr_by_macaddr(mac_addr);
    UNLOCK_TCPIP_CORE();
}

static void make_dhcpd_packet(struct dhcpd *packet, struct dhcpd *oldpacket, char type)
{
    uint32_t lease_time = server_config.lease;
    //uint32_t dns_server = 0;
    unsigned char *option = packet->options;
    char domain_name[255] = {0};
    lease_time = htonl(lease_time);
    memset(packet, 0, sizeof(struct dhcpd));
    packet->op = BOOTREPLY;
    packet->htype = ETH_10MB;
    packet->hlen = ETH_10MB_LEN;
    packet->xid = oldpacket->xid;
    sys_memcpy(packet->chaddr, oldpacket->chaddr, 16);
    packet->flags = oldpacket->flags;
    packet->ciaddr = oldpacket->ciaddr;
    packet->siaddr = server_config.siaddr.s_addr;
    packet->giaddr = oldpacket->giaddr;
    packet->cookie = htonl(DHCP_MAGIC);
    packet->options[0] = DHCP_END;
    sys_memcpy(packet->sname,server_config.sname, 6);
    dhcpd_add_option(option, DHCP_MESSAGE_TYPE, sizeof(type), &type);
    dhcpd_add_option(option, DHCP_SERVER_ID, sizeof(server_config.server.s_addr), &server_config.server.s_addr);
    dhcpd_add_option(option, DHCP_LEASE_TIME, sizeof(lease_time),&lease_time);
    dhcpd_add_option(option, DHCP_SUBNET, sizeof(server_config.mask.s_addr), &server_config.mask.s_addr);
    dhcpd_add_option(option, DHCP_ROUTER, sizeof(server_config.server.s_addr), &server_config.server.s_addr);
    dhcpd_add_option(option, DHCP_DNS_SERVER, sizeof(server_config.server.s_addr), &server_config.server.s_addr);
    sys_memcpy(domain_name, DEFAULT_DOMAIN, sizeof(DEFAULT_DOMAIN));
    dhcpd_add_option(option, DHCP_DOMAIN_NAME, strlen(domain_name), domain_name);
}

static int discover(struct dhcpd *packetinfo)
{
    struct in_addr addr;
    //struct in_addr req_ip;
    int8_t idx = 0, deleted_lease_idx = -1;
    int decline_idx = -1;
    struct dhcpOfferedAddr *lease;
    //struct dhcpOfferedAddr *lease_new;
    //struct dhcpOfferedAddr *lease_change=NULL;
    if (memcmp(packetinfo->chaddr, "\x00\x00\x00\x00\x00\x00", 6) == 0 ||
    memcmp(packetinfo->chaddr, "\xff\xff\xff\xff\xff\xff", 6) == 0) {
        return -1;
    }

    addr = DHCPD_FindAddress();
    lease = DHCPD_FindLeaseByChaddr(packetinfo->chaddr);
    if (lease) {
        decline_idx = dhcpd_find_decline_ip(lease->yiaddr.s_addr);
        if (decline_idx != -1) {
            memset(lease, 0, sizeof(struct dhcpOfferedAddr));
            lease = NULL;
            dhcpd_clean_arp();
            decline_ip[decline_idx] = 0;
        }
    }
    if (lease == NULL) {
        // find a empty lease first, then find the deleted lease
        for (idx = 0; idx < server_config.max_leases; idx++) {
            if((leases[idx].flag & DELETED) != 0) {
                deleted_lease_idx = idx;
            } else if (memcmp(leases[idx].chaddr, "\x00\x00\x00\x00\x00\x00", 6) == 0) {
                deleted_lease_idx = -1;
                break;
            }
        }

        //no empty lease and no deleted lease
        if ((idx >= server_config.max_leases) && (deleted_lease_idx == -1))
            return -1;

        // no empty lease but has deleted lease
        if (deleted_lease_idx != -1)
            idx = deleted_lease_idx;

        MEMCPY(leases[idx].chaddr,packetinfo->chaddr,6);
        leases[idx].yiaddr = addr;
        lease = &(leases[idx]);
    }

    memset(&payload_out, 0, sizeof(struct dhcpd));
    make_dhcpd_packet(&payload_out, packetinfo, DHCPOFFER);
    payload_out.yiaddr = lease->yiaddr.s_addr;
    if (packetinfo->flags == 0) {
#if LWIP_IPV6
            destAddr.u_addr.ip4.addr = lease->yiaddr.s_addr;
#else
            destAddr.addr = lease->yiaddr.s_addr;
#endif
    }

    return 0;
}

static int request(struct dhcpd *packetinfo)
{
    uint32_t lease_time = server_config.lease;
    char type;
    unsigned char *option = payload_out.options;
    struct dhcpOfferedAddr *lease;
    char domain_name[255] = {0};
    uint32_t request_addr = 0;
    lease_time = htonl(lease_time);

    memset(&payload_out, 0, sizeof(struct dhcpd));

    if (dhcpd_pickup_opt(packetinfo, DHCP_REQUESTED_IP, sizeof(request_addr), &request_addr) == NULL) {
        request_addr = 0;
    }

    lease = DHCPD_FindLeaseByChaddr(packetinfo->chaddr);
    if ((lease != NULL) && (request_addr == lease->yiaddr.s_addr)) {
        type = DHCPACK;
        payload_out.op = BOOTREPLY;
        payload_out.htype = ETH_10MB;
        payload_out.hlen = ETH_10MB_LEN;
        payload_out.xid = packetinfo->xid;
        sys_memcpy(payload_out.chaddr, packetinfo->chaddr, 16);
        payload_out.flags = packetinfo->flags;
        payload_out.ciaddr = packetinfo->ciaddr;
        payload_out.siaddr = server_config.siaddr.s_addr;
        payload_out.giaddr = packetinfo->giaddr;
        payload_out.cookie = htonl(DHCP_MAGIC);
        payload_out.options[0] = DHCP_END;
        payload_out.yiaddr = lease->yiaddr.s_addr;
        dhcpd_add_option(option, DHCP_MESSAGE_TYPE, sizeof(type), &type);
        dhcpd_add_option(option, DHCP_SERVER_ID, sizeof(server_config.server.s_addr), &server_config.server.s_addr);
        dhcpd_add_option(option, DHCP_LEASE_TIME, sizeof(lease_time), &lease_time);
        dhcpd_add_option(option, DHCP_SUBNET, sizeof(server_config.mask.s_addr), &server_config.mask.s_addr);
        dhcpd_add_option(option, DHCP_ROUTER, sizeof(server_config.server.s_addr), &server_config.server.s_addr);
        dhcpd_add_option(option, DHCP_DNS_SERVER, sizeof(server_config.server.s_addr), &server_config.server.s_addr);
        sys_memcpy(domain_name, DEFAULT_DOMAIN, sizeof(DEFAULT_DOMAIN));
        dhcpd_add_option(option, DHCP_DOMAIN_NAME, strlen(domain_name), domain_name);
        if (packetinfo->flags == 0) {
#if LWIP_IPV6
            destAddr.u_addr.ip4.addr = lease->yiaddr.s_addr;
#else
            destAddr.addr = lease->yiaddr.s_addr;
#endif
        }

        if ((lease->flag & DELETED) != 0) {
            lease->flag &= ~DELETED;
        }

        dbg_print(NOTICE, "DHCPD: Assign %d.%d.%d.%d for %02x:%02x:%02x:%02x:%02x:%02x.\n\r\n",
                (uint32_t)(payload_out.yiaddr & 0xFF), (uint32_t)((payload_out.yiaddr >> 8) & 0xFF),
                (uint32_t)((payload_out.yiaddr >> 16) & 0xFF), (uint32_t)(payload_out.yiaddr >> 24),
                packetinfo->chaddr[0], packetinfo->chaddr[1], packetinfo->chaddr[2],
                packetinfo->chaddr[3], packetinfo->chaddr[4], packetinfo->chaddr[5]);
    } else if ((lease != NULL) && (request_addr == 0) && (packetinfo->ciaddr == lease->yiaddr.s_addr)) {
        // client request the same IP address, renew the lease pool
        type = DHCPACK;
        payload_out.op = BOOTREPLY;
        payload_out.htype = ETH_10MB;
        payload_out.hlen = ETH_10MB_LEN;
        payload_out.xid = packetinfo->xid;
        sys_memcpy(payload_out.chaddr, packetinfo->chaddr, 16);
        payload_out.flags = packetinfo->flags;
        payload_out.ciaddr = packetinfo->ciaddr;
        payload_out.siaddr = server_config.siaddr.s_addr;
        payload_out.giaddr = packetinfo->giaddr;
        payload_out.cookie = htonl(DHCP_MAGIC);
        payload_out.options[0] = DHCP_END;
        payload_out.yiaddr = lease->yiaddr.s_addr;
        dhcpd_add_option(option, DHCP_MESSAGE_TYPE, sizeof(type), &type);
        dhcpd_add_option(option, DHCP_SERVER_ID, sizeof(server_config.server.s_addr), &server_config.server.s_addr);
        dhcpd_add_option(option, DHCP_LEASE_TIME, sizeof(lease_time), &lease_time);
        dhcpd_add_option(option, DHCP_SUBNET, sizeof(server_config.mask.s_addr), &server_config.mask.s_addr);
        dhcpd_add_option(option, DHCP_ROUTER, sizeof(server_config.server.s_addr), &server_config.server.s_addr);
        dhcpd_add_option(option, DHCP_DNS_SERVER, sizeof(server_config.server.s_addr), &server_config.server.s_addr);
        sys_memcpy(domain_name, DEFAULT_DOMAIN, sizeof(DEFAULT_DOMAIN));
        dhcpd_add_option(option, DHCP_DOMAIN_NAME, strlen(domain_name), domain_name);
        if (packetinfo->flags == 0) {
#if LWIP_IPV6
            destAddr.u_addr.ip4.addr = lease->yiaddr.s_addr;
#else
            destAddr.addr = lease->yiaddr.s_addr;
#endif
        }

        if ((lease->flag & DELETED) != 0) {
            lease->flag &= ~DELETED;
        }

        dbg_print(NOTICE, "DHCPD: IP lease renewal %d.%d.%d.%d for %02x:%02x:%02x:%02x:%02x:%02x.\n\r\n",
                (uint32_t)(payload_out.yiaddr & 0xFF), (uint32_t)((payload_out.yiaddr >> 8) & 0xFF),
                (uint32_t)((payload_out.yiaddr >> 16) & 0xFF), (uint32_t)(payload_out.yiaddr >> 24),
                packetinfo->chaddr[0], packetinfo->chaddr[1], packetinfo->chaddr[2],
                packetinfo->chaddr[3], packetinfo->chaddr[4], packetinfo->chaddr[5]);
    } else {
        type = DHCPNAK;
        payload_out.op = BOOTREPLY;
        payload_out.htype = ETH_10MB;
        payload_out.hlen = ETH_10MB_LEN;
        payload_out.xid = packetinfo->xid;
        sys_memcpy(payload_out.chaddr, packetinfo->chaddr, 16);
        payload_out.flags = packetinfo->flags;
        payload_out.ciaddr = packetinfo->ciaddr;
        payload_out.giaddr = packetinfo->giaddr;
        payload_out.cookie = htonl(DHCP_MAGIC);
        payload_out.options[0] = DHCP_END;
        dhcpd_add_option(option, DHCP_MESSAGE_TYPE, sizeof(type), &type);
        dhcpd_add_option(option, DHCP_SERVER_ID, sizeof(server_config.server.s_addr), &server_config.server.s_addr);
    }

    return 0;
}

static int release(struct dhcpd *packetinfo)
{
    uint32_t server_id;

    if (dhcpd_pickup_opt(packetinfo, DHCP_SERVER_ID, sizeof(server_id), &server_id) == NULL) {
        return -1;
    }

    if (server_config.server.s_addr == server_id) {
        _dhcpd_delete_ipaddr_by_macaddr(packetinfo->chaddr);
    }
    return 0;
}

static int decline(struct dhcpd *packetinfo)
{
    uint32_t server_id;
    uint32_t requested_ip;

    if (dhcpd_pickup_opt(packetinfo, DHCP_SERVER_ID, sizeof(server_id), &server_id) == NULL) {
        return -1;
    }
    if (dhcpd_pickup_opt(packetinfo, DHCP_REQUESTED_IP, sizeof(requested_ip), &requested_ip) == NULL) {
        return -1;
    }

    if (server_config.server.s_addr == server_id) {
        if (dhcpd_find_decline_ip(requested_ip) != -1)
            return 0;

        for (int i = 0; i < DECLINE_IP_MAX; i++) {
            if (decline_ip[i] == 0) {
                decline_ip[i] = requested_ip;
                break;
            }
        }
    }
    return 0;
}

#if 0
#define START_IP    0
#define END_IP      1
#define BOOT_IP     2
static void ip_create_by_config(int flag, char * ipaddr)
{
    int i = 0, j = 0;

    while(ipaddr[i] != '\0') {
        if(ipaddr[i] == '.') {
            j++;
        }

        i++;

        if(j >= 3) {
            break;
        }
    }

    ipaddr[i] = '\0';

    switch(flag) {
    case START_IP:
        strcat(ipaddr,"150");
        break;
    case END_IP:
        strcat(ipaddr,"200");
        break;
    case BOOT_IP:
        strcat(ipaddr,"5");
        break;
    default:
        break;
    }

    LWIP_DEBUGF(DHCP_DEBUG | LWIP_DBG_TRACE, ("DHCP IP: [ %s ]\n\r",ipaddr));
}
#endif

static int init_config(struct netif *net_if)
{
    memset(&server_config, 0, sizeof(struct server_config_t));

    /* we use first listen interface as server IP */
    //server_config.interface = LAN_NAME;
    //if (read_interface(server_config.interface, &server_config.server.s_addr, &server_config.mask.s_addr, server_config.arp) < 0)
    //    return -1;

#if LWIP_IPV6
    // server address, gateway
    server_config.server.s_addr = net_if->gw.u_addr.ip4.addr;
    //net mask
    server_config.mask.s_addr = net_if->netmask.u_addr.ip4.addr;
    //start address
    server_config.start.s_addr = PP_HTONL(PP_HTONL(net_if->ip_addr.u_addr.ip4.addr) + 1);
    //end address
    server_config.end.s_addr = PP_HTONL(PP_HTONL(net_if->ip_addr.u_addr.ip4.addr) + 1 + DHCPD_MAX_LEASES);
#else
    // server address, gateway
    server_config.server.s_addr = net_if->gw.addr;
    //net mask
    server_config.mask.s_addr = net_if->netmask.addr;
    //start address
    server_config.start.s_addr = PP_HTONL(PP_HTONL(net_if->ip_addr.addr) + 1);
    //end address
    server_config.end.s_addr = PP_HTONL(PP_HTONL(net_if->ip_addr.addr) + 1 + DHCPD_MAX_LEASES);
#endif
    //end address - start address(ip lease count)
    server_config.max_leases = DHCPD_MAX_LEASES;

    // lease time
    server_config.lease = DEFAULT_LEASE_TIME; //3600
    server_config.conflict_time = DEFAULT_CONFLICT_TIME; //3600
    server_config.decline_time = DEFAULT_DECLINE_TIME; //3600
    server_config.min_lease = DEFAULT_MIN_LEASE_TIME; //60
    server_config.offer_time = DEFAULT_MIN_LEASE_TIME; //60
    server_config.auto_time = DEFAULT_AUTO_TIME; //3
    server_config.sname = DEFAULT_SNAME;
    server_config.boot_file = DEFAULT_BOOT_FILE;

#if LWIP_IPV6
    server_config.siaddr.s_addr = PP_HTONL(PP_HTONL(net_if->ip_addr.u_addr.ip4.addr) + 1 + DHCPD_MAX_LEASES + 1);
#else
    server_config.siaddr.s_addr = PP_HTONL(PP_HTONL(net_if->ip_addr.addr) + 1 + DHCPD_MAX_LEASES + 1);
#endif

    return 0;
}

uint8_t dhcp_process(void *packet_addr)
{
    char type;
    uint32_t s_addr;

    if (dhcpd_pickup_opt((struct dhcpd *)packet_addr, DHCP_MESSAGE_TYPE, sizeof(type), &type) == NULL) {
        LWIP_DEBUGF(DHCP_DEBUG | LWIP_DBG_TRACE, ("[DHCPD]: couldn't get option from packet, ignoring"));
        return 0;
    }

    switch (type) {
    case DHCPDISCOVER:
        LWIP_DEBUGF(DHCP_DEBUG | LWIP_DBG_TRACE, ("[DHCPD]: discover packet....\r\n"));
        discover(packet_addr);
        break;

    case DHCPREQUEST:
        // add detection if DHCPREQUEST frame was sent to us for wifi concurrent mode, refer to bugtrack<51>.
        if ((((struct dhcpd *)packet_addr)->ciaddr != 0) && (((((struct dhcpd *)packet_addr)->ciaddr >> 16) & 0xFF) != ((server_config.siaddr.s_addr >> 16) & 0xFF))) {
            return 0;
        } else if (dhcpd_pickup_opt((struct dhcpd *)packet_addr, DHCP_REQUESTED_IP, sizeof(s_addr), &s_addr)) {
            if (((server_config.siaddr.s_addr >> 16) & 0xFF) != ((s_addr >> 16) & 0xFF)) {
                return 0;
            }
        }
        LWIP_DEBUGF(DHCP_DEBUG | LWIP_DBG_TRACE, ("[DHCPD]: request packet...\n\r"));
        request(packet_addr);
        break;

    case DHCPRELEASE:
        release(packet_addr);
        return 0;

    case DHCPDECLINE:
        decline(packet_addr);
        return 0;

    default:
        LWIP_DEBUGF(DHCP_DEBUG | LWIP_DBG_TRACE, ("[DHCPD]: unknown message\n\r"));
        return 0;
    }
    return 1;
}

static void UDP_Receive(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, uint16_t port)
{
    struct pbuf *q;

#if LWIP_IPV6
    if (addr->type == IPADDR_TYPE_V6) {
        destAddr.type = IPADDR_TYPE_V6;
        destAddr.u_addr.ip6.addr[0] = 0xff;
    } else {
        destAddr.type = IPADDR_TYPE_V4;
        destAddr.u_addr.ip4.addr = htonl(IPADDR_BROADCAST);
    }
#else
    ip_addr_set_ip4_u32(&destAddr, htonl(IPADDR_BROADCAST));
#endif

    if (p != NULL) {
        LWIP_DEBUGF(DHCP_DEBUG | LWIP_DBG_TRACE, ("[DHCPD]: UDP_Receive ....\r\n"));
        if (dhcp_process(p->payload) != 0) {
            LWIP_DEBUGF(DHCP_DEBUG | LWIP_DBG_TRACE, ("[DHCPD]: dhcp packet send....\r\n"));
            q = pbuf_alloc(PBUF_TRANSPORT, sizeof(struct dhcpd), PBUF_REF);
            if (q) {
                q->payload = &payload_out;
                udp_sendto(upcb, q, &destAddr, port);
                pbuf_free(q);
            }
        }
        pbuf_free(p);
    }
}

void dhcpd_daemon(struct netif *net_if)
{
    int8_t idx = 0;

    if (UdpPcb == NULL) {
        // memset(leases, 0, sizeof(struct dhcpOfferedAddr) * DHCPD_MAX_LEASES);
        memset(decline_ip, 0, DECLINE_IP_MAX * 4);
        init_config(net_if);
        for (idx = 0; idx < server_config.max_leases; idx++) {
            if (memcmp(leases[idx].chaddr, "\x00\x00\x00\x00\x00\x00", 6) != 0) {
                leases[idx].flag |= DELETED;
            }
        }
        UdpPcb = udp_new();
        udp_bind(UdpPcb, IP_ADDR_ANY, 67);
        udp_bind_netif(UdpPcb, net_if);
        udp_recv(UdpPcb, UDP_Receive, NULL);
    }
}

int stop_dhcpd_daemon(struct netif *net_if)
{
    if (UdpPcb && net_if) {
        if (UdpPcb->netif_idx == netif_get_index(net_if)) {
            udp_remove(UdpPcb);
            UdpPcb = NULL;
            return 0;
        } else {
            return -1;
        }
    }
    return 0;
}

void *dhcpd_find_ethaddr_from_packet(struct pbuf *p)
{
    struct dhcpd *dhcpd_payload = NULL;
    void *dest = NULL;
    // use DHCPD only in IPv4.
    if (4 == (*(uint8_t *)p->payload >> 4)) {
        // IP_HLEN(IPv4) = 20. UDP_HLEN = 8.
        if (p->len == 20 && p->next) {
            if (p->next->len == 8 && p->next->next && p->next->next->len == sizeof(struct dhcpd)) {
                dhcpd_payload = (struct dhcpd *)p->next->next->payload;
            } else if (p->next->len == (8 + sizeof(struct dhcpd))) {
                dhcpd_payload = (struct dhcpd *)((uint8_t *)p->payload + 8);
            }
        } else if (p->len == 28 && p->next && p->next->len == sizeof(struct dhcpd)) {
            dhcpd_payload = (struct dhcpd *)p->next->payload;
        } else if (p->len == (28 + sizeof(struct dhcpd))) {
            dhcpd_payload = (struct dhcpd *)((uint8_t *)p->payload + 28);
        }
        if (dhcpd_payload && (dhcpd_payload->cookie == htonl(DHCP_MAGIC)) && (dhcpd_payload->op == BOOTREPLY)) {
            dest = &(dhcpd_payload->chaddr);
        }
    }
    return dest;
}

#endif
