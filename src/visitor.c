#include "include/visitor.h"
#include "include/AST.h"
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void built_in_print(value_t **argv, int argc)
{
  if (argc == 0)
    printf("\n");
  for (int i = 0; i < argc; i++)
  {
    switch(argv[i]->type)
    {
      case VAL_STRING:
        printf("%s",argv[i]->string_val);
        break;
      case VAL_INT:
        printf("%d",argv[i]->int_val);
        break;
      case VAL_FLOAT:
        printf("%.2f",argv[i]->float_val);
        break;
      default:
        printf("Unexpeted type %d", argv[i]->type);
        break;
    }
  }
}

static value_t *binary_add(value_t *left, value_t *right)
{
  value_t *value = init_val(left->type);
  switch (left->type)
  {
    case VAL_INT:
      value->int_val = left->int_val + right->int_val;
      break;
    case VAL_FLOAT:
      value->float_val = left->float_val + right->float_val;
      break;
    case VAL_STRING:
    {
      int length = strlen(left->string_val) + strlen(right->string_val) + 1;
      value->string_val = calloc(1, sizeof(char) * length);
      strcat(value->string_val, left->string_val);
      strcat(value->string_val, right->string_val);
      break;
    }
    default:
      printf("Unexpected operands %d, %d\n", left->type, right->type);
      break;
  }
  return value;
}

static value_t *binary_sub(value_t *left, value_t *right)
{
  value_t *value = init_val(left->type);
  switch (left->type)
  {
    case VAL_INT:
      value->int_val = left->int_val - right->int_val;
      break;
    case VAL_FLOAT:
      value->float_val = left->float_val - right->float_val;
      break;
    default:
      printf("Unexpected operands %d, %d\n", left->type, right->type);
      break;
  }
  return value;
}

static value_t *binary_mul(value_t *left, value_t *right)
{
  value_t *value = init_val(left->type);
  switch (left->type)
  {
    case VAL_INT:
      value->int_val = left->int_val * right->int_val;
      break;
    case VAL_FLOAT:
      value->float_val = left->float_val * right->float_val;
      break;
    default:
      printf("Unexpected operands %d, %d\n", left->type, right->type);
      break;
  }
  return value;
}

static value_t *binary_div(value_t *left, value_t *right)
{
  value_t *value = init_val(left->type);
  switch (left->type)
  {
    case VAL_INT:
      if (right->int_val == 0)
      {
        printf("Division by zero error\n");
        break;
      }
      value->int_val = left->int_val / right->int_val;
      break;
    case VAL_FLOAT:
      if (right->float_val == 0.0f)
      {
        printf("Division by zero error\n");
        break;
      }
      value->float_val = left->float_val / right->float_val;
      break;
    default:
      printf("Unexpected operands %d, %d\n", left->type, right->type);
      break;
  }
  return value;
}

// Comparison functions
static value_t *binary_equal(value_t *left, value_t *right)
{
  value_t *value = init_val(VAL_BOOL);
  switch (left->type)
  {
    case VAL_INT:
      value->bool_val = (left->int_val == right->int_val);
      break;
    case VAL_FLOAT:
      value->float_val = (left->float_val == right->float_val);
      break;
    case VAL_STRING:
      value->bool_val = (strcmp(left->string_val, right->string_val) == 0);
      break;
    case VAL_BOOL:
      value->bool_val = (left->bool_val == right->bool_val);
      break;
    default:
      printf("Unexpected operands %d, %d\n", left->type, right->type);
      break;
  }
  return value;
}

static value_t *binary_greater(value_t *left, value_t *right)
{
  value_t *value = init_val(VAL_BOOL);
  switch (left->type)
  {
    case VAL_INT:
      value->bool_val = (left->int_val > right->int_val);
      break;
    case VAL_FLOAT:
      value->bool_val = (left->float_val > right->float_val);
      break;
    case VAL_STRING:
      value->bool_val = (strcmp(left->string_val, right->string_val) > 0);
      break;
    default:
      printf("Unexpected operands %d, %d\n", left->type, right->type);
      break;
  }
  return value;
}

static value_t *binary_less(value_t *left, value_t *right)
{
  value_t *value = init_val(VAL_BOOL);
  switch (left->type)
  {
    case VAL_INT:
      value->bool_val = (left->int_val < right->int_val);
      break;
    case VAL_FLOAT:
      value->bool_val = (left->float_val < right->float_val);
      break;
    case VAL_STRING:
      value->bool_val = (strcmp(left->string_val, right->string_val) < 0);
      break;
    default:
      printf("Unexpected operands %d, %d\n", left->type, right->type);
      break;
  }
  return value;
}

static value_t *binary_greater_equal(value_t *left, value_t *right)
{
  value_t *value = init_val(VAL_BOOL);
  switch (left->type)
  {
    case VAL_INT:
      value->bool_val = (left->int_val >= right->int_val);
      break;
    case VAL_FLOAT:
      value->bool_val = (left->float_val >= right->float_val);
      break;
    case VAL_STRING:
      value->bool_val = (strcmp(left->string_val, right->string_val) >= 0);
      break;
    default:
      printf("Unexpected operands %d, %d\n", left->type, right->type);
      break;
  }
  return value;
}

