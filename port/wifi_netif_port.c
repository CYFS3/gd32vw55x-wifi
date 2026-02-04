/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024-12-25     RT-Thread    the first version
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <lwip/netif.h>
#include <lwip/netifapi.h>
#include <lwip/ip_addr.h>
#include <netdev.h>

#ifdef PKG_USING_GD32VW55X_WIFI

#include "wifi_management.h"
#include "wifi_netif.h"

#define DBG_TAG "wifi.port.netif"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#if defined(RT_USING_FINSH) && defined(RT_LWIP_USING_PING)
#include <lwip/inet.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#endif

/* RT-Thread netdev 对接 GD32 WIFI 网络接口 */

/**
 * @brief 将 GD32 WIFI 网络接口适配到 RT-Thread netdev 框架
 *
 * 本文件需要实现以下功能:
 * 1. 创建 netdev 设备并注册
 * 2. 对接 LwIP netif 与 RT-Thread netdev
 * 3. 处理 WIFI 状态变化回调
 * 4. 实现网络接口的启动/停止控制
 */

static struct netdev *wifi_netdev = RT_NULL;

#ifndef ip_2_ip4
#define ip_2_ip4(ipaddr) (ipaddr)
#endif

void wifi_status_changed_callback(uint32_t event, void *data);

static void wifi_netdev_sync_flags(struct netdev *netdev, struct netif *lwip_netif)
{
    if (lwip_netif->flags & NETIF_FLAG_BROADCAST)
    {
        netdev->flags |= NETDEV_FLAG_BROADCAST;
    }
    if (lwip_netif->flags & NETIF_FLAG_ETHARP)
    {
        netdev->flags |= NETDEV_FLAG_ETHARP;
    }
    if (lwip_netif->flags & NETIF_FLAG_IGMP)
    {
        netdev->flags |= NETDEV_FLAG_IGMP;
    }
#if LWIP_VERSION_MAJOR >= 2U
    if (lwip_netif->flags & NETIF_FLAG_MLD6)
    {
        netdev->flags |= NETDEV_FLAG_MLD6;
    }
#endif
}

static void wifi_netdev_event_connect(void *eloop_data, void *user_ctx)
{
    RT_UNUSED(eloop_data);
    wifi_status_changed_callback(WIFI_MGMT_EVENT_CONNECT_SUCCESS, user_ctx);
}

static void wifi_netdev_event_disconnect(void *eloop_data, void *user_ctx)
{
    RT_UNUSED(eloop_data);
    wifi_status_changed_callback(WIFI_MGMT_EVENT_DISCONNECT, user_ctx);
}

static void wifi_netdev_event_dhcp(void *eloop_data, void *user_ctx)
{
    RT_UNUSED(eloop_data);
    wifi_status_changed_callback(WIFI_MGMT_EVENT_DHCP_SUCCESS, user_ctx);
}

static void wifi_netdev_register_events(struct netdev *netdev, int vif_idx)
{
    if (eloop_event_register(ELOOP_EVENT_ID(vif_idx, WIFI_MGMT_EVENT_CONNECT_SUCCESS),
                             wifi_netdev_event_connect, RT_NULL, netdev))
    {
        LOG_W("WIFI netdev register connect event failed");
    }
    if (eloop_event_register(ELOOP_EVENT_ID(vif_idx, WIFI_MGMT_EVENT_DISCONNECT),
                             wifi_netdev_event_disconnect, RT_NULL, netdev))
    {
        LOG_W("WIFI netdev register disconnect event failed");
    }
    if (eloop_event_register(ELOOP_EVENT_ID(vif_idx, WIFI_MGMT_EVENT_DHCP_SUCCESS),
                             wifi_netdev_event_dhcp, RT_NULL, netdev))
    {
        LOG_W("WIFI netdev register DHCP event failed");
    }
}

/**
 * @brief 网络接口启动回调
 */
