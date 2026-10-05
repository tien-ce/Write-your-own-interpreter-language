#include "include/ti_runtime_context.h"
#include "include/ti_runtime_visitor.h"
#include "include/tracked_memory.h"
#include "include/debug.h"
#include "TienInterpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* -------------------- Static Helper Functions -------------------- */

/**
 * @brief Evaluate condition expression node and ensure boolean result.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context.
 * @param cond_node Condition AST expression node.
 * @param result Receives the boolean result on success.
 * @return true on success, false if evaluation failed (NULL propagated from the expression).
 */
static bool eval_boolean_condition(ti_runtime_t *rt, context_t *ctx, ast_t *cond_node, bool *result)
{
    /* Evaluate the condition expression node; NULL means it already failed or was cancelled */
    value_t *value = visitor_visit(rt, ctx, cond_node);
    if (value == NULL) {
        return false;
    }

    /* Enforce boolean type requirement; halt execution on type mismatch */
    if (value->type != VAL_BOOL) {
        ti_raise(rt, TI_ERR_TYPE_MISMATCH, cond_node->line, "Unexpected type %s, only expect bool value",
                 val_type_to_str(value->type));
        val_free(value);
        return false;
    }

    /* Extract boolean result and reclaim temporary evaluation value */
    *result = value->bool_val;
    val_free(value);
    return true;
}

/**
 * @brief Execute a child compound block in a newly created local scope.
 * @param rt Pointer to active runtime instance.
 * @param parent_ctx Pointer to parent context scope.
 * @param body_node Pointer to compound AST block (NULL means an empty branch).
 * @return true on success, false if the body failed.
 */
static bool visitor_execute_body(ti_runtime_t *rt, context_t *parent_ctx, ast_t *body_node)
{
    if (!body_node) {
        return true;
    }

    /* Create new local scope linked to parent context */
    context_t *local_ctx = context_init(&rt->alloc_list);
    local_ctx->parent = parent_ctx;

    /* Execute statements within the local scope */
    value_t *result = visitor_visit(rt, local_ctx, body_node);
    bool succeeded = (result != NULL);
    val_free(result);

    /* Propagate control flow state (return, break, continue) and payload to parent context */
    if (succeeded && local_ctx->flow_state != FLOW_NORMAL) {
        parent_ctx->flow_state = local_ctx->flow_state;
        parent_ctx->return_value = local_ctx->return_value;
        local_ctx->return_value = NULL; /* Transfer ownership to parent context */
    }

    /* Free local scope (also releases any return payload left behind by a failure) */
    context_free_internal(&rt->alloc_list, local_ctx);
    tracked_free(&rt->alloc_list, local_ctx);
    return succeeded;
}

/* -------------------- Public Statement Evaluators -------------------- */

/**
 * @brief Execute a while loop statement node.
 * Consume break, continue signal. Each iteration is a safe point (cancellation, queued events).
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to context.
 * @param node While statement AST node.
 * @return TI_VAL_OK on success, NULL on failure or cancellation.
 */
value_t *eval_while_statement(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    for (;;) {
        /* Safe point: abort long-running loops and service native events */
        if (ti_runtime_safe_point(rt)) {
            return NULL;
        }

        /* Loop while condition evaluates to true */
        bool condition = false;
        if (!eval_boolean_condition(rt, ctx, node->value.while_statement.condition, &condition)) {
            return NULL;
        }
        if (!condition) {
            break;
        }

        /* Execute loop body statements within a child scope */
        if (!visitor_execute_body(rt, ctx, node->value.while_statement.body)) {
            return NULL;
        }

        /* Inspect and handle control flow signals from the loop body */
        if (ctx->flow_state == FLOW_BREAK) {
            /* Consume 'break' signal and terminate loop */
            ctx->flow_state = FLOW_NORMAL;
            break;
        } else if (ctx->flow_state == FLOW_CONTINUE) {
            /* Consume 'continue' signal and proceed to next iteration */
            ctx->flow_state = FLOW_NORMAL;
            continue;
        } else if (ctx->flow_state == FLOW_RETURN) {
            /* Preserve 'return' signal and exit loop so parent caller can propagate it */
            break;
        }
    }
    return TI_VAL_OK;
}

