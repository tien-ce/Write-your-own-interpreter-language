#include "include/ti_runtime.h"
#include "include/ti_runtime_registry.h"
#include "TienInterpreter.h"

/* -------------------- Execution Lock Operations -------------------- */

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
    /* Give under the registry hold: destroy cannot free the signal meanwhile */
    ti_runtime_t *rt = ti_registry_lock(handle);
    if (rt == NULL) {
        return TI_ERR_STALE_HANDLE;
    }

    ti_signal_give(rt->wake);

    ti_registry_unlock();
    return TI_OK;
}