static int wifi_netdev_set_up(struct netdev *netdev)
{
    struct netif *lwip_netif;
    int ret;

    LOG_I("WIFI netdev set up");

    ret = wifi_netlink_wifi_open();
    if (ret)
    {
        LOG_E("WIFI open failed: %d", ret);
        return -RT_ERROR;
    }

    lwip_netif = (struct netif *)netdev->user_data;
    if (lwip_netif)
    {
        net_if_up(lwip_netif);
    }

    return RT_EOK;
}

/**
 * @brief 网络接口关闭回调
 */
static int wifi_netdev_set_down(struct netdev *netdev)
{
    struct netif *lwip_netif;

    LOG_I("WIFI netdev set down");

    lwip_netif = (struct netif *)netdev->user_data;
    if (lwip_netif)
    {
        net_if_down(lwip_netif);
    }

    wifi_netlink_wifi_close();

    return RT_EOK;
}

/**
 * @brief 设置 DHCP 状态
 */
static int wifi_netdev_set_dhcp(struct netdev *netdev, rt_bool_t is_enabled)
{
    struct netif *lwip_netif;

    LOG_I("WIFI set DHCP: %s", is_enabled ? "enabled" : "disabled");

    lwip_netif = (struct netif *)netdev->user_data;
    if (lwip_netif == RT_NULL)
    {
        return -RT_ERROR;
    }

    netdev_low_level_set_dhcp_status(netdev, is_enabled);

    if (is_enabled)
    {
        net_if_use_static_ip(false);
        if (net_dhcp_start(lwip_netif))
        {
            LOG_E("WIFI DHCP start failed");
            return -RT_ERROR;
        }
    }
    else
    {
        net_dhcp_stop(lwip_netif);
    }

    return RT_EOK;
}

/**
 * @brief 设置 IP 地址
 */
static int wifi_netdev_set_addr_info(struct netdev *netdev, ip_addr_t *ip_addr,
                                      ip_addr_t *netmask, ip_addr_t *gw)
{
    struct netif *lwip_netif;
    uint32_t ip = 0, mask = 0, gateway = 0;

    LOG_I("WIFI set addr info");

    if ((ip_addr == RT_NULL) && (netmask == RT_NULL) && (gw == RT_NULL))
    {
        return RT_EOK;
    }

    lwip_netif = (struct netif *)netdev->user_data;
    if (lwip_netif == RT_NULL)
    {
        return -RT_ERROR;
    }

    net_if_use_static_ip(true);
    net_dhcp_stop(lwip_netif);
    netdev_low_level_set_dhcp_status(netdev, RT_FALSE);

    net_if_get_ip(lwip_netif, &ip, &mask, &gateway);

    if (ip_addr)
    {
        ip = ip_2_ip4(ip_addr)->addr;
    }
    if (netmask)
    {
        mask = ip_2_ip4(netmask)->addr;
    }
    if (gw)
    {
        gateway = ip_2_ip4(gw)->addr;
    }

    net_if_set_ip(lwip_netif, ip, mask, gateway);

    return RT_EOK;
}

/**
 * @brief 设置 DNS 服务器
 */
static int wifi_netdev_set_dns_server(struct netdev *netdev, uint8_t dns_num, ip_addr_t *dns_server)
{
    LOG_I("WIFI set DNS server");

    if (netdev == RT_NULL || dns_server == RT_NULL)
    {
        return -RT_ERROR;
    }

    if (dns_num >= NETDEV_DNS_SERVERS_NUM)
    {
        return -RT_ERROR;
    }

    netdev_low_level_set_dns_server(netdev, dns_num, dns_server);

    return RT_EOK;
}

static int wifi_dummy_set_dns_server(struct netdev *netdev, uint8_t dns_num, ip_addr_t *dns_server)
{
    if (netdev == RT_NULL || dns_server == RT_NULL)
    {
        return -RT_ERROR;
    }

    if (dns_num >= NETDEV_DNS_SERVERS_NUM)
    {
        return -RT_ERROR;
    }

    netdev_low_level_set_dns_server(netdev, dns_num, dns_server);

    return RT_EOK;
}

#if defined(RT_USING_FINSH) && defined(RT_LWIP_USING_PING)
#ifndef PING_DATA_SIZE
#define PING_DATA_SIZE 32
#endif

