/*
 * Kernel Schedualer Interface
 *
 * File Name:   sched.c
 * Author:      Yang Yujun
 * E-mail:      <yujiantianhu@163.com>
 * Created on:  2023.07.09
 *
 * Copyright (c) 2023   Yang Yujun <yujiantianhu@163.com>
 *
 */

/*!< The includes */
#include <kernel/kernel.h>
#include <kernel/sched.h>

/*!< The globals */
kuint32_t g_sched_preempt_cnt = 0;

/*!< TCB */
struct scheduler_table sgrt_scheduler_table =
{
    .max_tidarr		= 0,
    .max_tids		= THREAD_MAX_NUM,
    .max_tidset		= 0,
    .ref_tidarr		= 0,
    .sgrt_cnt		= {},

    .sgrt_ready		= LIST_HEAD_INIT(&sgrt_scheduler_table.sgrt_ready),
    .sgrt_suspend	= LIST_HEAD_INIT(&sgrt_scheduler_table.sgrt_suspend),
    .sgrt_sleep		= LIST_HEAD_INIT(&sgrt_scheduler_table.sgrt_sleep),

    .sprt_work		= mrt_nullptr,
    .sprt_tids		= mrt_nullptr,
    .sprt_tid_array	= { mrt_nullptr },
    .sgrt_lock		= SPIN_LOCK_INIT(),
};

/*!< save to (*.data) section, do not defines in stack */
static struct scheduler_context sgrt_context;
static kuint32_t thread_schedule_ref = 0;

/*!< The defines */
#define SCHED_THREAD_HANDLER(tid)               __THREAD_HANDLER(&sgrt_scheduler_table, tid)
#define SCHED_RUNNING_THREAD                    __THREAD_RUNNING_LIST(&sgrt_scheduler_table)
#define SCHED_READY_LIST                        __THREAD_READY_LIST(&sgrt_scheduler_table)
#define SCHED_SUSPEND_LIST                      __THREAD_SUSPEND_LIST(&sgrt_scheduler_table)
#define SCHED_SLEEP_LIST                        __THREAD_SLEEP_LIST(&sgrt_scheduler_table)
#define __SCHED_LOCK                            sgrt_scheduler_table.sgrt_lock

/*!< set thread status */
#define __SET_THREAD_STATUS(tid, value)	\
    do {	\
        if ((value) < NR_THREAD_STATUS_MAX)	\
        {	\
            struct thread *sprt_task = SCHED_THREAD_HANDLER(tid);	\
            sprt_task->to_status = (value);	\
        }	\
    } while (0)

#define __SYNC_THREAD_STATUS(tid, value)	\
    do {	\
        if ((value) < NR_THREAD_STATUS_MAX)	\
        {	\
            struct thread *sprt_task = SCHED_THREAD_HANDLER(tid);	\
            sprt_task->status = (value);	\
            sprt_task->to_status = NR_THREAD_NONE;    \
        }	\
    } while (0)

/*!< get thread status */
#define __GET_THREAD_STATUS(tid)	\
({	\
    struct thread *sprt_task = SCHED_THREAD_HANDLER(tid);	\
    sprt_task->status;	\
})

/*!< The functions */
static kint32_t __find_thread_from_scheduler(tid_t tid, struct list_head *sprt_head);
static kint32_t __schedule_add_status_list(struct thread *sprt_thread, struct list_head *sprt_head);
static void __schedule_del_status_list(struct thread *sprt_thread, struct list_head *sprt_head);
static kint32_t schedule_despoil_work_role(tid_t tid);
static kint32_t schedule_reinstall_work_role(tid_t tid);
static kint32_t schedule_add_ready_list(tid_t tid);
static kint32_t schedule_detach_ready_list(tid_t tid);
static kint32_t schedule_add_suspend_list(tid_t tid);
static kint32_t schedule_detach_suspend_list(tid_t tid);
static kint32_t schedule_add_sleep_list(tid_t tid);
static kint32_t schedule_detach_sleep_list(tid_t tid);

/* -------------------------------------------------------------------------- */
/*!< API functions */
/*!
 * @brief	get current thread from tcb
 * @param  	tid
 * @retval 	running thread
 * @note   	none
 */
