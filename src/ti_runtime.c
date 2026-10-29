#include "include/ti_runtime.h"
#include "include/tracked_memory.h"
#include "TienInterpreter.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#endif

/* -------------------- Handle Encoding -------------------- */

#define HANDLE_SLOT_BITS 8u
#define HANDLE_SLOT_MASK 0xFFu
#define HANDLE_GEN_MASK  0x00FFFFFFu

#if TI_MAX_RUNTIMES < 1 || TI_MAX_RUNTIMES > 256
#error "TI_MAX_RUNTIMES must be in range [1, 256]"
#endif

/* -------------------- Static Types -------------------- */

typedef struct {
    ti_runtime_t *rt;  /* Live runtime occupying the slot, NULL when free */
    uint32_t      gen; /* Generation of the current/last occupant (0 = never used) */
} slot_t;

/* -------------------- Static Variables -------------------- */

/* Handle table; also guards every runtime event queue through the same lock */
static slot_t s_slots[TI_MAX_RUNTIMES];

#if defined(ESP_PLATFORM)
static portMUX_TYPE s_slot_mux = portMUX_INITIALIZER_UNLOCKED;
#elif defined(_WIN32)
static SRWLOCK s_slot_lock = SRWLOCK_INIT;
#elif defined(__unix__) || defined(__APPLE__)
static pthread_mutex_t s_slot_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/* -------------------- Static Function Prototypes -------------------- */

static void slot_lock(void);
static void slot_unlock(void);
static ti_runtime_t *slot_lookup_locked(ti_handle_t handle);
static bool slot_attach(ti_runtime_t *rt);
static ti_task_t *slot_detach(ti_runtime_t *rt);
static void args_free(value_t **args, int arg_count);
static ti_task_t *task_create(const char *func_name, value_t **args, int arg_count);
static void runtime_sync_destroy(ti_runtime_t *rt);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Enter the critical section guarding the slot table and all event queues.
 */
static void slot_lock(void)
{
#if defined(ESP_PLATFORM)
    /* Spinlock + interrupt mask: safe across both cores and from ISR context */
    portENTER_CRITICAL_SAFE(&s_slot_mux);
#elif defined(_WIN32)
    AcquireSRWLockExclusive(&s_slot_lock);
#elif defined(__unix__) || defined(__APPLE__)
    pthread_mutex_lock(&s_slot_mutex);
#endif
    /* Other targets are single-threaded: no locking required */
}

/**
 * @brief Leave the critical section guarding the slot table and all event queues.
 */
static void slot_unlock(void)
{
#if defined(ESP_PLATFORM)
    portEXIT_CRITICAL_SAFE(&s_slot_mux);
#elif defined(_WIN32)
    ReleaseSRWLockExclusive(&s_slot_lock);
#elif defined(__unix__) || defined(__APPLE__)
    pthread_mutex_unlock(&s_slot_mutex);
#endif
}

/**
 * @brief Resolve a handle to its runtime; the caller must hold the slot lock.
 * @param handle Runtime handle.
 * @return Live runtime, or NULL if the handle is stale or invalid.
 */
static ti_runtime_t *slot_lookup_locked(ti_handle_t handle)
{
    uint32_t index = handle & HANDLE_SLOT_MASK;
    uint32_t gen = handle >> HANDLE_SLOT_BITS;

    /* Generation 0 is never issued, so TI_INVALID_HANDLE always fails here */
    if (gen == 0 || index >= TI_MAX_RUNTIMES) {
        return NULL;
    }

    /* A mismatching generation means the runtime was destroyed (and maybe the slot reused) */
    if (s_slots[index].rt == NULL || s_slots[index].gen != gen) {
        return NULL;
    }
    return s_slots[index].rt;
}

/**
 * @brief Register a runtime in a free slot and assign its handle.
 * @param rt Runtime to register.
 * @return true on success, false if every slot is occupied.
 */
