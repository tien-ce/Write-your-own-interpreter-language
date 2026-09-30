#ifndef TI_RUNTIME_H
#define TI_RUNTIME_H

#include "ti_runtime_context.h"
#include "ti_type_func.h"
#include "ti_type.h"
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
    context_t          *global_context;      /* Global variable scope */
    function_t         *user_functions;      /* Script-defined functions for this runtime */
    int                 user_function_count; /* Number of script functions */
    int                 call_depth;          /* Current recursion depth */
    int                 max_call_depth;      /* Recursion depth safety limit (default 64) */
    volatile bool       is_interrupted;      /* Cancellation flag to stop running script from outside */
    alloc_hdr_t        *alloc_list;          /* Dedicated allocation tracking list for this runtime */
    volatile ti_rt_status_t status;          /* Execution state (TI_RT_ERROR after ti_raise) */
    ti_error_t          error;               /* First raised error, valid when status == TI_RT_ERROR */
    ti_handle_t         handle;              /* Generation handle of this runtime (TI_INVALID_HANDLE if unregistered) */
    ti_task_t          *task_head;           /* Pending event FIFO head (written under slot lock, atomically) */
    ti_task_t          *task_tail;           /* Pending event FIFO tail (guarded by slot lock) */
    int                 task_count;          /* Number of pending events (guarded by slot lock) */
    ti_task_t          *active_task;         /* Task being dispatched, reclaimed on destroy if unwound */
    bool                in_dispatch;         /* Re-entrancy guard for the event dispatcher */
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
    return rt != NULL && (rt->is_interrupted || rt->status != TI_RT_OK);
}

/* -------------------- Runtime Lifecycle Functions -------------------- */

/**
 * @brief Initialize an existing runtime instance structure (not registered in the handle table).
 * @param rt Pointer to runtime structure to initialize.
 */
void ti_runtime_init(ti_runtime_t *rt);

/**
 * @brief Allocate, initialize and register a new runtime instance in the handle table.
 * @return Pointer to newly allocated ti_runtime_t, or NULL if out of memory or no free slot.
 */
ti_runtime_t *ti_runtime_create(void);

/**
 * @brief Invalidate the runtime handle, discard pending events and release all resources.
 * Must not be called while the runtime is executing.
 * @param rt Pointer to runtime instance to destroy.
 */
void ti_runtime_destroy(ti_runtime_t *rt);

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
 * @param rt Pointer to runtime instance (must be called on the executing thread).
 */
void ti_runtime_dispatch_pending_events(ti_runtime_t *rt);

#ifdef __cplusplus
}
#endif

#endif /* !TI_RUNTIME_H */
