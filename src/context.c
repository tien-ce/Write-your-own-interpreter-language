#include "include/visitor_internal.h"
#include "include/tracked_memory.h"
#include "TienInterpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------- Context & Variable Operations -------------------- */

/* Initialize a new interpreter context scope */
context_t *context_init(void)
{
    context_t *context = tracked_calloc(1, sizeof(struct InterpreterContext));
    context->variables = NULL;
    context->variable_size = 0;
    context->parent = NULL;
    return context;
}

/* Free an interpreter context and its scoped variables */
void context_free(context_t *ctx)
{
    context_free_internal(ctx);
    tracked_free(ctx);
}

/* Allocate a new variable_t with the given variable name */
variable_t *variable_init(const char *variable_name)
{
    variable_t *variable = tracked_calloc(1, sizeof(struct VARIABLE_STRUCT));
    variable->name = variable_name;
    return variable;
}

/* Find a variable by name walking up from the current context to root parent */
variable_t *context_find_variable(context_t *ctx, const char *variable_name)
{
    context_t *current_ctx = ctx;
    while (current_ctx != NULL) {
        for (int i = 0; i < current_ctx->variable_size; i++) {
            if (strcmp(current_ctx->variables[i]->name, variable_name) == 0) {
                return current_ctx->variables[i];
            }
        }
        current_ctx = current_ctx->parent;
    }
    return NULL;
}

/* Create a deep copy of a variable's value_t */
value_t *context_copy_value(variable_t *variable)
{
    if (variable == NULL || variable->value == NULL) {
        ti_log("[ERROR]: Attempted to access NULL variable\n");
        ti_fatal();
        return NULL;
    }

    value_t *value = val_init(variable->value->type);
    switch (variable->value->type) {
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
        ti_log("[ERROR]: Unknown value type %d in context_copy_value\n", variable->value->type);
        ti_fatal();
        break;
    }
    return value;
}

/* Add a newly defined variable to the given context scope */
void context_add_variable(context_t *ctx, const char *name, value_t *value)
{
    int size = ctx->variable_size;
    for (int i = 0; i < size; i++) {
        if (strcmp(ctx->variables[i]->name, name) == 0) {
            ti_log("[ERROR]: Redefinition of variable '%s'\n", name);
            ti_fatal();
        }
    }

    variable_t *variable = variable_init(name);
    variable->value = value;
    if (ctx->variables == NULL) {
        ctx->variables = tracked_calloc(1, sizeof(struct VARIABLE_STRUCT *));
        ctx->variables[0] = variable;
        ctx->variable_size = 1;
    } else {
        ctx->variables = tracked_realloc(ctx->variables, (ctx->variable_size + 1) * sizeof(struct VARIABLE_STRUCT *));
        ctx->variables[ctx->variable_size] = variable;
        ctx->variable_size += 1;
    }
}

/* Free all variables and internal structures inside a context scope */
void context_free_internal(context_t *ctx)
{
    if (!ctx) {
        return;
    }

    for (int i = 0; i < ctx->variable_size; i++) {
        variable_t *var = ctx->variables[i];
        if (var) {
            if (var->value) {
                val_free_internal(var->value);
                tracked_free(var->value);
            }
            if (var->name) {
                tracked_free((void *)var->name);
            }
            tracked_free(var);
        }
    }

    tracked_free(ctx->variables);
    ctx->variables = NULL;
    ctx->variable_size = 0;
}
