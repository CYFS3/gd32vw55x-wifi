/*!
    \file    dnsd.c
    \brief   dns server

    \version 2021-10-30, V1.0.0, firmware for GD32VW55x
*/

/*
    Copyright (c) 2021, GigaDevice Semiconductor Inc.

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

#include "app_cfg.h"
#include "wrapper_os.h"
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "wifi_netif.h"
#include "wifi_vif.h"
#include "dbg_print.h"

static uint8_t running = 0;

#define DNS_PACKET_LEN 256
#define DNS_TYPE_A (0x0001)
#define DNS_TTL (300)
#define DNS_PORT (53)
#define DNSD_TASK_STK_SIZE                  512
#define DNSD_TASK_QUEUE_SIZE                0
#define DNSD_TASK_PRIO                      16

struct dns_headers
{
    uint16_t trans_id;
    // uint16_t flags;
    union {
        struct {
            uint16_t RD:1;           // Recursion Desired
            uint16_t TC:1;           // Trun Cation
            uint16_t AA:1;           // Authoritative Answer
            uint16_t OPCODE:4;       // representing a standard query
            uint16_t QR:1;           // Query (0), or Response (1).
            uint16_t RCODE:4;        // Response code
            uint16_t Reserved:3;
            uint16_t RA:1;           // Recursion Available
        };
        uint16_t flags;
    };
    uint16_t query_num;
    uint16_t answer_num;
    uint16_t authority_num;
    uint16_t additional_num;
} __PACKED;


struct dns_query  {
    uint16_t type;
    uint16_t class;
} __PACKED;


struct dns_answer {
    uint16_t pointer;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t ip_len;
    uint32_t ip_addr;
}__PACKED;

static uint16_t get_query_name_len(const uint8_t *start)
{
    uint16_t sub_len;
    uint16_t len = 0;
    uint8_t *s = (uint8_t *)start;

    do {
        sub_len = *s;
        s = s + sub_len + 1;
        len += sub_len + 1;
    } while(*s);

    return len + 1;
}

static int32_t handle_dns_query(uint8_t *rx_buf, int32_t rx_len, uint8_t *answer_buf, int32_t answer_max_len)
{
    struct dns_headers *query_header, *answer_header;
    struct dns_query *qurey_entry;
    struct dns_answer *answer_entry;
    uint32_t ip;
    uint16_t query_num;
    uint16_t answer_num;
    uint16_t name_len; // include '\0'
    uint8_t *query, *answer;
    uint32_t i;
    uint32_t vif_idx = WIFI_VIF_INDEX_DEFAULT;
    struct netif *net_if = vif_idx_to_net_if(vif_idx);

    if (rx_len > answer_max_len) {
        return -1;
    }

    query_header = (struct dns_headers *)rx_buf;

    if (query_header->OPCODE != 0 || query_header->QR == 1) {
        return 0;
    }

    query_num = ntohs(query_header->query_num);
    if ((query_num * sizeof(struct dns_answer) + rx_len) > answer_max_len) {
        return -1;
    }

    sys_memset(answer_buf, 0, answer_max_len);
    // duplicate header and query entrys
    sys_memcpy(answer_buf, rx_buf, rx_len);

    answer_header = (struct dns_headers *)answer_buf;

    answer_header->QR = 1;
    answer_num = 0;

    query = rx_buf + sizeof(struct dns_headers);
    answer = answer_buf + rx_len;

    net_if_get_ip(net_if, &ip, NULL, NULL);

    for (i = 0; i < query_num; i++) {
        name_len = get_query_name_len(query);

        if (name_len == 0) {
            return -1;
        }

        qurey_entry = (struct dns_query *)(query + name_len);

        if (ntohs(qurey_entry->type) == DNS_TYPE_A) {
            answer_entry = (struct dns_answer *)answer;

            answer_entry->pointer = htons(0xC000 | (query - rx_buf));
            answer_entry->type = qurey_entry->type;
            answer_entry->class = qurey_entry->class;
            answer_entry->ttl = htonl(DNS_TTL);

            answer_entry->ip_len = htons(sizeof(ip));
            answer_entry->ip_addr = ip;
            answer = answer + sizeof(struct dns_answer);
            answer_num ++;
        }

        query = query + name_len + sizeof(struct dns_query);
    }

    if (answer_num == 0)
        return 0;

    answer_header->answer_num = htons(answer_num);

    return rx_len + answer_num * sizeof(struct dns_answer);
}

static void dns_server(void *parm)
{
    int s;
    struct sockaddr_in server_addr;
    int32_t len;
#if LWIP_IPV6
    struct sockaddr_in6 src;
#else
    struct sockaddr_in src;
#endif
    socklen_t src_len;
    int rx_timeout = 1000;
    uint8_t rx_buf[DNS_PACKET_LEN];


    sys_memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(DNS_PORT);
    server_addr.sin_len = sizeof(server_addr);

    s = socket(AF_INET, SOCK_DGRAM, 0);

    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &rx_timeout, sizeof(rx_timeout));

    bind(s, (struct sockaddr *)&server_addr, sizeof(server_addr));

    running = 1;

    while(running) {
        src_len = sizeof(src);
        sys_memset(&src, 0, src_len);

        len = recvfrom(s, (uint8_t *)rx_buf, DNS_PACKET_LEN - 1, 0, (struct sockaddr *)&src, (socklen_t *)&src_len);

        if (len > 0) {
            uint8_t tx_buf[DNS_PACKET_LEN];
            rx_buf[len] = 0;
            len = handle_dns_query(rx_buf, len, tx_buf, DNS_PACKET_LEN);
            if (len > 0) {
                len = sendto(s, tx_buf, len, 0, (struct sockaddr *)&src, sizeof(src));
                if (len < 0) {
                    dbg_print(NOTICE, "DNSD send: errno %d\r\n", errno);
                    break;
                }
            }
        }
    }

    close(s);
    sys_task_delete(NULL);
}

void dns_server_start(void)
{
    running = 0;
    while (sys_task_exist((const uint8_t *)"dns_server")) {
        sys_ms_sleep(1);
    }
    sys_task_create(NULL, (const uint8_t *)"dns_server", NULL, DNSD_TASK_STK_SIZE, DNSD_TASK_QUEUE_SIZE, 0, DNSD_TASK_PRIO,
                (task_func_t)dns_server, NULL);
}

void dns_server_stop(void)
{
    running = 0;
}

