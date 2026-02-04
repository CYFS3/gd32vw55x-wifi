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

/*!
    \file    sys_arch.c
    \brief   System architecture functions for LWIP.

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

//*****************************************************************************
//
// Include OS functionality.
//
//*****************************************************************************

/* ------------------------ System architecture includes ----------------------------- */
#include "arch/sys_arch.h"

/* ------------------------ RT-Thread & wrapper_os includes -------------------------- */
#include <stdbool.h>     // 提供 true/false 定义
#include "wrapper_os.h"  // 提供 sys_queue_xxx, sys_sema_xxx 等函数声明

/* ------------------------ lwIP includes --------------------------------- */
#include "lwip/opt.h"

#include "lwip/debug.h"
#include "lwip/def.h"
#include "lwip/sys.h"
#include "lwip/mem.h"
#include "lwip/stats.h"
#include "dbg_print.h"

/*---------------------------------------------------------------------------*
 * Routine:  sys_mbox_new
 *---------------------------------------------------------------------------*
 * Description:
 *      Creates a new mailbox
 * Inputs:
 *      int size                -- Size of elements in the mailbox
 * Outputs:
 *      sys_mbox_t              -- Handle to new mailbox
 *---------------------------------------------------------------------------*/
err_t sys_mbox_new( sys_mbox_t *pxMailBox, int iSize )
{
    err_t xReturn = ERR_MEM;

    if (sys_queue_init(pxMailBox, iSize, sizeof(void *)) == 0)
    {
        xReturn = ERR_OK;
        SYS_STATS_INC_USED( mbox );
    }

    return xReturn;
}

/*---------------------------------------------------------------------------*
 * Routine:  sys_mbox_free
 *---------------------------------------------------------------------------*
 * Description:
 *      Deallocates a mailbox. If there are messages still present in the
 *      mailbox when the mailbox is deallocated, it is an indication of a
 *      programming error in lwIP and the developer should be notified.
 * Inputs:
 *      sys_mbox_t mbox         -- Handle of mailbox
 * Outputs:
 *      sys_mbox_t              -- Handle to new mailbox
 *---------------------------------------------------------------------------*/
void sys_mbox_free( sys_mbox_t *pxMailBox )
{
    if (!sys_queue_is_empty(pxMailBox)) {
        dbg_print(ERR, "free mbox not empty\r\n");
        return;
    }

    sys_queue_free(pxMailBox);
}

/*---------------------------------------------------------------------------*
 * Routine:  sys_mbox_post
 *---------------------------------------------------------------------------*
 * Description:
 *      Post the "msg" to the mailbox.
 * Inputs:
 *      sys_mbox_t mbox         -- Handle of mailbox
 *      void *data              -- Pointer to data to post
 *---------------------------------------------------------------------------*/
void sys_mbox_post( sys_mbox_t *pxMailBox, void *pxMessageToPost )
{
    sys_queue_write(pxMailBox, &pxMessageToPost, -1, false);
}

/*---------------------------------------------------------------------------*
 * Routine:  sys_mbox_trypost
 *---------------------------------------------------------------------------*
 * Description:
 *      Try to post the "msg" to the mailbox.  Returns immediately with
 *      error if cannot.
 * Inputs:
 *      sys_mbox_t mbox         -- Handle of mailbox
 *      void *msg               -- Pointer to data to post
 * Outputs:
 *      err_t                   -- ERR_OK if message posted, else ERR_MEM
 *                                  if not.
 *---------------------------------------------------------------------------*/
err_t sys_mbox_trypost( sys_mbox_t *pxMailBox, void *pxMessageToPost )
{
    err_t xReturn;

    if (sys_queue_write(pxMailBox, &pxMessageToPost, 0, false) == 0)
    {
        xReturn = ERR_OK;
    }
    else
    {
        /* The queue was already full. */
        xReturn = ERR_MEM;
        SYS_STATS_INC( mbox.err );
    }

    return xReturn;
}


/*---------------------------------------------------------------------------*
 * Routine:  sys_mbox_trypost_fromisr
 *---------------------------------------------------------------------------*
 * Description:
 *      Try to post the "msg" to the mailbox.  Returns immediately with
 *      error if cannot.
 * Inputs:
 *      sys_mbox_t mbox         -- Handle of mailbox
 *      void *msg               -- Pointer to data to post
 * Outputs:
 *      err_t                   -- ERR_OK if message posted, else ERR_MEM
 *                                  if not.
 *---------------------------------------------------------------------------*/
