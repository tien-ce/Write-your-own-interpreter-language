#include "include/visitor.h"
#include "include/AST.h"
#include <stdio.h>
#include <stdlib.h>

context_t *init_interpreter_context(void)
{
  return NULL;
}

value_t *init_val(int type)
{
  return NULL;
}

value_t *visitor_visit(InterpreterContext *ctx, ast_t *node)
{
  return NULL;
}

value_t *visitor_visit_statements(InterpreterContext *ctx, ast_t *node)
{
  return NULL;
}

value_t *visitor_visit_statement(InterpreterContext *ctx, ast_t *node)
{
  return NULL;
}

value_t *visitor_visit_expr(InterpreterContext *ctx, ast_t *node)
{
  return NULL;
}

value_t *visitor_visit_binary_expr(InterpreterContext *ctx, ast_t *node)
{
  return NULL;
}

value_t *visitor_visit_unary_expr(InterpreterContext *ctx, ast_t *node)
{
  return NULL;
}

value_t *visitor_visit_variable_definition(InterpreterContext *ctx, ast_t *node)
{
  return NULL;
}

value_t *visitor_visit_assignment(InterpreterContext *ctx, ast_t *node)
{
  return NULL;
}

value_t *visitor_visit_while_statement(InterpreterContext *ctx, ast_t *node)
{
  return NULL;
}

value_t *visitor_visit_if_statement(InterpreterContext *ctx, ast_t *node)
{
  return NULL;
}

value_t *visitor_visit_for_statement(InterpreterContext *ctx, ast_t *node)
{
  return NULL;
}

value_t *visitor_visit_function_call(InterpreterContext *ctx, ast_t *node)
{
  return NULL;
}
