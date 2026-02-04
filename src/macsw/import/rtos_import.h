/*!
    \file    rtos_import.h
    \brief   RTOS functions import for WIFI lib.

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

#ifndef __RTOS_IMPORT_H_
#define __RTOS_IMPORT_H_

/* Include rtconfig.h to get RT_USING_LWIP definition */
#include "rtconfig.h"

/* Force include LwIP sys.h first to avoid conflicts */
#ifdef RT_USING_LWIP
#include "lwip/sys.h"
#endif

#include <stdint.h>

#if 0
#if defined ( __ICCARM__ )
#define __inline__              inline
#define __inline                inline
#define __inline_definition
#elif defined ( __CC_ARM   )
#define __inline__              __inline
#define inline                  __inline
#define __inline_definition
#elif defined   (  __GNUC__  )
#define __inline__              inline
#define __inline                inline
#define __inline_definition     inline
#endif
#endif

#ifndef OS_OK
#define OS_OK            0
#define OS_ERROR        -1
#define OS_TIMEOUT      -2
#endif

/**
 * @brief semaphore/mutex/lock struct
 */
typedef void *_sema;
typedef void *_mutex;
typedef void *_lock;
typedef void *_queue;

/**
 * @brief task struct which encapsulate OS Task
 */
typedef void *_task;
typedef void (*wlan_task_func_t)(void *argv);

/**
 * @brief timer struct which encapsulate OS timer
 */
typedef void *_timer;
typedef void (*wlan_timer_func_t)(void *p_tmr, void *p_arg);

extern void *sys_malloc(size_t size);
extern void sys_mfree(void *ptr);
extern void sys_memcpy(void *des, const void *src, uint32_t n);
extern int32_t sys_memcmp(const void *buf1, const void *buf2, uint32_t count);
extern void sys_memset(void *s, uint8_t c, uint32_t count);
extern void sys_memmove(void *des, const void *src, uint32_t n);

extern int32_t sys_sema_init(_sema *sema, int32_t init_val);
extern int32_t sys_sema_init_ext(_sema *sema, int max_count, int init_count);
extern void sys_sema_free(_sema *sema);
extern void sys_sema_up(_sema *sema);
extern void sys_sema_up_from_isr(_sema *sema);
extern int32_t sys_sema_down(_sema *sema, uint32_t timeout_ms);

extern int sys_mutex_init(_mutex *mutex);
extern void sys_mutex_free(_mutex *mutex);
extern int32_t sys_mutex_get(_mutex *mutex);
extern void sys_mutex_put(_mutex *mutex);

extern uint32_t sys_os_now(bool isr);
extern void sys_ms_sleep(int ms);
extern void sys_us_delay(uint32_t us);
extern void sys_sched_lock(void);
extern void sys_sched_unlock(void);

extern void sys_timer_init(_timer *timer, const uint8_t *name, uint32_t delay, uint8_t periodic,
                    wlan_timer_func_t func, void *arg);
extern void sys_timer_delete(_timer *timer);
extern void sys_timer_start_ext(_timer *timer, uint32_t delay, uint8_t from_isr);
extern uint8_t sys_timer_stop(_timer *timer, uint8_t from_isr);
extern uint8_t sys_timer_pending(_timer *timer);

extern int32_t sys_random_bytes_get(void *dst, uint32_t size);

extern uint16_t sys_heap_block_size(void);
extern int32_t sys_free_heap_size(void);

extern void *sys_task_create(void *static_tcb, const uint8_t *name, uint32_t *stack_base, uint32_t stack_size,
                    uint32_t queue_size, uint32_t queue_item_size, uint32_t priority, wlan_task_func_t func, void *ctx);
extern void sys_task_delete(void *task);
extern int32_t sys_task_wait(uint32_t timeout_ms, void *msg_ptr);
extern int32_t sys_task_post(void *receiver_task, void *msg_ptr, uint8_t from_isr);
extern int32_t sys_task_msg_num(void *task, uint8_t from_isr);
extern uint32_t sys_stack_free_get(void *task);

extern int sys_queue_write(_queue *queue, void *msg, int timeout, bool isr);
extern int sys_queue_read(_queue *queue, void *msg, int timeout, bool isr);
extern int32_t sys_queue_init(_queue *queue, int32_t queue_size, uint32_t item_size);
extern void sys_queue_free(_queue *queue);
extern bool sys_queue_is_empty(_queue *queue);
extern int sys_queue_cnt(_queue *queue);

extern void sys_task_notify(void *task, bool isr);
extern int sys_task_wait_notification(int timeout);
extern _task sys_current_task_handle_get();
extern uint32_t sys_in_critical(void);
extern void sys_enter_critical(void);
extern void sys_exit_critical(void);
extern uint8_t sys_ps_get(void);
extern void sys_add_heap_region(uint32_t ucStartAddress, uint32_t xSizeInBytes);
extern uint8_t sys_task_exist(const uint8_t *name);

#endif /* __RTOS_IMPORT_H_ */