err_t sys_mbox_trypost_fromisr( sys_mbox_t *pxMailBox, void *pxMessageToPost )
{
    err_t xReturn;

    if (sys_queue_write(pxMailBox, &pxMessageToPost, 0, true) == 0)
    {
        xReturn = ERR_OK;
    }
    else
    {
        /* The queue was already full. */
        xReturn = ERR_MEM;
        SYS_STATS_INC( mbox.err );
    }

    return xReturn;
}
/*---------------------------------------------------------------------------*
 * Routine:  sys_arch_mbox_fetch
 *---------------------------------------------------------------------------*
 * Description:
 *      Blocks the thread until a message arrives in the mailbox, but does
 *      not block the thread longer than "timeout" milliseconds (similar to
 *      the sys_arch_sem_wait() function). The "msg" argument is a result
 *      parameter that is set by the function (i.e., by doing "*msg =
 *      ptr"). The "msg" parameter maybe NULL to indicate that the message
 *      should be dropped.
 *
 *      The return values are the same as for the sys_arch_sem_wait() function:
 *      Number of milliseconds spent waiting or SYS_ARCH_TIMEOUT if there was a
 *      timeout.
 *
 *      Note that a function with a similar name, sys_mbox_fetch(), is
 *      implemented by lwIP.
 * Inputs:
 *      sys_mbox_t mbox         -- Handle of mailbox
 *      void **msg              -- Pointer to pointer to msg received
 *      u32_t timeout           -- Number of milliseconds until timeout
 * Outputs:
 *      u32_t                   -- SYS_ARCH_TIMEOUT if timeout, else number
 *                                  of milliseconds until received.
 *---------------------------------------------------------------------------*/
u32_t sys_arch_mbox_fetch( sys_mbox_t *pxMailBox, void **ppvBuffer, u32_t ulTimeOut )
{
    void *pvDummy = NULL;
    uint32_t xStartTime;
    unsigned long ulReturn;
    int timeout;

    xStartTime = sys_now();

    if (ulTimeOut == 0)
        timeout = -1;
    else
        timeout = ulTimeOut;

    /* note: size of messages in lwip mbox is sizeof(void *) */
    if (sys_queue_read(pxMailBox, &pvDummy, timeout, false))
    {
        ulReturn = SYS_ARCH_TIMEOUT;
    }
    else
    {
        ulReturn = sys_now() - xStartTime;
    }

    if (ppvBuffer)
        *ppvBuffer = pvDummy;

    return ulReturn;
}

/*---------------------------------------------------------------------------*
 * Routine:  sys_arch_mbox_tryfetch
 *---------------------------------------------------------------------------*
 * Description:
 *      Similar to sys_arch_mbox_fetch, but if message is not ready
 *      immediately, we'll return with SYS_MBOX_EMPTY.  On success, 0 is
 *      returned.
 * Inputs:
 *      sys_mbox_t mbox         -- Handle of mailbox
 *      void **msg              -- Pointer to pointer to msg received
 * Outputs:
 *      u32_t                   -- SYS_MBOX_EMPTY if no messages.  Otherwise,
 *                                  return ERR_OK.
 *---------------------------------------------------------------------------*/
u32_t sys_arch_mbox_tryfetch( sys_mbox_t *pxMailBox, void **ppvBuffer )
{
    void *pvDummy;
    unsigned long ulReturn = ERR_OK;

    if( ppvBuffer== NULL )
    {
        ppvBuffer = &pvDummy;
    }

    if (sys_queue_read(pxMailBox, &( *ppvBuffer ), 0, false))
    {
        ulReturn = SYS_MBOX_EMPTY;
    }

    return ulReturn;
}

/*---------------------------------------------------------------------------*
 * Routine:  sys_sem_new
 *---------------------------------------------------------------------------*
 * Description:
 *      Creates and returns a new semaphore. The "ucCount" argument specifies
 *      the initial state of the semaphore.
 *      NOTE: Currently this routine only creates counts of 1 or 0
 * Inputs:
 *      sys_mbox_t mbox         -- Handle of mailbox
 *      u8_t ucCount              -- Initial ucCount of semaphore (1 or 0)
 * Outputs:
 *      sys_sem_t               -- Created semaphore or 0 if could not create.
 *---------------------------------------------------------------------------*/
