#include "include/visitor.h"
#include "include/AST.h"
#include "include/tracked_memory.h"
#include "TienInterpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------- Variables -------------------- */

static builtin_func_t *g_builtins;
static int g_builtin_count = 0;

/* -------------------- Static Function Prototypes -------------------- */

static bool eval_boolean_condition(InterpreterContext *ctx, ast_t *cond_node);
static void visitor_execute_body(InterpreterContext *parent_ctx, ast_t *body_node);
static value_t *binary_add(value_t *left, value_t *right);
static value_t *binary_sub(value_t *left, value_t *right);
static value_t *binary_mul(value_t *left, value_t *right);
static value_t *binary_div(value_t *left, value_t *right);
static value_t *binary_equal(value_t *left, value_t *right);
static value_t *binary_greater(value_t *left, value_t *right);
static value_t *binary_less(value_t *left, value_t *right);
static value_t *binary_greater_equal(value_t *left, value_t *right);
static value_t *binary_less_equal(value_t *left, value_t *right);
static variable_t *find_variable_from_context(context_t *ctx, char *variable_name);
static value_t *copy_value_from_variable(variable_t *variable);
static void add_variable_to_context(context_t *ctx, char *name, value_t *value);
static void free_internal_value(value_t *value);
static void free_internal_context(context_t *ctx);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Evaluate condition expression node and ensure boolean result.
 */
static bool eval_boolean_condition(InterpreterContext *ctx, ast_t *cond_node)
{
    value_t *value = visitor_visit(ctx, cond_node);
    if (!value || value->type != VAL_BOOL)
    {
        ti_log("[ERROR]: Unexpected type %d, only expect bool value\n", value ? value->type : -1);
        ti_fatal();
    }

    bool res = value->bool_val;
    free_internal_value(value);
    tracked_free(value);
    return res;
}

/**
 * @brief Execute a child compound block in a newly created local scope.
 */
static void visitor_execute_body(InterpreterContext *parent_ctx, ast_t *body_node)
{
    if (!body_node) return;

    // 1. Create new scope 
    context_t *local_ctx = init_interpreter_context();
    local_ctx->parent = parent_ctx;

    // 2. Execute the statements 
    value_t *capture = visitor_visit(local_ctx, body_node);
    if (capture != NULL)
    {
        ti_log("[Warning]: Compound return value, please check it\n");
        free_internal_value(capture);
        tracked_free(capture);
    }

    // 3. Free the local scope
    free_internal_context(local_ctx);
    tracked_free(local_ctx);
}

/**
 * @brief Evaluate binary addition for integers, floats, or strings (concatenation).
 */
static value_t *binary_add(value_t *left, value_t *right)
{
  if (left == NULL || right == NULL) {
    ti_log("[ERROR]: Invalid operands in binary add\n");
    ti_fatal();
    return NULL;
  }
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
      const char *s_left = left->string_val ? left->string_val : "";
      const char *s_right = right->string_val ? right->string_val : "";
      int length = strlen(s_left) + strlen(s_right) + 1;
      value->string_val = tracked_calloc(1, sizeof(char) * length);
      strcat(value->string_val, s_left);
      strcat(value->string_val, s_right);
      break;
    }
    default:
      ti_log("[ERROR]: Unexpected operands %d, %d in binary add\n", left->type, right->type);
      ti_fatal();
      break;
  }
  return value;
}

/**
 * @brief Evaluate binary subtraction for integers or floats.
 */
static value_t *binary_sub(value_t *left, value_t *right)
{
  if (left == NULL || right == NULL) {
    ti_log("[ERROR]: Invalid operands in binary sub\n");
    ti_fatal();
    return NULL;
  }
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
      ti_log("[ERROR]: Unexpected operands %d, %d in binary sub\n", left->type, right->type);
      ti_fatal();
      break;
  }
  return value;
}

/**
 * @brief Evaluate binary multiplication for integers or floats.
 */
