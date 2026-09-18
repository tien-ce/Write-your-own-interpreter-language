#include "include/context.h"
#include "include/ti_type.h"
#include "include/value.h"
#include "include/visitor_internal.h"
#include "include/tracked_memory.h"
#include "include/debug.h"
#include "TienInterpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* -------------------- Function Registry Table -------------------- */

static function_t *s_bultin_functions = NULL;
static int s_bultin_function_count = 0;

/**
 * @brief Find a built-in native function by name.
 * @param name Function identifier name in Ti scripts.
 * @return Pointer to function_t if found, NULL otherwise.
 */
static function_t *builtin_find_function(const char *name)
{
    for (int i = 0; i < s_bultin_function_count; i++) {
        if (strcmp(name, s_bultin_functions[i].name) == 0) {
            return &s_bultin_functions[i];
        }
    }
    return NULL;
}

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
for (int i = 0; i < s_bultin_function_count; i++) {
    if (strcmp(name, s_bultin_functions[i].name) == 0) {
        ti_log("Function name already exists\n");
        return false;
    }
    }
    function_t *temp = tracked_realloc(s_bultin_functions, sizeof(function_t) * (s_bultin_function_count + 1));
    if (temp == NULL) {
        ti_log("Memory issue\n");
        return false;
    }
    s_bultin_functions = temp;
    s_bultin_functions[s_bultin_function_count].name = name;
    s_bultin_functions[s_bultin_function_count].type = FUNC_BUILTIN;
    s_bultin_functions[s_bultin_function_count].return_type = return_type;
    s_bultin_functions[s_bultin_function_count].params = params;
    s_bultin_functions[s_bultin_function_count].param_count = param_count;
    s_bultin_functions[s_bultin_function_count].native_fn = function;
    s_bultin_function_count++;
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
value_t *run_ti_function(context_t *ctx, function_t *func, value_t **argv, int argc, ast_t *node)
{
    /* Create new context for function */
    (void)ctx; //Function only connect with global context (not from caller)

    /* Get gloabl context and create new stack frame*/ 
    context_t *gloabl_ctx = visitor_get_global_context();
    context_t *func_ctx = context_init();
    func_ctx->parent = gloabl_ctx;

    /* Copy the argument value and create variables for current context*/ 
    // Checking match between param count and param types should be done before call this function */
    for (int i = 0; i < argc; i ++)
    {
        value_t *param_val = val_copy(argv[i]);
        context_add_variable(ctx, tracked_strdup(func->params[i].name), param_val);
    }
    /* Execute the body function */
    ast_t *body = func->def->value.function_definition.body;
    visitor_visit(func_ctx, body);

    /* Capture the trap signal */ 
    if (func_ctx->flow_state == FLOW_BREAK) {
        ti_log("[Runtime Error] 'break' statement not within a loop inside function '%s' at line %d\n", func->name, node->line);
        ti_fatal();
    } else if (func_ctx->flow_state == FLOW_CONTINUE) {
        ti_log("[Runtime Error] 'continue' statement not within a loop inside function '%s' at line %d\n", func->name, node->line);
        ti_fatal();
    }

    /* Havest (get) the return value */ 
    value_t *ret_val = NULL;
    if (func_ctx->flow_state == FLOW_RETURN)
    {
        ret_val = func_ctx->return_value;
        func_ctx->return_value = NULL; // Hand over the onwer of return value to avoid context freeing
        func_ctx->flow_state = FLOW_NORMAL; // Consume the follow flag
    }
    else {
        /* The function end but not meet return */
        if (func->return_type != VAL_VOID)
        {
            ti_log("[Runtime Error] Non-void function '%s' reached end of body without returning a value at line %d\n", func->name, node->line);
            ti_fatal();
        }
        else {
            ret_val = val_new_void();
        }
    }

    /* Checking the return value and expected return value*/
    if (ret_val != NULL && ret_val->type != func->return_type)
    {
        ti_log("[Runtime Error] Function '%s' declared to return %s, but returned %s at line %d\n",
               func->name,
               val_type_to_str(func->return_type),
               ret_val ? val_type_to_str(ret_val->type) : "null",
               node->line);
        ti_fatal();
    }
    context_free(func_ctx); // Reclaim stack frame of funciton
    return ret_val;
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
    context_t *global_ctx = visitor_get_global_context();
    if (builtin_find_function(name) != NULL || context_find_function(global_ctx, name) != NULL) {
            ti_log("[Runtime Error] Redefinition of function '%s' at line %d\n", name, node->line);
            ti_fatal();
            return NULL;
    }
    function_t *temp = tracked_realloc(s_bultin_functions, sizeof(function_t) * (s_bultin_function_count + 1));
    if (temp == NULL) {
        ti_log("[Runtime Error] Memory issue at line %d\n", node->line);
        ti_fatal();
        return NULL;
    }
    s_bultin_functions = temp;

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

    s_bultin_functions[s_bultin_function_count].name = name;
    s_bultin_functions[s_bultin_function_count].type = FUNC_TI;
    s_bultin_functions[s_bultin_function_count].return_type = node->value.function_definition.return_type;
    s_bultin_functions[s_bultin_function_count].params = params;
    s_bultin_functions[s_bultin_function_count].param_count = param_count;
    s_bultin_functions[s_bultin_function_count].def = node;
    s_bultin_function_count++;
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
value_t *run_function(context_t *ctx, function_t *func, value_t **argv, int argc, ast_t *node)
{
    /* 1. Check argument count (if not variadic) */
    if (func->param_count >= 0 && argc != func->param_count) {
        ti_log("[Runtime Error] Function '%s' expects %d argument(s), but received %d at line %d\n",
               func->name, func->param_count, argc, node->line);
        ti_fatal();
        return NULL;
    }

    /* 2. Check argument types against declared parameter types */
    for (int i = 0; i < func->param_count; i++) {
        if (argv[i]->type != func->params[i].type) {
            ti_log("[Runtime Error] Function '%s' parameter %d ('%s') expected type %s, but got %s at line %d\n",
                   func->name, i + 1,
                   func->params[i].name ? func->params[i].name : "unnamed",
                   val_type_to_str(func->params[i].type),
                   val_type_to_str(argv[i]->type),
                   node->line);
            ti_fatal();
            return NULL;
        }
    }

    /* 3. Dispatch to implementation */
    if (func->type == FUNC_BUILTIN) {
        return func->native_fn(argv, argc);
    } else if (func->type == FUNC_TI) {
        return run_ti_function(ctx, func, argv, argc, node);
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
            ti_log("[Runtime Error] Argument %d in call to '%s' evaluated to NULL at line %d\n", i, node->value.function_call.func_name, node->line);
            ti_fatal();
        }
        argv[i] = value;
    }

    bool found = false;
    for (int i = 0; i < s_bultin_function_count; i++) {
        if (strcmp(node->value.function_call.func_name, s_bultin_functions[i].name) == 0) {
            found = true;
            ret = run_function(ctx, &s_bultin_functions[i], argv, argc, node);
            break;
        }
    }

    if (!found) {
        ti_log("[Runtime Error] Call to undefined function '%s' at line %d\n", node->value.function_call.func_name, node->line);
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
