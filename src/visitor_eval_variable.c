#include "include/visitor_internal.h"
#include "include/tracked_memory.h"
#include "include/debug.h"
#include "TienInterpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------- Variable & Identifier Evaluators -------------------- */

/**
 * @brief Evaluate a variable definition node and register into context.
 * @param ctx Pointer to context.
 * @param node Variable definition AST node.
 * @return Always NULL.
 */
value_t *eval_variable_definition(context_t *ctx, ast_t *node)
{
    char *variable_name = tracked_strdup(node->value.variable_definition.variable_name);
    value_t *value = visitor_visit(ctx, node->value.variable_definition.value);
    if (value == NULL) {
        ti_log("[Runtime Error] Variable definition '%s' evaluated to NULL at line %d\n", variable_name, node->line);
        ti_fatal();
    }
    if (node->value.variable_definition.variable_type != value->type) {
        ti_log("[Runtime Error] Type mismatch in definition of '%s'. Expected %s, but got %s at line %d\n",
               variable_name,
               val_type_to_str(node->value.variable_definition.variable_type),
               val_type_to_str(value->type),
               node->line);
        ti_fatal();
    }
    context_add_variable(ctx, variable_name, value);
    return NULL;
}

/**
 * @brief Evaluate an assignment statement node.
 * @param ctx Pointer to context.
 * @param node Assignment AST node.
 * @return Always NULL.
 */
value_t *eval_assignment(context_t *ctx, ast_t *node)
{
    ast_t *target_node = node->value.assignment.target;
    ast_t *value_node = node->value.assignment.value;
    variable_t *variable = context_find_variable(ctx, target_node->value.identifier);
    if (variable != NULL) {
        value_t *val = visitor_visit(ctx, value_node); 
        if (val == NULL) {
            ti_log("[Runtime Error] Assignment expression for '%s' evaluated to NULL at line %d\n", target_node->value.identifier, node->line);
            ti_fatal();
        }
        if (variable->value != NULL && variable->value->type != val->type) {
            ti_log("[Runtime Error] Type mismatch in assignment to '%s'. Expected %s, but got %s at line %d\n",
                   target_node->value.identifier,
                   val_type_to_str(variable->value->type),
                   val_type_to_str(val->type),
                   node->line);
            ti_fatal();
        }
        if (variable->value != NULL) {
            val_free_internal(variable->value);
            tracked_free(variable->value);
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
    variable_t *variable = context_find_variable(ctx, node->value.identifier);
    if (variable != NULL) {
        if (variable->value == NULL) {
            ti_log("[Runtime Error] Variable '%s' has NULL value at line %d\n", node->value.identifier, node->line);
            ti_fatal();
        }
        value_t *value = context_copy_value(variable);
        return value;
    }
    ti_log("[Runtime Error] Undefined variable '%s' at line %d\n", node->value.identifier, node->line);
    ti_fatal();
    return NULL;
}
