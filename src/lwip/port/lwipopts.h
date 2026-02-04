/*
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * Copyright (c) 2024, GigaDevice Semiconductor Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Simon Goldschmidt
 *
 */
#ifndef LWIP_HDR_LWIPOPTS_H__
#define LWIP_HDR_LWIPOPTS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"
#include "wlan_config.h"
#include "app_cfg.h"
#include "macif_rx_def.h"

// #define LWIP_DEBUG

#ifdef CONFIG_MQTT
#define LWIP_MQTT
#define LWIP_SSL_MQTT
#endif

#ifdef CONFIG_IPV6_SUPPORT
#define LWIP_IPV6                     1
#define LWIP_IPV6_DHCP6               1
#define IPV6_ADDR_STRING_LENGTH_MAX   40
#endif

#define NET_UDP_PBUF_REALLOC          1

#define LWIP_NETIF_API                1

#define TCPIP_MBOX_SIZE               10
#if defined(LWIP_SSL_MQTT) || defined(CONFIG_ATCMD_HTTP_CLIENT)
#define TCPIP_THREAD_STACKSIZE        1024 // 1024
#else
#define TCPIP_THREAD_STACKSIZE        416 //448 // 1024
#endif
// Using the same priority with wifi core task improved iperf throughput
#define TCPIP_THREAD_PRIO             16 + 2 // TASK_PRIO_APP_BASE + 2

#define DEFAULT_THREAD_STACKSIZE      1024
#define DEFAULT_THREAD_PRIO           1
#define DEFAULT_RAW_RECVMBOX_SIZE     (MACIF_RX_BUF_CNT - 3)//32
#define DEFAULT_UDP_RECVMBOX_SIZE     (MACIF_RX_BUF_CNT - 3)//32
#define DEFAULT_TCP_RECVMBOX_SIZE     (MACIF_RX_BUF_CNT - 3)//32
#define DEFAULT_ACCEPTMBOX_SIZE       (MACIF_RX_BUF_CNT - 3)//32

#ifndef LWIP_NETIF_LOOPBACK
#define LWIP_NETIF_LOOPBACK           1
#endif

// 添加广播配置
#ifndef IP_SOF_BROADCAST
#define IP_SOF_BROADCAST              1
#endif

#ifndef IP_SOF_BROADCAST_RECV
#define IP_SOF_BROADCAST_RECV         1
#endif

#define LWIP_HAVE_LOOPIF              1
#define LWIP_LOOPBACK_MAX_PBUFS       0

#define LWIP_CHKSUM_ALGORITHM         3
#define LWIP_CHKSUM                   wifi_ip_chksum
#define LWIP_TCPIP_CORE_LOCKING_INPUT 1

#define LWIP_COMPAT_MUTEX             1
#define LWIP_COMPAT_MUTEX_ALLOWED     1

#define PBUF_LINK_ENCAPSULATION_HLEN  348

#define IP_REASS_MAX_PBUFS            (MACIF_RX_BUF_CNT - 2)

#define MEMP_NUM_NETBUF               34
#define MEMP_NUM_NETCONN              12 // 10 // 8

#define MEMP_NUM_UDP_PCB              16
#define MEMP_NUM_REASSDATA            LWIP_MIN((IP_REASS_MAX_PBUFS), 5)

#define MEMP_NUM_TCP_PCB              6 //5//

#if CFG_TXDESC0 > CFG_TXDESC1
#define MAC_TXQ_DEPTH_0_1             CFG_TXDESC0
#else
#define MAC_TXQ_DEPTH_0_1             CFG_TXDESC1
#endif
#if CFG_TXDESC2 > CFG_TXDESC3
#define MAC_TXQ_DEPTH_2_3             CFG_TXDESC2
#else
#define MAC_TXQ_DEPTH_2_3             CFG_TXDESC3
#endif
#if MAC_TXQ_DEPTH_0_1 > MAC_TXQ_DEPTH_2_3
#define MAC_TXQ_DEPTH                 MAC_TXQ_DEPTH_0_1
#else
#define MAC_TXQ_DEPTH                 MAC_TXQ_DEPTH_2_3
#endif

