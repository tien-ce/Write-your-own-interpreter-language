#include "include/ti_runtime_context.h"
#include "include/tracked_memory.h"
#include "TienInterpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------- Context & Variable Operations -------------------- */

/* Initialize a new interpreter context scope */
context_t *context_init(alloc_hdr_t **list)
{
    context_t *context = tracked_calloc(list, 1, sizeof(struct CONTEXT_STRUCT));
    if (!context) {
        return NULL;
    }
    context->variables = NULL;
    context->variable_count = 0;
    context->parent = NULL;
    context->flow_state = FLOW_NORMAL;
    context->return_value = NULL;
    context->alloc_list = list;
    return context;
}

/* Free an interpreter context and its scoped variables */
void context_free(alloc_hdr_t **list, context_t *ctx)
{
    if (!ctx) {
        return;
    }
    context_free_internal(list, ctx);
    tracked_free(list, ctx);
}

/* Allocate a new variable_t with the given variable name */
variable_t *variable_init(alloc_hdr_t **list, const char *variable_name)
{
    variable_t *variable = tracked_calloc(list, 1, sizeof(struct VARIABLE_STRUCT));
    if (!variable) {
        return NULL;
    }
    variable->name = variable_name;
    variable->value = NULL;
    return variable;
}

/* Find a variable by name walking up from the current context to root parent */
variable_t *context_find_variable(context_t *ctx, const char *variable_name)
{
    context_t *current_ctx = ctx;
    while (current_ctx != NULL) {
        for (int i = 0; i < current_ctx->variable_count; i++) {
            if (strcmp(current_ctx->variables[i]->name, variable_name) == 0) {
                return current_ctx->variables[i];
            }
        }
        current_ctx = current_ctx->parent;
    }
    return NULL;
}

/* Create a deep copy of a variable's value_t */
value_t *context_copy_value(alloc_hdr_t **list, variable_t *variable)
{
    (void)list;
    if (variable == NULL || variable->value == NULL) {
        ti_log("[ERROR]: Attempted to access NULL variable\n");
        ti_fatal();
        return NULL;
    }

    return val_copy(variable->value);
}

/* Add a newly defined variable to the given context scope */
void context_add_variable(alloc_hdr_t **list, context_t *ctx, const char *name, value_t *value)
{
    if (!ctx) {
        return;
    }
    int size = ctx->variable_count;
    for (int i = 0; i < size; i++) {
        if (strcmp(ctx->variables[i]->name, name) == 0) {
            ti_log("[ERROR]: Redefinition of variable '%s'\n", name);
            ti_fatal();
        }
    }

    variable_t *variable = variable_init(list, name);
    variable->value = value;
    if (ctx->variables == NULL) {
        ctx->variables = tracked_calloc(list, 1, sizeof(struct VARIABLE_STRUCT *));
        ctx->variables[0] = variable;
        ctx->variable_count = 1;
    } else {
        ctx->variables = tracked_realloc(list, ctx->variables, (ctx->variable_count + 1) * sizeof(struct VARIABLE_STRUCT *));
        ctx->variables[ctx->variable_count] = variable;
        ctx->variable_count += 1;
    }
}

/* Free a variable structure, its name string, and its value payload */
void variable_free(alloc_hdr_t **list, variable_t *var)
{
    if (var == NULL) {
        return;
    }
    if (var->value != NULL) {
        val_free(var->value);
        var->value = NULL;
    }
    if (var->name != NULL) {
        tracked_free(list, (void *)var->name);
        var->name = NULL;
    }
    tracked_free(list, var);
}

/* Free dynamically allocated parameter array and name strings */
void params_free(alloc_hdr_t **list, param_t *params, int param_count)
{
    if (params == NULL) {
        return;
    }
    for (int i = 0; i < param_count; i++) {
        if (params[i].name != NULL) {
            tracked_free(list, params[i].name);
            params[i].name = NULL;
        }
    }
    tracked_free(list, params);
}

/* Free an entire function_t structure and its owned parameters */
void function_free(alloc_hdr_t **list, function_t *func)
{
    if (func == NULL) {
        return;
    }
    if (func->type == FUNC_TI && func->params != NULL) {
        params_free(list, func->params, func->param_count);
        func->params = NULL;
    }
}

/* Free all variables and internal structures inside a context scope */
void context_free_internal(alloc_hdr_t **list, context_t *ctx)
{
    if (!ctx) {
        return;
    }

    /* Free variables */
    if (ctx->variables != NULL) {
        for (int i = 0; i < ctx->variable_count; i++) {
            variable_free(list, ctx->variables[i]);
        }
        tracked_free(list, ctx->variables);
        ctx->variables = NULL;
        ctx->variable_count = 0;
    }

    /* Free unconsumed return value */
    if (ctx->return_value != NULL) {
        val_free(ctx->return_value);
        ctx->return_value = NULL;
    }
}
