#include "include/ti_runtime_context.h"
#include "include/ti_runtime_visitor.h"
#include "include/tracked_memory.h"
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
 * @return Evaluated boolean value.
 */
static bool eval_boolean_condition(ti_runtime_t *rt, context_t *ctx, ast_t *cond_node)
{
    /* Evaluate the condition expression node */
    value_t *value = visitor_visit(rt, ctx, cond_node);

    /* Enforce boolean type requirement; halt execution on type mismatch */
    if (!value || value->type != VAL_BOOL) {
        ti_log("[Runtime Error] Unexpected type %d, only expect bool value at line %d\n", value ? (int)value->type : -1, cond_node->line);
        ti_fatal();
    }

    /* Extract boolean result and reclaim temporary evaluation value */
    bool res = value->bool_val;
    val_free(value);
    return res;
}

/**
 * @brief Execute a child compound block in a newly created local scope.
 * @param rt Pointer to active runtime instance.
 * @param parent_ctx Pointer to parent context scope.
 * @param body_node Pointer to compound AST block.
 */
static void visitor_execute_body(ti_runtime_t *rt, context_t *parent_ctx, ast_t *body_node)
{
    if (!body_node) {
        return;
    }

    /* Create new local scope linked to parent context */
    context_t *local_ctx = context_init(&rt->alloc_list);
    local_ctx->parent = parent_ctx;

    /* Execute statements within the local scope */
    value_t *capture = visitor_visit(rt, local_ctx, body_node);
    if (capture != NULL) {
        ti_log("[Warning]: Compound return value, please check it\n");
        val_free(capture);
    }

    /* Propagate control flow state (return, break, continue) and payload to parent context */
    if (local_ctx->flow_state != FLOW_NORMAL) {
        parent_ctx->flow_state = local_ctx->flow_state;
        parent_ctx->return_value = local_ctx->return_value;
        local_ctx->return_value = NULL; /* Transfer ownership to parent context */
    }

    /* Free local scope */
    context_free_internal(&rt->alloc_list, local_ctx);
    tracked_free(&rt->alloc_list, local_ctx);
}

/* -------------------- Public Statement Evaluators -------------------- */

/**
 * @brief Execute a while loop statement node.
 * Consume break, continue signal.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to context.
 * @param node While statement AST node.
 * @return Always NULL.
 */
value_t *eval_while_statement(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    /* Loop while condition evaluates to true */
    while (eval_boolean_condition(rt, ctx, node->value.while_statement.condition)) {
        /* Check external cancellation request to abort long-running execution */
        if (rt != NULL && rt->is_interrupted) {
            break;
        }

        /* Execute loop body statements within a child scope */
        visitor_execute_body(rt, ctx, node->value.while_statement.body);

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
    return NULL;
}

/**
 * @brief Execute an if/else statement node.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to context.
 * @param node If statement AST node.
 * @return Always NULL.
 */
value_t *eval_if_statement(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    /* Evaluate branch condition and execute the corresponding body */
    if (eval_boolean_condition(rt, ctx, node->value.if_statement.condition)) {
        visitor_execute_body(rt, ctx, node->value.if_statement.body);
    } else {
        visitor_execute_body(rt, ctx, node->value.if_statement.else_body);
    }
    return NULL;
}

/**
 * @brief Execute a for statement node (stub).
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to context.
 * @param node For statement AST node.
 * @return Always NULL.
 */
value_t *eval_for_statement(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    (void)rt;
    (void)ctx;
    (void)node;
    return NULL;
}

/**
 * @brief Execute all statements inside a compound block sequentially.
 * Evaluates statements one by one. If any statement alters flow_state
 * (e.g. return, break, continue), statement execution halts immediately
 * and control returns to the caller.
 *
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context.
 * @param node Compound AST node.
 * @return Always NULL.
 */
value_t *eval_compound_statement(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    int count = node->value.compound.statement_count;
    for (int i = 0; i < count; i++) {
        /* Evaluate statement node sequentially */
        value_t *value = visitor_visit(rt, ctx, node->value.compound.statements[i]);
        if (value != NULL) {
            val_free(value);
        }

        /* Check control flow state: break statement loop if flow is interrupted */
        if (ctx->flow_state != FLOW_NORMAL) {
            break;
        }
    }
    return NULL; 
}

/**
 * @brief Execute a return statement node: return [expr];.
 * Evaluates the optional return expression, sets FLOW_RETURN in the active
 * context, and records the evaluated return_value pointer.
 *
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context.
 * @param node Return statement AST node.
 * @return Always NULL (result stored in ctx->return_value).
 */
value_t *eval_return_statement(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    /* Evaluate return expression or initialize void return value */
    value_t *ret_val = NULL;
    if (node->value.return_statement.value != NULL) {
        ret_val = visitor_visit(rt, ctx, node->value.return_statement.value);
    } else {
        ret_val = val_new_void();
    }

    /* Record return payload and signal FLOW_RETURN to halt further statement execution */
    ctx->flow_state = FLOW_RETURN;
    ctx->return_value = ret_val;
    return NULL;
}

/**
 * @brief Execute a break statement node: break;.
 * Sets FLOW_BREAK flag in the active context to signal termination of innermost loop.
 *
 * @param ctx Pointer to active execution context.
 * @param node Break statement AST node.
 * @return Always NULL.
 */
value_t *eval_break_statement(context_t *ctx, ast_t *node)
{
    (void)node;
    /* Signal loop break to terminate the innermost active loop */
    ctx->flow_state = FLOW_BREAK;
    return NULL;
}

/**
 * @brief Execute a continue statement node: continue;.
 * Sets FLOW_CONTINUE flag in the active context to jump to next loop iteration.
 *
 * @param ctx Pointer to active execution context.
 * @param node Continue statement AST node.
 * @return Always NULL.
 */
value_t *eval_continue_statement(context_t *ctx, ast_t *node)
{
    (void)node;
    /* Signal loop continue to advance to next iteration of active loop */
    ctx->flow_state = FLOW_CONTINUE;
    return NULL;
}