err_t sys_sem_new( sys_sem_t *pxSemaphore, u8_t ucCount )
{
    err_t xReturn = ERR_MEM;

    if (sys_sema_init_ext(pxSemaphore, 1, ucCount) == 0)
    {
        xReturn = ERR_OK;
        SYS_STATS_INC_USED( sem );
    }
    else
    {
        SYS_STATS_INC( sem.err );
    }

    return xReturn;
}

/*---------------------------------------------------------------------------*
 * Routine:  sys_arch_sem_wait
 *---------------------------------------------------------------------------*
 * Description:
 *      Blocks the thread while waiting for the semaphore to be
 *      signaled. If the "timeout" argument is non-zero, the thread should
 *      only be blocked for the specified time (measured in
 *      milliseconds).
 *
 *      If the timeout argument is non-zero, the return value is the number of
 *      milliseconds spent waiting for the semaphore to be signaled. If the
 *      semaphore wasn't signaled within the specified time, the return value is
 *      SYS_ARCH_TIMEOUT. If the thread didn't have to wait for the semaphore
 *      (i.e., it was already signaled), the function may return zero.
 *
 *      Notice that lwIP implements a function with a similar name,
 *      sys_sem_wait(), that uses the sys_arch_sem_wait() function.
 * Inputs:
 *      sys_sem_t sem           -- Semaphore to wait on
 *      u32_t timeout           -- Number of milliseconds until timeout
 * Outputs:
 *      u32_t                   -- Time elapsed or SYS_ARCH_TIMEOUT.
 *---------------------------------------------------------------------------*/
u32_t sys_arch_sem_wait( sys_sem_t *pxSemaphore, u32_t ulTimeOut )
{
    uint32_t xStartTime;
    unsigned long ulReturn;

    xStartTime = sys_now();

    if (sys_sema_down(pxSemaphore, ulTimeOut))
    {
        /* Timed out. */
        ulReturn = SYS_ARCH_TIMEOUT;
    }
    else
    {
        ulReturn = sys_now() - xStartTime;
    }

    return ulReturn;
}

/*---------------------------------------------------------------------------*
 * Routine:  sys_sem_signal
 *---------------------------------------------------------------------------*
 * Description:
 *      Signals (releases) a semaphore
 * Inputs:
 *      sys_sem_t sem           -- Semaphore to signal
 *---------------------------------------------------------------------------*/
void sys_sem_signal( sys_sem_t *pxSemaphore )
{
    sys_sema_up(pxSemaphore);
}

/*---------------------------------------------------------------------------*
 * Routine:  sys_sem_free
 *---------------------------------------------------------------------------*
 * Description:
 *      Deallocates a semaphore
 * Inputs:
 *      sys_sem_t sem           -- Semaphore to free
 *---------------------------------------------------------------------------*/
void sys_sem_free( sys_sem_t *pxSemaphore )
{
    SYS_STATS_DEC(sem.used);
    sys_sema_free(pxSemaphore);
}

/*---------------------------------------------------------------------------*
 * Routine:  sys_init
 *---------------------------------------------------------------------------*
 * Description:
 *      Initialize sys arch
 *---------------------------------------------------------------------------*/
void sys_init(void)
{
}

u32_t sys_now(void)
{
    return sys_os_now(false);
}

/*---------------------------------------------------------------------------*
 * Routine:  sys_thread_new
 *---------------------------------------------------------------------------*
 * Description:
 *      Starts a new thread with priority "prio" that will begin its
 *      execution in the function "thread()". The "arg" argument will be
 *      passed as an argument to the thread() function. The id of the new
 *      thread is returned. Both the id and the priority are system
 *      dependent.
 * Inputs:
 *      char *name              -- Name of thread
 *      void (* thread)(void *arg) -- Pointer to function to run.
 *      void *arg               -- Argument passed into function
 *      int stacksize           -- Required stack amount in bytes
 *      int prio                -- Thread priority
 * Outputs:
 *      sys_thread_t            -- Pointer to per-thread timeouts.
 *---------------------------------------------------------------------------*/
sys_thread_t sys_thread_new( const char *pcName, void( *pxThread )( void *pvParameters ), void *pvArg, int iStackSize, int iPriority )
{
    return sys_task_create_dynamic((const uint8_t *)pcName, iStackSize, iPriority, pxThread, pvArg);
}

/*-------------------------------------------------------------------------*
 * End of File:  sys_arch.c
 *-------------------------------------------------------------------------*/
