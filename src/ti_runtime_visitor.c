#include "include/ti_runtime_visitor.h"
#include "include/ti_type_ast.h"
#include "include/ti_runtime.h"
#include "TienInterpreter.h"
#include <stdio.h>

/**
 * @brief Master AST recursive evaluator and dispatcher.
 * Routes each AST node to its specialized evaluator module.
 *
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context scope.
 * @param node Pointer to AST node to evaluate.
 * @return Pointer to evaluated result value_t (or NULL).
 */
value_t *visitor_visit(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    /* Immediate short-circuit if node is NULL or execution has been cancelled from outside */
    if (!node || (rt != NULL && rt->is_interrupted)) {
        return NULL;
    }

    switch (node->type) {
    case AST_FUNCTION_CALL:
        return eval_function_call(rt, ctx, node);
    case AST_COMPOUND:
        return eval_compound_statement(rt, ctx, node);
    case AST_VARIABLE_DEFINITION:
        return eval_variable_definition(rt, ctx, node);
    case AST_ASSIGNMENT:
        return eval_assignment(rt, ctx, node);
    case AST_STRING_LITERAL:
        return eval_string_literal(ctx, node);
    case AST_INT_LITERAL:
        return eval_int_literal(ctx, node);
    case AST_FLOAT_LITERAL:
        return eval_float_literal(ctx, node);
    case AST_BOOLEAN:
        return eval_boolean_literal(ctx, node);
    case AST_IDENTIFIER:
        return eval_identifier(ctx, node);
    case AST_BINARY_EXPR:
        return eval_binary_expr(rt, ctx, node);
    case AST_UNARY_EXPR:
        return eval_unary_expr(rt, ctx, node);
    case AST_WHILE_STATEMENT:
        return eval_while_statement(rt, ctx, node);
    case AST_IF_STATEMENT:
        return eval_if_statement(rt, ctx, node);
    case AST_FUNCTION_DEFINITION:
        return eval_function_definition(rt, ctx, node);
    case AST_FOR_STATEMENT:
        return eval_for_statement(rt, ctx, node);
    case AST_RETURN_STATEMENT:
        return eval_return_statement(rt, ctx, node);
    case AST_BREAK_STATEMENT:
        return eval_break_statement(ctx, node);
    case AST_CONTINUE_STATEMENT:
        return eval_continue_statement(ctx, node);
    default:
        ti_log("[Visitor Error] Unexpected AST node type %d in visitor_visit\n", (int)node->type);
        break;
    }
    return NULL;
}
