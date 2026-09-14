#ifndef VISITOR_INTERNAL_H
#define VISITOR_INTERNAL_H

#include "AST.h"
#include "ti_type.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- Value & Context Types -------------------- */

typedef struct VALUE_STRUCT {
    val_type_t type;
    union {
        int int_val;
        float float_val;
        char *string_val;
        bool bool_val;
    };
} value_t;

/**
 * @brief Function pointer type for native C functions callable from Ti.
 * Return the value_t*
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

/**
 * @brief Allocate a new variable_t with the given variable name.
 * @param variable_name Name string for the variable.
 * @return Newly allocated variable_t.
 */
variable_t *variable_init(const char *variable_name);

/**
 * @brief Find a variable by name walking up from the current context to root parent.
 * @param ctx Starting context scope.
 * @param variable_name Identifier name to look up.
 * @return Pointer to variable_t if found, NULL otherwise.
 */
variable_t *context_find_variable(context_t *ctx, const char *variable_name);

/**
 * @brief Create a deep copy of a variable's value_t.
 * @param variable Source variable pointer.
 * @return Newly allocated copied value_t.
 */
value_t *context_copy_value(variable_t *variable);

/**
 * @brief Add a newly defined variable to the given context scope.
 * @param ctx Pointer to target context scope.
 * @param name Variable identifier name.
 * @param value Evaluated value pointer.
 */
void context_add_variable(context_t *ctx, const char *name, value_t *value);

/**
 * @brief Free all variables and internal structures inside a context scope.
 * @param ctx Pointer to context scope.
 */
void context_free_internal(context_t *ctx);

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

/**
 * @brief Free dynamically allocated payload inside value_t (e.g. string_val).
 * @param value Pointer to value_t.
 */
void val_free_internal(value_t *value);

/* -------------------- AST Evaluator / Visitor Dispatcher -------------------- */

/**
 * @brief Core AST recursive evaluator and dispatcher.
 * Routes each AST node to its respective statement/expression evaluator.
 *
 * @param ctx Pointer to active execution context scope.
 * @param node Pointer to AST node to evaluate.
 * @return Pointer to evaluated result value_t (or NULL).
 */
value_t *visitor_visit(InterpreterContext *ctx, ast_t *node);

/* -------------------- Expression Evaluators -------------------- */

/**
 * @brief Evaluate a binary expression node (+, -, *, /, ==, <, etc.).
 * @param ctx Pointer to active execution context scope.
 * @param node Pointer to binary expression AST node.
 * @return Evaluated result value_t.
 */
value_t *eval_binary_expr(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Evaluate a unary expression node (!, -, +).
 * @param ctx Pointer to active execution context scope.
 * @param node Pointer to unary expression AST node.
 * @return Evaluated result value_t.
 */
value_t *eval_unary_expr(InterpreterContext *ctx, ast_t *node);

/* -------------------- Statement / Control Flow Evaluators -------------------- */

/**
 * @brief Evaluate an if statement node (including else branch).
 * @param ctx Pointer to active execution context scope.
 * @param node Pointer to if statement AST node.
 * @return Always NULL.
 */
value_t *eval_if_statement(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Evaluate a while loop statement node.
 * @param ctx Pointer to active execution context scope.
 * @param node Pointer to while statement AST node.
 * @return Always NULL.
 */
value_t *eval_while_statement(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Evaluate a for loop statement node (stub).
 * @param ctx Pointer to active execution context scope.
 * @param node Pointer to for statement AST node.
 * @return Always NULL.
 */
value_t *eval_for_statement(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Evaluate a compound statement block node.
 * @param ctx Pointer to active execution context scope.
 * @param node Pointer to compound AST node.
 * @return Always NULL.
 */
value_t *eval_compound_statement(InterpreterContext *ctx, ast_t *node);

/* -------------------- Variable & Identifier Evaluators -------------------- */

/**
 * @brief Evaluate a variable definition node and register into context.
 * @param ctx Pointer to active execution context scope.
 * @param node Variable definition AST node.
 * @return Always NULL.
 */
value_t *eval_variable_definition(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Evaluate an assignment statement node.
 * @param ctx Pointer to active execution context scope.
 * @param node Assignment AST node.
 * @return Always NULL.
 */
value_t *eval_assignment(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Look up and evaluate an identifier node.
 * @param ctx Pointer to active execution context scope.
 * @param node Identifier AST node.
 * @return Evaluated value_t pointer.
 */
value_t *eval_identifier(InterpreterContext *ctx, ast_t *node);

/* -------------------- Literal Evaluators -------------------- */

/**
 * @brief Evaluate a string literal node.
 * @param ctx Pointer to active execution context scope.
 * @param node String literal AST node.
 * @return Newly allocated string value_t.
 */
value_t *eval_string_literal(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Evaluate an integer literal node.
 * @param ctx Pointer to active execution context scope.
 * @param node Integer literal AST node.
 * @return Newly allocated integer value_t.
 */
value_t *eval_int_literal(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Evaluate a float literal node.
 * @param ctx Pointer to active execution context scope.
 * @param node Float literal AST node.
 * @return Newly allocated float value_t.
 */
value_t *eval_float_literal(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Evaluate a boolean literal node.
 * @param ctx Pointer to active execution context scope.
 * @param node Boolean literal AST node.
 * @return Newly allocated boolean value_t.
 */
value_t *eval_boolean_literal(InterpreterContext *ctx, ast_t *node);

/* -------------------- Function Call & Definition Evaluators -------------------- */

/**
 * @brief Evaluate a function call node and dispatch to native builtin.
 * @param ctx Pointer to active execution context scope.
 * @param node Function call AST node.
 * @return Returned value_t from function callback.
 */
value_t *eval_function_call(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Evaluate a function definition node (stub).
 * @param ctx Pointer to active execution context scope.
 * @param node Function definition AST node.
 * @return Always NULL.
 */
value_t *eval_function_definition(InterpreterContext *ctx, ast_t *node);

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

#endif /* !VISITOR_INTERNAL_H */
