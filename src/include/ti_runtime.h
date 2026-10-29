#ifndef TI_RUNTIME_H
#define TI_RUNTIME_H

#include "ti_runtime_context.h"
#include "ti_type_func.h"
#include "ti_type.h"
#include "ti_port.h"
#include "tracked_memory.h"
#include <stdbool.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- Runtime Configuration -------------------- */

/* Maximum number of simultaneously live runtimes (handle slot table size, at most 256) */
#ifndef TI_MAX_RUNTIMES
#define TI_MAX_RUNTIMES 4
#endif

/* Maximum number of events queued per runtime before posting is rejected */
#ifndef TI_MAX_PENDING_EVENTS
#define TI_MAX_PENDING_EVENTS 32
#endif

/* -------------------- Pending Event Task -------------------- */

/**
 * @brief Event queued by native code, waiting to be dispatched to a TI callback.
 * Allocated as a single raw block: [ti_task_t][args array][func_name string].
 */
typedef struct TI_TASK_STRUCT {
    struct TI_TASK_STRUCT *next;      /* Next pending task in FIFO order */
    char                  *func_name; /* Target TI callback name (owned copy) */
    value_t              **args;      /* Argument values (owned) */
    int                    arg_count; /* Number of arguments */
} ti_task_t;

/* -------------------- Runtime Error Record -------------------- */

/* Capacity of the formatted error message (fixed buffer: no heap use while handling errors) */
#define TI_ERROR_MESSAGE_SIZE 128

/**
 * @brief Execution state of a runtime while a script is running.
 */
typedef enum {
    TI_RT_OK,          // Running normally
    TI_RT_INTERRUPTED, // Cancellation requested (never catchable)
    TI_RT_ERROR,       // Runtime error raised, evaluators unwind to the top level
} ti_rt_status_t;

/**
 * @brief Description of the first runtime error raised on a runtime.
 */
typedef struct {
    ti_status_t kind;                         // Error category (TI_ERR_*)
    int         line;                         // Source line where the error was raised (0 if unknown)
    char        message[TI_ERROR_MESSAGE_SIZE]; // Formatted, null-terminated description
} ti_error_t;

/* -------------------- Runtime Instance Structure -------------------- */

typedef struct TI_RUNTIME_STRUCT {
    /* Identity: generation handle used by every external API */
    ti_handle_t         handle;              /* Handle of this runtime (TI_INVALID_HANDLE if unregistered) */

    /* Memory: allocation tracking, released in one pass on destroy */
    alloc_hdr_t        *alloc_list;          /* Dedicated allocation tracking list for this runtime */

    /* Script state: scopes and functions owned by the running script */
    context_t          *global_context;      /* Global variable scope */
    function_t         *user_functions;      /* Script-defined functions (top-level definitions only) */
    int                 user_function_count; /* Number of script functions */

    /* Call stack: recursion limits */
    int                 call_depth;          /* Current recursion depth */
    int                 max_call_depth;      /* Recursion depth safety limit (default 64) */

    /* Execution status: cancellation and error propagation */
    bool                is_interrupted;      /* Cancellation flag set from any context (atomic access only) */
    volatile ti_rt_status_t status;          /* Execution state (TI_RT_ERROR after ti_raise) */
    ti_error_t          error;               /* First raised error, valid when status == TI_RT_ERROR */

    /* Event queue: producers post under the slot lock, the executing context pops */
    ti_task_t          *task_head;           /* Pending event FIFO head (written under slot lock, atomically) */
    ti_task_t          *task_tail;           /* Pending event FIFO tail (guarded by slot lock) */
    int                 task_count;          /* Number of pending events (guarded by slot lock) */

    /* Event dispatch: state of the dispatcher running on the executing context */
    ti_task_t          *active_task;         /* Task being dispatched, reclaimed on destroy if unwound */
    bool                in_dispatch;         /* Re-entrancy guard for the event dispatcher */

    /* Execution lock: serializes script code between the script and ti_call callers */
    ti_mutex_t         *exec_lock;           /* Held by whoever is running TI code */
    ti_caller_t         exec_owner;          /* Identity of the lock holder (TI_CALLER_NONE if free; atomic access) */
    int                 lock_waiters;        /* Callers waiting for exec_lock (atomic access) */
    ti_signal_t        *handoff;             /* Given by a finished ti_call so the yielding script resumes */

    /* Wake-up: interrupts a sleeping delay on new events or stop */
    ti_signal_t        *wake;                /* Given by post_event, stop and ti_give */

    /* Usage pinning: keeps the runtime alive while external calls use it */
    int                 users;               /* Calls currently pinned via acquire (guarded by slot lock) */
    ti_signal_t        *idle;                /* Given when users drops to 0, lets destroy proceed */
} ti_runtime_t;

/**
 * @brief Check whether evaluation must unwind (cancellation requested or a runtime error raised).
 * Every caller of visitor_visit checks this right after the call, before treating a NULL result
 * as a semantic error, then releases whatever it owns and returns NULL.
 * @param rt Pointer to runtime instance (NULL is treated as "keep running").
 * @return true if the caller must stop evaluating and return NULL.
 */