extern int lwip_ping_recv(int s, int *ttl);
extern err_t lwip_ping_send(int s, ip_addr_t *addr, int size);
#endif

/**
 * @brief PING 测试
 */
#ifdef RT_USING_FINSH
static int wifi_netdev_ping(struct netdev *netdev, const char *host,
                            size_t data_len, uint32_t timeout, struct netdev_ping_resp *ping_resp, rt_bool_t isbind)
{
#ifdef RT_LWIP_USING_PING
    int s, ttl, recv_len, result = 0;
    int elapsed_time;
    rt_tick_t recv_start_tick;
#if LWIP_VERSION_MAJOR == 1U /* v1.x */
    int recv_timeout = timeout;
#else /* >= v2.x */
#if LWIP_SO_SNDRCVTIMEO_NONSTANDARD
    int recv_timeout = (int)timeout;
#else
    struct timeval recv_timeout = { timeout / 1000UL, timeout % 1000UL * 1000 };
#endif
#endif
    ip_addr_t target_addr;
    struct addrinfo hint, *res = RT_NULL;
    struct sockaddr_in *h = RT_NULL;
    struct in_addr ina;
    struct sockaddr_in local;

    RT_ASSERT(netdev);
    RT_ASSERT(host);
    RT_ASSERT(ping_resp);

    LOG_I("wifi_netdev_ping netdev=%s host=%s len=%u timeout=%u isbind=%d",
          netdev ? netdev->name : "null",
          host ? host : "null",
          (unsigned)data_len, (unsigned)timeout, isbind);

    rt_memset(&hint, 0x00, sizeof(hint));
    /* convert URL to IP */
    if (lwip_getaddrinfo(host, RT_NULL, &hint, &res) != 0)
    {
        return -RT_ERROR;
    }
    SMEMCPY(&h, &res->ai_addr, sizeof(struct sockaddr_in *));
    SMEMCPY(&ina, &h->sin_addr, sizeof(ina));
    lwip_freeaddrinfo(res);
    if (inet_aton(inet_ntoa(ina), &target_addr) == 0)
    {
        return -RT_ERROR;
    }
    SMEMCPY(&(ping_resp->ip_addr), &target_addr, sizeof(ip_addr_t));
    if (data_len == 0)
    {
        data_len = PING_DATA_SIZE;
    }

    /* new a socket */
    if ((s = lwip_socket(AF_INET, SOCK_RAW, IP_PROTO_ICMP)) < 0)
    {
        return -RT_ERROR;
    }

    local.sin_len = sizeof(local);
    local.sin_family = AF_INET;
    local.sin_port = 0;
#ifndef NETDEV_USING_IPV6
    local.sin_addr.s_addr = (netdev->ip_addr.addr);
#else
    local.sin_addr.s_addr = (netdev->ip_addr.u_addr.ip4.addr);
#endif
    if (isbind)
    {
        if (lwip_bind(s, (struct sockaddr *)&local, sizeof(struct sockaddr_in)) < 0)
        {
            LOG_W("wifi_netdev_ping bind failed errno=%d", rt_get_errno());
        }
    }

    if (lwip_setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout)) < 0)
    {
        LOG_W("wifi_netdev_ping setsockopt failed errno=%d", rt_get_errno());
    }

    if (lwip_ping_send(s, &target_addr, (int)data_len) == ERR_OK)
    {
        recv_start_tick = rt_tick_get();
        recv_len = lwip_ping_recv(s, &ttl);
        if (recv_len < 0)
        {
            LOG_I("wifi_netdev_ping recv_len=%d errno=%d", recv_len, rt_get_errno());
        }
        if (recv_len > 0)
        {
            elapsed_time = (rt_tick_get() - recv_start_tick) * 1000UL / RT_TICK_PER_SECOND;
            ping_resp->data_len = (uint16_t)recv_len;
            ping_resp->ttl = ttl;
            ping_resp->ticks = elapsed_time;
        }
        else
        {
            result = -RT_ETIMEOUT;
            goto __exit;
        }
    }
    else
    {
        result = -RT_ETIMEOUT;
        goto __exit;
    }