#define MAC_RXQ_DEPTH                 (MACIF_RX_BUF_CNT + CFG_RXBUF1_MPDU)

#define TCP_MSS                       1460
#define PBUF_POOL_SIZE                (0)
#define LWIP_WND_SCALE                1
#define TCP_RCV_SCALE                 2
#define TCP_SNDLOWAT                  LWIP_MIN(LWIP_MAX(((TCP_SND_BUF)/4),               \
                                                        (2 * TCP_MSS) + 1),              \
                                               (TCP_SND_BUF) - 1)

#if 0
#define TCP_WND                       (2 * MAC_RXQ_DEPTH * TCP_MSS)
#define TCP_QUEUE_OOSEQ               0
#define LWIP_TCP_SACK_OUT             0
#ifdef CFG_SOFTAP
#define TCP_SND_BUF                   (4 * MAC_TXQ_DEPTH * TCP_MSS)
#else
#define TCP_SND_BUF                   (2 * MAC_TXQ_DEPTH * TCP_MSS)
#endif
#define TCP_SND_QUEUELEN              ((4 * TCP_SND_BUF) / TCP_MSS)

#define MEMP_NUM_TCP_SEG              ((4 * TCP_SND_BUF) / TCP_MSS)
#define MEMP_NUM_PBUF                 (TCP_SND_BUF / TCP_MSS + 8)
#define MEM_MIN_TCP                   (2300 + MEMP_NUM_PBUF * (100 + PBUF_LINK_ENCAPSULATION_HLEN))
#endif

#define TCP_WND                       (MAC_RXQ_DEPTH * TCP_MSS)
#define TCP_QUEUE_OOSEQ               1
#define TCP_OOSEQ_MAX_PBUFS           (MACIF_RX_BUF_CNT - 1)
#define LWIP_TCP_SACK_OUT             1

#define TCP_SND_BUF                   (MAC_TXQ_DEPTH * TCP_MSS)  // (4 * MAC_TXQ_DEPTH * TCP_MSS)
#define TCP_SND_QUEUELEN              ((2 * TCP_SND_BUF) / TCP_MSS)  // ((4 * TCP_SND_BUF) / TCP_MSS)

#define MEMP_NUM_TCP_SEG              TCP_SND_QUEUELEN  // ((4 * TCP_SND_BUF) / TCP_MSS)
#define MEMP_NUM_PBUF                 (MAC_TXQ_DEPTH * CFG_TCPTX  + 1)  // (TCP_SND_BUF / TCP_MSS + 8)
#define MEM_MIN_TCP                   (MEMP_NUM_PBUF * (PBUF_LINK_ENCAPSULATION_HLEN + 1600))  // (2300 + MEMP_NUM_PBUF * (100 + PBUF_LINK_ENCAPSULATION_HLEN))

#define MEM_MIN                       MEM_MIN_TCP

#define MEM_ALIGNMENT                 4
#if MEM_MIN > 8192
#define MEM_SIZE                      (MEM_MIN + 512)
#else
#define MEM_SIZE                      (8192 + 512)
#endif

#define LWIP_HOOK_FILENAME            "lwiphooks.h"

#define LWIP_RAW                      1
#define LWIP_MULTICAST_TX_OPTIONS     1

// #define LWIP_TIMEVAL_PRIVATE          0  // use sys/time.h for struct timeval
#define LWIP_PROVIDE_ERRNO            1

#define LWIP_ACD                      0
#define LWIP_DHCP_DOES_ACD_CHECK      0
#define LWIP_DHCP                     1
#define LWIP_DNS                      1
#define LWIP_IGMP                     1
#define LWIP_SO_RCVTIMEO              1

#define LWIP_DHCPD                    1

#define LWIP_PING                     1
#define LWIP_SO_SNDRCVTIMEO_NONSTANDARD     1

