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
 * @brief Release the frame of a user function call that is being abandoned or has finished.
 * @param rt Pointer to active runtime instance.
 * @param func_ctx Context created for the call.
 */
static void function_frame_release(ti_runtime_t *rt, context_t *func_ctx)
{
    /* context_free also releases any return payload still held by the frame */
    context_free(&rt->alloc_list, func_ctx);
    rt->call_depth--;
}

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

    /* Safe point on entry: abort cancelled recursion and service native events */
    if (ti_runtime_safe_point(rt)) {
        return NULL;
    }

    /* Guard against maximum recursion depth to prevent stack overflow */
    if (rt->call_depth >= rt->max_call_depth) {
        ti_raise(rt, TI_ERR_LIMIT_EXCEEDED, node->line, "Maximum recursion depth exceeded (%d)", rt->max_call_depth);
        return NULL;
    }

    rt->call_depth++;

    context_t *func_ctx = NULL;
    value_t *ret_val = NULL;
    value_t *result = NULL;

    /* Create isolated context for function call linked to global context */
    func_ctx = context_init(&rt->alloc_list);
    func_ctx->parent = rt->global_context;

    /* Deep copy evaluated arguments and bind them to parameter variables in local context */
    for (int i = 0; i < argc; i++) {
        value_t *param_val = val_copy(argv[i]);
        char *param_name = tracked_strdup(&rt->alloc_list, func->params[i].name);
        ti_status_t status = (param_val != NULL && param_name != NULL)
                                 ? context_add_variable(&rt->alloc_list, func_ctx, param_name, param_val)
                                 : TI_ERR_NO_MEMORY;
        if (status != TI_OK) {
            ti_raise(rt, status, node->line, "Cannot bind parameter '%s' of function '%s': %s",
                     func->params[i].name, func->name,
                     status == TI_ERR_RUNTIME ? "duplicate parameter name" : ti_err_to_str(status));
            val_free(param_val);
            tracked_free(&rt->alloc_list, param_name);
            goto out;
        }
    }

    /* Execute the function body compound block */
    ast_t *body = func->def->value.function_definition.body;
    value_t *body_result = visitor_visit(rt, func_ctx, body);

    /* Safe point on exit: NULL means the body failed; also catch an error raised by a callback
     * in the last statement. Skip the return-value checks below. */
    if (body_result == NULL || ti_should_unwind(rt)) {
        goto out;
    }

    /* Trap and report unhandled loop control signals escaping function body */
    if (func_ctx->flow_state == FLOW_BREAK) {
        ti_raise(rt, TI_ERR_RUNTIME, node->line, "'break' statement not within a loop inside function '%s'", func->name);
        goto out;
    } else if (func_ctx->flow_state == FLOW_CONTINUE) {
        ti_raise(rt, TI_ERR_RUNTIME, node->line, "'continue' statement not within a loop inside function '%s'", func->name);
        goto out;
    }

    /* Harvest return value from context if a return statement was executed */
    if (func_ctx->flow_state == FLOW_RETURN) {
        ret_val = func_ctx->return_value;
        func_ctx->return_value = NULL; // Hand over ownership of return value
        func_ctx->flow_state = FLOW_NORMAL; // Consume the flow flag
    } else if (func->return_type != VAL_VOID) {
        /* Implicit return is only valid for void functions */
        ti_raise(rt, TI_ERR_RUNTIME, node->line, "Non-void function '%s' reached end of body without returning a value", func->name);
        goto out;
    } else {
        ret_val = val_new_void();
    }

    /* Verify return value type matches declared function return type */
    if (ret_val != NULL && ret_val->type == VAL_LIST && func->return_type == VAL_LIST &&
        func->return_element_type != VAL_NULL && ret_val->list_val->elem_type != func->return_element_type) {
        /* Same container type, different element type (list int vs list string) */
        ti_raise(rt, TI_ERR_TYPE_MISMATCH, node->line, "Function '%s' declared to return list %s, but returned list %s",
                 func->name, val_type_to_str(func->return_element_type), val_type_to_str(ret_val->list_val->elem_type));
        goto out;
    }
    if (ret_val != NULL && ret_val->type != func->return_type) {
        ti_raise(rt, TI_ERR_TYPE_MISMATCH, node->line, "Function '%s' declared to return %s, but returned %s",
                 func->name, val_type_to_str(func->return_type), val_type_to_str(ret_val->type));
        goto out;
    }

    /* Success: hand the return value to the caller */
    result = ret_val;
    ret_val = NULL;

out:
    /* Single cleanup point: unreturned value, then the call frame */
    val_free(ret_val);
    function_frame_release(rt, func_ctx);
    return result;
}

/**
 * @brief Evaluate a function definition node and register it into the runtime function table.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to context.
 * @param node Function definition AST node.
 * @return TI_VAL_OK on success, NULL on failure.
 */
