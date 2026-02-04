/*!
    \file    wrapper_rtthread.c
    \brief   RT-Thread wrapper for GD32VW55x SDK

    \version 2024-08-19, V1.0.0, firmware for GD32VW55x
*/

/*
    Copyright (c) 2024, GigaDevice Semiconductor Inc.

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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

// 定义内核源码标志，以访问调度器内部API
#define __RT_KERNEL_SOURCE__

#include "wrapper_rtthread.h"
#include "board.h"
#include "boot.h"
#include <rtsched.h>

#include "rtos_import.h"

#include "systime.h"
#include "dlist.h"

#ifdef RT_USING_PM
#include "drivers/pm.h"
#endif

/* Define OS_TASK_PRIO_MAX for priority conversion */
#ifndef OS_TASK_PRIO_MAX
#define OS_TASK_PRIO_MAX    RT_THREAD_PRIORITY_MAX
#endif

uint8_t sys_ps_mode = SYS_PS_OFF;
static uint32_t task_id = 2; //0: idle thread, 1: timer thread, -1: invalid

dlist_t added_heaps;

/* Use board.h HEAP definitions if available, otherwise define from linker script */
#if !defined(HEAP_BEGIN) || !defined(HEAP_END)
#ifdef HEAP_BEGIN
#undef HEAP_BEGIN
#endif
#ifdef HEAP_END
#undef HEAP_END
#endif
extern int _end;
extern int _heap_end;
#define HEAP_BEGIN      ((void *)&_end)
#define HEAP_END        ((void *)&_heap_end)
#endif

#define TX_RUNNING_CHAR     ( 'X' )
#define TX_BLOCKED_CHAR     ( 'B' )
#define TX_READY_CHAR       ( 'R' )
#define TX_DELETED_CHAR     ( 'D' )
#define TX_SUSPENDED_CHAR   ( 'S' )


void rt_hw_console_output(const char *str)
{
#ifdef LOG_UART
    log_uart_put_data(str, strlen(str));
#endif
}

__INLINE rt_int32_t sys_timeout_2_tickcount(int timeout_ms)
{
    rt_int32_t timeout_tick = RT_WAITING_NO;


    if (timeout_ms < 0) {
        timeout_tick = RT_WAITING_FOREVER;
    } else if (timeout_ms != 0) {
        timeout_tick = timeout_ms/OS_MS_PER_TICK;
        if (timeout_tick == 0) {
            timeout_tick = 1;
        }
    }

    return timeout_tick;
}

#ifndef CFG_HEAP_MEM_CHECK
/***************** heap management implementation *****************/
/*!
    \brief      allocate a block of memory with a minimum of 'size' bytes.
    \param[in]  size: the minimum size of the requested block in bytes
    \param[out] none
    \retval     address to allocated memory, NULL pointer if there is an error
*/
void *sys_malloc(size_t size)
{
    return rt_malloc(size);
}

/*!
    \brief      allocate a certian chunks of memory with specified size
                Note: The allocated memory is filled with bytes of value zero.
                All chunks in the allocated memory are contiguous.
    \param[in]  count: multiple number of size want to malloc
    \param[in]  size:  number of size want to malloc
    \param[out] none
    \retval     address to allocated memory, NULL pointer if there is an error
*/
void *sys_calloc(size_t count, size_t size)
{
    return rt_calloc(count, size);
}

/*!
    \brief      change the size of a previously allocated memory block.
    \param[in]  mem: address to the old buffer
    \param[in]  size: number of the new buffer size
    \param[out] none
    \retval     address to allocated memory, NULL pointer if there is an error
*/
void *sys_realloc(void *mem, size_t size)
{
    return rt_realloc(mem, size);
}

/*!
    \brief      free a memory to the heap
    \param[in]  ptr: pointer to the address want to free
    \param[out] none
    \retval     none
*/
void sys_mfree(void *ptr)
{
    rt_free(ptr);
}
#endif

/*!
    \brief      get system free heap size
    \param[in]  none
    \param[out] none
    \retval     system free heap size value(0x00000000-0xffffffff)
*/
int32_t sys_free_heap_size(void)
{
    rt_size_t total, used, total_added = 0, used_added = 0;
    dlist_t *pos, *n;
    add_heap_wrapper_t *heap;
#ifdef RT_USING_HEAP
    rt_memory_info(&total, &used, NULL);

#ifdef RT_USING_MEMHEAP_AS_HEAP
    if (list_empty(&added_heaps)) {
        return total - used;
    }

    list_for_each_safe(pos, n, &added_heaps) {
        heap = list_entry(pos, add_heap_wrapper_t, list);
        rt_memheap_info(&heap->_heap_added, &total_added, &used_added, NULL);
        total += total_added;
        used += used_added;
    }
#endif

    return total - used;
#else /* RT_USING_HEAP */
    return 0;
#endif /* RT_USING_HEAP */
}

/*!
    \brief      get minimum free heap size that has been reached
    \param[in]  none
    \param[out] none
    \retval     system minimum free heap size value(0x00000000-0xffffffff)
*/
int32_t sys_min_free_heap_size(void)
{
    rt_size_t total = 0, max_used = 0;
    rt_size_t total_added = 0, max_used_added = 0;
    dlist_t *pos, *n;
    add_heap_wrapper_t *heap;

#ifdef RT_USING_HEAP
    rt_memory_info(&total, NULL, &max_used);

#ifdef RT_USING_MEMHEAP_AS_HEAP
    list_for_each_safe(pos, n, &added_heaps) {
        heap = list_entry(pos, add_heap_wrapper_t, list);
        rt_memheap_info(&heap->_heap_added, &total_added, NULL, &max_used_added);
        total += total_added;
        max_used += max_used_added;
    }
#endif

    return total - max_used;
#else  /* RT_USING_HEAP */
    return 0;
#endif /* RT_USING_HEAP */
}

#define RT_MEMHEAP_SIZE         RT_ALIGN(sizeof(struct rt_memheap_item), RT_ALIGN_SIZE)
#define RT_MEMHEAP_MINIALLOC    RT_ALIGN(12, RT_ALIGN_SIZE)
/* For small memory allocator - use conservative estimate */
#define SIZEOF_STRUCT_MEM       RT_ALIGN(16, RT_ALIGN_SIZE)
#define MIN_SIZE_ALIGNED        RT_ALIGN(12, RT_ALIGN_SIZE)

/*!
    \brief      get system least heap block size
                Note: it's used to count all memory used by heap and heap management structures.
                    A least block is usually consist of a heap block header and a minimum block.
                    However heap allocation may not align the allocated heap area with this minimum block,
                    the counting results may be inaccurate.
    \param[in]  none
    \param[out] none
    \retval     system heap block size value in bytes(0x0000-0xffff)
*/
uint16_t sys_heap_block_size(void)
{
#ifdef RT_USING_MEMHEAP_AS_HEAP
    return RT_MEMHEAP_SIZE + RT_MEMHEAP_MINIALLOC;
#else
    return SIZEOF_STRUCT_MEM + MIN_SIZE_ALIGNED;
#endif
}

/*!
    \brief      get heap info
    \param[in]  total_size: get total heap size
    \param[in]  free_size: get free heap size
    \param[in]  min_free_size: get minimum free heap size
    \param[out] none
    \retval     none
*/
void sys_heap_info(int *total_size, int *free_size, int *min_free_size)
{
    rt_size_t max_used = 0, used_size;
    rt_size_t max_used_added = 0, total_added = 0, used_added = 0;
    dlist_t *pos, *n;
    add_heap_wrapper_t *heap;

#ifdef RT_USING_HEAP
    rt_memory_info(total_size, &used_size, &max_used);
#ifdef RT_USING_MEMHEAP_AS_HEAP
    list_for_each_safe(pos, n, &added_heaps) {
        heap = list_entry(pos, add_heap_wrapper_t, list);
        rt_memheap_info(&heap->_heap_added, &total_added, &used_added, &max_used_added);
        *total_size += total_added;
        used_size += used_added;
        max_used += max_used_added;
    }
#endif

    *free_size = *total_size - used_size;
    *min_free_size = *total_size - max_used;
#else /* RT_USING_HEAP */
    *total_size = 0;
    *free_size = 0;
    *min_free_size = 0;
#endif /* RT_USING_HEAP */
}

