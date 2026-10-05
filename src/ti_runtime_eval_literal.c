#include "include/ti_runtime_visitor.h"
#include "include/ti_type_value.h"
#include "include/ti_type_value_dict.h"
#include "include/ti_type_value_list.h"
#include "TienInterpreter.h"
#include "include/debug.h"
#include "include/tracked_memory.h"
#include <stdlib.h>

/* -------------------- Literal Evaluators -------------------- */

/**
 * @brief Evaluate a string literal node.
 * @param ctx Pointer to context.
 * @param node String literal AST node.
 * @return Newly allocated string value_t.
 */
value_t *eval_string_literal(context_t *ctx, ast_t *node)
{
    (void)ctx;
    return val_new_string(node->value.string_value);
}

/**
 * @brief Evaluate an integer literal node.
 * @param ctx Pointer to context.
 * @param node Integer literal AST node.
 * @return Newly allocated integer value_t.
 */
value_t *eval_int_literal(context_t *ctx, ast_t *node)
{
    (void)ctx;
    return val_new_int(node->value.int_value);
}

/**
 * @brief Evaluate a float literal node.
 * @param ctx Pointer to context.
 * @param node Float literal AST node.
 * @return Newly allocated float value_t.
 */
value_t *eval_float_literal(context_t *ctx, ast_t *node)
{
    (void)ctx;
    return val_new_float(node->value.float_value);
}

/**
 * @brief Evaluate a boolean literal node.
 * @param ctx Pointer to context.
 * @param node Boolean literal AST node.
 * @return Newly allocated boolean value_t.
 */
value_t *eval_boolean_literal(context_t *ctx, ast_t *node)
{
    (void)ctx;
    return val_new_bool(node->value.bool_value);
}

value_t *eval_dict_literal(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    (void) ctx;
    value_t *result = NULL;
    value_t *dict_val = val_new_dict();
    if (dict_val == NULL || dict_val->dict_val == NULL) {
        ti_raise(rt, TI_ERR_NO_MEMORY, node->line, "Cannot allocate dictionary");
        goto out;
    }

    /* Set key:value into dict */
    for (int i = 0; i < node->value.dict_literal.pair_count; i++) {
        const char *key = node->value.dict_literal.keys[i];
        /* Values are literals only (strict validation in the parser) */
        ast_t *val_node = node->value.dict_literal.values[i];
        value_t *val = NULL;
        switch (val_node->type) {
        case AST_INT_LITERAL:
            val = val_new_int(val_node->value.int_value);
            break;
        case AST_FLOAT_LITERAL:
            val = val_new_float(val_node->value.float_value);
            break;
        case AST_STRING_LITERAL:
            val = val_new_string(val_node->value.string_value);
            break;
        case AST_BOOLEAN:
            val = val_new_bool(val_node->value.bool_value);
            break;
        default:
            /* Never reached because of strict validation in the parser */
            break;
        }

        /* val_dict_set takes ownership of val even on failure */
        ti_status_t status = val_dict_set(dict_val->dict_val, key, val);
        if (status != TI_OK) {
            ti_raise(rt, status, node->line, "Cannot set dictionary key '%s': %s", key, ti_err_to_str(status));
            goto out;
        }
    }

    /* Success: ownership of the dictionary moves to the caller */
    result = dict_val;
    dict_val = NULL;

out:
    val_free(dict_val);
    return result;
}

/* Evaluate a list literal node into a new list value_t */
value_t *eval_list_literal(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    val_type_t element_type = node->value.list_literal.element_type;
    int element_count = node->value.list_literal.element_count;
    value_t *result = NULL;
    value_t *list_val = NULL;
    value_t *elem_val = NULL;

    /* Empty literal has nothing to infer from: the declaration must provide the element type */
    if (element_count == 0) {
        result = val_new_list(element_type);
        if (result == NULL) {
            ti_raise(rt, TI_ERR_RUNTIME, node->line, "Cannot determine element type of empty list");
        }
        return result;
    }

    for (int i = 0; i < element_count; i++) {
        /* Evaluate element expression; NULL means it already failed or was cancelled */
        elem_val = visitor_visit(rt, ctx, node->value.list_literal.elements[i]);
        if (elem_val == NULL) {
            goto out;
        }

        /* First element fixes the element type when the declaration did not */
        if (list_val == NULL) {
            if (element_type == VAL_NULL) {
                element_type = elem_val->type;
            }
            list_val = val_new_list(element_type);
            if (list_val == NULL) {
                ti_raise(rt, TI_ERR_TYPE_MISMATCH, node->line, "Invalid list element type %s",
                         val_type_to_str(elem_val->type));
                goto out;
            }
        }

        /* Strict homogeneous typing: every element must match the list element type */
        if (elem_val->type != element_type) {
            ti_raise(rt, TI_ERR_TYPE_MISMATCH, node->line, "List element %d expected type %s, but got %s",
                     i, val_type_to_str(element_type), val_type_to_str(elem_val->type));
            goto out;
        }

        /* val_list_push takes ownership of elem_val even on failure */
        ti_status_t status = val_list_push(list_val->list_val, elem_val);
        elem_val = NULL;
        if (status != TI_OK) {
            ti_raise(rt, status, node->line, "Cannot append list element %d: %s", i, ti_err_to_str(status));
            goto out;
        }
    }

    /* Success: ownership of the list moves to the caller */
    result = list_val;
    list_val = NULL;

out:
    /* Single cleanup point: the current element and a partially built list */
    val_free(elem_val);
    val_free(list_val);
    return result;
}
