#ifndef VISITOR_H
#define VISITOR_H

#include "AST.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/* Function pointer to function return value_t value */
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
  /* Variable */
  variable_t **variables;
  int variable_size;
} context_t, InterpreterContext;

/* Memory & Object Initializers */
value_t *init_val(int type);
variable_t *init_variable(char *variable_name);

/* Value Helper Constructors (for easy built-in function creation) */
value_t *val_new_null(void);
value_t *val_new_int(int v);
value_t *val_new_float(float v);
value_t *val_new_string(const char *s);
value_t *val_new_bool(bool b);

/* AST Evaluator / Visitor */
value_t *visitor_visit_expr(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_binary_expr(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_unary_expr(InterpreterContext *ctx, ast_t *node);

value_t *visitor_visit_variable_definition(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_assignment(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_while_statement(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_if_statement(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_for_statement(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_function_call(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_compound(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_string_literal(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_int_literal(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_float_literal(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_identifier(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit(InterpreterContext *ctx, ast_t *node);

bool register_builtin_function(const char *name, native_fn_t function);
context_t *init_interpreter_context(void);
void free_context(context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif // VISITOR_H