/**
 * @brief Execute an if/else statement node.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to context.
 * @param node If statement AST node.
 * @return TI_VAL_OK on success, NULL on failure.
 */
value_t *eval_if_statement(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    /* A failed condition (NULL) must not select the else branch */
    bool condition = false;
    if (!eval_boolean_condition(rt, ctx, node->value.if_statement.condition, &condition)) {
        return NULL;
    }

    /* Execute the selected branch */
    ast_t *branch_node = condition ? node->value.if_statement.body : node->value.if_statement.else_body;
    return visitor_execute_body(rt, ctx, branch_node) ? TI_VAL_OK : NULL;
}

/**
 * @brief Execute a for statement node (stub).
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to context.
 * @param node For statement AST node.
 * @return Always TI_VAL_OK.
 */
value_t *eval_for_statement(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    (void)rt;
    (void)ctx;
    (void)node;
    return TI_VAL_OK;
}

/**
 * @brief Execute all statements inside a compound block sequentially.
 * Evaluates statements one by one. If any statement fails (NULL) execution stops and the failure
 * propagates; if one alters flow_state (return, break, continue) execution halts and control
 * returns to the caller. Each statement boundary is a safe point.
 *
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context.
 * @param node Compound AST node.
 * @return TI_VAL_OK on success, NULL on failure or cancellation.
 */
value_t *eval_compound_statement(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    int count = node->value.compound.statement_count;
    for (int i = 0; i < count; i++) {
        /* Safe point: notice cancellation or an error raised by a callback in the last statement */
        if (ti_runtime_safe_point(rt)) {
            return NULL;
        }

        /* Evaluate statement node sequentially; NULL means the statement failed */
        value_t *value = visitor_visit(rt, ctx, node->value.compound.statements[i]);
        if (value == NULL) {
            return NULL;
        }
        val_free(value);

        /* Check control flow state: break statement loop if flow is interrupted */
        if (ctx->flow_state != FLOW_NORMAL) {
            break;
        }
    }
    return TI_VAL_OK;
}

/**
 * @brief Execute a return statement node: return [expr];.
 * Evaluates the optional return expression, sets FLOW_RETURN in the active
 * context, and records the evaluated return_value pointer.
 *
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context.
 * @param node Return statement AST node.
 * @return TI_VAL_OK on success (result stored in ctx->return_value), NULL on failure.
 */
value_t *eval_return_statement(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    /* Evaluate return expression or initialize void return value */
    value_t *ret_val = NULL;
    if (node->value.return_statement.value != NULL) {
        ret_val = visitor_visit(rt, ctx, node->value.return_statement.value);
        if (ret_val == NULL) {
            return NULL;
        }
    } else {
        ret_val = val_new_void();
    }

    /* Record return payload and signal FLOW_RETURN to halt further statement execution */
    ctx->flow_state = FLOW_RETURN;
    ctx->return_value = ret_val;
    return TI_VAL_OK;
}

/**
 * @brief Execute a break statement node: break;.
 * Sets FLOW_BREAK flag in the active context to signal termination of innermost loop.
 *
 * @param ctx Pointer to active execution context.
 * @param node Break statement AST node.
 * @return Always TI_VAL_OK.
 */
value_t *eval_break_statement(context_t *ctx, ast_t *node)
{
    (void)node;
    /* Signal loop break to terminate the innermost active loop */
    ctx->flow_state = FLOW_BREAK;
    return TI_VAL_OK;
}

/**
 * @brief Execute a continue statement node: continue;.
 * Sets FLOW_CONTINUE flag in the active context to jump to next loop iteration.
 *
 * @param ctx Pointer to active execution context.
 * @param node Continue statement AST node.
 * @return Always TI_VAL_OK.
 */
value_t *eval_continue_statement(context_t *ctx, ast_t *node)
{
    (void)node;
    /* Signal loop continue to advance to next iteration of active loop */
    ctx->flow_state = FLOW_CONTINUE;
    return TI_VAL_OK;
}
