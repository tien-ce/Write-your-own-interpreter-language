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
value_t *eval_string_literal(InterpreterContext *ctx, ast_t *node)
{
    (void)ctx;
    value_t *value = val_init(VAL_STRING);
    value->string_val = tracked_strdup(node->value.string_value);
    return value;
}

/**
 * @brief Evaluate an integer literal node.
 * @param ctx Pointer to context.
 * @param node Integer literal AST node.
 * @return Newly allocated integer value_t.
 */
value_t *eval_int_literal(InterpreterContext *ctx, ast_t *node)
{
    (void)ctx;
    value_t *value = val_init(VAL_INT);
    value->int_val = node->value.int_value;
    return value;
}

/**
 * @brief Evaluate a float literal node.
 * @param ctx Pointer to context.
 * @param node Float literal AST node.
 * @return Newly allocated float value_t.
 */
value_t *eval_float_literal(InterpreterContext *ctx, ast_t *node)
{
    (void)ctx;
    value_t *value = val_init(VAL_FLOAT);
    value->float_val = node->value.float_value;
    return value;
}

/**
 * @brief Evaluate a boolean literal node.
 * @param ctx Pointer to context.
 * @param node Boolean literal AST node.
 * @return Newly allocated boolean value_t.
 */
value_t *eval_boolean_literal(InterpreterContext *ctx, ast_t *node)
{
    (void)ctx;
    value_t *value = val_init(VAL_BOOL);
    value->bool_val = node->value.bool_value;
    return value;
}