static value_t *binary_less_equal(value_t *left, value_t *right)
{
  value_t *value = init_val(VAL_BOOL);
  switch (left->type)
  {
    case VAL_INT:
      value->bool_val = (left->int_val <= right->int_val);
      break;
    case VAL_FLOAT:
      value->bool_val = (left->float_val <= right->float_val);
      break;
    case VAL_STRING:
      value->bool_val = (strcmp(left->string_val, right->string_val) <= 0);
      break;
    default:
      printf("Unexpected operands %d, %d\n", left->type, right->type);
      break;
  }
  return value;
}
static variable_t *find_variable_from_context(context_t *ctx, char *variable_name)
{
  context_t *current_ctx = ctx;
  while(current_ctx != NULL)
  {
    for(int i = 0; i < current_ctx->variable_size; i ++)
    {
      if(strcmp(current_ctx->variables[i]->name, variable_name) == 0)
      {
        return current_ctx->variables[i];
      }
    }
    current_ctx = current_ctx->parent;
  }
  return NULL;
}

value_t *copy_value_from_variable(variable_t *variable)
{
  value_t *value = init_val(variable->value->type);
  switch (variable->value->type)
    {
        case VAL_INT:
            value->int_val = variable->value->int_val;
            break;
        case VAL_FLOAT:
            value->float_val = variable->value->float_val;
            break;
        case VAL_STRING:
            if (variable->value->string_val)
                value->string_val = strdup(variable->value->string_val);
            break;
        default:
            break;
    }
  return value;
}

static void add_variable_to_context(context_t *ctx, char *name, value_t *value)
{
  int size = ctx->variable_size;
  for (int i = 0; i < size; i++)
  {
    if (strcmp(ctx->variables[i]->name, name) == 0)
    {
      printf("Redefinition of variable: %s", name);
      exit(1);
    }
  }
  variable_t *variable = init_variable(name);
  variable->value = value;
  if(ctx->variables == NULL)
  {
    ctx->variables = calloc(1, sizeof(struct VARIABLE_STRUCT*));
    ctx->variables[0] = variable;
    ctx->variable_size = 1;
  }
  else
  {
    // Allocate the new one
    ctx->variables = realloc(ctx->variables, (ctx->variable_size + 1) * sizeof(struct VARIABLE_STRUCT*));
    ctx->variables[ctx->variable_size] = variable;
    ctx->variable_size += 1;
  }
}

static void free_internal_value(value_t *value){
  if(value->type == VAL_STRING)
  {
    free(value->string_val);
  }
}

static void free_internal_context(context_t *ctx)
{
    if (!ctx) return;

    for (int i = 0; i < ctx->variable_size; i++)
    {
        variable_t *var = ctx->variables[i];
        if (var)
        {
            if (var->value)
            {
              free_internal_value(var->value);
              free(var->value);
            }
            if (var->name)
            {
                free(var->name);
            }
            free(var);
        }
    }

    free(ctx->variables);
    ctx->variables = NULL;
    ctx->variable_size = 0;
}

context_t *init_interpreter_context(void)
{
  context_t *contex = calloc(1, sizeof(struct InterpreterContext));
  return contex;
}

void free_context(context_t *ctx)
{
  free_internal_context(ctx);
  free(ctx);
}

value_t *init_val(int type)
{
  value_t *value = calloc(1,sizeof(struct VALUE_STRUCT));
  value->type = type; 
  return value;
}

variable_t *init_variable(char *variable_name){
  variable_t *variable = calloc(1,sizeof(struct VARIABLE_STRUCT));
  variable->name = variable_name;
  return variable;
}

value_t *visitor_visit(InterpreterContext *ctx, ast_t *node)
{
  switch(node->type)
  {
    case AST_FUNCTION_CALL:
      return visitor_visit_function_call(ctx,node);
    case AST_COMPOUND:
      return visitor_visit_compound(ctx,node);
    case AST_VARIABLE_DEFINITION:
      return visitor_visit_variable_definition(ctx, node);
    case AST_ASSIGNMENT:
      return visitor_visit_assignment(ctx, node);
    case AST_STRING_LITERAL:
      return visitor_visit_string_literal(ctx,node);
    case AST_INT_LITERAL:
      return visitor_visit_int_literal(ctx,node);
    case AST_IDENTIFIER:
      return visitor_visit_identifier(ctx,node);
    case AST_BINARY_EXPR:
      return visitor_visit_binary_expr(ctx, node);
    case AST_UNARY_EXPR:
        return visitor_visit_unary_expr(ctx, node);
    case AST_WHILE_STATEMENT:
        return visitor_visit_while_statement(ctx,node);
    default:
        break;
  }
  return NULL;
}

value_t *visitor_visit_expr(InterpreterContext *ctx, ast_t *node)
{
  printf("Visit expesstion\n");
  return NULL;
}