struct thread *get_current_thread(void)
{
    return SCHED_RUNNING_THREAD;
}

/*!
 * @brief	get ready thread list head from tcb
 * @param  	tid
 * @retval 	ready thread list
 * @note   	none
 */
struct list_head *get_ready_thread_table(void)
{
    return SCHED_READY_LIST;
}

/*!
 * @brief	get thread from tcb
 * @param  	tid
 * @retval 	thread
 * @note   	none
 */
struct thread *get_thread_handle(tid_t tid)
{
    if (tid >= THREAD_MAX_NUM)
        return mrt_nullptr;
    
    return SCHED_THREAD_HANDLER(tid);
}

/*!
 * @brief	set name to thread
 * @param  	name: thread name
 * @retval 	none
 * @note   	none
 */
void thread_set_name(tid_t tid, const kchar_t *name)
{
    struct thread *sprt_thread;

    if (!name || !(*name))
        return;

    sprt_thread = SCHED_THREAD_HANDLER(tid);

    memset(sprt_thread->name, 0, THREAD_NAME_SIZE);
    kstrlcpy(sprt_thread->name, name, THREAD_NAME_SIZE);
}

/*!
 * @brief	set name to current thread
 * @param  	name: thread name
 * @retval 	none
 * @note   	none
 */
void thread_set_self_name(const kchar_t *name)
{
    struct thread *sprt_work;

    if (!name || !(*name))
        return;

    sprt_work = SCHED_RUNNING_THREAD;

    memset(sprt_work->name, 0, THREAD_NAME_SIZE);
    kstrlcpy(sprt_work->name, name, THREAD_NAME_SIZE);
}

/*!
 * @brief	set state to thread
 * @param  	sprt_thread: name handler
 * @retval 	none
 * @note   	none
 */
void thread_set_state(struct thread *sprt_thread, kuint32_t state)
{
    __SET_THREAD_STATUS(sprt_thread->tid, state);
}

/*!
 * @brief   get the scheduler lock
 * @param   none
 * @retval  lock
 * @note    none
 */
struct spin_lock *scheduler_lock(void)
{
    return &__SCHED_LOCK;
}

/*!
 * @brief	find a free tid
 * @param  	i_start: base
 * @param	count: limit
 * @retval 	none
 * @note   	none
 */
tid_t get_unused_tid_from_scheduler(kuint32_t i_start, kuint32_t count)
{
    kuint32_t i;

    spin_lock_irqsave(&__SCHED_LOCK);
    for (i = i_start; i < (i_start + count); i++)
    {
        if (!SCHED_THREAD_HANDLER(i))
        {
            spin_unlock_irqrestore(&__SCHED_LOCK);
            return i;
        }
    }

    spin_unlock_irqrestore(&__SCHED_LOCK);

    return -ER_MORE;
}

/*!
 * @brief	stat on the number of scheduling
 * @param  	none
 * @retval 	none
 * @note   	none
 */
static void scheduler_record(void)
{
    struct scheduler_table *sprt_tab = &sgrt_scheduler_table;

    if ((sprt_tab->sgrt_cnt.sched_cnt++) >= __THREAD_MAX_STATS)
    {
        sprt_tab->sgrt_cnt.sched_cnt = 0;
        sprt_tab->sgrt_cnt.cnt_out++;
    }
}

/*!
 * @brief	get the stats of scheduling
 * @param  	none
 * @retval 	stats
 * @note   	none
 */
kuint64_t scheduler_stats_get(void)
{
    struct scheduler_table *sprt_tab = &sgrt_scheduler_table;
    kuint64_t sum;

    sprt_tab = &sgrt_scheduler_table;

    spin_lock_irqsave(&__SCHED_LOCK);
    sum = (__THREAD_MAX_STATS * sprt_tab->sgrt_cnt.cnt_out + sprt_tab->sgrt_cnt.sched_cnt);
    spin_unlock_irqrestore(&__SCHED_LOCK);

    return sum;
}

/*!
 * @brief	suspend current thread
 * @param  	none
 * @retval 	0: fail; 1: succuess
 * @note   	suspend current thread, and switch to next
 */
