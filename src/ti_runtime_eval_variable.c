#include "include/ti_runtime_context.h"
#include "include/ti_runtime_visitor.h"
#include "include/ti_type.h"
#include "include/ti_type_value_dict.h"
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
    
    if (rt != NULL && rt->is_interrupted) {
        if (value) val_free(value);
        return NULL;
    }

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
    if (rt != NULL && rt->is_interrupted) {
        return NULL;
    }
    ast_t *target_node = node->value.assignment.target;
    ast_t *value_node = node->value.assignment.value;

    /* Evaluate right-hand side expression */
    value_t *val = visitor_visit(rt, ctx, value_node); 
    if (val == NULL) {
        ti_log("[Runtime Error] Assignment expression for '%s' evaluated to NULL at line %d\n", target_node->value.identifier, node->line);
        ti_fatal();
    }

    switch((int)target_node->type)
    {
        case AST_IDENTIFIER: // variable = rhs
        {
            /* Look up target variable in current and parent scopes */
            variable_t *variable = context_find_variable(ctx, target_node->value.identifier);
            if (variable != NULL) {

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
                    val_free(variable->value);
                }
                variable->value = val;
            } else {
                ti_log("[Runtime Error] Undefined variable '%s' at line %d\n", target_node->value.identifier, node->line);
                ti_fatal();
            }
            break;
        }
        case AST_ARRAY_ACCESS:// arr[0] = rhs or arr["key"] = rhs
        {
            const char *container_name = target_node->value.array_access.id;
            /* Get the variable from context */
            variable_t *container_var = context_find_variable(ctx, container_name);
            if (container_var == NULL)
            {
                ti_log("[Runtime Error] Undefined variable '%s' at line %d\n", container_name, node->line);
                ti_fatal();
            }
            /* Only some specific types have access action */
            switch ((int)container_var->value->type)
            {
                case VAL_DICT:
                    /* Calculate the key from ast node */
                    value_t *key_val = visitor_visit(rt,ctx, target_node->value.array_access.index_expr);
                    if (key_val == NULL || key_val->type != VAL_STRING)
                    {
                        ti_log("[Runtime Error] Dictionary keys must be strings for variable '%s' at line %d\n", container_name, node->line);
                        ti_fatal();
                    }
                    /* Set new value into dict */
                    val_dict_set(container_var->value->dict_val, key_val->string_val, val);
                    /* Free temporary key val but still need to keep target value */
                    val_free(key_val);
                    break;
                default:
                    ti_log("[Runtime Error] Type '%s' does not support subscript assignment at line %d\n", val_type_to_str(container_var->value->type), node->line);
                    ti_fatal();
                    return NULL;
            }
        }
            break;
        default:
            ti_log("[Runtime Error] Invalid assignment target type at line %d\n", node->line);
            ti_fatal();
            return NULL;
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

/**
 * @brief Evaluate an array/dictionary access expression (R-Value).
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to context.
 * @param node Array access AST node.
 * @return Evaluated value_t pointer (deep copy).
 */
value_t *eval_array_access(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    const char *container_name = node->value.array_access.id;
    
    /* 1. Look up the container variable */
    variable_t *container_var = context_find_variable(ctx, container_name);
    if (container_var == NULL || container_var->value == NULL) {
        ti_log("[Runtime Error] Undefined variable '%s' at line %d\n", container_name, node->line);
        ti_fatal();
        return NULL;
    }

    /* 2. Evaluate the index/key expression */
    value_t *key_val = visitor_visit(rt, ctx, node->value.array_access.index_expr);
    if (key_val == NULL) {
        ti_log("[Runtime Error] Invalid index expression for '%s' at line %d\n", container_name, node->line);
        ti_fatal();
        return NULL;
    }

    value_t *result_val = NULL;

    /* 3. Dispatch based on container type */
    switch ((int)container_var->value->type) {
        case VAL_DICT:
            if (key_val->type != VAL_STRING) {
                ti_log("[Runtime Error] Dictionary keys must be strings for variable '%s' at line %d\n", container_name, node->line);
                val_free(key_val);
                ti_fatal();
                return NULL;
            }
            
            result_val = val_dict_get(container_var->value->dict_val, key_val->string_val);
            if (result_val == NULL) {
                ti_log("[Runtime Error] Key '%s' not found in dictionary '%s' at line %d\n", key_val->string_val, container_name, node->line);
                val_free(key_val);
                ti_fatal();
                return NULL;
            }
            break;

        default:
            ti_log("[Runtime Error] Type '%s' does not support subscript access at line %d\n", 
                   val_type_to_str(container_var->value->type), node->line);
            val_free(key_val);
            ti_fatal();
            return NULL;
    }

    /* 4. Clean up temporary evaluated key */
    val_free(key_val);

    return result_val;
}
