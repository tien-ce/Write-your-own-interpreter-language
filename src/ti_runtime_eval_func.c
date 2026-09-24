#include "include/ti_runtime_context.h"
#include "include/ti_type.h"
#include "include/ti_type_value.h"
#include "include/ti_runtime_visitor.h"
#include "include/tracked_memory.h"
#include "include/debug.h"
#include "TienInterpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "chashmap.h"

/* -------------------- Function Registry Table -------------------- */

/* This is shared between multiple threads and registered at interpreter startup */
static chashmap_t *s_builtin_map = NULL;

/**
 * @brief Find a built-in native function by name.
 * @param name Function identifier name in Ti scripts.
 * @return Pointer to function_t if found, NULL otherwise.
 */
static function_t *builtin_find_function(const char *name)
{
    if (s_builtin_map == NULL) {
        return NULL;
    }
    return (function_t *)chashmap_get(s_builtin_map, name);
}

/**
 * @brief Find a user script-defined function by name in the runtime table.
 * @param rt Pointer to active runtime instance.
 * @param name Function identifier name in Ti scripts.
 * @return Pointer to function_t if found, NULL otherwise.
 */
static function_t *user_find_function(ti_runtime_t *rt, const char *name)
{
    if (!rt || !rt->user_functions) {
        return NULL;
    }
    for (int i = 0; i < rt->user_function_count; i++) {
        if (strcmp(name, rt->user_functions[i].name) == 0) {
            return &rt->user_functions[i];
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
    /* Lazy initialize the hashmap if it doesn't exist */
    if (s_builtin_map == NULL) {
        s_builtin_map = chashmap_create(32, NULL); 
    }

    /* O(1) Check for existing function */
    if (chashmap_get(s_builtin_map, name) != NULL) {
        ti_log("Function name already exists\n");
        return false;
    }

    /* Allocate an independent function_t object on the heap */
    function_t *func = tracked_calloc(NULL, 1, sizeof(function_t));
    if (func == NULL) {
        ti_log("Memory issue\n");
        return false;
    }

    func->name = name;
    func->type = FUNC_BUILTIN;
    func->return_type = return_type;
    func->params = params;
    func->param_count = param_count;
    func->native_fn = function;

    /* Insert the function pointer into the hashmap */
    chashmap_set(s_builtin_map, name, (void *)func);
    return true;
}

/* -------------------- Function Evaluators -------------------- */

/**
 * @brief Execute a user-defined Ti function.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context scope.
 * @param func Pointer to target function structure.
 * @param argv Array of evaluated argument values.
 * @param argc Number of arguments passed.
 * @param node Call AST node.
 * @return Evaluated return value_t (or NULL).
 */
value_t *run_ti_function(ti_runtime_t *rt, context_t *ctx, function_t *func, value_t **argv, int argc, ast_t *node)
{
    (void)ctx; // Function only connects with runtime global context (not from caller)

    /* Guard against maximum recursion depth to prevent stack overflow */
    if (rt->call_depth >= rt->max_call_depth) {
        ti_log("[Runtime Error] Maximum recursion depth exceeded\n");
        ti_fatal();
        return NULL;
    }

    rt->call_depth++;

    /* Create isolated context for function call linked to global context */
    context_t *func_ctx = context_init(&rt->alloc_list);
    func_ctx->parent = rt->global_context;

    /* Deep copy evaluated arguments and bind them to parameter variables in local context */
    for (int i = 0; i < argc; i++) {
        value_t *param_val = val_copy(argv[i]);
        context_add_variable(&rt->alloc_list, func_ctx, tracked_strdup(&rt->alloc_list, func->params[i].name), param_val);
    }

    /* Execute the function body compound block */
    ast_t *body = func->def->value.function_definition.body;
    visitor_visit(rt, func_ctx, body);

    /* Trap and report unhandled loop control signals escaping function body */
    if (func_ctx->flow_state == FLOW_BREAK) {
        ti_log("[Runtime Error] 'break' statement not within a loop inside function '%s' at line %d\n", func->name, node->line);
        ti_fatal();
    } else if (func_ctx->flow_state == FLOW_CONTINUE) {
        ti_log("[Runtime Error] 'continue' statement not within a loop inside function '%s' at line %d\n", func->name, node->line);
        ti_fatal();
    }

    /* Harvest return value from context if a return statement was executed */
    value_t *ret_val = NULL;
    if (func_ctx->flow_state == FLOW_RETURN) {
        ret_val = func_ctx->return_value;
        func_ctx->return_value = NULL; // Hand over ownership of return value
        func_ctx->flow_state = FLOW_NORMAL; // Consume the flow flag
    } else {
        /* Handle implicit return when function body completes without return statement */
        if (func->return_type != VAL_VOID) {
            ti_log("[Runtime Error] Non-void function '%s' reached end of body without returning a value at line %d\n", func->name, node->line);
            ti_fatal();
        } else {
            ret_val = val_new_void();
        }
    }

    /* Verify return value type matches declared function return type */
    if (ret_val != NULL && ret_val->type != func->return_type) {
        ti_log("[Runtime Error] Function '%s' declared to return %s, but returned %s at line %d\n",
               func->name,
               val_type_to_str(func->return_type),
               ret_val ? val_type_to_str(ret_val->type) : "null",
               node->line);
        ti_fatal();
    }

    context_free(&rt->alloc_list, func_ctx); // Reclaim stack frame of function
    rt->call_depth--;
    return ret_val;
}

/**
 * @brief Evaluate a function definition node and register it into the runtime function table.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to context.
 * @param node Function definition AST node.
 * @return Always NULL.
 */
value_t *eval_function_definition(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    (void)ctx;
    const char *name = node->value.function_definition.func_name;

    /* Prevent duplicate function registration across built-in and user-defined functions */
    if (builtin_find_function(name) != NULL || user_find_function(rt, name) != NULL) {
        ti_log("[Runtime Error] Redefinition of function '%s' at line %d\n", name, node->line);
        ti_fatal();
        return NULL;
    }

    /* Dynamically expand the runtime user function table */
    function_t *temp = tracked_realloc(&rt->alloc_list, rt->user_functions, sizeof(function_t) * (rt->user_function_count + 1));
    if (temp == NULL) {
        ti_log("[Runtime Error] Memory issue at line %d\n", node->line);
        ti_fatal();
        return NULL;
    }
    rt->user_functions = temp;

    /* Copy and track parameter metadata (types and names) */
    int param_count = node->value.function_definition.param_count;
    param_t *params = NULL;
    if (param_count > 0) {
        params = tracked_calloc(&rt->alloc_list, param_count, sizeof(param_t));
        for (int i = 0; i < param_count; i++) {
            ast_t *param_node = node->value.function_definition.params[i];
            params[i].type = param_node->value.param.param_type;
            params[i].name = tracked_strdup(&rt->alloc_list, param_node->value.param.param_name);
        }
    }

    /* Register function entry into the runtime table */
    rt->user_functions[rt->user_function_count].name = name;
    rt->user_functions[rt->user_function_count].type = FUNC_TI;
    rt->user_functions[rt->user_function_count].return_type = node->value.function_definition.return_type;
    rt->user_functions[rt->user_function_count].params = params;
    rt->user_functions[rt->user_function_count].param_count = param_count;
    rt->user_functions[rt->user_function_count].def = node;
    rt->user_function_count++;
    return NULL;
}

/**
 * @brief Dispatch and execute a function call with parameter count and type validation.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context scope.
 * @param func Pointer to target function structure.
 * @param argv Array of evaluated argument values.
 * @param argc Number of arguments passed.
 * @param node Function call AST node.
 * @return Evaluated return value_t (or NULL).
 */
value_t *run_function(ti_runtime_t *rt, context_t *ctx, function_t *func, value_t **argv, int argc, ast_t *node)
{
    /* Check argument count (if not variadic) */
    if (func->param_count >= 0 && argc != func->param_count) {
        ti_log("[Runtime Error] Function '%s' expects %d argument(s), but received %d at line %d\n",
               func->name, func->param_count, argc, node->line);
        ti_fatal();
        return NULL;
    }

    /* Check argument types against declared parameter types */
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

    /* Dispatch to implementation */
    if (func->type == FUNC_BUILTIN) {
        return func->native_fn(argv, argc);
    } else if (func->type == FUNC_TI) {
        return run_ti_function(rt, ctx, func, argv, argc, node);
    }

    return NULL;
}

/**
 * @brief Evaluate a function call node (native builtin or Ti function).
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to context.
 * @param node Function call AST node.
 * @return Returned value_t from function callback or execution.
 */
value_t *eval_function_call(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    const char *func_name = node->value.function_call.func_name;
    int argc = node->value.function_call.arg_count;
    value_t **argv = tracked_calloc(&rt->alloc_list, argc, sizeof(struct VALUE_STRUCT *));

    /* Evaluate all argument expressions before invocation */
    for (int i = 0; i < argc; i++) {
        value_t *value = visitor_visit(rt, ctx, node->value.function_call.args[i]);
        
        if (rt != NULL && rt->is_interrupted) {
            if (value) val_free(value);
            for (int j = 0; j < i; j++) {
                if (argv[j]) val_free(argv[j]);
            }
            tracked_free(&rt->alloc_list, argv);
            return NULL;
        }

        if (value == NULL) {
            ti_log("[Runtime Error] Argument %d in call to '%s' evaluated to NULL at line %d\n", i, func_name, node->line);
            ti_fatal();
        }
        argv[i] = value;
    }

    /* Look up function: user-defined script functions take precedence over built-ins */
    function_t *func = user_find_function(rt, func_name);
    if (func == NULL) {
        func = builtin_find_function(func_name);
    }

    /* Handle call to undefined function */
    if (func == NULL) {
        ti_log("[Runtime Error] Call to undefined function '%s' at line %d\n", func_name, node->line);
        ti_fatal();
        for (int i = 0; i < argc; i++) {
            if (argv[i] != NULL) {
                val_free(argv[i]);
            }
        }
        tracked_free(&rt->alloc_list, argv);
        return NULL;
    }

    /* Execute the resolved function */
    value_t *ret = run_function(rt, ctx, func, argv, argc, node);

    /* Free intermediate argument values and argument array */
    for (int i = 0; i < argc; i++) {
        if (argv[i] != NULL) {
            val_free(argv[i]);
        }
    }
    tracked_free(&rt->alloc_list, argv);
    return ret;
}