void schedule_self_suspend(void)
{
    spin_lock_irqsave(&__SCHED_LOCK);
    __SET_THREAD_STATUS(SCHED_RUNNING_THREAD->tid, NR_THREAD_SUSPEND);
    spin_unlock_irqrestore(&__SCHED_LOCK);

    schedule_thread();
}

/*!
 * @brief	kill current thread
 * @param  	none
 * @retval 	0: fail; 1: succuess
 * @note   	kill current thread, and switch to next
 */
void schedule_self_sleep(void)
{
    spin_lock_irqsave(&__SCHED_LOCK);
    __SET_THREAD_STATUS(SCHED_RUNNING_THREAD->tid, NR_THREAD_SLEEP);
    spin_unlock_irqrestore(&__SCHED_LOCK);

    schedule_thread();
}

/*!
 * @brief	suspend another thread
 * @param  	tid: target thread
 * @retval 	err code
 * @note   	none
 */
kint32_t schedule_thread_suspend(tid_t tid)
{
    kint32_t retval;

    if (tid == SCHED_RUNNING_THREAD->tid)
    {
        schedule_self_suspend();
        return ER_NORMAL;
    }

    spin_lock_irqsave(&__SCHED_LOCK);
    __SET_THREAD_STATUS(tid, NR_THREAD_SUSPEND);

    retval = schedule_thread_switch(tid);
    spin_unlock_irqrestore(&__SCHED_LOCK);
    
    return retval;
}

/*!
 * @brief	kill another thread
 * @param  	tid: target thread
 * @retval 	err code
 * @note   	none
 */
kint32_t schedule_thread_sleep(tid_t tid)
{
    kint32_t retval;

    if (tid == SCHED_RUNNING_THREAD->tid)
    {
        schedule_self_sleep();
        return ER_NORMAL;
    }

    spin_lock_irqsave(&__SCHED_LOCK);
    __SET_THREAD_STATUS(tid, NR_THREAD_SLEEP);

    retval = schedule_thread_switch(tid);
    spin_unlock_irqrestore(&__SCHED_LOCK);
    
    return retval;
}

/*!
 * @brief	wake up another thread
 * @param  	tid: target thread
 * @retval 	err code
 * @note   	if target thread is suspending, wake it up and add to ready list
 */
kint32_t schedule_thread_wakeup(tid_t tid)
{
    kuint32_t status;
    kint32_t retval;

	spin_lock_irqsave(&__SCHED_LOCK);

    status = __GET_THREAD_STATUS(tid);
    if ((status != NR_THREAD_SUSPEND) &&
        (status != NR_THREAD_SLEEP))
    {
        retval = -ER_UNVALID;
        goto END;
    }

    __SET_THREAD_STATUS(tid, NR_THREAD_READY);
    retval = schedule_thread_switch(tid);

END:
	spin_unlock_irqrestore(&__SCHED_LOCK);
    return retval;
}

/*!
 * @brief	check if ready list is empty
 * @param  	none
 * @retval 	1: empty; 0: not empty
 * @note   	none
 */
kbool_t is_ready_thread_empty(void)
{
    return !!mrt_list_head_empty(SCHED_READY_LIST);
}

/*!
 * @brief	check if suspend list is empty
 * @param  	none
 * @retval 	1: empty; 0: not empty
 * @note   	none
 */
kbool_t is_suspend_thread_empty(void)
{
    return !!mrt_list_head_empty(SCHED_SUSPEND_LIST);
}

/*!
 * @brief	check if sleep list is empty
 * @param  	none
 * @retval 	1: empty; 0: not empty
 * @note   	none
 */
kbool_t is_sleep_thread_empty(void)
{
    return !!mrt_list_head_empty(SCHED_SLEEP_LIST);
}

/*!
 * @brief	get the highest ready thread (if ready list is not empty)
 * @param  	none
 * @retval 	first thread
 * @note   	none
 */
struct thread *get_first_ready_thread(void)
{
    kbool_t existed = mrt_list_head_empty(SCHED_READY_LIST);
    return existed ? mrt_nullptr : mrt_list_first_entry(SCHED_READY_LIST, struct thread, sgrt_link);
}

