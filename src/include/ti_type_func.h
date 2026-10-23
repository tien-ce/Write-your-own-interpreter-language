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
 * The handle identifies the calling runtime; natives may store it (e.g. to post events later),
 * because a stale handle is safely rejected once the runtime is destroyed.
 * @param handle Handle of the runtime executing the call.
 * @param args Array of borrowed argument values (freed by the caller after return).
 * @param argc Number of arguments passed.
 * @return Newly allocated result value_t.
 */
typedef value_t *(*native_fn_t)(ti_handle_t handle, value_t **args, int argc);

typedef struct PARAM_STRUCT {
    val_type_t type;          /* Expected parameter type (VAL_INT, VAL_STRING, etc.); VAL_ANY (natives only) accepts any type */
    char *name;               /* Parameter identifier name (e.g. "a", "count") */
    val_type_t element_type;  /* Expected element type when type is VAL_LIST (VAL_NULL accepts any) */
} param_t;

typedef struct FUNCTION_STRUCT {
    const char *name;          /* Function identifier in Ti scripts */
    func_type_t type;          /* FUNC_BUILTIN or FUNC_TI */
    val_type_t  return_type;   /* Declared return type (VAL_INT, VAL_VOID, etc.) */
    val_type_t  return_element_type; /* Element type when return_type is VAL_LIST (VAL_NULL otherwise) */
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
 * @param params Array of parameter metadata (NULL only when param_count is 0 or -1). A parameter of
 *               type VAL_ANY accepts a value of any type, so the argument count stays checked.
 * @param param_count Number of parameters (-1 for fully variadic: neither count nor types are checked).
 * @param function Native C callback function.
 * @return true on success, false if name exists, the parameter table is inconsistent or out of memory.
 */
bool register_builtin_function(const char *name, val_type_t return_type, param_t *params, int param_count, native_fn_t function);

/**
 * @brief One row of a built-in table: the same five values register_builtin_function takes.
 */
typedef struct {
    const char  *name;         /* Identifier scripts call */
    val_type_t   return_type;  /* Declared return type */
    param_t     *params;       /* Parameter table (NULL when param_count is 0 or -1) */
    int          param_count;  /* Fixed count, or -1 for variadic */
    native_fn_t  function;     /* Native implementation */
} ti_builtin_def_t;

/**
 * @brief Register every row of a built-in table with register_builtin_function.
 * @param defs Array of rows (tables are normally static const).
 * @param count Number of rows.
 * @return true if every row registered; false if any failed (the remaining rows are still tried).
 */
bool ti_register_builtin_table(const ti_builtin_def_t *defs, int count);

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
