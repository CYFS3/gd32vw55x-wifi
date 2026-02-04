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
 * Author: Adam Dunkels <adam@sics.se>
 *
 */
#ifndef __ARCH_SYS_ARCH_H__
#define __ARCH_SYS_ARCH_H__

/*
 * 注意：不要在这里包含 wrapper_os.h，因为会导致循环包含：
 * sys_arch.h -> wrapper_os.h -> lwip/sys.h -> sys_arch.h
 *
 * wrapper_os.h 会在包含 lwip/sys.h 之后包含此文件，
 * 所以这里只定义基础类型，wrapper_os 的函数声明在 wrapper_os.h 中。
 */

/* 直接定义 RTOS 类型（与 wrapper_os.h 保持一致） */
typedef void *os_sema_t;
typedef void *os_mutex_t;
typedef void *os_task_t;
typedef void *os_queue_t;

/* 添加函数声明 */
void sys_enter_critical(void);
void sys_exit_critical(void);

#define SYS_ARCH_DECL_PROTECT(lev)
#define SYS_ARCH_PROTECT(lev)       sys_enter_critical()
#define SYS_ARCH_UNPROTECT(lev)     sys_exit_critical()

/* lwip RTOS 适配层类型 */
typedef os_queue_t sys_mbox_t;
typedef os_sema_t  sys_sem_t;
typedef os_mutex_t sys_mutex_t;
typedef os_task_t  sys_thread_t;

/* 无效值定义 */
#define SYS_MBOX_NULL       ( ( sys_mbox_t ) NULL )
#define SYS_SEM_NULL        ( ( sys_sem_t ) NULL )

/* 有效性检查宏 */
#define sys_mbox_valid( x )       ( ( ( *x ) == NULL) ? 0 : 1 )
#define sys_mbox_set_invalid( x ) ( ( *x ) = NULL )
#define sys_sem_valid( x )        ( ( ( *x ) == NULL) ? 0 : 1 )
#define sys_sem_set_invalid( x )  ( ( *x ) = NULL )

#endif /* __ARCH_SYS_ARCH_H__ */