static bool slot_attach(ti_runtime_t *rt)
{
    bool attached = false;

    slot_lock();
    for (uint32_t i = 0; i < TI_MAX_RUNTIMES; i++) {
        if (s_slots[i].rt != NULL) {
            continue;
        }

        /* Advance the generation so handles of previous occupants stay stale */
        uint32_t gen = (s_slots[i].gen + 1u) & HANDLE_GEN_MASK;
        if (gen == 0) {
            gen = 1;
        }

        s_slots[i].gen = gen;
        s_slots[i].rt = rt;
        rt->handle = (gen << HANDLE_SLOT_BITS) | i;
        attached = true;
        break;
    }
    slot_unlock();

    return attached;
}

/**
 * @brief Unregister a runtime from the handle table and steal its pending event list.
 * @param rt Runtime to unregister.
 * @return Head of the detached pending task list (caller frees it outside the lock).
 */
static ti_task_t *slot_detach(ti_runtime_t *rt)
{
    slot_lock();

    /* Release the slot only if it is still owned by this runtime */
    if (slot_lookup_locked(rt->handle) == rt) {
        s_slots[rt->handle & HANDLE_SLOT_MASK].rt = NULL;
    }
    rt->handle = TI_INVALID_HANDLE;

    /* Take the whole queue in O(1); no producer can reach it once the slot is released */
    ti_task_t *pending = rt->task_head;
    __atomic_store_n(&rt->task_head, NULL, __ATOMIC_RELAXED);
    rt->task_tail = NULL;
    rt->task_count = 0;

    slot_unlock();
    return pending;
}

/**
 * @brief Free every non-NULL value of an argument array (the array itself is not freed).
 * @param args Argument array (or NULL).
 * @param arg_count Number of entries.
 */
static void args_free(value_t **args, int arg_count)
{
    if (args == NULL) {
        return;
    }
    for (int i = 0; i < arg_count; i++) {
        if (args[i] != NULL) {
            val_free(args[i]);
        }
    }
}

/**
 * @brief Allocate a task holding a copy of func_name and of the args pointer array.
 * @param func_name Callback function name.
 * @param args Argument array whose values the task takes over.
 * @param arg_count Number of arguments.
 * @return New task, or NULL on allocation failure (args are left untouched).
 */
static ti_task_t *task_create(const char *func_name, value_t **args, int arg_count)
{
    size_t name_size = strlen(func_name) + 1;
    size_t args_size = (size_t)arg_count * sizeof(value_t *);

    /* Single block keeps heap fragmentation low: [ti_task_t][args][func_name] */
    ti_task_t *task = (ti_task_t *)ti_raw_malloc(sizeof(ti_task_t) + args_size + name_size);
    if (task == NULL) {
        return NULL;
    }

    task->next = NULL;
    task->args = (value_t **)(task + 1);
    task->func_name = (char *)task->args + args_size;
    task->arg_count = arg_count;

    if (arg_count > 0) {
        memcpy(task->args, args, args_size);
    }
    memcpy(task->func_name, func_name, name_size);
    return task;
}

/* -------------------- Runtime Lifecycle Operations -------------------- */

/**
 * @brief Release the lock and signal objects of a runtime (each may be NULL).
 * @param rt Runtime whose synchronization objects are destroyed.
 */
static void runtime_sync_destroy(ti_runtime_t *rt)
{
    ti_mutex_destroy(rt->exec_lock);
    ti_signal_destroy(rt->handoff);
    ti_signal_destroy(rt->wake);
    ti_signal_destroy(rt->idle);
    rt->exec_lock = NULL;
    rt->handoff = NULL;
    rt->wake = NULL;
    rt->idle = NULL;
}