/*!
    \brief      copy buffer content from source address to destination address.
    \param[in]  src: the address of source buffer
    \param[in]  n: the length to copy
    \param[out] des: the address of destination buffer
    \retval     none
*/
void sys_memcpy(void *des, const void *src, uint32_t n)
{
    memcpy(des, src, n);
}

/*!
    \brief      move buffer content from source address to destination address
                Note: It could work between two overlapped buffers.
    \param[in]  src: the address of source buffer
    \param[in]  n: the length to move
    \param[out] des: the address of destination buffer
    \retval     none
*/
void sys_memmove(void *des, const void *src, uint32_t n)
{
    memmove(des, src, n);
}

/*!
    \brief      set the content of the buffer to specified value
    \param[in]  s: The address of a buffer
    \param[in]  c: the value want to memset
    \param[in]  count: count value want to memset
    \param[out] none
    \retval     none
*/
void sys_memset(void *s, uint8_t c, uint32_t count)
{
    uint32_t dword_cnt;
    uint32_t dword_value;
    uint8_t *p_dst = (uint8_t *)s;

    while (((uint32_t)p_dst & 0x03) != 0) {
        if (count == 0) {
            return;
        }
        *p_dst++ = c;
        count--;
    }

    dword_cnt = (count >> 2);
    count &= 0x03;
    dword_value = (((uint32_t)(c) << 0) & 0x000000FF) | (((uint32_t)(c) << 8) & 0x0000FF00) |
                    (((uint32_t)(c) << 16) & 0x00FF0000) | (((uint32_t)(c) << 24) & 0xFF000000);

    while (dword_cnt > 0u) {
        *(uint32_t *)p_dst = dword_value;
        p_dst += 4;
        dword_cnt--;
    }

    while (count > 0u) {
        *p_dst++ = c;
        count--;
    }
}

/*!
    \brief      compare two buffers
    \param[in]  buf1: address to the source buffer 1
    \param[in]  buf2: address to the source buffer 2
    \param[in]  count: the compared buffer size in bytes
    \param[out] none
    \retval      0 if buf1 equals buf2, non-zero otherwise.
*/
int32_t sys_memcmp(const void *buf1, const void *buf2, uint32_t count)
{
    if(!count)
        return 0;

    while (--count && *((char *)buf1) == *((char *)buf2)) {
        buf1 = (char *)buf1 + 1;
        buf2 = (char *)buf2 + 1;
    }

    return (*((uint8_t *)buf1) - *((uint8_t *)buf2));
}

/***************** OS API wrappers *****************/
/*!
    \brief      create a task wrapping a task handle and a message queue
                Note:  Message queue wrapped in the task_wrapper_t is created to only hold pointers,
                  therefore the objects pointed by these pointers are not thread safe. You need to protect
                  them for some scenarios.
    \param[in]  static_tcb: pointer to the task buffer
    \param[in]  name: the task's name
    \param[in]  stack_base: the task's stack base address
    \param[in]  stack_size: the task's stack size, in words (4 bytes)
    \param[in]  queue_size: the task's message queue size
    \param[in]  queue_item_size: the task's message queue item size
    \param[in]  priority: the task's priority
    \param[in]  func: the task's entry function
    \param[in]  ctx: the task's parameter
    \param[out] none
    \retval     the task handle if succeeded, NULL otherwise.
*/
void *sys_task_create(void *static_tcb, const uint8_t *name, uint32_t *stack_base, uint32_t stack_size,
                    uint32_t queue_size, uint32_t queue_item_size, uint32_t priority, task_func_t func, void *ctx)
{
    rt_err_t ret;
    task_wrapper_t *task_wrapper = NULL;
    rt_thread_t task_handle;

    task_wrapper = (task_wrapper_t *)sys_zalloc(sizeof(task_wrapper_t));
    if (task_wrapper == NULL) {
        dbg_print(ERR, "sys_task_create, malloc wrapper failed\r\n");
        return NULL;
    }

    task_wrapper->notification_sem = rt_sem_create("", 0, RT_IPC_FLAG_FIFO);
    if (task_wrapper->notification_sem == NULL)
        goto exit;

    if (queue_size > 0) {
        task_wrapper->task_queue = rt_mq_create("", queue_item_size, queue_size, RT_IPC_FLAG_FIFO);
        if (task_wrapper->task_queue == NULL) {
            dbg_print(ERR, "sys_task_create, create task queue failed\r\n");
            goto exit;
        }
    }

    priority = OS_TASK_PRIO_MAX - 1 - priority;
    /* protect task creation and task wrapper pointer storing against inconsistency of them if preempted in the middle */
    sys_enter_critical();
    if (static_tcb != NULL && stack_base != NULL) {
        ret = rt_thread_init(static_tcb, (const char *)name, func, ctx, stack_base, stack_size * sizeof(uint32_t), priority, 10);
        if (ret != RT_EOK) {
            dbg_print(ERR, "sys_task_create init task failed\r\n");
            goto exit;
        }
        task_handle = static_tcb;
    } else {
        task_handle = rt_thread_create((const char *)name, func, ctx, stack_size * sizeof(uint32_t), priority, 10);
        if (task_handle == NULL) {
            dbg_print(ERR, "sys_task_create create task failed\r\n");
            sys_exit_critical();
            goto exit;
        }
    }

    rt_thread_startup(task_handle);

    task_wrapper->task_handle = task_handle;
    task_handle->user_data = (uint32_t)task_wrapper;
    task_wrapper->id = task_id++;

    sys_exit_critical();

    return (void *)task_handle;

exit:
    if (queue_size > 0 && task_wrapper->task_queue) {
        rt_mq_delete(task_wrapper->task_queue);
    }

    if (task_wrapper->notification_sem) {
        rt_sem_delete(task_wrapper->notification_sem);
    }
    sys_mfree(task_wrapper);

    return NULL;
}

/*!
    \brief      return RTOS task name
    \param[in]  task: pointer to the task handle
    \param[out] none
    \retval     task name
*/
char* sys_task_name_get(void *task)
{
    rt_thread_t thread = (rt_thread_t)task;

    if (task == NULL) {
        return (rt_thread_self()->parent.name);
    }

    return (thread->parent.name);
}

/*!
    \brief      delete a task and free all associated resources
                Note: Message queue wrapped in the task_wrapper_t is created to only hold pointers,
                  therefore the objects pointed by these pointers are not thread safe. You need to protect
                  them for some scenarios.
    \param[in]  task: pointer to the task handle, delete the current task if it's NULL
    \param[out] none
    \retval     none
*/
void sys_task_delete(void *task)
{
    rt_thread_t task_handle = (rt_thread_t)task;
    task_wrapper_t *task_wrapper;

    if (task == NULL) {
        task_handle = rt_thread_self();
    }

    task_wrapper = (task_wrapper_t *)task_handle->user_data;

    /* if task is deleted by another task, delete task first, then free task_wrapper. Otherwise, when
        task_wrapper is freed but task is still alive, task_wrapper pointer got by task is invalid.
    */
    if (task && task != rt_thread_self()) {
        if (rt_object_is_systemobject((rt_object_t)task))
            rt_thread_detach(task);
        else
            rt_thread_delete(task);
    }

    if (task_wrapper != NULL) {
        if (task_wrapper->task_queue)
            rt_mq_delete(task_wrapper->task_queue);

        if (task_wrapper->notification_sem)
            rt_sem_delete(task_wrapper->notification_sem);
        sys_mfree(task_wrapper);
    }

    /* rt-thread doesn't support/need deleting task of itself, just let it exit from task function */
}