__exit:
    lwip_close(s);

    if (result == RT_EOK && ping_resp->data_len == 0)
    {
        result = -RT_ETIMEOUT;
    }

    return result;
#else
    RT_UNUSED(netdev);
    RT_UNUSED(host);
    RT_UNUSED(data_len);
    RT_UNUSED(timeout);
    RT_UNUSED(ping_resp);
    RT_UNUSED(isbind);
    return -RT_ERROR;
#endif /* RT_LWIP_USING_PING */
}
#endif

/**
 * @brief 设置默认网络接口
 */
static int wifi_netdev_set_default(struct netdev *netdev)
{
    struct netif *lwip_netif;

    LOG_I("WIFI set default");

    lwip_netif = (struct netif *)netdev->user_data;
    if (lwip_netif == RT_NULL)
    {
        return -RT_ERROR;
    }

    net_if_set_default(lwip_netif);

    return RT_EOK;
}

/* netdev 操作函数表 */
static const struct netdev_ops wifi_netdev_ops =
{
    .set_up         = wifi_netdev_set_up,
    .set_down       = wifi_netdev_set_down,
    .set_dhcp       = wifi_netdev_set_dhcp,
    .set_addr_info  = wifi_netdev_set_addr_info,
    .set_dns_server = wifi_netdev_set_dns_server,
#if defined(RT_USING_FINSH) && defined(RT_LWIP_USING_PING)
    .ping           = wifi_netdev_ping,
#endif
    .set_default    = wifi_netdev_set_default,
};

static const struct netdev_ops wifi_dummy_netdev_ops =
{
    .set_dns_server = wifi_dummy_set_dns_server,
};

/**
 * @brief WIFI 状态变化回调
 *
 * @param event WIFI 事件类型
 * @param data 事件数据
 */
void wifi_status_changed_callback(uint32_t event, void *data)
{
    struct netdev *netdev = (struct netdev *)data;
    struct netif *lwip_netif = RT_NULL;

    if (netdev == RT_NULL)
    {
        netdev = wifi_netdev;
    }

    if (netdev != RT_NULL)
    {
        lwip_netif = (struct netif *)netdev->user_data;
    }

    switch (event)
    {
    case WIFI_MGMT_EVENT_CONNECT_SUCCESS:
        LOG_I("WIFI connected");
        if (lwip_netif)
        {
            netifapi_netif_set_link_up(lwip_netif);
        }
        if (netdev)
        {
            netdev_low_level_set_link_status(netdev, RT_TRUE);
        }
        break;

    case WIFI_MGMT_EVENT_DISCONNECT:
        LOG_I("WIFI disconnected");
        if (lwip_netif)
        {
            netifapi_netif_set_link_down(lwip_netif);
        }
        if (netdev)
        {
            netdev_low_level_set_link_status(netdev, RT_FALSE);
            netdev_low_level_set_internet_status(netdev, RT_FALSE);
        }
        break;

    case WIFI_MGMT_EVENT_DHCP_SUCCESS:
        LOG_I("WIFI got IP");
        if (netdev)
        {
            uint32_t ip = 0, mask = 0, gw = 0;
            ip_addr_t addr;
            ip_addr_t netmask;
            ip_addr_t gateway;
            uint32_t dns;

            if (lwip_netif)
            {
                net_if_get_ip(lwip_netif, &ip, &mask, &gw);

#if LWIP_IPV6
                ip_addr_set_ip4_u32_val(addr, ip);
                ip_addr_set_ip4_u32_val(netmask, mask);
                ip_addr_set_ip4_u32_val(gateway, gw);
#else
                ip_addr_set_ip4_u32(&addr, ip);
                ip_addr_set_ip4_u32(&netmask, mask);
                ip_addr_set_ip4_u32(&gateway, gw);
#endif
                netdev_low_level_set_ipaddr(netdev, &addr);
                netdev_low_level_set_netmask(netdev, &netmask);
                netdev_low_level_set_gw(netdev, &gateway);
            }

            if (net_get_dns(&dns) == 0)
            {
                ip_addr_t dns_addr;
#if LWIP_IPV6
                ip_addr_set_ip4_u32_val(dns_addr, dns);
#else
                ip_addr_set_ip4_u32(&dns_addr, dns);
#endif
                netdev_low_level_set_dns_server(netdev, 0, &dns_addr);
            }

            netdev_low_level_set_dhcp_status(netdev, RT_TRUE);
            netdev_low_level_set_internet_status(netdev, RT_TRUE);
            /* Some connect handlers may be unregistered by upper layer; ensure link is up once IP is ready. */
            if (lwip_netif)
            {
                netifapi_netif_set_link_up(lwip_netif);
            }
            netdev_low_level_set_link_status(netdev, RT_TRUE);
        }
        break;

    default:
        break;
    }
}

