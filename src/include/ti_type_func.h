#ifndef TI_TYPE_FUNC_H
#define TI_TYPE_FUNC_H

#include "ti_type_value.h"
#include "ti_type.h"
#include <stdbool.h>

/* Forward declarations to decouple internal AST and context headers */
struct CONTEXT_STRUCT;
typedef struct CONTEXT_STRUCT context_t;

struct AST_STRUCT;
typedef struct AST_STRUCT ast_t;

struct TI_RUNTIME_STRUCT;
typedef struct TI_RUNTIME_STRUCT ti_runtime_t;

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- Function Types -------------------- */

/**
 * @brief Function pointer type for native C functions callable from Ti.
 * Return the value_t*
 */
typedef value_t *(*native_fn_t)(value_t **args, int argc);

typedef struct PARAM_STRUCT {
    val_type_t type;          /* Expected parameter type (VAL_INT, VAL_STRING, etc.) */
    char *name;               /* Parameter identifier name (e.g. "a", "count") */
} param_t;

typedef struct FUNCTION_STRUCT {
    const char *name;          /* Function identifier in Ti scripts */
    func_type_t type;          /* FUNC_BUILTIN or FUNC_TI */
    val_type_t  return_type;   /* Declared return type (VAL_INT, VAL_VOID, etc.) */
    param_t    *params;        /* Array of parameter metadata */
    int         param_count;   /* Number of declared parameters (-1 for variadic builtins) */
    union {
        native_fn_t native_fn; /* Builtin native C callback */
        ast_t      *def;       /* Interpreter TI AST function definition */
    };
} function_t;

/* -------------------- Function Operations -------------------- */

/**
 * @brief Register a native C function into the interpreter global functions table.
 * @param name Function name in Ti scripts.
 * @param return_type Declared return value type.
 * @param params Array of parameter metadata (or NULL).
 * @param param_count Number of parameters (-1 for variadic).
 * @param function Native C callback function.
 * @return true on success, false if name exists or out of memory.
 */
bool register_builtin_function(const char *name, val_type_t return_type, param_t *params, int param_count, native_fn_t function);

/**
 * @brief Dispatch and execute a function call with parameter count and type validation.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context scope.
 * @param func Pointer to target function structure.
 * @param argv Array of evaluated argument values.
 * @param argc Number of arguments passed.
 * @param node Function call AST node.
 * @return Evaluated return value_t (or NULL).
 */
value_t *run_function(ti_runtime_t *rt, context_t *ctx, function_t *func, value_t **argv, int argc, ast_t *node);

/**
 * @brief Execute a user-defined Ti function.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context scope.
 * @param func Pointer to target function structure.
 * @param argv Array of evaluated argument values.
 * @param argc Number of arguments passed.
 * @param node Call AST node.
 * @return Evaluated return value_t (or NULL).
 */
value_t *run_ti_function(ti_runtime_t *rt, context_t *ctx, function_t *func, value_t **argv, int argc, ast_t *node);

/**
 * @brief Free dynamically allocated parameter array and name strings.
 * @param list Pointer to head of allocation list (or NULL).
 * @param params Parameter array pointer.
 * @param param_count Number of parameters.
 */
void params_free(alloc_hdr_t **list, param_t *params, int param_count);

/**
 * @brief Free an entire function_t structure and its owned parameters.
 * @param list Pointer to head of allocation list (or NULL).
 * @param func Pointer to function_t.
 */
void function_free(alloc_hdr_t **list, function_t *func);


#ifdef __cplusplus
}
#endif

#endif /* !TI_TYPE_FUNC_H */