value_t *visitor_visit_binary_expr(InterpreterContext *ctx, ast_t *node)
{
    value_t *left = visitor_visit(ctx, node->value.binary_expr.left);
    value_t *right = visitor_visit(ctx, node->value.binary_expr.right);
    value_t *result = NULL;

    if (left->type != right->type)
    {
        printf("Error: Invalid operands to binary expression: %d and %d\n",
               left->type,
               right->type);
        exit(1);
    }

  switch (node->value.binary_expr.op)
  {
    case OP_ADD:
      result = binary_add(left, right);
      break;
    case OP_SUB:
      result = binary_sub(left, right);
      break;
    case OP_MUL:
      result = binary_mul(left, right);
      break;
    case OP_DIV:
      result = binary_div(left, right);
      break;
    case OP_EQ:
      result = binary_equal(left, right);
      break;
    case OP_GT:
      result = binary_greater(left, right);
      break;
    case OP_LT:
      result = binary_less(left, right);
      break;
    case OP_GTE:
      result = binary_greater_equal(left, right);
      break;
    case OP_LTE:
      result = binary_less_equal(left, right);
      break;
    default:
      printf("Unknown operator: %d\n", node->value.binary_expr.op);
      break;
  }

out:
    free_internal_value(left);
    free_internal_value(right);
    free(left);
    free(right);
    return result;
}

value_t *visitor_visit_unary_expr(InterpreterContext *ctx, ast_t *node)
{
  return NULL;
}

value_t *visitor_visit_variable_definition(InterpreterContext *ctx, ast_t *node)
{
  char *variable_name = strdup(node->value.variable_definition.variable_name);
  value_t *value = visitor_visit(ctx, node->value.variable_definition.value);
  add_variable_to_context(ctx, variable_name, value);
  return NULL;
}

value_t *visitor_visit_assignment(InterpreterContext *ctx, ast_t *node)
{
  printf("Visitor assignment\n");
  variable_t *variable = find_variable_from_context(ctx, node->value.identifier);
  // ------------------ Here ---------------------------------
  return NULL;
}

value_t *visitor_visit_while_statement(InterpreterContext *ctx, ast_t *node)
{
  printf("Visit while statement\n");
  value_t *value = visitor_visit(ctx, node->value.while_statement.condition);
  if(value->type != VAL_BOOL)
  {
    printf("[Error]: Unexpeted type %d, only expect bool value\n", value->type);
    exit(1);
  }
  while(value->bool_val)
  {
    context_t *local_ctx = init_interpreter_context();
    local_ctx->parent = ctx;
    // Remove the old value 
    free_internal_value(value);
    free(value);
    value_t *capture = visitor_visit(local_ctx,node->value.while_statement.body);
    if (capture != NULL)
    {
      printf("[Warning]: Compound return value, please check it\n");
      free_internal_value(capture);
      free(capture);
    }
    // Free local context 
    free_internal_context(local_ctx);
    free(local_ctx);
    // Check condition agian
    value = visitor_visit(ctx, node->value.while_statement.condition);
    if(value->type != VAL_BOOL)
    {
      printf("[Error]: Unexpeted type %d, only expect bool value\n", value->type);
      exit(1);
    }
  }
  // Free final value (false condition)
  free_internal_value(value);
  free(value);
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
  value_t *ret = NULL;
  value_t **argv = calloc(node->value.function_call.num_arg, sizeof(struct VALUE_STRUCT*));
  int argc = node->value.function_call.num_arg;
  if(strcmp(node->value.function_call.func, "print") == 0)
  {
    for (int i = 0; i < node->value.function_call.num_arg; i ++)
    {
      value_t *value = visitor_visit(ctx,node->value.function_call.args[i]);
      argv[i] = value;
    }
    built_in_print(argv,argc);
  }
  for (int i = 0; i < argc; i++){
    free_internal_value(argv[i]);
    free(argv[i]);
  }
  free(argv);
  return ret;
}

value_t *visitor_visit_string_literal(InterpreterContext *ctx, ast_t *node)
{
  value_t *value = init_val(VAL_STRING);
  value->string_val = strdup(node->value.string_value);
  return value;
}

value_t *visitor_visit_int_literal(InterpreterContext *ctx, ast_t *node)
{
  value_t *value = init_val(VAL_INT);
  value->int_val = node->value.int_value;
  return value;
}

value_t *visitor_visit_float_literal(InterpreterContext *ctx, ast_t *node)
{
  value_t *value = init_val(VAL_FLOAT);
  value->float_val = node->value.float_value;
  return value;
}

value_t *visitor_visit_compound(InterpreterContext *ctx, ast_t *node)
{
  int size = node->value.compound.compound_size;
  for (int i = 0; i < size; i ++)
  {
    value_t *value = visitor_visit(ctx,node->value.compound.compound_value[i]);
    if (value != NULL)
    {
      printf("[Warning]: Still threre are statement return not NULL, please change it");
      free(value);
    }
  }
  return NULL; 
}

value_t *visitor_visit_identifier(InterpreterContext *ctx, ast_t *node)
{
  variable_t *variable = find_variable_from_context(ctx, node->value.identifier);
  if (variable != NULL)
  {
    value_t *value = copy_value_from_variable(variable);
    return value;
  }
  printf("Undefined variabe: %s", node->value.identifier);
  exit(1);
}