/* Initialize an existing runtime instance structure */
bool ti_runtime_init(ti_runtime_t *rt)
{
    if (!rt) {
        return false;
    }

    /* Identity */
    rt->handle = TI_INVALID_HANDLE;

    /* Memory */
    rt->alloc_list = NULL;

    /* Script state */
    rt->global_context = context_init(&rt->alloc_list);
    rt->user_functions = NULL;
    rt->user_function_count = 0;

    /* Call stack */
    rt->call_depth = 0;
    rt->max_call_depth = 64;

    /* Execution status */
    __atomic_store_n(&rt->is_interrupted, false, __ATOMIC_RELAXED);
    rt->status = TI_RT_OK;
    rt->error.kind = TI_OK;
    rt->error.line = 0;
    rt->error.message[0] = '\0';

    /* Event queue */
    rt->task_head = NULL;
    rt->task_tail = NULL;
    rt->task_count = 0;

    /* Event dispatch */
    rt->active_task = NULL;
    rt->in_dispatch = false;

    /* Execution lock */
    rt->exec_lock = ti_mutex_create();
    rt->exec_owner = TI_CALLER_NONE;
    rt->lock_waiters = 0;
    rt->handoff = ti_signal_create();

    /* Wake-up */
    rt->wake = ti_signal_create();

    /* Usage pinning */
    rt->users = 0;
    rt->idle = ti_signal_create();

    /* Any missing object means the runtime cannot synchronize: roll everything back */
    if (!rt->global_context || !rt->exec_lock || !rt->handoff || !rt->wake || !rt->idle) {
        runtime_sync_destroy(rt);
        tracked_free_all(&rt->alloc_list);
        rt->global_context = NULL;
        return false;
    }
    return true;
}

/* Allocate, initialize and register a new runtime instance in the handle table */
ti_runtime_t *ti_runtime_create(void)
{
    ti_runtime_t *rt = (ti_runtime_t *)tracked_calloc(NULL, 1, sizeof(ti_runtime_t));
    if (!rt) {
        return NULL;
    }
    if (!ti_runtime_init(rt)) {
        tracked_free(NULL, rt);
        return NULL;
    }

    /* Publish the runtime through a generation handle */
    if (!slot_attach(rt)) {
        ti_log("[Runtime Error] No free runtime slot (TI_MAX_RUNTIMES = %d)\n", TI_MAX_RUNTIMES);
        runtime_sync_destroy(rt);
        tracked_free_all(&rt->alloc_list);
        tracked_free(NULL, rt);
        return NULL;
    }
    return rt;
}

/* Invalidate the runtime handle, discard pending events and release all resources */
void ti_runtime_destroy(ti_runtime_t *rt)
{
    if (!rt) {
        return;
    }

    /* Invalidate the handle first: from here on every post/stop on it fails safely */
    ti_task_t *pending = slot_detach(rt);

    /* Wait for calls pinned before the invalidation; none can pin it anymore */
    for (;;) {
        slot_lock();
        int users = rt->users;
        slot_unlock();
        if (users == 0) {
            break;
        }
        /* Timed wait: re-check even if the idle signal was consumed earlier */
        ti_signal_take(rt->idle, 10);
    }

    /* Discard events that were never dispatched */
    while (pending != NULL) {
        ti_task_t *next = pending->next;
        ti_task_free(pending);
        pending = next;
    }

    /* Reclaim a task whose callback was unwound by a fatal error mid-dispatch */
    ti_task_free(rt->active_task);
    rt->active_task = NULL;

    /* Global variables own heap values (value_t) that are not on the alloc list: release them first */
    context_free_internal(&rt->alloc_list, rt->global_context);

    /*
     * Batch deallocate 100% of all runtime-allocated objects:
     * Every variable, scope frame, user function, and dynamic string created
     * during execution is linked to rt->alloc_list. A single pass reclaims
     * all memory instantly without requiring manual recursive traversal.
     */
    tracked_free_all(&rt->alloc_list);

    /* Nobody uses the lock or signals anymore */
    runtime_sync_destroy(rt);

    /* Free the runtime container itself */
    tracked_free(NULL, rt);
}

/* Resolve a handle and pin the runtime so it cannot be destroyed until released */
ti_runtime_t *ti_runtime_acquire(ti_handle_t handle)
{
    /* Lookup and pin under one lock hold: destroy cannot slip in between */
    slot_lock();
    ti_runtime_t *rt = slot_lookup_locked(handle);
    if (rt != NULL) {
        rt->users++;
    }
    slot_unlock();
    return rt;
}