/*!
 * @brief	get the highest suspend thread (if suspend list is not empty)
 * @param  	none
 * @retval 	first thread
 * @note   	none
 */
struct thread *get_first_suspend_thread(void)
{
    kbool_t existed = mrt_list_head_empty(SCHED_SUSPEND_LIST);
    return existed ? mrt_nullptr : mrt_list_first_entry(SCHED_SUSPEND_LIST, struct thread, sgrt_link);
}

/*!
 * @brief	get the highest sleep thread (if sleep list is not empty)
 * @param  	none
 * @retval 	first thread
 * @note   	none
 */
struct thread *get_first_sleep_thread(void)
{
    kbool_t existed = mrt_list_head_empty(SCHED_SLEEP_LIST);
    return existed ? mrt_nullptr : mrt_list_first_entry(SCHED_SLEEP_LIST, struct thread, sgrt_link);
}

/*!
 * @brief	check if thread is valid
 * @param  	none
 * @retval 	1: valid; 0: unvalid
 * @note   	none
 */
kbool_t is_thread_valid(tid_t tid)
{
    struct thread *sprt_thread = get_thread_handle(tid);
    return ((sprt_thread->status != NR_THREAD_SLEEP) && (sprt_thread->to_status != NR_THREAD_SLEEP));
}

/*!
 * @brief	get next ready thread
 * @param  	sprt_prev
 * @retval 	next
 * @note   	none
 */
struct thread *next_ready_thread(struct thread *sprt_prev)
{
    if (!sprt_prev)
        return get_first_ready_thread();

    if (mrt_list_head_empty(SCHED_READY_LIST) ||
        mrt_list_head_empty(&sprt_prev->sgrt_link) ||
        mrt_list_head_until(sprt_prev, SCHED_READY_LIST, sgrt_link))
        return mrt_nullptr;

    return mrt_list_next_entry(sprt_prev, sgrt_link);
}

/*!
 * @brief	get next suspend thread
 * @param  	sprt_prev
 * @retval 	next
 * @note   	none
 */
struct thread *next_suspend_thread(struct thread *sprt_prev)
{
    if (!sprt_prev)
        return get_first_suspend_thread();

    if (mrt_list_head_empty(SCHED_SUSPEND_LIST) ||
        mrt_list_head_empty(&sprt_prev->sgrt_link) ||
        mrt_list_head_until(sprt_prev, SCHED_SUSPEND_LIST, sgrt_link))
        return mrt_nullptr;

    return mrt_list_next_entry(sprt_prev, sgrt_link);
}

/*!
 * @brief	get next sleep thread
 * @param  	sprt_prev
 * @retval 	next
 * @note   	none
 */
struct thread *next_sleep_thread(struct thread *sprt_prev)
{
    if (!sprt_prev)
        return get_first_sleep_thread();

    if (mrt_list_head_empty(SCHED_SLEEP_LIST) ||
        mrt_list_head_empty(&sprt_prev->sgrt_link) ||
        mrt_list_head_until(sprt_prev, SCHED_SLEEP_LIST, sgrt_link))
        return mrt_nullptr;

    return mrt_list_next_entry(sprt_prev, sgrt_link);
}

/*!
 * @brief	switch thread from one status to another status
 * @param  	tid: target thread
 * @param	src: current status
 * @param	dst: target status
 * @retval 	err code
 * @note   	only the running thread need to save context; and only the ready thread maybe need to restore context
 */ 