static inline bool ti_should_unwind(const ti_runtime_t *rt)
{
    return rt != NULL && (__atomic_load_n(&rt->is_interrupted, __ATOMIC_RELAXED) || rt->status != TI_RT_OK);
}

/* -------------------- Runtime Lifecycle Functions -------------------- */

/**
 * @brief Initialize an existing runtime instance structure (not registered in the handle table).
 * @param rt Pointer to runtime structure to initialize.
 * @return true on success, false if a lock or signal could not be created (rt is left cleaned up).
 */
bool ti_runtime_init(ti_runtime_t *rt);

/**
 * @brief Allocate, initialize and register a new runtime instance in the handle table.
 * @return Pointer to newly allocated ti_runtime_t, or NULL if out of memory or no free slot.
 */
ti_runtime_t *ti_runtime_create(void);

/**
 * @brief Invalidate the runtime handle, discard pending events and release all resources.
 * Waits until every call pinned with ti_runtime_acquire has been released.
 * Must not be called while the runtime is executing.
 * @param rt Pointer to runtime instance to destroy.
 */
void ti_runtime_destroy(ti_runtime_t *rt);

/**
 * @brief Resolve a handle and pin the runtime so it cannot be destroyed until released.
 * @param handle Runtime handle.
 * @return Pinned runtime, or NULL if the handle is stale or invalid.
 */
ti_runtime_t *ti_runtime_acquire(ti_handle_t handle);

/**
 * @brief Drop a pin taken by ti_runtime_acquire; wakes a destroy waiting for idle.
 * @param rt Runtime returned by ti_runtime_acquire.
 */
void ti_runtime_release(ti_runtime_t *rt);

/**
 * @brief Resolve a handle to its live runtime instance.
 * The returned pointer is only valid while the caller guarantees the runtime is not destroyed
 * concurrently (i.e. the runtime owner thread, or a native call executing on that runtime).
 * @param handle Runtime handle.
 * @return Pointer to runtime instance, or NULL if the handle is stale or invalid.
 */
ti_runtime_t *ti_runtime_resolve(ti_handle_t handle);

/**
 * @brief Request execution cancellation to immediately halt running script.
 * Safe to call from any task/thread (and from ISR on ESP32).
 * @param handle Runtime handle.
 * @return TI_OK, or TI_ERR_STALE_HANDLE if the handle is stale or invalid.
 */
ti_status_t ti_runtime_stop(ti_handle_t handle);

/**
 * @brief Check if execution cancellation has been requested.
 * @param rt Pointer to runtime instance.
 * @return true if stopped/interrupted, false otherwise.
 */
bool ti_runtime_is_interrupted(ti_runtime_t *rt);

/**
 * @brief Record a runtime error on the runtime and switch it to TI_RT_ERROR.
 * Only the first error is kept while the runtime is already in the error state. The caller must
 * release what it owns and return NULL so the evaluators unwind to the top level.
 * @param rt Pointer to runtime instance (ignored if NULL).
 * @param kind Error category.
 * @param line Source line where the error occurred (0 if unknown).
 * @param fmt printf-style message format.
 */
void ti_raise(ti_runtime_t *rt, ti_status_t kind, int line, const char *fmt, ...);

/**
 * @brief Log the recorded runtime error as "[Runtime Error] <message> at line <n>".
 * @param rt Pointer to runtime instance (ignored if NULL or not in error state).
 */
void ti_runtime_report_error(ti_runtime_t *rt);

/* -------------------- Event Queue Functions -------------------- */

/**
 * @brief Queue an event for a TI callback function on the runtime referenced by handle.
 * Ownership of every value in args is always transferred, whether or not the call succeeds.
 * Safe to call from any task/thread, but not from ISR (allocates memory).
 * @param handle Target runtime handle.
 * @param func_name Name of the TI callback function (copied).
 * @param args Array of argument values (array is copied, values are owned by the call).
 * @param arg_count Number of arguments.
 * @return TI_OK, TI_ERR_STALE_HANDLE, TI_ERR_QUEUE_FULL, TI_ERR_NO_MEMORY or TI_ERR_INVALID_ARG.
 */
ti_status_t ti_runtime_post_event(ti_handle_t handle, const char *func_name, value_t **args, int arg_count);

/**
 * @brief Remove the oldest pending task from the runtime event queue.
 * @param rt Pointer to runtime instance.
 * @return Detached task owned by the caller, or NULL if the queue is empty.
 */
ti_task_t *ti_runtime_pop_task(ti_runtime_t *rt);

/**
 * @brief Free a task record together with its owned argument values.
 * @param task Pointer to task (or NULL).
 */
void ti_task_free(ti_task_t *task);

/**
 * @brief Execute up to TI_MAX_PENDING_EVENTS queued tasks on their TI callback functions.
 * Re-entrant calls (e.g. from inside a running callback) return immediately.
 * A callback error is logged and consumed; cancellation stops the pass.
 * @param rt Pointer to runtime instance (must be called by the execution lock holder).
 */