#define SO_REUSE                      1

#define LWIP_GRATUITOUS_ARP           1

#define LWIP_STATS                    0
#define LWIP_STATS_DISPLAY            0

#ifdef CONFIG_AZURE_IOT_SUPPORT
#define LWIP_SO_SNDTIMEO              1
/* SNTP definitions */
#define LWIP_SNTP                     1
#define SNTP_SUPPORT                  1
#define SNTP_SERVER_DNS               1
#define SNTP_UPDATE_DELAY             86400

#define LWIP_DHCP_MAX_NTP_SERVERS     4
void sntp_set_system_time(uint32_t sec);
#define SNTP_SET_SYSTEM_TIME( sec )   sntp_set_system_time( sec )

#undef LWIP_SO_SNDRCVTIMEO_NONSTANDARD
#define LWIP_SO_SNDRCVTIMEO_NONSTANDARD     0

#define SYS_TIMER_BUF_FOR_AZURE             10

#endif /* CONFIG_AZURE_IOT_SUPPORT */

#ifdef CONFIG_SNTP
#define LWIP_SNTP                     1
#define SNTP_SERVER_DNS               1
#define SNTP_SUPPRESS_DELAY_CHECK
uint32_t sntp_get_update_intv(void);
#define SNTP_UPDATE_DELAY             (sntp_get_update_intv())
#define LWIP_DHCP_MAX_NTP_SERVERS     4
#define SYS_TIMER_BUF_FOR_SNTP        1
void sntp_set_system_time(uint32_t sec);
#define SNTP_SET_SYSTEM_TIME( sec )   sntp_set_system_time( sec )
#endif

#ifdef CONFIG_ATCMD
#define LWIP_SO_SNDTIMEO              1
#define LWIP_SO_LINGER                1
#define TCP_LISTEN_BACKLOG            1
#endif

#ifdef CONFIG_ATCMD_HTTP_CLIENT
#undef LWIP_TCPIP_CORE_LOCKING_INPUT
#define LWIP_TCPIP_CORE_LOCKING_INPUT 0 //disabled to reduce the depth of RX task stack
#define LWIP_ALTCP                      1
#define LWIP_ALTCP_TLS                  1
#define LWIP_ALTCP_TLS_MBEDTLS          1
#endif

#if defined(CONFIG_ATCMD) || defined(CONFIG_LWIP_SOCKETS_TEST)
#define LWIP_TCP_KEEPALIVE            1
#endif

#ifdef TUYAOS_SUPPORT
#define LWIP_TCPIP_TIMEOUT            1
#define LWIP_SO_SNDTIMEO              1
#define MEM_LIBC_MALLOC               1
#define LWIP_TCP_KEEPALIVE            1
#define TCP_KEEPIDLE_DEFAULT          10000UL
#define TCP_KEEPINTVL_DEFAULT         1000UL
#define TCP_KEEPCNT_DEFAULT           10U
#endif

#ifdef CFG_MATTER
#define LWIP_IPV6                     1
#endif

#if 0
/* Prevent having to link sys_arch.c (we don't test the API layers in unit tests) */
#define NO_SYS                          0
#define LWIP_NETCONN                    0
#define LWIP_SOCKET                     0
#define SYS_LIGHTWEIGHT_PROT            0

#define LWIP_IPV6                       1
#define IPV6_FRAG_COPYHEADER            1
#define LWIP_IPV6_DUP_DETECT_ATTEMPTS   0

/* Turn off checksum verification of fuzzed data */
#define CHECKSUM_CHECK_IP               0
#define CHECKSUM_CHECK_UDP              0
#define CHECKSUM_CHECK_TCP              0
#define CHECKSUM_CHECK_ICMP             0
#define CHECKSUM_CHECK_ICMP6            0

