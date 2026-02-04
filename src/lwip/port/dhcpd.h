/*!
    \file    dhcpd.h
    \brief   Declaration related to the dhcpd.

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

#ifndef DHCPD_H_
#define DHCPD_H_

#include "lwip/inet.h"
#include "leases.h"
#include "stdint.h"
struct dhcpd
{
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint32_t cookie;
    uint8_t options[308]; /* 312 - cookie */
} PACK_STRUCT_STRUCT;

struct server_config_t
{
    struct in_addr  server;
    struct in_addr  mask;
    struct in_addr  start;
    struct in_addr  end;

    char *interface;        /* The name of the interface to use */
    uint8_t arp[6];         /* Our arp address */
    uint32_t lease;         /* lease time in seconds (host order) */
    uint32_t max_leases;    /* maximum number of leases (including reserved address) */
    uint32_t auto_time;     /* how long should udhcpd wait before writing a config file if this is zero, it will only write one on SIGUSR1 */
    uint32_t decline_time;  /* how long an address is reserved if a client returns a decline message */
    uint32_t conflict_time; /* how long an arp conflict offender is leased for */
    uint32_t offer_time;    /* how long an offered address is reserved */
    uint32_t min_lease;     /* minimum lease a client can request*/

    struct in_addr  siaddr; /* next server bootp option */
    char *sname;            /* bootp server name */
    char *boot_file;        /* bootp boot file option */
};

void dhcpd_daemon(struct netif *net_if);
int stop_dhcpd_daemon(struct netif *net_if);
uint8_t dhcp_process(void *packet_addr);
void dhcpd_delete_ipaddr_by_macaddr(uint8_t *mac_addr);
uint32_t dhcpd_find_ipaddr_by_macaddr(uint8_t *mac_addr);

#endif /* DHCPD_H_ */
