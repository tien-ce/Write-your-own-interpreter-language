#include "include/ti_runtime.h"
#include "include/ti_runtime_registry.h"
#include "TienInterpreter.h"
#include <string.h>

/* -------------------- Static Function Prototypes -------------------- */

static void args_free(value_t **args, int arg_count);
static ti_task_t *task_create(function_t *func, value_t **args, int arg_count);

/* -------------------- Static Functions -------------------- */

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
 * @brief Allocate a task holding the callback pointer and a copy of the args pointer array.
 * @param func Callback function (borrowed, not dereferenced here).
 * @param args Argument array whose values the task takes over.
 * @param arg_count Number of arguments.
 * @return New task, or NULL on allocation failure (args are left untouched).
 */
static ti_task_t *task_create(function_t *func, value_t **args, int arg_count)
{
    size_t args_size = (size_t)arg_count * sizeof(value_t *);

    /* Single block keeps heap fragmentation low: [ti_task_t][args] */
    ti_task_t *task = (ti_task_t *)ti_raw_malloc(sizeof(ti_task_t) + args_size);
    if (task == NULL) {
        return NULL;
    }

    task->next = NULL;
    task->func = func;
    task->args = (value_t **)(task + 1);
    task->arg_count = arg_count;

    if (arg_count > 0) {
        memcpy(task->args, args, args_size);
    }
    return task;
}

/* -------------------- Event Queue Operations -------------------- */

/* Queue an event for a TI callback function on the runtime referenced by handle */
ti_status_t ti_runtime_post_event(ti_handle_t handle, const value_t *func, value_t **args, int arg_count)
{
    /* Malformed array: values cannot be enumerated, so nothing can be released */
    if (arg_count < 0 || (arg_count > 0 && args == NULL)) {
        return TI_ERR_INVALID_ARG;
    }

    /* Rejected input still consumes the argument values (ownership is always taken). Only the value
     * is read: the function it points to may belong to a destroyed runtime, so ti_runtime_invoke
     * checks it at dispatch before touching it */
    bool args_valid = (func != NULL && func->type == VAL_FUNC && func->func_val != NULL);
    for (int i = 0; args_valid && i < arg_count; i++) {
        args_valid = (args[i] != NULL);
    }
    if (!args_valid) {
        args_free(args, arg_count);
        return TI_ERR_INVALID_ARG;
    }

    /* Allocate before taking the registry: keeps the hold short */
    ti_task_t *task = task_create(func->func_val, args, arg_count);
    if (task == NULL) {
        args_free(args, arg_count);
        return TI_ERR_NO_MEMORY;
    }

    /* Whole interaction under one registry hold: destroy cannot detach (and free) rt meanwhile */
    ti_runtime_t *rt = ti_registry_lock(handle);
    if (rt == NULL) {
        ti_task_free(task);
        return TI_ERR_STALE_HANDLE;
    }

    /* Full queue: release the registry first, then the task with its values */
    if (rt->task_count >= TI_MAX_PENDING_EVENTS) {
        ti_registry_unlock();
        ti_task_free(task);
        return TI_ERR_QUEUE_FULL;
    }

    /* Link at the tail; the atomic store pairs with the lock-free hint read at the safe point */
    if (rt->task_tail != NULL) {
        rt->task_tail->next = task;
    } else {
        __atomic_store_n(&rt->task_head, task, __ATOMIC_RELAXED);
    }
    rt->task_tail = task;
    rt->task_count++;

    /* Wake a sleeping delay so the event runs now instead of after the delay */
    ti_signal_give(rt->wake);

    ti_registry_unlock();
    return TI_OK;
}

/* Remove the oldest pending task from the runtime event queue */
ti_task_t *ti_runtime_pop_task(ti_runtime_t *rt)
{
    if (!rt) {
        return NULL;
    }

    /* A detached runtime has no producers left: its queue was already taken by destroy */
    if (ti_registry_lock(__atomic_load_n(&rt->handle, __ATOMIC_ACQUIRE)) == NULL) {
        return NULL;
    }

    /* Unlink the head (the hint read at the safe point sees the new head atomically) */
    ti_task_t *task = rt->task_head;
    if (task != NULL) {
        __atomic_store_n(&rt->task_head, task->next, __ATOMIC_RELAXED);
        if (rt->task_head == NULL) {
            rt->task_tail = NULL;
        }
        rt->task_count--;
    }
    ti_registry_unlock();

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
