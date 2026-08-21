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
 */
value_t *init_val(int type);

/**
 * @brief Allocate a new variable_t with the given variable name.
 */
variable_t *init_variable(char *variable_name);

/**
 * @brief Allocate a new interpreter context scope.
 */
context_t *init_interpreter_context(void);

/**
 * @brief Free an interpreter context and its scoped variables.
 */
void free_context(context_t *ctx);

/* -------------------- Value Helper Constructors -------------------- */

/**
 * @brief Create a null value_t.
 */
value_t *val_new_null(void);

/**
 * @brief Create an integer value_t.
 */
value_t *val_new_int(int v);

/**
 * @brief Create a float value_t.
 */
value_t *val_new_float(float v);

/**
 * @brief Create a string value_t (duplicates string).
 */
value_t *val_new_string(const char *s);

/**
 * @brief Create a boolean value_t.
 */
value_t *val_new_bool(bool b);

/* -------------------- AST Evaluator / Visitor -------------------- */

/**
 * @brief Main entry point to evaluate an AST node in the given context.
 */
value_t *visitor_visit(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Evaluate an expression node.
 */
value_t *visitor_visit_expr(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Evaluate a binary expression node (+, -, *, /, ==, <, etc.).
 */
value_t *visitor_visit_binary_expr(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Evaluate a unary expression node (!, -, etc.).
 */
value_t *visitor_visit_unary_expr(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Evaluate a variable definition node and add to context.
 */
value_t *visitor_visit_variable_definition(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Evaluate an assignment node.
 */
value_t *visitor_visit_assignment(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Execute a while loop node.
 */
value_t *visitor_visit_while_statement(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Execute an if/else statement node.
 */
value_t *visitor_visit_if_statement(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Execute a for statement node.
 */
value_t *visitor_visit_for_statement(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Evaluate a function call node.
 */
value_t *visitor_visit_function_call(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Execute a compound block node.
 */
value_t *visitor_visit_compound(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Evaluate a string literal node.
 */
value_t *visitor_visit_string_literal(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Evaluate an integer literal node.
 */
value_t *visitor_visit_int_literal(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Evaluate a float literal node.
 */
value_t *visitor_visit_float_literal(InterpreterContext *ctx, ast_t *node);

/**
 * @brief Look up and evaluate an identifier node.
 */
value_t *visitor_visit_identifier(InterpreterContext *ctx, ast_t *node);

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

#endif // VISITOR_H