kint32_t schedule_thread_switch(tid_t tid)
{
    kuint32_t src, dst;
    kint32_t retval;
    
    mrt_preempt_disable();

    src = SCHED_THREAD_HANDLER(tid)->status;
    dst = SCHED_THREAD_HANDLER(tid)->to_status;

    /*!<
     * thread switch:
     * (At all times, it is necessary to ensure that at least one thread (including idle threads) is running)
     * running ---> ready/suspend/sleep
     * ready ---> running/suspend/sleep
     * suspend ---> ready/sleep
     * sleep ---> ready/suspend
     *
     * (only running and ready status can be switched to any status)
     */
    if ((src == dst) || (dst >= NR_THREAD_STATUS_MAX))
        goto fail;

    /*!< for idle thread, only ready and running status can be chosen */
    if ((tid == THREAD_TID_IDLE) && ((dst != NR_THREAD_RUNNING) && (dst != NR_THREAD_READY)))
        goto fail;

    /*!< detached from current list */
    switch (src)
    {
        case NR_THREAD_RUNNING:
            if (schedule_reinstall_work_role(tid))
                goto fail;
            
            break;

        case NR_THREAD_READY:
            schedule_detach_ready_list(tid);
            break;

        case NR_THREAD_SUSPEND:
            schedule_detach_suspend_list(tid);
            break;

        case NR_THREAD_SLEEP:
            schedule_detach_sleep_list(tid);
            break;

        default:
            break;
    }

    mrt_barrier();

    /*!< add to new list */
    switch (dst)
    {
        case NR_THREAD_RUNNING:
            retval = schedule_despoil_work_role(tid);
            break;

        case NR_THREAD_READY:
            retval = schedule_add_ready_list(tid);
            break;

        case NR_THREAD_SUSPEND:
            retval = schedule_add_suspend_list(tid);
            break;

        case NR_THREAD_SLEEP:
            retval = schedule_add_sleep_list(tid);
            break;

        default:
            retval = -ER_ERROR;
            break;
    }

    if (retval < 0)
    {
        print_warn("switch thread failed ! current and target status is : %d, %d\n", src, dst);
        goto fail;
    }

    if (!SCHED_RUNNING_THREAD)
    {
        print_err("no thread is running !!! dangerous action !!!\n");
        goto fail;
    }

    /*!< update thread status */
    __SYNC_THREAD_STATUS(tid, dst);
    mrt_preempt_enable();

    return ER_NORMAL;
    
fail:
    __SYNC_THREAD_STATUS(tid, src);
    mrt_preempt_enable();

    return -ER_UNVALID;
}

/*!
 * @brief	change thread status to running
 * @param  	tid: target thread
 * @retval 	err code
 * @note   	only ready status can be switched to running!!!
 */
static kint32_t schedule_despoil_work_role(tid_t tid)
{
    struct thread *sprt_thread;
    struct thread *sprt_running;
    kint32_t retval;

    sprt_thread = SCHED_THREAD_HANDLER(tid);

    if (!sprt_thread)
        return -ER_FAULT;

    /*!< only ready status can be switched to running */
    if (NR_THREAD_READY != sprt_thread->status)
        return -ER_UNVALID;

    /*!< get current */
    sprt_running = SCHED_RUNNING_THREAD;

    if (sprt_running)
    {
        /*!< current thread add to ready list */
        retval = schedule_add_ready_list(sprt_running->tid);
        if (retval < 0)
            return retval;

        __SYNC_THREAD_STATUS(sprt_running->tid, NR_THREAD_READY);
    }

    /*!< update current */
    SCHED_RUNNING_THREAD = sprt_thread;

    return ER_NORMAL;
}

/*!
 * @brief	change current thread status
 * @param  	none
 * @retval 	err code
 * @note   	running ---> xxx
 */
static kint32_t schedule_reinstall_work_role(tid_t tid)
{
    struct thread *sprt_thread;

    if (tid != SCHED_RUNNING_THREAD->tid)
        return -ER_UNVALID;

    /*!< no thread ready; current should be set to idle thread */
    if (mrt_list_head_empty(SCHED_READY_LIST))
        return -ER_FAULT;

    /*!< get the first ready thread */
    sprt_thread = mrt_list_first_valid_entry(SCHED_READY_LIST, struct thread, sgrt_link);
    if (sprt_thread)
    {
        /*!< detached from ready list */
        schedule_detach_ready_list(sprt_thread->tid);

        /*!< ready thread ---> running */
        SCHED_RUNNING_THREAD = sprt_thread;
        mrt_barrier();
        __SYNC_THREAD_STATUS(sprt_thread->tid, NR_THREAD_RUNNING);
    }

    return sprt_thread ? ER_NORMAL : (-ER_FAILD);
}

