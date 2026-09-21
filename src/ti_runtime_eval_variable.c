#include "include/ti_runtime_visitor.h"
#include "include/tracked_memory.h"
#include "include/debug.h"
#include "TienInterpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------- Variable & Identifier Evaluators -------------------- */

/**
 * @brief Evaluate a variable definition node and register into context.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to context.
 * @param node Variable definition AST node.
 * @return Always NULL.
 */
value_t *eval_variable_definition(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    /* Duplicate variable identifier name on runtime memory list */
    char *variable_name = tracked_strdup(&rt->alloc_list, node->value.variable_definition.variable_name);

    /* Recursively evaluate initializer expression */
    value_t *value = visitor_visit(rt, ctx, node->value.variable_definition.value);
    if (value == NULL) {
        ti_log("[Runtime Error] Variable definition '%s' evaluated to NULL at line %d\n", variable_name, node->line);
        ti_fatal();
    }

    /* Enforce declared type matches evaluated value type */
    if (node->value.variable_definition.variable_type != value->type) {
        ti_log("[Runtime Error] Type mismatch in definition of '%s'. Expected %s, but got %s at line %d\n",
               variable_name,
               val_type_to_str(node->value.variable_definition.variable_type),
               val_type_to_str(value->type),
               node->line);
        ti_fatal();
    }

    /* Register new variable binding into the current scope context */
    context_add_variable(&rt->alloc_list, ctx, variable_name, value);
    return NULL;
}

/**
 * @brief Evaluate an assignment statement node.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to context.
 * @param node Assignment AST node.
 * @return Always NULL.
 */
value_t *eval_assignment(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    ast_t *target_node = node->value.assignment.target;
    ast_t *value_node = node->value.assignment.value;

    /* Look up target variable in current and parent scopes */
    variable_t *variable = context_find_variable(ctx, target_node->value.identifier);
    if (variable != NULL) {
        /* Evaluate right-hand side expression */
        value_t *val = visitor_visit(rt, ctx, value_node); 
        if (val == NULL) {
            ti_log("[Runtime Error] Assignment expression for '%s' evaluated to NULL at line %d\n", target_node->value.identifier, node->line);
            ti_fatal();
        }

        /* Verify type compatibility with existing variable */
        if (variable->value != NULL && variable->value->type != val->type) {
            ti_log("[Runtime Error] Type mismatch in assignment to '%s'. Expected %s, but got %s at line %d\n",
               target_node->value.identifier,
               val_type_to_str(variable->value->type),
               val_type_to_str(val->type),
               node->line);
            ti_fatal();
        }

        /* Free previous value to prevent leaks, then assign new value */
        if (variable->value != NULL) {
            val_free_internal(&rt->alloc_list, variable->value);
            tracked_free(&rt->alloc_list, variable->value);
        }
        variable->value = val;
    } else {
        ti_log("[Runtime Error] Undefined variable '%s' at line %d\n", target_node->value.identifier, node->line);
        ti_fatal();
    }
    return NULL;
}

/**
 * @brief Look up and evaluate an identifier node.
 * @param ctx Pointer to context.
 * @param node Identifier AST node.
 * @return Evaluated value_t pointer.
 */
value_t *eval_identifier(context_t *ctx, ast_t *node)
{
    /* Search for variable by name across active context hierarchy */
    variable_t *variable = context_find_variable(ctx, node->value.identifier);
    if (variable != NULL) {
        if (variable->value == NULL) {
            ti_log("[Runtime Error] Variable '%s' has NULL value at line %d\n", node->value.identifier, node->line);
            ti_fatal();
        }
        /* Return an independent deep copy of the variable's value */
        value_t *value = context_copy_value(ctx ? ctx->alloc_list : NULL, variable);
        return value;
    }

    ti_log("[Runtime Error] Undefined variable '%s' at line %d\n", node->value.identifier, node->line);
    ti_fatal();
    return NULL;
}