/*!
    \brief      wait for a message from the message queue wrapped in the current task
    \param[in]  timeout_ms: millisecond timeout
                            0 means forever,
                            if the time is less than a tick, will roll up to 1 tick
    \param[out] msg_ptr: pointer to the message
    \retval     function run status
      \arg        OS_ERROR: sys_task_wait, task wrapper is NULL
      \arg        OS_TIMEOUT: get message timeout
      \arg        OS_OK: run success
*/
int32_t sys_task_wait(uint32_t timeout_ms, void *msg_ptr)
{
    rt_thread_t task_handle;
    task_wrapper_t *task_wrapper;
    int32_t result;

    task_handle = rt_thread_self();

    task_wrapper = (task_wrapper_t *)task_handle->user_data;
    if (task_wrapper == NULL) {
        dbg_print(ERR, "sys_task_wait, task wrapper is NULL\r\n");
        return OS_ERROR;
    }

    if (task_wrapper->task_queue == NULL) {
        dbg_print(ERR, "sys_task_wait, task queue is NULL\r\n");
        return OS_ERROR;
    }

    result = sys_queue_fetch((os_queue_t*)&task_wrapper->task_queue, msg_ptr, timeout_ms, 1);
    if (result != 0) {
        return OS_TIMEOUT;
    }

    return OS_OK;
}

/*!
    \brief      post a message to the message queue wrapped in the target task
    \param[in]  receiver_task: pointer to the message receiver task handle
    \param[in]  msg_ptr: pointer to the message
    \param[in]  from_isr: whether it's called from an ISR, it's only meaningful to some OS
                    with implementations seperated for different execution context, like FreeRTOS
    \param[out] none
    \retval     function run status
      \arg        OS_ERROR: have error happen
      \arg        OS_OK: run success
*/
int32_t sys_task_post(void *receiver_task, void *msg_ptr, uint8_t from_isr)
{
    rt_thread_t task_handle = (rt_thread_t)receiver_task;
    task_wrapper_t *task_wrapper;
    int32_t ret;

    if (task_handle == NULL) {
        task_handle = rt_thread_self();
    }

    // Check if task is still valid (not in defunct state)
    if (rt_object_get_type(&task_handle->parent) != RT_Object_Class_Thread) {
        dbg_print(ERR, "sys_task_post, task is not valid\r\n");
        return OS_ERROR;
    }

    task_wrapper = (task_wrapper_t *)task_handle->user_data;
    if (task_wrapper == NULL) {
        const char *thread_name = task_handle->parent.name;
        dbg_print(ERR, "sys_task_post, task wrapper is NULL, thread: %s\r\n", thread_name);
        return OS_ERROR;
    }

    if (task_wrapper->task_queue == NULL) {
        dbg_print(ERR, "sys_task_post, task queue is NULL\r\n");
        return OS_ERROR;
    }

    ret = sys_queue_post((os_queue_t*)&task_wrapper->task_queue, msg_ptr);
    if (ret != OS_OK) {
        dbg_print(ERR, "sys_task_post failed, ret=%d, thread: %s\r\n",
                  ret, task_handle->parent.name);
        return OS_ERROR;
    }

    return OS_OK;
}

/*!
    \brief      flush all messages of the message queue wrapped in the target task
                Note: All messages will be recycled to the free queue and the task will be resumed to
                  run after the operation, since we are queuing pointers in the task wrapped message queue,
                  do not to forget to handle all these pointers well to avoid memory leak.
    \param[in]  task: pointer to the task handle
    \param[out] none
    \retval     none
*/
void sys_task_msg_flush(void *task)
{
    rt_thread_t task_handle = (rt_thread_t)task;
    task_wrapper_t *task_wrapper;

    if (task == NULL) {
        task_handle = rt_thread_self();
    }

    task_wrapper = (task_wrapper_t *)task_handle->user_data;
    if (task_wrapper != NULL && task_wrapper->task_queue != NULL) {
        rt_mq_control(task_wrapper->task_queue, RT_IPC_CMD_RESET, NULL);
    } else {
        dbg_print(ERR, "sys_task_msg_flush, can't find task queue\r\n");
    }
}

/*!
    \brief      get the number of waiting messages in the task queue
    \param[in]  task: pointer to the task handle
    \param[in]  from_isr: whether is from ISR
    \param[out] none
    \retval     the number of waiting messages if succeeded, OS_ERROR otherwise.
*/
int32_t sys_task_msg_num(void *task, uint8_t from_isr)
{
    rt_thread_t task_handle = (rt_thread_t)task;
    task_wrapper_t *task_wrapper;
    int num = 0;

    if (task == NULL) {
        task_handle = rt_thread_self();
    }

    task_wrapper = (task_wrapper_t *)task_handle->user_data;
    if (task_wrapper == NULL || task_wrapper->task_queue == NULL) {
        dbg_print(ERR, "sys_task_msg_num, can't find task queue\r\n");
        return OS_ERROR;
    }

    sys_enter_critical();
    num = task_wrapper->task_queue->entry;
    sys_exit_critical();
    return num;
}

/*!
    \brief      initialize notification for a WIFI task.
    \param[in]  task: pointer to the task handle
    \param[out] none
    \retval     return 0 on success and != 0 if error occurred.
*/
int sys_task_init_notification(void *task)
{
    return 0;
}

/*!
    \brief      task suspend itself until it is notified (or timeout expires)
    \param[in]  timeout: Maximum duration to wait, in ms, if no notification is pending.
                         0 means do not wait and -1 means wait indefinitely.
    \param[out] none
    \retval     the number of pending notification (0 if timeout was reached)
*/
int sys_task_wait_notification(int timeout)
{
    rt_thread_t task_handle;
    task_wrapper_t *task_wrapper;
    int32_t result;

    task_handle = rt_thread_self();
    if (task_handle == NULL) {
        dbg_print(ERR, "sys_task_wait_notification, rt_thread_self() returned NULL\r\n");
        return OS_ERROR;
    }

    task_wrapper = (task_wrapper_t *)task_handle->user_data;
    if (task_wrapper == NULL || task_wrapper->notification_sem == NULL) {
        dbg_print(ERR, "sys_task_wait_notification, task wrapper or notification is NULL\r\n");
        return OS_ERROR;
    }
	
	const char *thread_name = task_handle->parent.name;

    result = rt_sem_take(task_wrapper->notification_sem, sys_timeout_2_tickcount(timeout));
    if (result == RT_EOK) {
        return 1;  // 返回待处理通知数量：1
    } else if (result == -RT_ETIMEOUT || result == -RT_EEMPTY || result == -4) {
        // 超时或信号量为空都不打印错误，这是正常情况
        // -4 是原生RT-Thread的RT_EEMPTY错误码（在POSIX模式下可能出现）
        return 0;  // 返回 0 表示超时
    } else {
        dbg_print(ERR, "sys_task_wait_notification failed, thread: %s, result: %d (RT_ETIMEOUT=%d, RT_EEMPTY=%d)\r\n",
                  thread_name, result, -RT_ETIMEOUT, -RT_EEMPTY);
        return OS_ERROR;  // 真正的错误
    }
}

/*!
    \brief      send notification to a task
    \param[in]  task: Task to notify.
    \param[in]  isr: Indicate if this is called from ISR.
    \param[out] none
    \retval     none
*/
void sys_task_notify(void *task, bool isr)
{
    rt_thread_t task_handle = (rt_thread_t)task;
    task_wrapper_t *task_wrapper;

    if (task_handle == NULL) {
        dbg_print(ERR, "sys_task_notify, task is NULL\r\n");
        return;
    }

    task_wrapper = (task_wrapper_t *)task_handle->user_data;
    if (task_wrapper == NULL || task_wrapper->notification_sem == NULL) {
        const char *thread_name = task_handle->parent.name;
        dbg_print(ERR, "sys_task_notify, task wrapper or notification is NULL for thread: %s\r\n",
                  thread_name);
        return;
    }

    rt_sem_release(task_wrapper->notification_sem);
}

