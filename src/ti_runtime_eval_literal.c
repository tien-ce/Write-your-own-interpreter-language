#include "include/ti_runtime_visitor.h"
#include "include/ti_type_value.h"
#include "include/ti_type_value_dict.h"
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

value_t *eval_dict_literal(context_t *ctx, ast_t *node)
{
    (void) ctx;
    /* Create the wrapper */
    value_t *dict_val = val_new_dict(); 
    /* Set key:value into dict*/
    for (int i = 0; i < node->value.dict_literal.pair_count; i++)
    {
        /* Get key */
        const char *key = node->value.dict_literal.keys[i];
        /* Get value directly through AST Node, because the value of dict litteral can only other litteral */
        ast_t *val_node = node->value.dict_literal.values[i];
        value_t *val = NULL; // Store the geted value
        switch (val_node->type)
        {
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
                /* Never jump to here becausef of  Strict Validation in Parser */
                break;
        }
        /* Set value into dict with corresponding key */
        val_dict_set(dict_val->dict_val, key, val);
    }
    return dict_val;
} 