/**
 * @brief 注册 WIFI netdev 设备
 */
int wifi_netdev_register(void)
{
    int vif_idx;
    struct netif *n;
    for (vif_idx = 0; vif_idx < CFG_VIF_NUM; vif_idx++)
    {
        struct netif *lwip_netif = &wifi_vif_tab[vif_idx].net_if;
        struct netdev *netdev;
        char name[NETIF_NAMESIZE] = {0};

        if (net_if_get_name(lwip_netif, name, sizeof(name)) < 0)
        {
            continue;
        }

        netdev = netdev_get_by_name(name);
        if (netdev == RT_NULL)
        {
            netdev = (struct netdev *)rt_calloc(1, sizeof(struct netdev));
            if (netdev == RT_NULL)
            {
                return -RT_ENOMEM;
            }

#ifdef SAL_USING_LWIP
            extern int sal_lwip_netdev_set_pf_info(struct netdev *netdev);
            sal_lwip_netdev_set_pf_info(netdev);
#endif

            netdev_register(netdev, name, (void *)lwip_netif);
        }
        else
        {
            netdev->user_data = (void *)lwip_netif;
        }

        /* 设置 netdev 操作函数 */
        netdev->ops = &wifi_netdev_ops;

        /* 同步 netif 基本信息 */
        netdev->mtu = lwip_netif->mtu;
        netdev->hwaddr_len = lwip_netif->hwaddr_len;
        rt_memcpy(netdev->hwaddr, lwip_netif->hwaddr, lwip_netif->hwaddr_len);
        netdev->ip_addr = lwip_netif->ip_addr;
        netdev->gw = lwip_netif->gw;
        netdev->netmask = lwip_netif->netmask;
        wifi_netdev_sync_flags(netdev, lwip_netif);

#if LWIP_DHCP
        netdev_low_level_set_dhcp_status(netdev, RT_TRUE);
#else
        netdev_low_level_set_dhcp_status(netdev, RT_FALSE);
#endif

#ifdef NETDEV_USING_LINK_STATUS_CALLBACK
        extern void netdev_status_change(struct netdev *netdev, enum netdev_cb_type type);
        netdev_set_status_callback(netdev, netdev_status_change);
#endif

        wifi_netdev_register_events(netdev, vif_idx);

        if (vif_idx == WIFI_VIF_INDEX_DEFAULT)
        {
            wifi_netdev = netdev;
            netdev_set_default(netdev);
        }
    }

    /* Ensure all lwIP netifs have netdev objects to avoid NULL access in dns_setserver */
    for (n = netif_list; n != RT_NULL; n = n->next)
    {
        struct netdev *netdev = netdev_get_by_name(n->name);

        if (netdev == RT_NULL)
        {
            char name[NETIF_NAMESIZE] = {0};

            rt_strncpy(name, n->name, NETIF_NAMESIZE);
            netdev = (struct netdev *)rt_calloc(1, sizeof(struct netdev));
            if (netdev == RT_NULL)
            {
                return -RT_ENOMEM;
            }

            netdev_register(netdev, name, (void *)n);
            netdev->ops = &wifi_dummy_netdev_ops;
            netdev->mtu = n->mtu;
            netdev->hwaddr_len = n->hwaddr_len;
            rt_memcpy(netdev->hwaddr, n->hwaddr, n->hwaddr_len);
        }
    }

    LOG_I("WIFI netdev registered");

    return RT_EOK;
}

#endif /* PKG_USING_GD32VW55X_WIFI */