static value_t *binary_mul(value_t *left, value_t *right)
{
  if (left == NULL || right == NULL) {
    ti_log("[ERROR]: Invalid operands in binary mul\n");
    ti_fatal();
    return NULL;
  }
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
      ti_log("[ERROR]: Unexpected operands %d, %d in binary mul\n", left->type, right->type);
      ti_fatal();
      break;
  }
  return value;
}

/**
 * @brief Evaluate binary division for integers or floats (with division-by-zero check).
 */
static value_t *binary_div(value_t *left, value_t *right)
{
  if (left == NULL || right == NULL) {
    ti_log("[ERROR]: Invalid operands in binary div\n");
    ti_fatal();
    return NULL;
  }
  value_t *value = init_val(left->type);
  switch (left->type)
  {
    case VAL_INT:
      if (right->int_val == 0)
      {
        ti_log("[ERROR]: Division by zero error\n");
        ti_fatal();
        break;
      }
      value->int_val = left->int_val / right->int_val;
      break;
    case VAL_FLOAT:
      if (right->float_val == 0.0f)
      {
        ti_log("[ERROR]: Division by zero error\n");
        ti_fatal();
        break;
      }
      value->float_val = left->float_val / right->float_val;
      break;
    default:
      ti_log("[ERROR]: Unexpected operands %d, %d in binary div\n", left->type, right->type);
      ti_fatal();
      break;
  }
  return value;
}

/**
 * @brief Evaluate equality comparison (==) between two values.
 */
static value_t *binary_equal(value_t *left, value_t *right)
{
  if (left == NULL || right == NULL) {
    ti_log("[ERROR]: Invalid operands in binary equal\n");
    ti_fatal();
    return NULL;
  }
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
    {
      const char *s_left = left->string_val ? left->string_val : "";
      const char *s_right = right->string_val ? right->string_val : "";
      value->bool_val = (strcmp(s_left, s_right) == 0);
      break;
    }
    case VAL_BOOL:
      value->bool_val = (left->bool_val == right->bool_val);
      break;
    case VAL_NULL:
      value->bool_val = true;
      break;
    default:
      ti_log("[ERROR]: Unexpected operands %d, %d in binary equal\n", left->type, right->type);
      ti_fatal();
      break;
  }
  return value;
}

/**
 * @brief Evaluate greater-than comparison (>) between two values.
 */
static value_t *binary_greater(value_t *left, value_t *right)
{
  if (left == NULL || right == NULL) {
    ti_log("[ERROR]: Invalid operands in binary greater\n");
    ti_fatal();
    return NULL;
  }
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
    {
      const char *s_left = left->string_val ? left->string_val : "";
      const char *s_right = right->string_val ? right->string_val : "";
      value->bool_val = (strcmp(s_left, s_right) > 0);
      break;
    }
    default:
      ti_log("[ERROR]: Unexpected operands %d, %d in binary greater\n", left->type, right->type);
      ti_fatal();
      break;
  }
  return value;
}

/**
 * @brief Evaluate less-than comparison (<) between two values.
 */
static value_t *binary_less(value_t *left, value_t *right)
{
  if (left == NULL || right == NULL) {
    ti_log("[ERROR]: Invalid operands in binary less\n");
    ti_fatal();
    return NULL;
  }
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
    {
      const char *s_left = left->string_val ? left->string_val : "";
      const char *s_right = right->string_val ? right->string_val : "";
      value->bool_val = (strcmp(s_left, s_right) < 0);
      break;
    }
    default:
      ti_log("[ERROR]: Unexpected operands %d, %d in binary less\n", left->type, right->type);
      ti_fatal();
      break;
  }
  return value;
}

/**
 * @brief Evaluate greater-than-or-equal comparison (>=) between two values.
 */
static value_t *binary_greater_equal(value_t *left, value_t *right)
{
  if (left == NULL || right == NULL) {
    ti_log("[ERROR]: Invalid operands in binary greater equal\n");
    ti_fatal();
    return NULL;
  }
  value_t *value = init_val(VAL_BOOL);
  switch (left->type)
  {
    case VAL_INT:
      value->bool_val = (left->int_val >= right->int_val);
      break;
    case VAL_FLOAT:
      value->float_val = (left->float_val >= right->float_val);
      break;
    case VAL_STRING:
    {
      const char *s_left = left->string_val ? left->string_val : "";
      const char *s_right = right->string_val ? right->string_val : "";
      value->bool_val = (strcmp(s_left, s_right) >= 0);
      break;
    }
    default:
      ti_log("[ERROR]: Unexpected operands %d, %d in binary greater equal\n", left->type, right->type);
      ti_fatal();
      break;
  }
  return value;
}

