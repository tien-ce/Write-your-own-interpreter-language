#ifndef VISITOR_H
#define VISITOR_H

#include "AST.h"
#include <stdbool.h>

typedef enum {
  VAL_NULL,
  VAL_INT,
  VAL_FLOAT,
  VAL_STRING,
  VAL_BOOL
} value_type_t;

typedef struct VALUE_STRUCT {
  int type;
  union {
    int int_val;
    float float_val;
    char *string_val;
    bool bool_val;
  };
} value_t;

typedef struct VARIABLE_STRUCT {
  char *name;
  int type;
  value_t *value;
} variable_t;

typedef struct InterpreterContext {
  variable_t **variables;
  int variable_size;
} context_t, InterpreterContext;

context_t *init_interpreter_context(void);
value_t *init_val(int type);

value_t *visitor_visit(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_statements(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_statement(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_expr(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_binary_expr(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_unary_expr(InterpreterContext *ctx, ast_t *node);

value_t *visitor_visit_variable_definition(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_assignment(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_while_statement(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_if_statement(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_for_statement(InterpreterContext *ctx, ast_t *node);
value_t *visitor_visit_function_call(InterpreterContext *ctx, ast_t *node);

#endif
