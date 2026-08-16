#ifndef VISITOR_H
#define VISITOR_H

#include "AST.h"
#include <stdbool.h>

typedef struct VALUE_STRUCT {
  enum {
    VAL_NULL,
    VAL_INT,
    VAL_FLOAT,
    VAL_STRING,
    VAL_BOOL,
    VAL_FUNC,
  } type;
  union {
    int int_val;
    float float_val;
    char *string_val;
    bool bool_val;
  };
} value_t;

typedef struct VARIABLE_STRUCT {
  char *name;
  value_t *value;
} variable_t;

typedef struct InterpreterContext {
  struct InterpreterContext *parent;
  variable_t **variables;
  int variable_size;
} context_t, InterpreterContext;

value_t *init_val(int type);
variable_t *init_variable(char *variable_name);

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


context_t *init_interpreter_context(void);
void free_context(context_t *ctx);
#endif
