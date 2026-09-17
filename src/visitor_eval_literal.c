#include "include/visitor_internal.h"
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