/**
 * @brief Evaluate less-than-or-equal comparison (<=) between two values.
 */
static value_t *binary_less_equal(value_t *left, value_t *right)
{
  if (left == NULL || right == NULL) {
    ti_log("[ERROR]: Invalid operands in binary less equal\n");
    ti_fatal();
    return NULL;
  }
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
    {
      const char *s_left = left->string_val ? left->string_val : "";
      const char *s_right = right->string_val ? right->string_val : "";
      value->bool_val = (strcmp(s_left, s_right) <= 0);
      break;
    }
    default:
      ti_log("[ERROR]: Unexpected operands %d, %d in binary less equal\n", left->type, right->type);
      ti_fatal();
      break;
  }
  return value;
}

/**
 * @brief Look up a variable by name in current and ancestor scopes.
 */
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

/**
 * @brief Create a deep copy of a variable's value_t.
 */
static value_t *copy_value_from_variable(variable_t *variable)
{
  if (variable == NULL || variable->value == NULL)
  {
    ti_log("[ERROR]: Attempted to access NULL variable\n");
    ti_fatal();
    return NULL;
  }
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
        value->string_val = variable->value->string_val ? tracked_strdup(variable->value->string_val) : NULL;
        break;
    case VAL_BOOL:
        value->bool_val = variable->value->bool_val;
        break;
    case VAL_NULL:
        break;
    default:
        ti_log("[ERROR]: Unknown value type %d in copy_value_from_variable\n", variable->value->type);
        ti_fatal();
        break;
  }
  return value;
}

/**
 * @brief Add a newly defined variable to the given context scope.
 */
static void add_variable_to_context(context_t *ctx, char *name, value_t *value)
{
  int size = ctx->variable_size;
  for (int i = 0; i < size; i++)
  {
    if (strcmp(ctx->variables[i]->name, name) == 0)
    {
      ti_log("[ERROR]: Redefinition of variable '%s'\n", name);
      ti_fatal();
    }
  }
  variable_t *variable = init_variable(name);
  variable->value = value;
  if(ctx->variables == NULL)
  {
    ctx->variables = tracked_calloc(1, sizeof(struct VARIABLE_STRUCT*));
    ctx->variables[0] = variable;
    ctx->variable_size = 1;
  }
  else
  {
    // Allocate the new one
    ctx->variables = tracked_realloc(ctx->variables, (ctx->variable_size + 1) * sizeof(struct VARIABLE_STRUCT*));
    ctx->variables[ctx->variable_size] = variable;
    ctx->variable_size += 1;
  }
}

/**
 * @brief Free dynamically allocated payload inside value_t (e.g. string_val).
 */
static void free_internal_value(value_t *value)
{
  if (value == NULL) return;
  if(value->type == VAL_STRING && value->string_val != NULL)
  {
    tracked_free(value->string_val);
    value->string_val = NULL;
  }
}

/**
 * @brief Free all variables and internal structures inside a context scope.
 */
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
              tracked_free(var->value);
            }
            if (var->name)
            {
                tracked_free(var->name);
            }
            tracked_free(var);
        }
    }

    tracked_free(ctx->variables);
    ctx->variables = NULL;
    ctx->variable_size = 0;
}

/* -------------------- Public Functions -------------------- */

context_t *init_interpreter_context(void)
{
  context_t *contex = tracked_calloc(1, sizeof(struct InterpreterContext));
  return contex;
}

void free_context(context_t *ctx)
{
  free_internal_context(ctx);
  tracked_free(ctx);
}

value_t *init_val(int type)
{
  value_t *value = tracked_calloc(1,sizeof(struct VALUE_STRUCT));
  value->type = type;
  return value;
}

value_t *val_new_null(void)
{
  return init_val(VAL_NULL);
}

