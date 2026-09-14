#include "include/visitor_internal.h"
#include "include/AST.h"
#include "TienInterpreter.h"
#include <stdio.h>

/**
 * @brief Master AST recursive evaluator and dispatcher.
 * Routes each AST node to its specialized evaluator module.
 *
 * @param ctx Pointer to active execution context scope.
 * @param node Pointer to AST node to evaluate.
 * @return Pointer to evaluated result value_t (or NULL).
 */
value_t *visitor_visit(InterpreterContext *ctx, ast_t *node)
{
    if (!node) {
        return NULL;
    }

    switch (node->type) {
    case AST_FUNCTION_CALL:
        return eval_function_call(ctx, node);
    case AST_COMPOUND:
        return eval_compound_statement(ctx, node);
    case AST_VARIABLE_DEFINITION:
        return eval_variable_definition(ctx, node);
    case AST_ASSIGNMENT:
        return eval_assignment(ctx, node);
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
        return eval_binary_expr(ctx, node);
    case AST_UNARY_EXPR:
        return eval_unary_expr(ctx, node);
    case AST_WHILE_STATEMENT:
        return eval_while_statement(ctx, node);
    case AST_IF_STATEMENT:
        return eval_if_statement(ctx, node);
    case AST_FUNCTION_DEFINITION:
        return eval_function_definition(ctx, node);
    case AST_FOR_STATEMENT:
        return eval_for_statement(ctx, node);
    default:
        ti_log("[Visitor Error] Unexpected AST node type %d in visitor_visit\n", (int)node->type);
        break;
    }
    return NULL;
}
