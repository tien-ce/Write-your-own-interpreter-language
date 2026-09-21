#include "include/ti_runtime.h"
#include "include/tracked_memory.h"
#include <stdlib.h>

/* -------------------- Runtime Lifecycle Operations -------------------- */

/* Initialize an existing runtime instance structure */
void ti_runtime_init(ti_runtime_t *rt)
{
    if (!rt) {
        return;
    }
    rt->alloc_list = NULL;
    rt->global_context = context_init(&rt->alloc_list);
    rt->user_functions = NULL;
    rt->user_function_count = 0;
    rt->call_depth = 0;
    rt->max_call_depth = 64;
    rt->is_interrupted = false;
}

/* Allocate and initialize a new runtime instance on heap */
ti_runtime_t *ti_runtime_create(void)
{
    ti_runtime_t *rt = (ti_runtime_t *)tracked_calloc(NULL, 1, sizeof(ti_runtime_t));
    if (!rt) {
        return NULL;
    }
    ti_runtime_init(rt);
    return rt;
}

/* Clean up and release all resources owned by a runtime instance */
void ti_runtime_destroy(ti_runtime_t *rt)
{
    if (!rt) {
        return;
    }

    /* 
     * Batch deallocate 100% of all runtime-allocated objects:
     * Every variable, scope frame, user function, and dynamic string created
     * during execution is linked to rt->alloc_list. A single pass reclaims
     * all memory instantly without requiring manual recursive traversal.
     */
    tracked_free_all(&rt->alloc_list);

    /* Free the runtime container itself */
    tracked_free(NULL, rt);
}

/* Request execution cancellation to immediately halt running script */
void ti_runtime_stop(ti_runtime_t *rt)
{
    if (rt) {
        rt->is_interrupted = true;
    }
}

/* Check if execution cancellation has been requested */
bool ti_runtime_is_interrupted(ti_runtime_t *rt)
{
    return rt ? rt->is_interrupted : false;
}