/* Drop a pin taken by ti_runtime_acquire */
void ti_runtime_release(ti_runtime_t *rt)
{
    if (rt == NULL) {
        return;
    }

    /* Signal while still locked: destroy cannot free rt before this block ends */
    slot_lock();
    if (--rt->users == 0) {
        ti_signal_give(rt->idle);
    }
    slot_unlock();
}

/* Resolve a handle to its live runtime instance */
ti_runtime_t *ti_runtime_resolve(ti_handle_t handle)
{
    slot_lock();
    ti_runtime_t *rt = slot_lookup_locked(handle);
    slot_unlock();
    return rt;
}

/* Request execution cancellation to immediately halt running script */
ti_status_t ti_runtime_stop(ti_handle_t handle)
{
    ti_status_t status = TI_ERR_STALE_HANDLE;

    /* Set the flag and pin the runtime under the lock so it cannot be freed in between */
    slot_lock();
    ti_runtime_t *rt = slot_lookup_locked(handle);
    if (rt != NULL) {
        __atomic_store_n(&rt->is_interrupted, true, __ATOMIC_RELAXED);
        rt->users++;
        status = TI_OK;
    }
    slot_unlock();

    /* Wake a sleeping delay so it notices the stop immediately (outside the critical section) */
    if (rt != NULL) {
        ti_signal_give(rt->wake);
        ti_runtime_release(rt);
    }
    return status;
}

/* Check if execution cancellation has been requested */
bool ti_runtime_is_interrupted(ti_runtime_t *rt)
{
    return rt ? __atomic_load_n(&rt->is_interrupted, __ATOMIC_RELAXED) : false;
}

/* -------------------- Execution Lock Operations -------------------- */

/* Longest wait for the hand-off signal before the yielding script re-takes the lock (ms) */
#define TI_YIELD_WAIT_MS 10

/* Take the execution lock and record the caller as owner */
bool ti_runtime_exec_lock(ti_runtime_t *rt, uint32_t timeout_ms)
{
    if (rt == NULL || !ti_mutex_lock(rt->exec_lock, timeout_ms)) {
        return false;
    }
    __atomic_store_n(&rt->exec_owner, ti_current_caller(), __ATOMIC_RELAXED);
    return true;
}

/* Release the execution lock held by the caller */
void ti_runtime_exec_unlock(ti_runtime_t *rt)
{
    if (rt == NULL) {
        return;
    }
    /* Clear the owner while still holding the lock so nobody sees a stale owner */
    __atomic_store_n(&rt->exec_owner, TI_CALLER_NONE, __ATOMIC_RELAXED);
    ti_mutex_unlock(rt->exec_lock);
}

/* Check whether the calling context currently holds the execution lock */
bool ti_runtime_exec_held(ti_runtime_t *rt)
{
    /* Only the owner itself can read its own identity here, so the relaxed read is exact */
    return rt != NULL && __atomic_load_n(&rt->exec_owner, __ATOMIC_RELAXED) == ti_current_caller();
}

/* Let waiting ti_call callers run, then take the lock back */
void ti_runtime_yield(ti_runtime_t *rt)
{
    /* Never release a lock this context does not hold */
    if (!ti_runtime_exec_held(rt)) {
        return;
    }

    while (__atomic_load_n(&rt->lock_waiters, __ATOMIC_RELAXED) > 0) {
        /* Drop a hand-off left by an earlier call that finished while nobody was yielding */
        ti_signal_take(rt->handoff, 0);

        ti_runtime_exec_unlock(rt);

        /* A finished ti_call gives the hand-off; the timeout only bounds a lost signal */
        ti_signal_take(rt->handoff, TI_YIELD_WAIT_MS);

        ti_runtime_exec_lock(rt, TI_PORT_WAIT_FOREVER);
    }
}

