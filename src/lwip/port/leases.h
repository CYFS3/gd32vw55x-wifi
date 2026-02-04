/*!
    \file    leases.h
    \brief   Declaration related to the dhcpd lease.

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

#ifndef LEASES_H_
#define LEASES_H_

#define LEASE_H_
#include "lwip/inet.h"
struct dhcpOfferedAddr
{
    //struct dhcpOfferedAddr *next;
    uint8_t chaddr[16];
    struct in_addr yiaddr;
    uint32_t expires;    /* all variable is host oder except for address */
    unsigned char *hostname;
    uint8_t flag;    /* static lease?*/
};

/* flag */
#define DYNAMICL                0x0
#define STATICL                 0x1
#define DISABLED                0x2
#define RESERVED                0x4
#define DELETED                 0x8

#define SHOW_DYNAMIC            0x1
#define SHOW_STATIC             0x2
#define SHOW_DYNAMIC_STATIC     0x3
#define SHOW_ALL                0x8

#endif /* LEASES_H_ */