value_t *val_new_int(int v)
{
  value_t *val = init_val(VAL_INT);
  val->int_val = v;
  return val;
}

value_t *val_new_float(float v)
{
  value_t *val = init_val(VAL_FLOAT);
  val->float_val = v;
  return val;
}

value_t *val_new_string(const char *s)
{
  value_t *val = init_val(VAL_STRING);
  val->string_val = s ? tracked_strdup(s) : NULL;
  return val;
}

value_t *val_new_bool(bool b)
{
  value_t *val = init_val(VAL_BOOL);
  val->bool_val = b;
  return val;
}

variable_t *init_variable(char *variable_name){
  variable_t *variable = tracked_calloc(1,sizeof(struct VARIABLE_STRUCT));
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
    case AST_FLOAT_LITERAL:
      return visitor_visit_float_literal(ctx,node);
    case AST_BOOLEAN:
      return visitor_visit_boolean(ctx,node);
    case AST_IDENTIFIER:
      return visitor_visit_identifier(ctx,node);
    case AST_BINARY_EXPR:
      return visitor_visit_binary_expr(ctx, node);
    case AST_UNARY_EXPR:
        return visitor_visit_unary_expr(ctx, node);
    case AST_WHILE_STATEMENT:
        return visitor_visit_while_statement(ctx,node);
    case AST_IF_STATEMENT:
        return visitor_visit_if_statement(ctx, node);
    default:
        break;
  }
  return NULL;
}

value_t *visitor_visit_expr(InterpreterContext *ctx, ast_t *node)
{
  ti_log("Visit expression\n");
  return NULL;
}

value_t *visitor_visit_binary_expr(InterpreterContext *ctx, ast_t *node)
{
    value_t *left = visitor_visit(ctx, node->value.binary_expr.left);
    value_t *right = visitor_visit(ctx, node->value.binary_expr.right);
    value_t *result = NULL;

    if (left == NULL || right == NULL)
    {
        ti_log("[ERROR]: Binary expression operand evaluated to NULL\n");
        ti_fatal();
    }

    if (left->type != right->type)
    {
        ti_log("[ERROR]: Type mismatch in binary expression: %d and %d\n",
               left->type,
               right->type);
        ti_fatal();
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
    case OP_DEQ:
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
      ti_log("[ERROR]: Unknown operator: %d\n", node->value.binary_expr.op);
      ti_fatal();
      break;
  }

    free_internal_value(left);
    free_internal_value(right);
    tracked_free(left);
    tracked_free(right);
    return result;
}

value_t *visitor_visit_unary_expr(InterpreterContext *ctx, ast_t *node)
{
  return NULL;
}

value_t *visitor_visit_variable_definition(InterpreterContext *ctx, ast_t *node)
{
  char *variable_name = tracked_strdup(node->value.variable_definition.variable_name);
  value_t *value = visitor_visit(ctx, node->value.variable_definition.value);
  if (value == NULL)
  {
    ti_log("[ERROR]: Variable definition '%s' evaluated to NULL\n", variable_name);
    ti_fatal();
  }
  add_variable_to_context(ctx, variable_name, value);
  return NULL;
}

value_t *visitor_visit_assignment(InterpreterContext *ctx, ast_t *node)
{
  ast_t *id_node = node->value.assignment.id;
  ast_t *value_node = node->value.assignment.value;
  variable_t *variable = find_variable_from_context(ctx, id_node->value.identifier);
  if (variable != NULL)
  {
    value_t *val = visitor_visit(ctx, value_node); 
    if (val == NULL)
    {
      ti_log("[ERROR]: Assignment expression for '%s' evaluated to NULL\n", id_node->value.identifier);
      ti_fatal();
    }
    if (variable->value != NULL && variable->value->type != val->type)
    {
      ti_log("[ERROR]: Type mismatch in assignment to '%s'. Expected %d, but got %d\n",
          id_node->value.identifier,
          variable->value->type,
          val->type);
      ti_fatal();
    }
    if (variable->value != NULL)
    {
      free_internal_value(variable->value);
      tracked_free(variable->value);
    }
    variable->value = val;
  }
  else
  {
    ti_log("[ERROR]: Undefined variable %s\n", id_node->value.identifier);
    ti_fatal();
  }
  return NULL;
}