/**
 * @brief Shared wait loop: sleep until is_done becomes true or ms elapses, servicing events and ti_call callers.
 * @param rt Pointer to runtime instance.
 * @param ms Duration in milliseconds (TI_PORT_WAIT_FOREVER: no limit, only with is_done).
 * @param is_done Completion flag read with acquire ordering, or NULL for a plain delay.
 * @param expire_status Status returned when ms elapses (TI_OK for a delay, TI_ERR_TIMEOUT for a flag wait).
 * @return TI_OK when is_done is set, expire_status on expiry, TI_ERR_INTERRUPTED if stopped.
 */
static ti_status_t runtime_wait_loop(ti_runtime_t *rt, uint32_t ms, const bool *is_done, ti_status_t expire_status)
{
    /* Only the plain script context may release the lock; inside a callback it stays held */
    bool can_yield = ti_runtime_exec_held(rt) && !rt->in_dispatch;
    bool forever = (is_done != NULL && ms == TI_PORT_WAIT_FOREVER);

    /* Absolute deadline: time spent running events or waiting for the lock counts toward the delay */
    uint32_t start = ti_millis();

    for (;;) {
        /* Acquire pairs with the producer's store-release, so its result data is visible once the flag is */
        if (is_done != NULL && __atomic_load_n(is_done, __ATOMIC_ACQUIRE)) {
            return TI_OK;
        }

        if (ti_runtime_is_interrupted(rt)) {
            return TI_ERR_INTERRUPTED;
        }

        uint32_t elapsed = ti_millis() - start;
        if (!forever && elapsed >= ms) {
            return expire_status;
        }

        /* Run queued events first, then re-check the deadline (the callbacks took time) */
        if (can_yield && __atomic_load_n(&rt->task_head, __ATOMIC_RELAXED) != NULL) {
            ti_runtime_dispatch_pending_events(rt);
            continue;
        }

        /* Sleep with the lock released so ti_call callers can run; an event, a stop or ti_give gives wake */
        if (can_yield) {
            ti_runtime_exec_unlock(rt);
        }
        ti_signal_take(rt->wake, forever ? TI_PORT_WAIT_FOREVER : ms - elapsed);
        if (can_yield) {
            ti_runtime_exec_lock(rt, TI_PORT_WAIT_FOREVER);
        }
        /* Any wake-up may be unrelated (event, older give): the loop re-checks everything */
    }
}

/* Sleep for ms, servicing events and ti_call callers; wakes early only on cancellation */
ti_status_t ti_runtime_wait(ti_runtime_t *rt, uint32_t ms)
{
    if (rt == NULL) {
        return TI_ERR_INVALID_ARG;
    }
    return runtime_wait_loop(rt, ms, NULL, TI_OK);
}

/* Wait until *is_done is set or timeout_ms elapses, servicing events and ti_call callers */
ti_status_t ti_runtime_wait_flag(ti_runtime_t *rt, const bool *is_done, uint32_t timeout_ms)
{
    if (rt == NULL || is_done == NULL) {
        return TI_ERR_INVALID_ARG;
    }
    return runtime_wait_loop(rt, timeout_ms, is_done, TI_ERR_TIMEOUT);
}

/* Wake the runtime's wait loop from another thread */
ti_status_t ti_runtime_give(ti_handle_t handle)
{
    /* Pin under the lock so the runtime cannot be freed before the give */
    slot_lock();
    ti_runtime_t *rt = slot_lookup_locked(handle);
    if (rt != NULL) {
        rt->users++;
    }
    slot_unlock();

    if (rt == NULL) {
        return TI_ERR_STALE_HANDLE;
    }

    /* Give outside the critical section, then drop the pin */
    ti_signal_give(rt->wake);
    ti_runtime_release(rt);
    return TI_OK;
}

/* -------------------- Event Queue Operations -------------------- */

