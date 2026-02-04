/*!
    \file    dhcpd.c
    \brief   Config declaration for dhcpd.

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

#ifndef DHCPD_CONF_H_
#define DHCPD_CONF_H_

#include "wlan_config.h"

#ifdef CFG_STA_NUM
#define DHCPD_MAX_LEASES        CFG_STA_NUM + 1
#else
#define DHCPD_MAX_LEASES        5
#endif

#define DEFAULT_LEASE_TIME      3600
#define DEFAULT_AUTO_TIME       3
#define DEFAULT_CONFLICT_TIME   3600
#define DEFAULT_DECLINE_TIME    3600
#define DEFAULT_MIN_LEASE_TIME  60
#define DEFAULT_SNAME           "gigadevice"
#define DEFAULT_BOOT_FILE       ""
#define DEFAULT_DOMAIN          "www.gigadevice.com.cn"

#endif /* DHCPD_CONF_H_ */
