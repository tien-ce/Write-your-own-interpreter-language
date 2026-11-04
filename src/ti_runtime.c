#include "include/ti_runtime.h"
#include "include/ti_runtime_registry.h"
#include "include/tracked_memory.h"
#include "TienInterpreter.h"
#include <stdarg.h>
#include <stdio.h>

/* -------------------- Static Function Prototypes -------------------- */

static void runtime_sync_destroy(ti_runtime_t *rt);
static bool runtime_init(ti_runtime_t *rt);

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
    rt->exec_lock = NULL;
    rt->handoff = NULL;
    rt->wake = NULL;
}

/**
 * @brief Initialize a freshly allocated runtime (not yet registered in the handle table).
 * @param rt Runtime structure to initialize.
 * @return true on success, false if a lock or signal could not be created (rt is left cleaned up).
 */
static bool runtime_init(ti_runtime_t *rt)
{
    if (!rt) {
        return false;
    }

    /* Identity */
    rt->handle = TI_INVALID_HANDLE;

    /* Memory */
    rt->alloc_list = NULL;

    /* Script state */
    rt->global_context = context_init_global(&rt->alloc_list);
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

    /* Any missing object means the runtime cannot synchronize: roll everything back */
    if (!rt->global_context || !rt->exec_lock || !rt->handoff || !rt->wake) {
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
    if (!runtime_init(rt)) {
        tracked_free(NULL, rt);
        return NULL;
    }

    /* Publish the runtime through a generation handle */
    if (!ti_registry_attach(rt)) {
        ti_log("[Runtime Error] No free runtime slot (TI_MAX_RUNTIMES = %d)\n", TI_MAX_RUNTIMES);
        runtime_sync_destroy(rt);
        tracked_free_all(&rt->alloc_list);
        tracked_free(NULL, rt);
        return NULL;
    }
    return rt;
}

/* Detach the handle, wait until no external caller uses the runtime, then release everything */
ti_status_t ti_runtime_destroy(ti_handle_t handle)
{
    /* Lookup and detach in one registry hold: a second destroy of the same handle gets STALE here */
    ti_runtime_t *rt = ti_registry_detach(handle);
    if (rt == NULL) {
        return TI_ERR_STALE_HANDLE;
    }

    /* Take the whole queue in O(1); no producer can reach it once the slot is released */
    ti_task_t *pending = rt->task_head;
    __atomic_store_n(&rt->task_head, NULL, __ATOMIC_RELAXED);
    rt->task_tail = NULL;
    rt->task_count = 0;

    /*
     * No ti_execute can be running here: create, execute and destroy are called on the owner
     * thread one after another. Only external ti_call callers may still be inside.
     *
     *
     * Drain ti_call callers registered as waiters before the detach. Each one that gets the lock
     * sees rt->handle == TI_INVALID_HANDLE, leaves and gives handoff. No new waiter can register.
     */
    ti_runtime_exec_lock(rt, TI_PORT_WAIT_FOREVER);
    while (__atomic_load_n(&rt->lock_waiters, __ATOMIC_ACQUIRE) > 0) {
        ti_runtime_exec_unlock(rt);
        /* The timeout only bounds a hand-off consumed by someone else */
        ti_signal_take(rt->handoff, TI_YIELD_WAIT_MS);
        ti_runtime_exec_lock(rt, TI_PORT_WAIT_FOREVER);
    }
    ti_runtime_exec_unlock(rt);

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
    return TI_OK;
}

/* Resolve a handle to its live runtime instance */
ti_runtime_t *ti_runtime_resolve(ti_handle_t handle)
{
    /* Only a lookup: the pointer stays valid because the caller runs on this runtime (not pinned) */
    ti_runtime_t *rt = ti_registry_lock(handle);
    if (rt == NULL) {
        return NULL;
    }
    ti_registry_unlock();
    return rt;
}

/* Request execution cancellation to immediately halt running script */
ti_status_t ti_runtime_stop(ti_handle_t handle)
{
    /* Whole interaction under one registry hold: destroy cannot detach (and free) rt meanwhile */
    ti_runtime_t *rt = ti_registry_lock(handle);
    if (rt == NULL) {
        return TI_ERR_STALE_HANDLE;
    }

    /* Set the flag, then wake a sleeping delay so it notices the stop immediately */
    __atomic_store_n(&rt->is_interrupted, true, __ATOMIC_RELAXED);
    ti_signal_give(rt->wake);

    ti_registry_unlock();
    return TI_OK;
}

/* Check if execution cancellation has been requested */
bool ti_runtime_is_interrupted(ti_runtime_t *rt)
{
    return rt ? __atomic_load_n(&rt->is_interrupted, __ATOMIC_RELAXED) : false;
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