/*!
    \brief      get the minimum free stack size of the target task
    \param[in]  task: the pointer to the task handle
    \param[out] none
    \retval     the free stack size in words (4 bytes)(0x00000000-0xffffffff)
*/
uint32_t sys_stack_free_get(void *task)
{
    rt_thread_t thread = (rt_thread_t)task;
    rt_uint8_t *ptr;
    uint32_t cnt = 0;

    if (thread == NULL)
        thread = rt_thread_self();

    ptr = (rt_uint8_t *)thread->stack_addr;
    while (*ptr == '#') {// '#' is stack fill byte used by rtthread
        ptr++;
        cnt++;
    }

    return cnt/sizeof(rt_uint32_t);
}

static rt_uint8_t _rtt_task_stat_get(rt_thread_t thread)
{
    return rt_sched_thread_get_stat(thread);
}

static rt_uint8_t _rtt_task_priority_get(rt_thread_t thread)
{
    return rt_sched_thread_get_init_prio(thread);
}

static rt_uint32_t _rtt_task_id_get(rt_thread_t thread)
{
    char *idle_thread_name = "tidle0";
    char *timer_thread_name = "timer";

    if (strncmp(idle_thread_name, thread->parent.name, strlen(idle_thread_name)) == 0) {
        return 0;
    } else if (strncmp(timer_thread_name, thread->parent.name, strlen(timer_thread_name)) == 0) {
        return 1;
    } else {
        task_wrapper_t *task_wrapper = NULL;
        task_wrapper = (task_wrapper_t *)thread->user_data;
        if (task_wrapper)
            return task_wrapper->id;
        else
            return (-1); //invalid
    }
}

static rt_uint32_t _rtt_task_stack_base_get(rt_thread_t thread)
{
    return (rt_uint32_t)thread->stack_addr;
}

static rt_uint32_t _rtt_task_stack_free_get(rt_thread_t thread)
{
    return sys_stack_free_get(thread);
}

/*!
    \brief      list statistics for all tasks
    \param[in]  none
    \param[out] pwrite_buf: pointer to the result buffer, expect a statistics string
    \retval     none
*/
void sys_task_list(char *pwrite_buf)
{
    char *buf = NULL;
    task_status_t *p_task_array = NULL;
    uint32_t threads_num;
    threads_num = rt_object_get_length(RT_Object_Class_Thread);
    uint16_t i = 0;
    uint16_t j = 0;
    rt_thread_t thread_ptr;
    uint8_t *pp;
    char cStatus;
    struct rt_object *object;
    struct rt_list_node *node;
    struct rt_object_information *information;

    p_task_array = sys_zalloc(threads_num * sizeof(task_status_t));
    if (p_task_array == NULL) {
        dbg_print(ERR, "list statistics for all tasks failed, p_task_array == NULL.\r\n");
        return;
    }

    // 26: the length of string "\t%c\t%u\t%u\t%u\t0x%08x\r\n" in func vTaskList().
    if (NULL == pwrite_buf) {
        buf = sys_zalloc(threads_num * (configMAX_TASK_NAME_LEN + 26));
        if (buf == NULL) {
            dbg_print(ERR, "list statistics for all tasks failed, buf == NULL.\r\n");
            sys_mfree(p_task_array);
            return;
        }
    } else {
        buf = pwrite_buf;
    }

    sys_enter_critical();
    /* try to allocate on other memory heap */
    information = rt_object_get_information(RT_Object_Class_Thread);
    RT_ASSERT(information != RT_NULL);
    for (node  = information->object_list.next, i = 0;
         node != &(information->object_list);
         node  = node->next, i++)
    {
        object = rt_list_entry(node, struct rt_object, list);
        thread_ptr = (rt_thread_t)object;

        p_task_array[i].thread_ptr = thread_ptr;
        p_task_array[i].name = thread_ptr->parent.name;
        p_task_array[i].state = _rtt_task_stat_get(thread_ptr);
        p_task_array[i].priority = _rtt_task_priority_get(thread_ptr);
        p_task_array[i].stack_end = _rtt_task_stack_base_get(thread_ptr);
        p_task_array[i].stack_min_free_size = _rtt_task_stack_free_get(thread_ptr);
        p_task_array[i].id = _rtt_task_id_get(thread_ptr);
    }

    sys_exit_critical();
    pp = buf;
    for (i = 0; i < threads_num; i++) {
        bool padding = false;
        for( j = 0; j < (uint16_t) configMAX_TASK_NAME_LEN; j++ ) {
            if (padding) {
                pp[j] = ' ';
                continue;
            }

            if (p_task_array[i].name[j] == '\0') {
                padding = true;
                pp[j] = ' ';
            } else {
                pp[j] = p_task_array[i].name[j];
            }
        }

        pp += j;

        switch(p_task_array[i].state) {
            case RT_THREAD_READY:
                if (p_task_array[i].thread_ptr == rt_thread_self()) {
                    cStatus = TX_RUNNING_CHAR;
                } else {
                    cStatus = TX_READY_CHAR;
                }
                break;
            case RT_THREAD_RUNNING:
                cStatus = TX_RUNNING_CHAR;
                break;
            case RT_THREAD_SUSPEND:
            case RT_THREAD_SUSPEND_KILLABLE:
            case RT_THREAD_SUSPEND_UNINTERRUPTIBLE:
                cStatus = TX_SUSPENDED_CHAR;
                break;
            case RT_THREAD_INIT:
                cStatus = TX_BLOCKED_CHAR;
                break;
            case RT_THREAD_CLOSE:
                cStatus = TX_DELETED_CHAR;
                break;
            default:            /* Should not get here, but it is included
                                to prevent static checking errors. */
                cStatus = ( char ) 0x00;
                break;
        }

        /* Write the rest of the string. */
        sprintf(pp, "\t%c\t%u\t%u\t%u\t0x%08x\r\n", cStatus, ( unsigned int ) p_task_array[i].priority,
                ( unsigned int ) p_task_array[i].stack_min_free_size, ( unsigned int ) p_task_array[i].id,
                ( unsigned int ) p_task_array[i].stack_end );

        pp += strlen(pp);
    }

    printf("%s\r\n", buf);
    sys_mfree(p_task_array);
    if (NULL == pwrite_buf)
        sys_mfree(buf);
}


/*!
    \brief      create and initialize a semaphore extend
    \param[in]  sema: the pointer to the semaphore handle
    \param[in]  max_count: max count of semaphore
    \param[in]  init_count: initialize count of semaphore
    \param[out] sema: pointer to the semaphore
    \retval     OS_OK if succeeded, OS_ERROR otherwise.
*/
int32_t sys_sema_init_ext(os_sema_t *sema, int max_count, int init_count)
{
    *sema = rt_sem_create("", init_count, RT_IPC_FLAG_FIFO);
    if (*sema == NULL) {
        dbg_print(ERR, "sys_sema_init_ext fail, sema = NULL\r\n");
        return OS_ERROR;
    }

    if (rt_sem_control(*sema, RT_IPC_CMD_SET_VLIMIT, (void *)max_count) != RT_EOK) {
        dbg_print(ERR, "sys_sema_init_ext, max_count set failed\r\n");
        rt_sem_delete(*sema);
        return OS_ERROR;
    }

    return OS_OK;
}

/*!
    \brief      create and initialize a semaphore
    \param[in]  sema: the pointer to the semaphore handle
    \param[in]  init_val: initialize vlaue of semaphore
    \param[out] sema: pointer to the semaphore
    \retval     OS_OK if succeeded, OS_ERROR otherwise.
*/
int32_t sys_sema_init(os_sema_t *sema, int32_t init_val)
{
    *sema = rt_sem_create("", init_val, RT_IPC_FLAG_FIFO);
    if (*sema == NULL) {
        dbg_print(ERR, "sys_sema_init fail, sema = NULL\r\n");
        return OS_ERROR;
    }

    return OS_OK;
}

/*!
    \brief      free a semaphore
    \param[in]  sema: pointer to the semaphore handle
    \param[out] none
    \retval     none
*/
void sys_sema_free(os_sema_t *sema)
{
    if (*sema == NULL) {
        dbg_print(ERR, "sys_sema_free, sema = NULL\r\n");
        return;
    }

    rt_sem_delete(*sema);
    *sema = NULL;
}

