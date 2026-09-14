#include "include/visitor_internal.h"
#include "include/tracked_memory.h"
#include "TienInterpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* -------------------- Static Helper Functions -------------------- */

/**
 * @brief Evaluate condition expression node and ensure boolean result.
 * @param ctx Pointer to active execution context.
 * @param cond_node Condition AST expression node.
 * @return Evaluated boolean value.
 */
static bool eval_boolean_condition(InterpreterContext *ctx, ast_t *cond_node)
{
    value_t *value = visitor_visit(ctx, cond_node);
    if (!value || value->type != VAL_BOOL) {
        ti_log("[ERROR]: Unexpected type %d, only expect bool value\n", value ? (int)value->type : -1);
        ti_fatal();
    }

    bool res = value->bool_val;
    val_free_internal(value);
    tracked_free(value);
    return res;
}

/**
 * @brief Execute a child compound block in a newly created local scope.
 * @param parent_ctx Pointer to parent context scope.
 * @param body_node Pointer to compound AST block.
 */
static void visitor_execute_body(InterpreterContext *parent_ctx, ast_t *body_node)
{
    if (!body_node) {
        return;
    }

    /* 1. Create new local scope */
    context_t *local_ctx = context_init();
    local_ctx->parent = parent_ctx;

    /* 2. Execute statements */
    value_t *capture = visitor_visit(local_ctx, body_node);
    if (capture != NULL) {
        ti_log("[Warning]: Compound return value, please check it\n");
        val_free_internal(capture);
        tracked_free(capture);
    }

    /* 3. Free local scope */
    context_free_internal(local_ctx);
    tracked_free(local_ctx);
}

/* -------------------- Public Statement Evaluators -------------------- */

/**
 * @brief Execute a while loop statement node.
 * @param ctx Pointer to context.
 * @param node While statement AST node.
 * @return Always NULL.
 */
value_t *eval_while_statement(InterpreterContext *ctx, ast_t *node)
{
    while (eval_boolean_condition(ctx, node->value.while_statement.condition)) {
        visitor_execute_body(ctx, node->value.while_statement.body);
    }
    return NULL;
}

/**
 * @brief Execute an if/else statement node.
 * @param ctx Pointer to context.
 * @param node If statement AST node.
 * @return Always NULL.
 */
value_t *eval_if_statement(InterpreterContext *ctx, ast_t *node)
{
    if (eval_boolean_condition(ctx, node->value.if_statement.condition)) {
        visitor_execute_body(ctx, node->value.if_statement.body);
    } else {
        visitor_execute_body(ctx, node->value.if_statement.else_body);
    }
    return NULL;
}

/**
 * @brief Execute a for statement node (stub).
 * @param ctx Pointer to context.
 * @param node For statement AST node.
 * @return Always NULL.
 */
value_t *eval_for_statement(InterpreterContext *ctx, ast_t *node)
{
    (void)ctx;
    (void)node;
    return NULL;
}

/**
 * @brief Execute all statements inside a compound block.
 * @param ctx Pointer to context.
 * @param node Compound AST node.
 * @return Always NULL.
 */
value_t *eval_compound_statement(InterpreterContext *ctx, ast_t *node)
{
    int size = node->value.compound.compound_size;
    for (int i = 0; i < size; i++) {
        value_t *value = visitor_visit(ctx, node->value.compound.compound_value[i]);
        if (value != NULL) {
            val_free_internal(value);
            tracked_free(value);
        }
    }
    return NULL; 
}
