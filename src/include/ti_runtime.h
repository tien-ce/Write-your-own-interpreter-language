#ifndef TI_RUNTIME_H
#define TI_RUNTIME_H

#include "ti_runtime_context.h"
#include "ti_type_func.h"
#include "tracked_memory.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- Runtime Instance Structure -------------------- */

typedef struct TI_RUNTIME_STRUCT {
    context_t          *global_context;      /* Global variable scope */
    function_t         *user_functions;      /* Script-defined functions for this runtime */
    int                 user_function_count; /* Number of script functions */
    int                 call_depth;          /* Current recursion depth */
    int                 max_call_depth;      /* Recursion depth safety limit (default 64) */
    volatile bool       is_interrupted;      /* Cancellation flag to stop running script from outside */
    alloc_hdr_t        *alloc_list;          /* Dedicated allocation tracking list for this runtime */
} ti_runtime_t;

/* -------------------- Runtime Lifecycle Functions -------------------- */

/**
 * @brief Initialize an existing runtime instance structure.
 * @param rt Pointer to runtime structure to initialize.
 */
void ti_runtime_init(ti_runtime_t *rt);

/**
 * @brief Allocate and initialize a new runtime instance on heap.
 * @return Pointer to newly allocated ti_runtime_t.
 */
ti_runtime_t *ti_runtime_create(void);

/**
 * @brief Clean up and release all resources owned by a runtime instance.
 * @param rt Pointer to runtime instance to destroy.
 */
void ti_runtime_destroy(ti_runtime_t *rt);

/**
 * @brief Request execution cancellation to immediately halt running script.
 * Can be called from another task/thread to stop loops cleanly.
 * @param rt Pointer to runtime instance.
 */
void ti_runtime_stop(ti_runtime_t *rt);

/**
 * @brief Check if execution cancellation has been requested.
 * @param rt Pointer to runtime instance.
 * @return true if stopped/interrupted, false otherwise.
 */
bool ti_runtime_is_interrupted(ti_runtime_t *rt);

#ifdef __cplusplus
}
#endif

#endif /* !TI_RUNTIME_H */