/*!
    \brief      release a semaphore
                Note:  It could be called either in task context or interrupt context except for some OS
                  that has seperated implementations for the different context, like FreeRTOS
    \param[in]  sema: pointer to the semaphore handle
    \param[out] none
    \retval     none
*/
void sys_sema_up(os_sema_t *sema)
{
    if (*sema == NULL) {
        dbg_print(ERR, "sys_sema_up, sema = NULL\r\n");
        return;
    }

    if (rt_sem_release(*sema) != RT_EOK)
        dbg_print(ERR, "sys_sema_up, give semaphore error\r\n");
}

/*!
    \brief      release a semaphore in a interrupt context, it's only meaningful to some OS
                with implementations seperated for different execution context, like FreeRTOS
    \param[in]  sema: pointer to the semaphore handle
    \param[out] none
    \retval     none
*/
void sys_sema_up_from_isr(os_sema_t *sema)
{
    if (*sema == NULL) {
        dbg_print(ERR, "sys_sema_up_from_isr, sema = NULL\r\n");
        return;
    }

    if (rt_sem_release(*sema) != RT_EOK)
        dbg_print(ERR, "sys_sema_up_from_isr, give semaphore error\r\n");
}

/*!
    \brief      Require the semaphore within a given time constraint
    \param[in]  sema: pointer to the semaphore handle
    \param[in]  timeout_ms: millisecond value of timeout,
                    0 means forever,
                    if the time is less than a tick, will roll up to 1 tick.
    \param[out] none
    \retval     function run status
      \arg        OS_TIMEOUT: timeout
      \arg        OS_OK: run success
*/
int32_t sys_sema_down(os_sema_t *sema, uint32_t timeout_ms)
{
    uint32_t timeout_tick;
    rt_err_t result;

    if (*sema == NULL) {
        dbg_print(ERR, "sys_sema_down, sema = NULL\r\n");
        return OS_ERROR;
    }

    if (timeout_ms == 0) {
        timeout_tick = (uint32_t)RT_WAITING_FOREVER;
    } else {
        timeout_tick = timeout_ms / OS_MS_PER_TICK;
        if (timeout_tick == 0)
            timeout_tick = 1;
    }

    result = rt_sem_take(*sema, timeout_tick);
    if (result == RT_EOK)
        return OS_OK;

    if (result == -RT_ETIMEOUT) {
        return OS_TIMEOUT;
    } else {
        dbg_print(ERR, "sys_sema_down, error\r\n");
    }

    return OS_ERROR;
}

/*!
    \brief      return a semaphore count.
    \param[in]  sema: pointer to the semaphore handle
    \param[out] the sempahore count, or OS_ERROR if sema is NULL
    \retval     semaphore count.
*/
int sys_sema_get_count(os_sema_t *sema)
{
    int count;

    if (*sema == NULL) {
        dbg_print(ERR, "sys_sema_get_count, sema = NULL\r\n");
        return OS_ERROR;
    }

    sys_enter_critical();
    count = (*(rt_sem_t *)sema)->value;
    sys_exit_critical();
    return count;
}

#ifdef sys_mutex_new
#undef sys_mutex_new
#endif
#ifdef sys_mutex_lock
#undef sys_mutex_lock
#endif
#ifdef sys_mutex_unlock
#undef sys_mutex_unlock
#endif
#ifdef sys_mutex_free
#undef sys_mutex_free
#endif
#ifdef sys_mutex_valid
#undef sys_mutex_valid
#endif
#ifdef sys_mutex_set_invalid
#undef sys_mutex_set_invalid
#endif

/*!
    \brief      create and initialize a mutex
    \param[in]  mutex: pointer to the mutext handle
    \param[out] none
    \retval     function run status
      \arg        OS_ERROR: return error
      \arg        OS_OK: run success
*/
int sys_mutex_init(_mutex *mutex)
{
    *mutex = rt_mutex_create("", RT_IPC_FLAG_FIFO);

    DEBUG_ASSERT(*mutex != NULL);
    return OS_OK;
}

/*!
    \brief      free a mutex
    \param[in]  mutex: pointer to the mutex handle
    \param[out] none
    \retval     none
*/
void sys_mutex_free(_mutex *mutex)
{
    if (*mutex == NULL) {
        dbg_print(ERR, "sys_mutex_free, mutex = NULL\r\n");
        return;
    }

    rt_mutex_delete(*mutex);
    *mutex = NULL;
}

/*!
    \brief      require the mutex
    \param[in]  mutex: pointer to the mutex handle
    \param[out] none
    \retval     function run status
      \arg        OS_ERROR: return error
      \arg        OS_OK: run success
*/
int32_t sys_mutex_get(_mutex *mutex)
{
    if (*mutex == NULL) {
        dbg_print(ERR, "sys_mutex_get, mutex = NULL\r\n");
        return OS_ERROR;
    }

    while (rt_mutex_take(*mutex, RT_WAITING_FOREVER) != RT_EOK) {
        dbg_print(ERR, "[%s] sys_mutex_get 0x%08x failed, retry\r\n", rt_thread_self()->parent.name, *mutex);
    }
    return OS_OK;
}

/*!
    \brief      try to require the mutex
    \param[in]  mutex: pointer to the mutex handle
    \param[in]  timeout: Maximum duration to wait, in ms.
                         0 means do not wait and -1 means wait indefinitely.
    \param[out] none
    \retval     function run status
      \arg        OS_ERROR: return error
      \arg        OS_OK: run success
*/
int32_t sys_mutex_try_get(_mutex *mutex, int timeout)
{
    if (*mutex == NULL) {
        dbg_print(ERR, "sys_mutex_try_get, mutex = NULL\r\n");
        return OS_ERROR;
    }

    if (rt_mutex_trytake(*mutex) != RT_EOK) {
        dbg_print(ERR, "sys_mutex_try_get, mutex error\r\n");
        return OS_ERROR;
    }
    return OS_OK;
}


/*!
    \brief      release a mutex
                Note: It could be only called in task context for some OS
                  that has seperated implementations for the different context, like FreeRTOS
    \param[in]  mutex: pointer to the mutext
    \param[out] none
    \retval     none
*/
void sys_mutex_put(_mutex *mutex)
{
    if (*mutex == NULL) {
        dbg_print(ERR, "sys_mutex_put, mutex = NULL\r\n");
        return;
    }

    if (rt_mutex_release(*mutex) != RT_EOK)
        dbg_print(ERR, "sys_mutex_put, give mutex error\r\n");
}

/*!
    \brief      create and initialize a message queue
    \param[in]  queue_size: queue size
    \param[in]  item_size: queue item size
    \param[out] queue: pointer to the queue
    \retval     function run status
      \arg        OS_ERROR: return error
      \arg        OS_OK: run success
*/
int32_t sys_queue_init(os_queue_t *queue, int32_t queue_size, uint32_t item_size)
{
    if ((*queue = rt_mq_create("", item_size, queue_size, RT_IPC_FLAG_FIFO)) == NULL) {
        dbg_print(ERR, "sys_queue_init, return error\r\n");
        return OS_ERROR;
    }

    return OS_OK;
}

/*!
    \brief      free a message queue
    \param[in]  queue: pointer to the queue
    \param[out] none
    \retval     none
*/
void sys_queue_free(os_queue_t *queue)
{
    if (*queue == NULL) {
        dbg_print(ERR, "sys_queue_free, queue = NULL\r\n");
        return;
    }

    rt_mq_delete(*queue);
    *queue = NULL;
}