/* Minimal changes to opt.h required for tcp unit tests: */
#define MEM_SIZE                        16000
#define TCP_SND_QUEUELEN                40
#define MEMP_NUM_TCP_SEG                TCP_SND_QUEUELEN
#define TCP_SND_BUF                     (12 * TCP_MSS)
#define TCP_WND                         (10 * TCP_MSS)
#define LWIP_WND_SCALE                  1
#define TCP_RCV_SCALE                   0
#define PBUF_POOL_SIZE                  400 /* pbuf tests need ~200KByte */

/* Minimal changes to opt.h required for etharp unit tests: */
#define ETHARP_SUPPORT_STATIC_ENTRIES   1
#endif

#ifdef CONFIG_MQTT

#undef LWIP_TCPIP_CORE_LOCKING_INPUT
#define LWIP_TCPIP_CORE_LOCKING_INPUT 0 //disabled to reduce the depth of RX task stack

#ifdef LWIP_SSL_MQTT
#define LWIP_ALTCP                      1
#define LWIP_ALTCP_TLS                  1
#define LWIP_ALTCP_TLS_MBEDTLS          1
#endif

#define SYS_TIMER_BUF_FOR_MQTT 10
/**
 * Output ring-buffer size, must be able to fit largest outgoing publish message topic+payloads
 */
#define MQTT_OUTPUT_RINGBUF_SIZE 1024

/**
 * Number of bytes in receive buffer, must be at least the size of the longest incoming topic + 8
 * If one wants to avoid fragmented incoming publish, set length to max incoming topic length + max payload length + 8
 */
#define MQTT_VAR_HEADER_BUFFER_LEN 1024

/**
 * Maximum number of pending subscribe, unsubscribe and publish requests to server .
 */
#define MQTT_REQ_MAX_IN_FLIGHT 4

/**
 * Seconds between each cyclic timer call.
 */
#define MQTT_CYCLIC_TIMER_INTERVAL 5

/**
 * Publish, subscribe and unsubscribe request timeout in seconds.
 */
#define MQTT_REQ_TIMEOUT 30

/**
 * Seconds for MQTT connect response timeout after sending connect request
 */
#define MQTT_CONNECT_TIMOUT 100

//#define MEMP_NUM_SYS_TIMEOUT (LWIP_NUM_SYS_TIMEOUT_INTERNAL + SYS_TIMER_BUF_FOR_MQTT)

#endif

#ifndef SYS_TIMER_BUF_FOR_AZURE
#define SYS_TIMER_BUF_FOR_AZURE     0
#endif

#ifndef SYS_TIMER_BUF_FOR_MQTT
#define SYS_TIMER_BUF_FOR_MQTT      0
#endif

#ifndef LWIP_IPV6_DHCP6
#define LWIP_IPV6_DHCP6             0
#endif

#ifndef SYS_TIMER_BUF_FOR_SNTP
#define SYS_TIMER_BUF_FOR_SNTP      0
#endif

#ifdef CONFIG_SOFTAP_PROVISIONING
#define LWIP_HTTPD_SUPPORT_POST         1
#define HTTPD_FSDATA_FILE               "httpd_resource.c"
#endif

#define MEMP_NUM_SYS_TIMEOUT        ( LWIP_NUM_SYS_TIMEOUT_INTERNAL \
                                    + SYS_TIMER_BUF_FOR_AZURE \
                                    + SYS_TIMER_BUF_FOR_MQTT \
                                    + SYS_TIMER_BUF_FOR_SNTP \
                                    + LWIP_IPV6_DHCP6 )

#ifdef CFG_SOFTAP_MANY_CLIENTS
#undef MEMP_NUM_NETCONN
#define MEMP_NUM_NETCONN            (CFG_STA_NUM + 1 + 2)
#undef MEMP_NUM_TCP_PCB
#define MEMP_NUM_TCP_PCB            CFG_STA_NUM
#endif /* CFG_SOFTAP_MANY_CLIENTS */

extern uint16_t wifi_ip_chksum(const void *dataptr, int len);
#ifdef __cplusplus
 }
#endif
#endif /* LWIP_HDR_LWIPOPTS_H__ */