void ti_runtime_dispatch_pending_events(ti_runtime_t *rt);

/* -------------------- Execution Lock Functions -------------------- */

/**
 * @brief Take the execution lock (non-recursive) and record the caller as owner.
 * @param rt Pointer to runtime instance.
 * @param timeout_ms Wait limit in ms (TI_PORT_WAIT_FOREVER for no limit).
 * @return true if the lock is now held by the caller, false on timeout.
 */
bool ti_runtime_exec_lock(ti_runtime_t *rt, uint32_t timeout_ms);

/**
 * @brief Release the execution lock held by the caller.
 * @param rt Pointer to runtime instance.
 */
void ti_runtime_exec_unlock(ti_runtime_t *rt);

/**
 * @brief Check whether the calling context currently holds the execution lock.
 * @param rt Pointer to runtime instance.
 * @return true if the caller is the lock owner.
 */
bool ti_runtime_exec_held(ti_runtime_t *rt);

/**
 * @brief Let waiting ti_call callers run: release the lock, wait for the hand-off, take it back.
 * Repeats while callers are still waiting. Does nothing if the caller does not hold the lock.
 * @param rt Pointer to runtime instance.
 */
void ti_runtime_yield(ti_runtime_t *rt);

/**
 * @brief Sleep for ms milliseconds without blocking events or ti_call callers.
 * The lock is released while sleeping; each queued event or call runs, then the sleep continues
 * for the rest of the time. Only cancellation ends it early. Inside a callback (in_dispatch) or
 * without owning the lock it just sleeps and keeps whatever it holds.
 * @param rt Pointer to runtime instance.
 * @param ms Duration in milliseconds.
 * @return TI_OK when the time has elapsed, TI_ERR_INTERRUPTED if the runtime was stopped.
 */
ti_status_t ti_runtime_wait(ti_runtime_t *rt, uint32_t ms);

/**
 * @brief Wait until *is_done becomes true, servicing events and ti_call callers like ti_runtime_wait.
 * The flag is read with acquire ordering; the producer sets it with release ordering, then ti_runtime_give.
 * @param rt Pointer to runtime instance.
 * @param is_done Completion flag owned by the caller.
 * @param timeout_ms Wait limit in ms (TI_PORT_WAIT_FOREVER for no limit).
 * @return TI_OK when the flag is set, TI_ERR_TIMEOUT, TI_ERR_INTERRUPTED if stopped, or TI_ERR_INVALID_ARG.
 */
ti_status_t ti_runtime_wait_flag(ti_runtime_t *rt, const bool *is_done, uint32_t timeout_ms);

/**
 * @brief Wake a wait loop (ti_runtime_wait / ti_runtime_wait_flag) of the runtime referenced by handle.
 * Safe to call from any task/thread, but not from ISR.
 * @param handle Runtime handle.
 * @return TI_OK, or TI_ERR_STALE_HANDLE if the handle is stale or invalid.
 */
ti_status_t ti_runtime_give(ti_handle_t handle);

/**
 * @brief Run a script-defined function as a callback, isolating any error it raises.
 * The callback runs to completion (in_dispatch blocks yields and nested dispatch). A raised error
 * is copied to err, the runtime status is reset to TI_RT_OK and the error kind is returned, so the
 * script that was running is unaffected. Cancellation is reported but never cleared.
 * The caller must hold the execution lock and the runtime must not be unwinding.
 * @param rt Pointer to runtime instance.
 * @param func_name Name of the script function.
 * @param args Borrowed argument values (copied into the callback scope).
 * @param argc Number of arguments.
 * @param result Receives the return value (owned by the caller) or NULL; may be NULL to discard it.
 * @param err Receives the error description on failure; may be NULL.
 * @return TI_OK, TI_ERR_UNDEFINED, TI_ERR_INTERRUPTED or the kind of the error the callback raised.
 */
ti_status_t ti_runtime_invoke(ti_runtime_t *rt, const char *func_name, value_t **args, int argc,
                              value_t **result, ti_error_t *err);

/**
 * @brief Safe point: yield to waiting callers, run queued native events, report whether to unwind.
 * Called only at statement boundaries and loop iterations (not per node), where cancellation and
 * callback errors are noticed. While in_dispatch is set (a callback or event is running) it neither
 * yields nor dispatches, so callbacks always run to completion.
 * @param rt Pointer to runtime instance.
 * @return true if the caller must stop evaluating and return NULL.
 */
static inline bool ti_runtime_safe_point(ti_runtime_t *rt)
{
    if (rt != NULL && !rt->in_dispatch) {
        /* Relaxed unlocked read is only a hint; waiters register before blocking on the lock */
        if (__atomic_load_n(&rt->lock_waiters, __ATOMIC_RELAXED) > 0) {
            ti_runtime_yield(rt);
        }
        if (__atomic_load_n(&rt->task_head, __ATOMIC_RELAXED) != NULL) {
            ti_runtime_dispatch_pending_events(rt);
        }
    }
    return ti_should_unwind(rt);
}

#ifdef __cplusplus
}
#endif

#endif /* !TI_RUNTIME_H */