/*!
    \brief      post a message to the target message queue
    \param[in]  queue: pointer to the queue handle
    \param[in]  msg: pointer to the message
    \param[out] none
    \retval     function run status
      \arg        OS_ERROR: return error
      \arg        OS_OK: run success
*/
int32_t sys_queue_post(os_queue_t *queue, void *msg)
{
    rt_mq_t q = (rt_mq_t)*queue;
    rt_err_t result;

    if (q == NULL) {
        dbg_print(ERR, "sys_queue_post, queue = NULL\r\n");
        return OS_ERROR;
    }

    result = rt_mq_send(q, msg, q->msg_size);
    if (result != RT_EOK) {
        // Queue might be full - try urgent send (overwrites oldest)
        result = rt_mq_urgent(q, msg, q->msg_size);
        if (result != RT_EOK) {
            // Log with queue info for debugging
            dbg_print(ERR, "sys_queue_post failed, queue full (entry=%d, max=%d)\r\n",
                      q->entry, q->max_msgs);
            return OS_ERROR;
        }
        dbg_print(NOTICE, "sys_queue_post: queue was full, used urgent send\r\n");
    }

    return OS_OK;
}

/*!
    \brief      post a message to the target message queue
    \param[in]  queue: pointer to the queue handle
    \param[in]  msg: pointer to the message
    \param[in]  timeout_ms Maximum duration to wait, in ms.
                           0 means do not wait and -1 means wait indefinitely.
    \param[out] none
    \retval     function run status
      \arg        OS_ERROR: return error
      \arg        OS_OK: run success
*/
int32_t sys_queue_post_with_timeout(os_queue_t *queue, void *msg, int32_t timeout_ms)
{
    uint32_t timeout_tick;
    rt_mq_t q = (rt_mq_t)*queue;

    if (q == NULL) {
        dbg_print(ERR, "sys_queue_post_with_timeout, queue = NULL\r\n");
        return OS_ERROR;
    }

    timeout_tick = timeout_ms / OS_MS_PER_TICK;
    if (rt_mq_send_wait(q, msg, q->msg_size, timeout_tick) != RT_EOK) {
        dbg_print(ERR, "sys_queue_post_withtimeout failed\r\n");
        return OS_ERROR;
    }
    return OS_OK;
}

/*!
    \brief      fetch a message from the message queue within a given time constraint
    \param[in]  queue: pointer to the queue handle
    \param[in]  timeout_ms: timeout in ms. For blocking operation, 0 means forever, if the time is
                    less than a tick, will roll up to 1 tick; for non-blocking operation, it's unused.
    \param[in]  is_blocking: whether it's a blocking operation
    \param[out] msg: pointer to the message
    \retval     function run status
      \arg        OS_TIMEOUT: timeout
      \arg        OS_OK: run success
*/
int32_t sys_queue_fetch(os_queue_t *queue, void *msg, uint32_t timeout_ms, uint8_t is_blocking)
{
    uint32_t timeout_tick;
    rt_mq_t q = (rt_mq_t)*queue;
    int ret;

    if (q == NULL) {
        dbg_print(ERR, "sys_queue_fetch, queue = NULL\r\n");
        return OS_ERROR;
    }

    if (!is_blocking) {
        timeout_tick = 0;
    } else if (is_blocking && (timeout_ms == 0)) {
        timeout_tick = (uint32_t)RT_WAITING_FOREVER;
    } else {
        timeout_tick = (timeout_ms / OS_MS_PER_TICK);
        if (timeout_tick == 0) {
            timeout_tick = 1;
        }
    }

    if ((ret = rt_mq_recv(q, msg, q->msg_size, timeout_tick)) <= 0) {
        return OS_TIMEOUT;
    }

    return OS_OK;
}

/*!
    \brief      check if a RTOS message queue is empty or not.
    \param[in]  queue: pointer to the queue handle
    \param[out] none
    \retval     true if queue is empty, false otherwise.
*/
bool sys_queue_is_empty(os_queue_t *queue)
{
    bool res;
    rt_mq_t q = (rt_mq_t)*queue;

    DEBUG_ASSERT(q != NULL);

    sys_enter_critical();
    if (q->entry == 0) //empty
        res = true;
    else
        res = false;
    sys_exit_critical();

    return (res == true);
}

/*!
    \brief      get the number of messages pending a queue.
    \param[in]  queue: pointer to the queue handle
    \param[out] none
    \retval     The number of messages pending in the queue.
*/
int sys_queue_cnt(os_queue_t *queue)
{
    uint32_t res;
    rt_mq_t q = (rt_mq_t)*queue;

    if (q == NULL) {
        dbg_print(ERR, "sys_queue_cnt, queue = NULL\r\n");
        return OS_ERROR;
    }

    sys_enter_critical();
    res = q->entry;
    sys_exit_critical();

    return ((int)res);
}

/*!
    \brief      write a message at the end of a RTOS message queue.
    \param[in]  queue: pointer to the queue handle
    \param[in]  msg: Message to copy in the queue. (It is assume that buffer is of the size specified in @ref os_queue_t_create)
    \param[in]  timeout: Maximum duration to wait, in ms, if queue is full. 0 means do not wait and -1 means wait indefinitely.
    \param[in]  isr: Indicate if this is called from ISR. If set, @p timeout parameter is ignored.
    \param[out] none
    \retval     0 on success and != 0 if error occurred (i.e queue was full and maximum duration has been reached).
*/
int sys_queue_write(os_queue_t *queue, void *msg, int timeout, bool isr)
{
    int32_t res;
    uint32_t timeout_tick;
    rt_mq_t q = (rt_mq_t)*queue;

    if (q == NULL) {
        dbg_print(ERR, "sys_queue_write, queue = NULL\r\n");
        return OS_ERROR;
    }

    if (isr)
        timeout_tick = RT_WAITING_NO;
    else
        timeout_tick = sys_timeout_2_tickcount(timeout);

    if (rt_mq_send_wait(q, msg, q->msg_size, timeout_tick) != RT_EOK) {
        dbg_print(ERR, "sys_queue_write failed\r\n");
        return OS_ERROR;
    }

    return OS_OK;
}

/*!
    \brief      read a message from a RTOS message queue.
    \param[in]  queue: pointer to the queue handle
    \param[in]  msg: Buffer to copy into. (It is assume that buffer is of the size specified in @ref os_queue_t_create)
    \param[in]  timeout: Maximum duration to wait, in ms, if queue is empty. 0 means do not wait and -1 means wait indefinitely.
    \param[in]  isr: Indicate if this is called from ISR. If set, @p timeout parameter is ignored.
    \param[out] none
    \retval     0 on success and != 0 if error occurred (i.e queue was empty and maximum duration has been reached).
*/
int sys_queue_read(os_queue_t *queue, void *msg, int timeout, bool isr)
{
    uint32_t timeout_tick;
    rt_mq_t q = (rt_mq_t)*queue;
    int ret;

    if (q == NULL) {
        dbg_print(ERR, "sys_queue_read, queue = NULL\r\n");
        return OS_ERROR;
    }

    if (isr) {
        timeout_tick = 0;
    } else {
        timeout_tick = sys_timeout_2_tickcount(timeout);
    }

    if ((ret = rt_mq_recv(q, msg, q->msg_size, timeout_tick)) <= 0) {
        return OS_TIMEOUT;
    }

    return OS_OK;
}

/*!
    \brief      get the current system up time
    \param[in]  none
    \param[out] none
    \retval     milliseconds since the system boots up(0x00000000-0xffffffff)
*/
uint32_t sys_current_time_get(void)
{
    return (uint32_t)(rt_tick_get() * OS_MS_PER_TICK);
}

/*!
    \brief      get the system time
    \param[in]  p: not used
    \param[out] none
    \retval     milliseconds since the system boots up(0x00000000-0xffffffff)
*/
uint32_t sys_time_get(void *p)
{
    return sys_current_time_get();
}

/*!
    \brief      put current task to sleep for a specified period of time
    \param[in]  ms: millisecond value, if the time is less than a tick, will roll up to 1 tick.
    \param[out] none
    \retval     none
*/
void sys_ms_sleep(int ms)
{
    if (ms <= 0)
        return;

    rt_thread_mdelay(ms);
}

/*!
    \brief      delay the current task for several microseconds
                Note: The task will being blocked during the delayed time
    \param[in]  nus: microsecond value
    \param[out] none
    \retval     none
*/
void sys_us_delay(uint32_t nus)
{
    sys_enter_critical();
    systick_udelay(nus);
    sys_exit_critical();
}

