#ifndef VISITOR_H
#define VISITOR_H

#include "AST.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- Value & Context Types -------------------- */

typedef enum {
    VAL_NULL,
    VAL_INT,
    VAL_FLOAT,
    VAL_STRING,
    VAL_BOOL,
} value_type_t;

typedef struct VALUE_STRUCT {
    value_type_t type;
    union {
        int int_val;
        float float_val;
        char *string_val;
        bool bool_val;
    };
} value_t;

/**
 * @brief Function pointer type for native C functions callable from Ti.
 */
typedef value_t *(*native_fn_t)(value_t **args, int argc);

typedef struct BUILTIN_FUNC_STRUCT {
    const char *name; // Function name
    native_fn_t fn;
} builtin_func_t;

typedef struct VARIABLE_STRUCT {
    const char *name;
    value_t *value;
} variable_t;

typedef struct InterpreterContext {
    struct InterpreterContext *parent;
    variable_t **variables;
    int variable_size;
} context_t, InterpreterContext;

/* -------------------- Memory & Object Initializers -------------------- */

/**
 * @brief Allocate a new value_t of the specified type.
 * @param type Value type enum value.
 * @return Pointer to newly allocated value_t.
 */
value_t *val_init(int type);

/**
 * @brief Allocate a new interpreter context scope.
 * @return Pointer to newly allocated context_t.
 */
context_t *context_init(void);

/**
 * @brief Free an interpreter context and its scoped variables.
 * @param ctx Pointer to context scope to free.
 */
void context_free(context_t *ctx);

/* -------------------- Value Helper Constructors -------------------- */

/**
 * @brief Create a null value_t.
 * @return Newly allocated VAL_NULL value_t.
 */
value_t *val_new_null(void);

/**
 * @brief Create an integer value_t.
 * @param v Integer value.
 * @return Newly allocated VAL_INT value_t.
 */
value_t *val_new_int(int v);

/**
 * @brief Create a float value_t.
 * @param v Float value.
 * @return Newly allocated VAL_FLOAT value_t.
 */
value_t *val_new_float(float v);

/**
 * @brief Create a string value_t (duplicates string into tracked memory).
 * @param s String content (or NULL).
 * @return Newly allocated VAL_STRING value_t.
 */
value_t *val_new_string(const char *s);

/**
 * @brief Create a boolean value_t.
 * @param b Boolean value.
 * @return Newly allocated VAL_BOOL value_t.
 */
value_t *val_new_bool(bool b);

/* -------------------- AST Evaluator / Visitor -------------------- */

/**
 * @brief Main entry point to evaluate an AST node in the given context.
 * @param ctx Pointer to active execution context scope.
 * @param node Pointer to AST node to evaluate.
 * @return Pointer to evaluated result value_t (or NULL).
 */
value_t *visitor_visit(InterpreterContext *ctx, ast_t *node);

/* -------------------- Built-in Registration -------------------- */

/**
 * @brief Register a native C function into the interpreter global built-ins table.
 * @param name Function name in Ti scripts.
 * @param function Native C callback function.
 * @return true on success, false if name exists or out of memory.
 */
bool register_builtin_function(const char *name, native_fn_t function);

#ifdef __cplusplus
}
#endif

#endif /* !VISITOR_H */
