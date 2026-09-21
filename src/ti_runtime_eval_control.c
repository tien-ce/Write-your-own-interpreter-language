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
    value_t *value = visitor_visit(rt, ctx, cond_node);
    if (!value || value->type != VAL_BOOL) {
        ti_log("[Runtime Error] Unexpected type %d, only expect bool value at line %d\n", value ? (int)value->type : -1, cond_node->line);
        ti_fatal();
    }
    

    bool res = value->bool_val;
    val_free_internal(&rt->alloc_list, value);
    tracked_free(&rt->alloc_list, value);
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

    /* 1. Create new local scope */
    context_t *local_ctx = context_init(&rt->alloc_list);
    local_ctx->parent = parent_ctx;

    /* 2. Execute statements */
    value_t *capture = visitor_visit(rt, local_ctx, body_node);
    if (capture != NULL) {
        ti_log("[Warning]: Compound return value, please check it\n");
        val_free_internal(&rt->alloc_list, capture);
        tracked_free(&rt->alloc_list, capture);
    }

    /* 3. Propagate control flow state and return payload to parent context */
    if (local_ctx->flow_state != FLOW_NORMAL) {
        parent_ctx->flow_state = local_ctx->flow_state;
        parent_ctx->return_value = local_ctx->return_value;
        local_ctx->return_value = NULL; /* Transfer ownership to parent context */
    }

    /* 4. Free local scope */
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
    while (eval_boolean_condition(rt, ctx, node->value.while_statement.condition)) {
        if (rt != NULL && rt->is_interrupted) {
            break;
        }
        visitor_execute_body(rt, ctx, node->value.while_statement.body);
        /* Capture the follow flag */
        if (ctx->flow_state == FLOW_BREAK)
        {
            /* Consume the break */
            ctx->flow_state = FLOW_NORMAL;
            break; 
        }
        else if (ctx->flow_state == FLOW_CONTINUE)
        {
            /* Consume the continue */
            ctx->flow_state = FLOW_NORMAL;
            continue; 
        }
        else if (ctx->flow_state == FLOW_RETURN)
        {
            /* Reserve the signal */
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
        value_t *value = visitor_visit(rt, ctx, node->value.compound.statements[i]);
        if (value != NULL) {
            val_free_internal(&rt->alloc_list, value);
            tracked_free(&rt->alloc_list, value);
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
    value_t *ret_val = NULL;
    if (node->value.return_statement.value != NULL) {
        ret_val = visitor_visit(rt, ctx, node->value.return_statement.value);
    } else {
        ret_val = val_new_void(&rt->alloc_list);
    }

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
    ctx->flow_state = FLOW_CONTINUE;
    return NULL;
}