value_t *visitor_visit_while_statement(InterpreterContext *ctx, ast_t *node)
{
  while(eval_boolean_condition(ctx, node->value.while_statement.condition))
  {
      visitor_execute_body(ctx, node->value.while_statement.body);
  }
  return NULL;
}

value_t *visitor_visit_if_statement(InterpreterContext *ctx, ast_t *node)
{
    if(eval_boolean_condition(ctx, node->value.if_statement.condition))
    {
        visitor_execute_body(ctx, node->value.if_statement.body);
    }
    else
    {
        visitor_execute_body(ctx, node->value.if_statement.else_body);
    }
    return NULL;
}

value_t *visitor_visit_for_statement(InterpreterContext *ctx, ast_t *node)
{
  return NULL;
}

value_t *visitor_visit_function_call(InterpreterContext *ctx, ast_t *node)
{
  value_t *ret = NULL;
  int argc = node->value.function_call.num_arg;
  value_t **argv = tracked_calloc(argc , sizeof(struct VALUE_STRUCT*));
  for (int i = 0; i < argc; i ++)
  {
    value_t *value = visitor_visit(ctx,node->value.function_call.args[i]);
    if (value == NULL)
    {
      ti_log("[ERROR]: Argument %d in call to '%s' evaluated to NULL\n", i, node->value.function_call.func);
      ti_fatal();
    }
    argv[i] = value;
  }
  bool found = false;
  for (int i = 0; i < g_builtin_count; i++){
    if(strcmp(node->value.function_call.func, g_builtins[i].name) == 0)
    {
      ret = g_builtins[i].fn(argv,argc);  
      found = true;
      break;
    }
  }
  if (!found)
  {
    ti_log("[ERROR]: Call to undefined function '%s'\n", node->value.function_call.func);
    ti_fatal();
  }
  for (int i = 0; i < argc; i++){
    if (argv[i] != NULL) {
      free_internal_value(argv[i]);
      tracked_free(argv[i]);
    }
  }
  tracked_free(argv);
  return ret;
}

value_t *visitor_visit_string_literal(InterpreterContext *ctx, ast_t *node)
{
  value_t *value = init_val(VAL_STRING);
  value->string_val = tracked_strdup(node->value.string_value);
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
  (void)ctx;
  value_t *value = init_val(VAL_FLOAT);
  value->float_val = node->value.float_value;
  return value;
}

value_t *visitor_visit_boolean(InterpreterContext *ctx, ast_t *node)
{
  (void)ctx;
  value_t *value = init_val(VAL_BOOL);
  value->bool_val = node->value.bool_value;
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
      free_internal_value(value);
      tracked_free(value);
    }
  }
  return NULL; 
}

value_t *visitor_visit_identifier(InterpreterContext *ctx, ast_t *node)
{
  variable_t *variable = find_variable_from_context(ctx, node->value.identifier);
  if (variable != NULL)
  {
    if (variable->value == NULL)
    {
      ti_log("[ERROR]: Variable '%s' has NULL value\n", node->value.identifier);
      ti_fatal();
    }
    value_t *value = copy_value_from_variable(variable);
    return value;
  }
  ti_log("[ERROR]: Undefined variable: %s\n", node->value.identifier);
  ti_fatal();
  return NULL;
}

bool register_builtin_function(const char *name, native_fn_t function) 
{
  for (int i = 0; i < g_builtin_count; i ++)
  {
    if(strcmp(name,g_builtins[i].name) == 0)
    {
      ti_log("Function name already exists\n");
      return false;
    }
  }
  builtin_func_t *temp = realloc(g_builtins, sizeof(builtin_func_t) * (g_builtin_count + 1));
  if(temp == NULL)
  {
    ti_log("Memory issue\n");
    return false;
  }
  g_builtins = temp;
  g_builtins[g_builtin_count].name = name;
  g_builtins[g_builtin_count].fn = function;
  g_builtin_count++;
  return true;
}