value_t *eval_function_definition(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    (void)ctx;
    const char *name = node->value.function_definition.func_name;

    /* Prevent duplicate function registration across built-in and user-defined functions */
    if (builtin_find_function(name) != NULL || user_find_function(rt, name) != NULL) {
        ti_raise(rt, TI_ERR_RUNTIME, node->line, "Redefinition of function '%s'", name);
        return NULL;
    }

    /* Dynamically expand the runtime user function table */
    function_t *temp = tracked_realloc(&rt->alloc_list, rt->user_functions, sizeof(function_t) * (rt->user_function_count + 1));
    if (temp == NULL) {
        ti_raise(rt, TI_ERR_NO_MEMORY, node->line, "Cannot grow the function table");
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
            params[i].element_type = param_node->value.param.element_type;
            params[i].name = tracked_strdup(&rt->alloc_list, param_node->value.param.param_name);
        }
    }

    /* Register function entry into the runtime table */
    rt->user_functions[rt->user_function_count].name = name;
    rt->user_functions[rt->user_function_count].type = FUNC_TI;
    rt->user_functions[rt->user_function_count].return_type = node->value.function_definition.return_type;
    rt->user_functions[rt->user_function_count].return_element_type = node->value.function_definition.return_element_type;
    rt->user_functions[rt->user_function_count].params = params;
    rt->user_functions[rt->user_function_count].param_count = param_count;
    rt->user_functions[rt->user_function_count].def = node;
    rt->user_function_count++;
    return TI_VAL_OK;
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
        ti_raise(rt, TI_ERR_INVALID_ARG, node->line, "Function '%s' expects %d argument(s), but received %d",
                 func->name, func->param_count, argc);
        return NULL;
    }

    /* Check argument types against declared parameter types */
    for (int i = 0; i < func->param_count; i++) {
        if (argv[i]->type != func->params[i].type) {
            ti_raise(rt, TI_ERR_TYPE_MISMATCH, node->line, "Function '%s' parameter %d ('%s') expected type %s, but got %s",
                     func->name, i + 1,
                     func->params[i].name ? func->params[i].name : "unnamed",
                     val_type_to_str(func->params[i].type),
                     val_type_to_str(argv[i]->type));
            return NULL;
        }
        /* Same container type, different element type (list int vs list string) */
        if (argv[i]->type == VAL_LIST && func->params[i].element_type != VAL_NULL &&
            argv[i]->list_val->elem_type != func->params[i].element_type) {
            ti_raise(rt, TI_ERR_TYPE_MISMATCH, node->line, "Function '%s' parameter %d ('%s') expected list %s, but got list %s",
                     func->name, i + 1, func->params[i].name ? func->params[i].name : "unnamed",
                     val_type_to_str(func->params[i].element_type), val_type_to_str(argv[i]->list_val->elem_type));
            return NULL;
        }
    }

    /* Dispatch to implementation (natives only ever see the handle, never the runtime pointer) */
    if (func->type == FUNC_BUILTIN) {
        return func->native_fn(rt->handle, argv, argc);
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
    value_t *result = NULL;
    function_t *func = NULL;

    value_t **argv = tracked_calloc(&rt->alloc_list, argc, sizeof(struct VALUE_STRUCT *));
    if (argv == NULL) {
        ti_raise(rt, TI_ERR_NO_MEMORY, node->line, "Cannot allocate arguments for call to '%s'", func_name);
        return NULL;
    }

    /* Evaluate all argument expressions before invocation; NULL means the argument already failed */
    for (int i = 0; i < argc; i++) {
        argv[i] = visitor_visit(rt, ctx, node->value.function_call.args[i]);
        if (argv[i] == NULL) {
            goto out;
        }
    }

    /* Look up function: user-defined script functions take precedence over built-ins */
    func = user_find_function(rt, func_name);
    if (func == NULL) {
        func = builtin_find_function(func_name);
    }

    /* Handle call to undefined function */
    if (func == NULL) {
        ti_raise(rt, TI_ERR_UNDEFINED, node->line, "Call to undefined function '%s'", func_name);
        goto out;
    }

    /* Execute the resolved function */
    result = run_function(rt, ctx, func, argv, argc, node);

out:
    /* Single cleanup point: free evaluated arguments (unset slots are NULL) and the array */
    for (int i = 0; i < argc; i++) {
        val_free(argv[i]);
    }
    tracked_free(&rt->alloc_list, argv);
    return result;
}

/* -------------------- Event Dispatcher -------------------- */

/* Execute up to TI_MAX_PENDING_EVENTS queued tasks on their TI callback functions */
void ti_runtime_dispatch_pending_events(ti_runtime_t *rt)
{
    /* Callbacks run to completion: never start a nested dispatch from inside one */
    if (rt == NULL || rt->in_dispatch) {
        return;
    }
    rt->in_dispatch = true;

    /* Bound one pass to the queue capacity so a sustained event flood cannot starve the script */
    for (int budget = TI_MAX_PENDING_EVENTS; budget > 0 && !ti_should_unwind(rt); budget--) {
        ti_task_t *task = ti_runtime_pop_task(rt);
        if (task == NULL) {
            break;
        }

        /* Event callbacks must be script-defined functions */
        function_t *func = user_find_function(rt, task->func_name);
        if (func == NULL) {
            ti_log("[Event Error] Callback function '%s' is not defined, event dropped\n", task->func_name);
            ti_task_free(task);
            continue;
        }

        /* Keep the task reachable so ti_runtime_destroy reclaims it if a fatal error unwinds the call */
        rt->active_task = task;

        /*
         * run_function validates argument count and types; run_ti_function executes the body in a
         * fresh context parented to global_context and consumes FLOW_RETURN. The definition node
         * stands in for the missing call node in error reports.
         */
        value_t *ret_val = run_function(rt, rt->global_context, func, task->args, task->arg_count, func->def);
        if (ret_val != NULL) {
            val_free(ret_val);
        }

        /* Arguments were deep-copied into the callback scope, so the task can be released */
        rt->active_task = NULL;
        ti_task_free(task);
    }

    rt->in_dispatch = false;
}