/*!
 * @brief	change target thread status to ready
 * @param  	tid: target thread
 * @retval 	err code
 * @note   	add to ready list
 */
static kint32_t schedule_add_ready_list(tid_t tid)
{
    struct thread *sprt_thread;

    sprt_thread = SCHED_THREAD_HANDLER(tid);

    if (!sprt_thread)
        return -ER_FAULT;

    /*!< avoid duplicate additions */
    if (NR_THREAD_READY == sprt_thread->status)
        return -ER_UNVALID;

    return __schedule_add_status_list(sprt_thread, SCHED_READY_LIST);
}

/*!
 * @brief	change target thread status from ready
 * @param  	tid: target thread
 * @retval 	err code
 * @note   	del from ready list. it must be called by schedule_thread_switch, do not use alone !!!
 * 			(after detaching from the ready list, the thread will appear in a free state, 
 * 			so this function prohibits external calls to prevent the thread from leaving management and causing memory leakage)
 */
static kint32_t schedule_detach_ready_list(tid_t tid)
{
    struct thread *sprt_thread;

    sprt_thread = SCHED_THREAD_HANDLER(tid);

    if (!sprt_thread)
        return -ER_FAULT;

    /*!< check if is in ready status */
    if (NR_THREAD_READY != sprt_thread->status)
        return -ER_UNVALID;

    /*!< delete it */
    __schedule_del_status_list(sprt_thread, SCHED_READY_LIST);

    return ER_NORMAL;
}

/*!
 * @brief	change target thread status to suspend
 * @param  	tid: target thread
 * @retval 	err code
 * @note   	add to suspend list
 */
static kint32_t schedule_add_suspend_list(tid_t tid)
{
    struct thread *sprt_thread;

    sprt_thread = SCHED_THREAD_HANDLER(tid);

    if (!sprt_thread)
        return -ER_FAULT;

    /*!< avoid duplicate additions */
    if (NR_THREAD_SUSPEND == sprt_thread->status)
        return -ER_UNVALID;

    return __schedule_add_status_list(sprt_thread, SCHED_SUSPEND_LIST);
}

/*!
 * @brief	change target thread status from suspend
 * @param  	tid: target thread
 * @retval 	err code
 * @note   	del from suspend list. it must be called by schedule_thread_switch, do not use alone !!!
 * 			(after detaching from the suspend list, the thread will appear in a free state, 
 * 			so this function prohibits external calls to prevent the thread from leaving management and causing memory leakage)
 */
static kint32_t schedule_detach_suspend_list(tid_t tid)
{
    struct thread *sprt_thread;

    sprt_thread = SCHED_THREAD_HANDLER(tid);

    if (!sprt_thread)
        return -ER_FAULT;

    /*!< check if is in ready status */
    if (NR_THREAD_SUSPEND != sprt_thread->status)
        return -ER_UNVALID;

    /*!< delete it */
    __schedule_del_status_list(sprt_thread, SCHED_SUSPEND_LIST);

    return ER_NORMAL;
}

/*!
 * @brief	change target thread status to sleep
 * @param  	tid: target thread
 * @retval 	err code
 * @note   	add to sleep list
 */
static kint32_t schedule_add_sleep_list(tid_t tid)
{
    struct thread *sprt_thread;

    sprt_thread = SCHED_THREAD_HANDLER(tid);

    if (!sprt_thread)
        return -ER_FAULT;

    /*!< avoid duplicate additions */
    if (NR_THREAD_SLEEP == sprt_thread->status)
        return -ER_UNVALID;

    return __schedule_add_status_list(sprt_thread, SCHED_SLEEP_LIST);
}

/*!
 * @brief	change target thread status from sleep
 * @param  	tid: target thread
 * @retval 	err code
 * @note   	del from sleep list. it must be called by schedule_thread_switch, do not use alone !!!
 * 			(after detaching from the sleep list, the thread will appear in a free state, 
 * 			so this function prohibits external calls to prevent the thread from leaving management and causing memory leakage)
 */