/* Queue an event for a TI callback function on the runtime referenced by handle */
ti_status_t ti_runtime_post_event(ti_handle_t handle, const char *func_name, value_t **args, int arg_count)
{
    /* Malformed array: values cannot be enumerated, so nothing can be released */
    if (arg_count < 0 || (arg_count > 0 && args == NULL)) {
        return TI_ERR_INVALID_ARG;
    }

    /* Rejected input still consumes the argument values (ownership is always taken) */
    bool args_valid = (func_name != NULL && func_name[0] != '\0');
    for (int i = 0; args_valid && i < arg_count; i++) {
        args_valid = (args[i] != NULL);
    }
    if (!args_valid) {
        args_free(args, arg_count);
        return TI_ERR_INVALID_ARG;
    }

    /* Allocate outside the critical section: malloc may block */
    ti_task_t *task = task_create(func_name, args, arg_count);
    if (task == NULL) {
        args_free(args, arg_count);
        return TI_ERR_NO_MEMORY;
    }

    /* Validate the handle and link the task under the same lock that destroy takes */
    ti_status_t status = TI_ERR_STALE_HANDLE;
    ti_runtime_t *pinned = NULL;
    slot_lock();
    ti_runtime_t *rt = slot_lookup_locked(handle);
    if (rt != NULL && rt->task_count >= TI_MAX_PENDING_EVENTS) {
        status = TI_ERR_QUEUE_FULL;
    } else if (rt != NULL) {
        if (rt->task_tail != NULL) {
            rt->task_tail->next = task;
        } else {
            /* Atomic store pairs with the lock-free hint read at the visitor safe-point */
            __atomic_store_n(&rt->task_head, task, __ATOMIC_RELAXED);
        }
        rt->task_tail = task;
        rt->task_count++;
        rt->users++; /* Keep the runtime alive until the wake-up below is delivered */
        pinned = rt;
        status = TI_OK;
    }
    slot_unlock();

    /* Rejected task: release it together with its values outside the lock */
    if (status != TI_OK) {
        ti_task_free(task);
        return status;
    }

    /* Wake a sleeping delay so the event runs now instead of after the delay */
    ti_signal_give(pinned->wake);
    ti_runtime_release(pinned);
    return status;
}

/* Remove the oldest pending task from the runtime event queue */
ti_task_t *ti_runtime_pop_task(ti_runtime_t *rt)
{
    if (!rt) {
        return NULL;
    }

    slot_lock();
    ti_task_t *task = rt->task_head;
    if (task != NULL) {
        __atomic_store_n(&rt->task_head, task->next, __ATOMIC_RELAXED);
        if (rt->task_head == NULL) {
            rt->task_tail = NULL;
        }
        rt->task_count--;
    }
    slot_unlock();

    if (task != NULL) {
        task->next = NULL;
    }
    return task;
}

/* Free a task record together with its owned argument values */
void ti_task_free(ti_task_t *task)
{
    if (task == NULL) {
        return;
    }
    args_free(task->args, task->arg_count);
    ti_raw_free(task);
}

/* -------------------- Runtime Error Operations -------------------- */

/* Record a runtime error on the runtime and switch it to TI_RT_ERROR */
void ti_raise(ti_runtime_t *rt, ti_status_t kind, int line, const char *fmt, ...)
{
    /* The first error is the root cause; later ones raised while unwinding are ignored */
    if (rt == NULL || rt->status == TI_RT_ERROR) {
        return;
    }

    rt->error.kind = kind;
    rt->error.line = line;

    /* Format into the fixed buffer; vsnprintf always null-terminates and truncates if too long */
    va_list args;
    va_start(args, fmt);
    vsnprintf(rt->error.message, sizeof(rt->error.message), fmt, args);
    va_end(args);

    rt->status = TI_RT_ERROR;
}

/* Log the recorded runtime error */
void ti_runtime_report_error(ti_runtime_t *rt)
{
    if (rt == NULL || rt->status != TI_RT_ERROR) {
        return;
    }

    if (rt->error.line > 0) {
        ti_log("[Runtime Error] %s at line %d\n", rt->error.message, rt->error.line);
    } else {
        ti_log("[Runtime Error] %s\n", rt->error.message);
    }
}
