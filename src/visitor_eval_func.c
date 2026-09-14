#include "include/visitor_internal.h"
#include "include/tracked_memory.h"
#include "TienInterpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* -------------------- Built-in Registry Table -------------------- */

static builtin_func_t *s_builtins = NULL;
static int s_builtin_count = 0;

/**
 * @brief Register a native C function into the interpreter global built-ins table.
 * @param name Function name in Ti scripts.
 * @param function Native C callback function.
 * @return true on success, false if name exists or out of memory.
 */
bool register_builtin_function(const char *name, native_fn_t function) 
{
    for (int i = 0; i < s_builtin_count; i++) {
        if (strcmp(name, s_builtins[i].name) == 0) {
            ti_log("Function name already exists\n");
            return false;
        }
    }
    builtin_func_t *temp = realloc(s_builtins, sizeof(builtin_func_t) * (s_builtin_count + 1));
    if (temp == NULL) {
        ti_log("Memory issue\n");
        return false;
    }
    s_builtins = temp;
    s_builtins[s_builtin_count].name = name;
    s_builtins[s_builtin_count].fn = function;
    s_builtin_count++;
    return true;
}

/* -------------------- Function Evaluators -------------------- */

/**
 * @brief Evaluate a function definition node (stub).
 * @param ctx Pointer to context.
 * @param node Function definition AST node.
 * @return Always NULL.
 */
value_t *eval_function_definition(InterpreterContext *ctx, ast_t *node)
{
    (void)ctx;
    (void)node;
    return NULL;
}

/**
 * @brief Evaluate a function call node.
 * @param ctx Pointer to context.
 * @param node Function call AST node.
 * @return Returned value_t from native function callback.
 */
value_t *eval_function_call(InterpreterContext *ctx, ast_t *node)
{
    value_t *ret = NULL;
    int argc = node->value.function_call.num_arg;
    value_t **argv = tracked_calloc(argc, sizeof(struct VALUE_STRUCT *));

    for (int i = 0; i < argc; i++) {
        value_t *value = visitor_visit(ctx, node->value.function_call.args[i]);
        if (value == NULL) {
            ti_log("[ERROR]: Argument %d in call to '%s' evaluated to NULL\n", i, node->value.function_call.func);
            ti_fatal();
        }
        argv[i] = value;
    }

    bool found = false;
    for (int i = 0; i < s_builtin_count; i++) {
        if (strcmp(node->value.function_call.func, s_builtins[i].name) == 0) {
            ret = s_builtins[i].fn(argv, argc);  
            found = true;
            break;
        }
    }

    if (!found) {
        ti_log("[ERROR]: Call to undefined function '%s'\n", node->value.function_call.func);
        ti_fatal();
    }

    for (int i = 0; i < argc; i++) {
        if (argv[i] != NULL) {
            val_free_internal(argv[i]);
            tracked_free(argv[i]);
        }
    }
    tracked_free(argv);
    return ret;
}