/*!
    \brief      give up the execution of the current task
    \param[in]  none
    \param[out] none
    \retval     none
*/
void sys_yield(void)
{
    rt_thread_yield();
}


/*!
    \brief      pend the task scheduling
    \param[in]  none
    \param[out] none
    \retval     none
*/
void sys_sched_lock(void)
{
    rt_enter_critical();
}

/*!
    \brief      resume the task scheduling
    \param[in]  none
    \param[out] none
    \retval     none
*/
void sys_sched_unlock(void)
{
    rt_exit_critical();
}

uint32_t sys_get_schedule_state(void)
{

    return rt_critical_level();
}

/*!
    \brief      Initialize the given buffer with the random data
    \param[in]  size: size of random data
    \param[out] dst: pointer to the get random bytes
    \retval     function run status
      \arg        OS_OK: run success
*/
int32_t sys_random_bytes_get(void *dst, uint32_t size)
{
#ifndef CFG_PLATFORM_FPGA_V7
    return random_get(dst, size);
#else
    size_t i;
    unsigned char *dstc = (unsigned char *)dst;

    for (i = 0; i < size ; i++) {
        dstc[i] = co_rand_byte();
    }
    return 0;
#endif
}

/*!
    \brief      set system timer callback
    \param[in]  p_tmr:pointer to the timer callback
    \param[out] none
    \retval     none
*/
static void _sys_timer_callback(void *p_tmr)
{
    timer_wrapper_t *timer_wrapper;

    timer_wrapper = rt_container_of(*(os_timer_t *)p_tmr, timer_wrapper_t, os_timer);
    timer_wrapper->timer_func(p_tmr, timer_wrapper->p_arg);
}

/*!
    \brief      initialize a timer
    \param[in]  timer: pointer to the timer handle
    \param[in]  name: pointer to the timer name
    \param[in]  delay: the timeout in milliseconds
    \param[in]  periodic: whether it's periodic timer
    \param[in]  func: the timer call back function
    \param[in]  arg:the argument that will pass to the call back
    \param[out] none
    \retval     none
*/
void sys_timer_init(os_timer_t *timer, const uint8_t *name, uint32_t delay, uint8_t periodic, timer_func_t func, void *arg)
{
    timer_wrapper_t *timer_wrapper;

    timer_wrapper = (timer_wrapper_t *)sys_malloc(sizeof(timer_wrapper_t));
    if (timer_wrapper == NULL) {
        dbg_print(ERR, "sys_timer_init, malloc timer wrapper failed\r\n");
        return;
    }

    timer_wrapper->p_arg = arg;
    timer_wrapper->timer_func = func;

    rt_timer_init(&timer_wrapper->os_timer, (const char *)name, _sys_timer_callback, timer,
                    (delay / OS_MS_PER_TICK), RT_TIMER_FLAG_SOFT_TIMER | (periodic ? RT_TIMER_FLAG_PERIODIC : RT_TIMER_FLAG_ONE_SHOT));

    *timer = &timer_wrapper->os_timer;
}

/*!
    \brief      delete a timer
    \param[in]  timer: pointer to the timer handle
    \param[out] none
    \retval     none
*/
void sys_timer_delete(os_timer_t *timer)
{
    timer_wrapper_t *timer_wrapper;
    os_timer_t p_timer;

    if (*timer == NULL) {
        dbg_print(ERR, "sys_timer_delete, timer is NULL\r\n");
        return;
    }

    p_timer = *timer;
    *timer = NULL;

    if (rt_timer_detach(p_timer) != RT_EOK) {
        dbg_print(ERR, "sys_timer_delete failed\r\n");
        return;
    }
    timer_wrapper = rt_container_of(p_timer, timer_wrapper_t, os_timer);
    sys_mfree(timer_wrapper);
}

/*!
    \brief       start a timer
    \param[in]  timer: pointer to the timer handle
    \param[in]  from_isr: whether it's called from an ISR, it's only meaningful to some OS
                  with implementations seperated for different execution context, like FreeRTOS
    \param[out] none
    \retval     none
*/
void sys_timer_start(os_timer_t *timer, uint8_t from_isr)
{
    rt_err_t result;

    if (*timer == NULL) {
        dbg_print(ERR, "sys_timer_start, timer = NULL\r\n");
        return;
    }

    result = rt_timer_start(*timer);
    if (result != RT_EOK) {
        dbg_print(ERR, "sys_timer_start failed\r\n");
    }
}

/*!
    \brief      start the timer extension with a specified timeout
    \param[in]  timer: pointer to the timer handle
    \param[in]  delay: time want to delay
    \param[in]  from_isr: whether it's called from an ISR, it's only meaningful to some OS
                  with implementations seperated for different execution context, like FreeRTOS
    \param[out] none
    \retval     none
*/
void sys_timer_start_ext(os_timer_t *timer, uint32_t delay, uint8_t from_isr)
{
    rt_tick_t timer_ticks;
    rt_err_t result = 0;
    register rt_base_t level;

    if (*timer == NULL) {
        dbg_print(ERR, "sys_timer_start_ext, timer = NULL\r\n");
        return;
    }

    if (delay <= OS_MS_PER_TICK) {
        timer_ticks = 1;
    } else {
        timer_ticks = delay / OS_MS_PER_TICK;
    }

    rt_timer_control(*timer, RT_TIMER_CTRL_SET_TIME, &timer_ticks);
    result = rt_timer_start(*timer);
    if (result != RT_EOK) {
        dbg_print(ERR, "sys_timer_start_ext (0x%08x) return fail, from_isr is %d\r\n", *timer, from_isr);
    }
}

/*!
    \brief      stop the timer
    \param[in]  timer: pointer to the timer handle
    \param[in]  from_isr: whether it's called from an ISR, it's only meaningful to some OS
                      with implementations seperated for different execution context, like FreeRTOS
    \param[out] none
    \retval     function run state 1: run ok, 0: have error happen
*/
uint8_t sys_timer_stop(os_timer_t *timer, uint8_t from_isr)
{
    rt_err_t result;

    if (*timer == NULL) {
        dbg_print(ERR, "sys_timer_stop, timer = NULL\r\n");
        return 0;
    }

    result = rt_timer_stop(*timer);

    if (result != RT_EOK) {
        dbg_print(ERR, "sys_timer_stop failed\r\n");
        return 0;
    }

    return 1;
}

/*!
    \brief      check if the timer is active and pending for expiration
    \param[in]  timer: pointer to the timer handle
    \param[out] none
    \retval     1 if it's pending, 0 otherwise.
*/
uint8_t sys_timer_pending(os_timer_t *timer)
{
    rt_timer_t t = (rt_timer_t)*timer;

    return !!(t->parent.flag & RT_TIMER_FLAG_ACTIVATED);
}

/*!
    \brief      Miscellaneous initialization work after OS initialized and
                task scheduler started
                Note: It runs in the standalone start task
    \param[in]  none
    \param[out] none
    \retval     none
*/
void sys_os_misc_init(void)
{

}

/*!
    \brief      initialize the OS
    \param[in]  none
    \param[out] none
    \retval     none
*/
void sys_os_init(void)
{
#ifdef RT_USING_HEAP
    rt_system_heap_init((void *) HEAP_BEGIN, (void *) HEAP_END);
    INIT_DLIST_HEAD(&added_heaps);
//    sys_add_heap_region(0x20048000, 0x8000);
#endif
    rt_system_scheduler_init();
    rt_system_timer_init();
    rt_system_timer_thread_init();
#ifdef RT_USING_PM
    rt_system_lps_init();
#endif
    rt_thread_idle_init();
    rt_show_version();
}

/*!
    \brief      start the OS
    \param[in]  none
    \param[out] none
    \retval     none
*/
void sys_os_start(void)
{
    // vPortCriticalInit();

    // rt_hw_ticksetup();

    // /* start scheduler */
    // rt_system_scheduler_start();
}