static kint32_t schedule_detach_sleep_list(tid_t tid)
{
    struct thread *sprt_thread;

    sprt_thread = SCHED_THREAD_HANDLER(tid);

    if (!sprt_thread)
        return -ER_FAULT;

    /*!< check if is in ready status */
    if (NR_THREAD_SLEEP != sprt_thread->status)
        return -ER_UNVALID;

    /*!< delete it */
    __schedule_del_status_list(sprt_thread, SCHED_SLEEP_LIST);

    return ER_NORMAL;
}

/*!
 * @brief	check if target thread is in the target list
 * @param  	tid: target thread
 * @param	sprt_head: ready/suspend/sleep list
 * @retval 	err code
 * @note   	find thread if it is exsited
 */
static kint32_t __find_thread_from_scheduler(tid_t tid, struct list_head *sprt_head)
{
    struct thread *sprt_anyTask;
    struct thread *sprt_thread = SCHED_THREAD_HANDLER(tid); 

    if (mrt_list_head_empty(sprt_head))
        return -ER_NOTFOUND;

    foreach_list_next_entry(sprt_anyTask, sprt_head, sgrt_link)
    {
        if (sprt_anyTask == sprt_thread)
            return tid;
    }

    return -ER_NOTFOUND;
}

/*!
 * @brief	add to target list
 * @param  	sprt_thread: target thread
 * @param	sprt_head: ready/suspend/sleep list
 * @retval 	err code
 * @note   	every thread will be sorted by priority
 */
static kint32_t __schedule_add_status_list(struct thread *sprt_thread, struct list_head *sprt_head)
{
    struct thread *sprt_anyTask;
    kuint32_t iPriority, iPriority2;

    if ((!sprt_thread) || (!sprt_head))
        return -ER_FAULT;

    /*!< get priority */
    iPriority = thread_get_priority(sprt_thread->sprt_attr);

    /*!< target list is empty, add directly */
    if (mrt_list_head_empty(sprt_head))
        goto END;

    /*!< fault tolerance mechanism: check if this thread has been added to the list, and exit directly if it has been added */
    if (__find_thread_from_scheduler(sprt_thread->tid, sprt_head) >= 0)
        return ER_NORMAL;

#if CONFIG_ROLL_POLL
    list_head_add_tail(sprt_head, &sprt_thread->sgrt_link);
    return ER_NORMAL;

#else
    /*!< traversing the ready list, inserting new thread into the tail of thread which is the same priority */
    foreach_list_prev_entry(sprt_anyTask, sprt_head, sgrt_link)
    {
        iPriority2 = thread_get_priority(sprt_anyTask->sprt_attr);
        if (__THREAD_IS_LOW_PRIO(iPriority, iPriority2))
        {
            /*!< priority from high to low */
            list_head_add_head(&sprt_anyTask->sgrt_link, &sprt_thread->sgrt_link);
            return ER_NORMAL;
        }
    }

#endif

END:
    /*!< target thread has the highest priority */
    list_head_add_head(sprt_head, &sprt_thread->sgrt_link);

    return ER_NORMAL;
}

/*!
 * @brief	delete from target list
 * @param  	sprt_thread: target thread
 * @param	sprt_head: ready/suspend/sleep list
 * @retval 	err code
 * @note   	none
 */
static void __schedule_del_status_list(struct thread *sprt_thread, struct list_head *sprt_head)
{
    if ((!sprt_thread) || (!sprt_head))
        return;

    /*!< check if target link is in the list before deleting */
    list_head_del_safe(sprt_head, &sprt_thread->sgrt_link);
}

/*!
 * @brief	register a new thread, which will be added to ready list
 * @param  	sprt_thread: target thread
 * @param	tid: global tcb index
 * @retval 	err code
 * @note   	all new threads should be added to ready list at first
 */
