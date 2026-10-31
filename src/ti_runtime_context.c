#include "include/ti_runtime_context.h"
#include "include/tracked_memory.h"
#include "TienInterpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------- Constants -------------------- */

#define CONTEXT_GLOBAL_MAP_CAPACITY 16 /* Initial bucket count of the global variable map */

/* -------------------- Static Functions -------------------- */

/**
 * @brief chashmap payload destructor for the global variable map.
 * @param payload variable_t owned by the map.
 * @param user_data Allocation list (alloc_hdr_t **) the variable was tracked on.
 */
static void context_variable_free_cb(void *payload, void *user_data)
{
    variable_free((alloc_hdr_t **)user_data, (variable_t *)payload);
}

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
    context->var_map = NULL;
    context->parent = NULL;
    context->flow_state = FLOW_NORMAL;
    context->return_value = NULL;
    context->alloc_list = list;
    return context;
}

/* Initialize the root context scope backed by a variable hash map */
context_t *context_init_global(alloc_hdr_t **list)
{
    context_t *context = context_init(list);
    if (!context) {
        return NULL;
    }

    /* The map owns each variable_t and frees it on the runtime's allocation list */
    context->var_map = chashmap_create(CONTEXT_GLOBAL_MAP_CAPACITY, context_variable_free_cb, (void *)list, NULL, NULL);
    if (context->var_map == NULL) {
        tracked_free(list, context);
        return NULL;
    }
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
        /* Global scope: hash lookup; inner scopes shadow it because they are searched first */
        if (current_ctx->var_map != NULL) {
            variable_t *variable = (variable_t *)chashmap_get(current_ctx->var_map, variable_name);
            if (variable != NULL) {
                return variable;
            }
            current_ctx = current_ctx->parent;
            continue;
        }

        /* Local scope: few variables, a linear scan is cheapest */
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
    /* Pure helper: no logging or fatal, the evaluator decides how to report a NULL result */
    if (variable == NULL || variable->value == NULL) {
        return NULL;
    }
    return val_copy(variable->value);
}

/* Add a newly defined variable to the given context scope */
ti_status_t context_add_variable(alloc_hdr_t **list, context_t *ctx, const char *name, value_t *value)
{
    if (!ctx || !name || !value) {
        return TI_ERR_INVALID_ARG;
    }

    /* Global scope: store in the hash map */
    if (ctx->var_map != NULL) {
        /* Must check first: chashmap_set would overwrite and free an existing variable */
        if (chashmap_get(ctx->var_map, name) != NULL) {
            return TI_ERR_RUNTIME;
        }

        variable_t *variable = variable_init(list, name);
        if (variable == NULL) {
            return TI_ERR_NO_MEMORY;
        }
        variable->value = value;

        /* On failure the map hands the variable back: free only the struct, the caller keeps name and value */
        if (!chashmap_set(ctx->var_map, name, variable)) {
            tracked_free(list, variable);
            return TI_ERR_NO_MEMORY;
        }

        /* Success: the map now owns the variable, its name and its value */
        return TI_OK;
    }

    /* Local scope: a name may be defined only once per scope */
    for (int i = 0; i < ctx->variable_count; i++) {
        if (strcmp(ctx->variables[i]->name, name) == 0) {
            return TI_ERR_RUNTIME;
        }
    }

    variable_t *variable = variable_init(list, name);
    if (variable == NULL) {
        return TI_ERR_NO_MEMORY;
    }

    /* Grow the variable pointer array; keep the old block intact on failure */
    variable_t **grown = tracked_realloc(list, ctx->variables, (ctx->variable_count + 1) * sizeof(struct VARIABLE_STRUCT *));
    if (grown == NULL) {
        tracked_free(list, variable);
        return TI_ERR_NO_MEMORY;
    }

    /* Success: the context now owns the variable, its name and its value */
    variable->value = value;
    ctx->variables = grown;
    ctx->variables[ctx->variable_count] = variable;
    ctx->variable_count += 1;
    return TI_OK;
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

    /* Free the global variable map: its destructor frees every variable_t */
    if (ctx->var_map != NULL) {
        chashmap_destroy(ctx->var_map);
        ctx->var_map = NULL;
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
