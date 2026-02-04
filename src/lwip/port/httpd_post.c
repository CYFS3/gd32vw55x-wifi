/**
 * @file
 * HTTPD example for simple POST
 */

 /*
 * Copyright (c) 2017 Simon Goldschmidt
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
 * Author: Simon Goldschmidt <goldsimon@gmx.de>
 *
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

#include "lwip/apps/httpd.h"
#include "lwip/def.h"
#include "lwip/mem.h"

#include <stdio.h>
#include <string.h>

#include "wifi_softap_provisioning.h"
#if LWIP_HTTPD_SUPPORT_POST

#define SSID_BUFSIZE 32
#define PASS_BUFSIZE 64

static void *current_connection;
static void *valid_connection;

err_t
httpd_post_begin(void *connection, const char *uri, const char *http_request,
                 u16_t http_request_len, int content_len, char *response_uri,
                 u16_t response_uri_len, u8_t *post_auto_wnd)
{
  LWIP_UNUSED_ARG(connection);
  LWIP_UNUSED_ARG(http_request);
  LWIP_UNUSED_ARG(http_request_len);
  LWIP_UNUSED_ARG(content_len);
  LWIP_UNUSED_ARG(post_auto_wnd);

  if (!memcmp(uri, "/portal.html", 13)) {
    if (current_connection != connection) {
      current_connection = connection;
      valid_connection = NULL;
      /* default page is "login failed" */
      snprintf(response_uri, response_uri_len, "/save_failed.html");
      /* e.g. for large uploads to slow flash over a fast connection, you should
         manually update the rx window. That way, a sender can only send a full
         tcp window at a time. If this is required, set 'post_aut_wnd' to 0.
         We do not need to throttle upload speed here, so: */
      *post_auto_wnd = 1;
      return ERR_OK;
    }
  }
  return ERR_VAL;
}

err_t
httpd_post_receive_data(void *connection, struct pbuf *p)
{
  if (current_connection == connection) {
    u16_t token_ssid = pbuf_memfind(p, "ssid=", 5, 0);
    u16_t token_pass = pbuf_memfind(p, "password=", 9, 0);
    if ((token_ssid != 0xFFFF) && (token_pass != 0xFFFF)) {
      u16_t value_ssid = token_ssid + 5;
      u16_t value_pass = token_pass + 9;
      u16_t len_ssid = 0;
      u16_t len_pass = 0;
      u16_t tmp;
      /* find ssid len */
      tmp = pbuf_memfind(p, "&", 1, value_ssid);
      if (tmp != 0xFFFF) {
        len_ssid = tmp - value_ssid;
      } else {
        len_ssid = p->tot_len - value_ssid;
      }
      /* find pass len */
      tmp = pbuf_memfind(p, "&", 1, value_pass);
      if (tmp != 0xFFFF) {
        len_pass = tmp - value_pass;
      } else {
        len_pass = p->tot_len - value_pass;
      }
      if ((len_ssid > 0) && (len_ssid < SSID_BUFSIZE) &&
          (len_pass > 0) && (len_pass < PASS_BUFSIZE)) {
        /* provide contiguous storage if p is a chained pbuf */
        uint8_t buf_ssid[SSID_BUFSIZE];
        uint8_t buf_pass[PASS_BUFSIZE];
        uint8_t *ssid = (uint8_t *)pbuf_get_contiguous(p, buf_ssid, sizeof(buf_ssid), len_ssid, value_ssid);
        uint8_t *pass = (uint8_t *)pbuf_get_contiguous(p, buf_pass, sizeof(buf_pass), len_pass, value_pass);
        if (ssid && pass) {
          ssid[len_ssid] = 0;
          pass[len_pass] = 0;
          if (wifi_softap_provisioning_config(ssid, pass) < 0)
            return ERR_VAL;
          valid_connection = connection;
        }
      }
    }
    /* not returning ERR_OK aborts the connection, so return ERR_OK unless the
       conenction is unknown */
    return ERR_OK;
  }
  return ERR_VAL;
}

void
httpd_post_finished(void *connection, char *response_uri, u16_t response_uri_len)
{
  /* default page is "login failed" */
  snprintf(response_uri, response_uri_len, "/save_failed.html");
  if (current_connection == connection) {
    if (valid_connection == connection) {
      /* login succeeded */
      snprintf(response_uri, response_uri_len, "/save_ok.html");
    }
    current_connection = NULL;
    valid_connection = NULL;
  }
}

#endif /*LWIP_HTTPD_SUPPORT_POST*/