kint32_t register_new_thread(struct thread *sprt_thread, tid_t tid)
{
    struct thread_attr *sprt_it_attr;
    kint32_t retval;

    sprt_it_attr = sprt_thread->sprt_attr;

    if (SCHED_THREAD_HANDLER(tid))
        return -ER_UNVALID;

    /*!< stack must be valid */
    if (!sprt_it_attr->stack_addr)
        return -ER_NOMEM;

    spin_lock_irqsave(&__SCHED_LOCK);
    /*!< saved to tcb */
    SCHED_THREAD_HANDLER(tid) = sprt_thread;

    /*!< initial link */
    init_list_head(&sprt_thread->sgrt_link);

    /*!< initial spinlock */
    spin_lock_init(&sprt_thread->sgrt_lock);

    /*!< set name */
    sprintk(sprt_thread->name, "thread-%d", tid);

    /*!< add and sorted by priority */
    retval = schedule_add_ready_list(tid);
    if (retval < 0)
    {
        SCHED_THREAD_HANDLER(tid) = mrt_nullptr;
        spin_unlock_irqrestore(&__SCHED_LOCK);
        return retval;
    }

    /*!< set to ready status */
    __SYNC_THREAD_STATUS(tid, NR_THREAD_READY);
    spin_unlock_irqrestore(&__SCHED_LOCK);

    return ER_NORMAL;
}

/*!
 * @brief	init thread (ready to running)
 * @param  	none
 * @retval 	none
 * @note   	reload time slice
 */
void __thread_init_before(void)
{
    struct thread *sprt_thread = SCHED_RUNNING_THREAD;
    kuint32_t milseconds = thread_get_sched_msecs(sprt_thread->sprt_attr);
    
    sprt_thread->expires = jiffies + msecs_to_jiffies(milseconds);
}

/*!
 * @brief	start schedule (ready to running)
 * @param  	none
 * @retval 	none
 * @note   	select a highest priority thread from ready list, and swicth it to running status
 */
struct scheduler_context *__schedule_thread(void)
{
    struct thread *sprt_thread;
    struct thread *sprt_prev;
    kint32_t retval;

    /*!< no ready thread here, unable to start or switch */
    if (mrt_list_head_empty(SCHED_READY_LIST))
        goto fail;

    /*!< get the current thread */
    sprt_prev = SCHED_RUNNING_THREAD;
    if (!sprt_prev)
    {
        if (thread_schedule_ref)
            goto fail;
        else
        {
            /*!< scheduled by "start_kernel" for the first time */
            sprt_prev = mrt_list_first_valid_entry(SCHED_READY_LIST, struct thread, sgrt_link);
            if (!sprt_prev)
                goto fail;
            
            __SET_THREAD_STATUS(sprt_prev->tid, NR_THREAD_RUNNING);
        }
    }
    
    /*!< if not set target status, default to ready */
    if (!sprt_prev->to_status)
        __SET_THREAD_STATUS(sprt_prev->tid, NR_THREAD_READY);

    /*!< select next valid thread */
    retval = schedule_thread_switch(sprt_prev->tid);
    sprt_thread = SCHED_RUNNING_THREAD;
    if ((retval < 0) || (!sprt_thread))
        goto fail;
    
    sgrt_context.first = (kuaddr_t)&thread_schedule_ref;
    sgrt_context.entry = (kuaddr_t)&sprt_thread->start_routine;
    sgrt_context.args = (kuaddr_t)&sprt_thread->ptr_args;
    sgrt_context.prev_sp = 0;
    sgrt_context.next_sp = thread_get_stack(sprt_thread->sprt_attr);

    if (thread_schedule_ref)
        sgrt_context.prev_sp = thread_get_stack(sprt_prev->sprt_attr);

    scheduler_record();

    /*!< address of sgrt_context ===> r0 */
    return &sgrt_context;

fail:
    return mrt_nullptr;
}

/*!
 * @brief	start schedule (ready to running)
 * @param  	none
 * @retval 	none
 * @note   	select a highest priority thread from ready list, and swicth it to running status
 */
void schedule_thread(void)
{
    struct scheduler_context *sprt_context;

    mrt_preempt_disable();
    __push_psr();
    mrt_disable_cpu_irq();

    sprt_context = __schedule_thread();
    if (!sprt_context)
        goto END;

    /*!< sprt_context ===> r0 */
    context_switch(sprt_context);
    return;

END:
    __pop_psr();
    mrt_preempt_enable();
}

/*!< end of file */
