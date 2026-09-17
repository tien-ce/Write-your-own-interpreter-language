#include "include/ti_type.h"
#include "include/visitor_internal.h"
#include "include/tracked_memory.h"
#include "include/debug.h"
#include "TienInterpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* -------------------- Function Registry Table -------------------- */

static function_t *s_functions = NULL;
static int s_function_count = 0;

/**
 * @brief Register a native C function into the interpreter global functions table.
 * @param name Function name in Ti scripts.
 * @param return_type Declared return value type.
 * @param params Array of parameter metadata (or NULL).
 * @param param_count Number of parameters (-1 for variadic).
 * @param function Native C callback function.
 * @return true on success, false if name exists or out of memory.
 */
bool register_builtin_function(const char *name, val_type_t return_type, param_t *params, int param_count, native_fn_t function) 
{
    for (int i = 0; i < s_function_count; i++) {
        if (strcmp(name, s_functions[i].name) == 0) {
            ti_log("Function name already exists\n");
            return false;
        }
    }
    function_t *temp = tracked_realloc(s_functions, sizeof(function_t) * (s_function_count + 1));
    if (temp == NULL) {
        ti_log("Memory issue\n");
        return false;
    }
    s_functions = temp;
    s_functions[s_function_count].name = name;
    s_functions[s_function_count].type = FUNC_BUILTIN;
    s_functions[s_function_count].return_type = return_type;
    s_functions[s_function_count].params = params;
    s_functions[s_function_count].param_count = param_count;
    s_functions[s_function_count].native_fn = function;
    s_function_count++;
    return true;
}

/* -------------------- Function Evaluators -------------------- */

/**
 * @brief Execute a user-defined Ti function.
 * @param ctx Pointer to active execution context scope.
 * @param func Pointer to target function structure.
 * @param argv Array of evaluated argument values.
 * @param argc Number of arguments passed.
 * @return Evaluated return value_t (or NULL).
 */
value_t *run_ti_function(context_t *ctx, function_t *func, value_t **argv, int argc)
{
  /* Create new context for function */
  return NULL;
}

/**
 * @brief Evaluate a function definition node and register it into the function table.
 * @param ctx Pointer to context.
 * @param node Function definition AST node.
 * @return Always NULL.
 */
value_t *eval_function_definition(context_t *ctx, ast_t *node)
{
    (void)ctx;
    const char *name = node->value.function_definition.func_name;
    for (int i = 0; i < s_function_count; i++) {
        if (strcmp(name, s_functions[i].name) == 0) {
            ti_log("[ERROR]: Redefinition of function '%s'\n", name);
            ti_fatal();
            return NULL;
        }
    }
    function_t *temp = tracked_realloc(s_functions, sizeof(function_t) * (s_function_count + 1));
    if (temp == NULL) {
        ti_log("Memory issue\n");
        ti_fatal();
        return NULL;
    }
    s_functions = temp;

    int param_count = node->value.function_definition.param_count;
    param_t *params = NULL;
    if (param_count > 0) {
        params = tracked_calloc(param_count, sizeof(param_t));
        for (int i = 0; i < param_count; i++) {
            ast_t *param_node = node->value.function_definition.params[i];
            params[i].type = param_node->value.param.param_type;
            params[i].name = tracked_strdup(param_node->value.param.param_name);
        }
    }

    s_functions[s_function_count].name = name;
    s_functions[s_function_count].type = FUNC_TI;
    s_functions[s_function_count].return_type = node->value.function_definition.return_type;
    s_functions[s_function_count].params = params;
    s_functions[s_function_count].param_count = param_count;
    s_functions[s_function_count].def = node;
    s_function_count++;
    return NULL;
}

/**
 * @brief Dispatch and execute a function call with parameter count and type validation.
 * @param ctx Pointer to active execution context scope.
 * @param func Pointer to target function structure.
 * @param argv Array of evaluated argument values.
 * @param argc Number of arguments passed.
 * @return Evaluated return value_t (or NULL).
 */
value_t *run_function(context_t *ctx, function_t *func, value_t **argv, int argc)
{
    /* 1. Check argument count (if not variadic) */
    if (func->param_count >= 0 && argc != func->param_count) {
        ti_log("[ERROR]: Function '%s' expects %d argument(s), but received %d\n",
               func->name, func->param_count, argc);
        ti_fatal();
        return NULL;
    }

    /* 2. Check argument types against declared parameter types */
    for (int i = 0; i < func->param_count; i++) {
        if (argv[i]->type != func->params[i].type) {
            ti_log("[ERROR]: Function '%s' parameter %d ('%s') expected type %s, but got %s\n",
                   func->name, i + 1,
                   func->params[i].name ? func->params[i].name : "unnamed",
                   val_type_to_str(func->params[i].type),
                   val_type_to_str(argv[i]->type));
            ti_fatal();
            return NULL;
        }
    }

    /* 3. Dispatch to implementation */
    if (func->type == FUNC_BUILTIN) {
        return func->native_fn(argv, argc);
    } else if (func->type == FUNC_TI) {
        return run_ti_function(ctx, func, argv, argc);
    }

    return NULL;
}

/**
 * @brief Evaluate a function call node (native builtin or Ti function).
 * @param ctx Pointer to context.
 * @param node Function call AST node.
 * @return Returned value_t from function callback or execution.
 */
value_t *eval_function_call(context_t *ctx, ast_t *node)
{
    value_t *ret = NULL;
    int argc = node->value.function_call.arg_count;
    value_t **argv = tracked_calloc(argc, sizeof(struct VALUE_STRUCT *));

    for (int i = 0; i < argc; i++) {
        value_t *value = visitor_visit(ctx, node->value.function_call.args[i]);
        if (value == NULL) {
            ti_log("[ERROR]: Argument %d in call to '%s' evaluated to NULL\n", i, node->value.function_call.func_name);
            ti_fatal();
        }
        argv[i] = value;
    }

    bool found = false;
    for (int i = 0; i < s_function_count; i++) {
        if (strcmp(node->value.function_call.func_name, s_functions[i].name) == 0) {
            found = true;
            ret = run_function(ctx, &s_functions[i], argv, argc);
            break;
        }
    }

    if (!found) {
        ti_log("[ERROR]: Call to undefined function '%s'\n", node->value.function_call.func_name);
        ti_fatal();
    }

    /* Free argument*/
    for (int i = 0; i < argc; i++) {
        if (argv[i] != NULL) {
            val_free_internal(argv[i]);
            tracked_free(argv[i]);
        }
    }
    tracked_free(argv);
    return ret;
}
