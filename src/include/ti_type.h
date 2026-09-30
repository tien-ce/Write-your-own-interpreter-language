#ifndef TI_TYPE_H
#define TI_TYPE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- Canonical Type Definitions -------------------- */

/**
 * @brief Canonical data type enumeration for both AST syntax and runtime values.
 */
typedef enum {
    VAL_NULL,    // Null / uninitialized / empty return
    VAL_INT,     // Integer type
    VAL_FLOAT,   // Floating-point type
    VAL_STRING,  // String type
    VAL_BOOL,    // Boolean type
    VAL_VOID,    // Void return type for functions
    VAL_FUNC,
    VAL_DICT,
    VAL_LIST,    // Homogeneous list type (list int, list string, ...)
} val_type_t;

typedef enum {
    FUNC_BUILTIN,
    FUNC_TI,
} func_type_t;

/* -------------------- Runtime Handle & Status Types -------------------- */

/**
 * @brief Opaque generation-checked reference to a runtime instance.
 * Encoded as [generation:24 | slot:8]. A handle whose runtime has been destroyed
 * is rejected by every API instead of dereferencing freed memory.
 */
typedef uint32_t ti_handle_t;

/**
 * @brief Handle value that never refers to a live runtime.
 */
#define TI_INVALID_HANDLE ((ti_handle_t)0)

/**
 * @brief Result codes shared by handle-based APIs, value containers (list/dict) and the runtime
 * error record. Use ti_err_to_str() to render one.
 */
typedef enum {
    TI_OK = 0,           // Operation completed
    TI_ERR_INVALID_ARG,  // NULL or malformed argument
    TI_ERR_NO_MEMORY,    // Allocation failed
    TI_ERR_STALE_HANDLE, // Handle does not refer to a live runtime
    TI_ERR_QUEUE_FULL,   // Pending event queue reached its capacity
    TI_ERR_INTERRUPTED,  // Runtime has been stopped
    TI_ERR_TYPE_MISMATCH,       // Value type does not match the expected type
    TI_ERR_INDEX_OUT_OF_RANGE,  // List index outside [0, count)
    TI_ERR_KEY_NOT_FOUND,       // Dictionary key does not exist
    TI_ERR_LIMIT_EXCEEDED,      // Configured capacity limit reached (e.g. TI_MAX_LIST_ITEMS)
    TI_ERR_RUNTIME,             // Generic script runtime error
} ti_status_t;

#ifdef __cplusplus
}
#endif

#endif /* !TI_TYPE_H */