/*!
    \brief      get the current RTOS time, in tick.
    \param[in]  isr: Indicate if this is called from ISR.
    \param[out] none
    \retval     The current RTOS time (in tick)
*/
uint32_t sys_os_now(bool isr)
{
    return rt_tick_get();
}

/*!
    \brief      add heap region
    \param[in]  ucStartAddress: start address.
    \param[in]  xSizeInBytes: size in bytes.
    \param[out] none
    \retval     none
*/
void sys_add_heap_region(uint32_t ucStartAddress, uint32_t xSizeInBytes)
{
#ifdef RT_USING_HEAP
    add_heap_wrapper_t *heap = NULL;

    heap = (add_heap_wrapper_t *)sys_zalloc(sizeof(add_heap_wrapper_t));
    if (heap) {
        INIT_DLIST_HEAD(&heap->list);
        co_snprintf(heap->name, ADD_HEAP_NAME_LEN, "heap_%08x:", ucStartAddress);
        if (rt_memheap_init(&heap->_heap_added, heap->name, (void *)ucStartAddress, xSizeInBytes) == RT_EOK) {
            sys_enter_critical();
            list_add_tail(&heap->list, &added_heaps);
            sys_exit_critical();
        } else {
            sys_mfree(heap);
        }
    }
#endif
}

/*!
    \brief      remove heap region
    \param[in]  ucStartAddress: start address.
    \param[in]  xSizeInBytes: size in bytes.
    \param[out] none
    \retval     none
*/
void sys_remove_heap_region(uint32_t ucStartAddress, uint32_t xSizeInBytes)
{
#ifdef RT_USING_HEAP
    dlist_t *pos, *n;
    add_heap_wrapper_t *heap;

    if (list_empty(&added_heaps)) {
        return;
    }

    sys_enter_critical();
    list_for_each_safe(pos, n, &added_heaps) {
        heap = list_entry(pos, add_heap_wrapper_t, list);
        if ((uint32_t)heap->_heap_added.start_addr == ucStartAddress &&
                        heap->_heap_added.pool_size == RT_ALIGN_DOWN(xSizeInBytes, RT_ALIGN_SIZE)) {
            if (rt_memheap_detach(&heap->_heap_added) == RT_EOK) {
                dbg_print(NOTICE, "heap[%s] is removed\r\n", heap->name);
                list_del(&heap->list);
                sys_mfree(heap);
            }
            break;
        }
    }
    sys_exit_critical();
#endif
}

void dump_mem_block_list(void)
{
#ifdef RT_USING_HEAP
    // rt_memheap_dump() 在 RT-Thread 5.3.0 中不存在，使用替代方案
    // rt_memheap_dump();
    rt_kprintf("Memory heap dump not available in this RT-Thread version\n");
#endif /* RT_USING_HEAP */
}

/*!
    \brief      return RTOS current task handle
    \param[in]  none
    \param[out] none
    \retval     current task handle
*/
os_task_t sys_current_task_handle_get(void)
{
    return rt_thread_self();
}

/*!
    \brief      return RTOS current task stack depth from special sp index
    \param[in]  cur_sp sp index
    \param[out] none
    \retval     stack depth
*/
int32_t sys_current_task_stack_depth(unsigned long cur_sp)
{
    rt_thread_t thread;
    uint32_t depth;

    thread = rt_thread_self();
    sys_enter_critical();
    depth = (uint32_t)thread->stack_addr + (uint32_t)thread->stack_size - (uint32_t)thread->sp;
    sys_exit_critical();

    return depth;
}


/*!
    \brief      change the priority of a task
    \param[in]  task: Task handle.
    \param[in]  priority: priority to set to the task.
    \param[out] none
    \retval     none
*/
void sys_priority_set(void *task, os_prio_t priority)
{
    if (rt_thread_control((os_task_t)task, RT_THREAD_CTRL_CHANGE_PRIORITY, (void *)&priority))
        dbg_print(ERR, "sys_priority_set, failed\r\n");
}

/*!
    \brief      get the priority of a task
    \param[in]  task: Task handle.
    \param[out] priority to the task
    \retval     none
*/
os_prio_t sys_priority_get(void *task)
{
    return rt_sched_thread_get_curr_prio((rt_thread_t)task);
}

/* FreeRTOS compatibility layer - critical section tracking */
static volatile uint32_t critical_nesting = 0;
static rt_base_t critical_level = 0;

uint32_t vPortInCritical(void)
{
    return critical_nesting;
}

void vPortEnterCritical(void)
{
    if (critical_nesting == 0) {
        critical_level = rt_hw_interrupt_disable();
    }
    critical_nesting++;

    // 检测异常的嵌套层数
    if (critical_nesting > 20) {
        rt_kprintf("CRITICAL: critical_nesting = %d, possible stack corruption!\r\n", critical_nesting);
    }
}

void vPortExitCritical(void)
{
    if (critical_nesting > 0) {
        critical_nesting--;
        if (critical_nesting == 0) {
            rt_hw_interrupt_enable(critical_level);
        }
    } else {
        rt_kprintf("CRITICAL: critical_nesting underflow!\r\n");
    }
}

/*!
    \brief      rtos in critical
    \param[in]  none
    \param[out] 1: in critical context, 0: not in critical context
    \retval     interrupt status in the critical nesting
*/
uint32_t sys_in_critical(void)
{
   return vPortInCritical();
}

/*!
    \brief      rtos enter critical
    \param[in]  none
    \param[out] none
    \retval     none
*/
void sys_enter_critical(void)
{
    vPortEnterCritical();
}

/*!
    \brief      rtos exit critical
    \param[in]  none
    \param[out] none
    \retval     none
*/
void sys_exit_critical(void)
{
    vPortExitCritical();
}

/*!
    \brief      OS IRQ service hook called just after the ISR starts
    \param[in]  none
    \param[out] none
    \retval     none
*/
void sys_int_enter(void)
{
    rt_interrupt_enter();
}

/*!
    \brief      OS IRQ service hook called before the ISR exits
    \param[in]  none
    \param[out] none
    \retval     none
*/
void sys_int_exit(void)
{
    rt_interrupt_leave();
}

/*!
    \brief      set rtos power save mode
    \param[in]  mode  0: SYS_PS_OFF, 1: SYS_PS_DEEP_SLEEP
    \param[out] none
    \retval     none
*/
void sys_ps_set(uint8_t mode)
{
#ifdef RT_USING_PM
    if (mode == SYS_PS_DEEP_SLEEP) {
        for (int i = PM_SLEEP_MODE_NONE; i < PM_SLEEP_MODE_DEEP; i++) {
            rt_pm_release_all(i);
        }
        rt_pm_request(PM_SLEEP_MODE_DEEP);
    } else {
        rt_pm_request(PM_SLEEP_MODE_NONE);
    }
#endif
    sys_ps_mode = mode;
}

/*!
    \brief      get rtos power save mode
    \param[in]  none
    \param[out] none
    \retval     current rtos power mode, 0: SYS_PS_OFF, 1: SYS_PS_DEEP_SLEEP
*/
uint8_t sys_ps_get(void)
{
    return sys_ps_mode;
}

/*!
    \brief      get cpu sleep time and stats time
    \param[out] stats_ms
    \param[out] sleep_ms
    \retval     none
*/
void sys_cpu_sleep_time_get(uint32_t *stats_ms, uint32_t *sleep_ms)
{
#ifdef RT_USING_PM
    rtthread_cpu_sleep_time_get(stats_ms, sleep_ms);
#else
    *stats_ms = 0;
    *sleep_ms = 0;
#endif
    return;
}

/*!
    \brief      show cpu usage percentage per task
    \retval     none
*/
void sys_cpu_stats(void)
{
    /* TODO */
}

/*!
    \brief      check task exist or not
    \param[in]  name: task name
    \param[out] none
    \retval     1: task exist, 0: task not exist
*/
uint8_t sys_task_exist(const uint8_t *name)
{
    if (rt_thread_find((char *)name) == NULL)
        return 0;

    return 1;
}
