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
 * @return Result value_t for expressions, TI_VAL_OK for successful statements, NULL on failure.
 */
value_t *visitor_visit(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    /*
     * No cancellation/error check per node: failure travels as a NULL result (statements return
     * TI_VAL_OK on success), and cancellation is noticed at the safe points.
     */
    if (!node) {
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
    case AST_DICT_LITERAL:
        return eval_dict_literal(rt, ctx, node);
    case AST_LIST_LITERAL:
        return eval_list_literal(rt, ctx, node);
    case AST_BOOLEAN:
        return eval_boolean_literal(ctx, node);
    case AST_IDENTIFIER:
        return eval_identifier(rt, ctx, node);
    case AST_ARRAY_ACCESS:
        return eval_array_access(rt, ctx, node);
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
        ti_raise(rt, TI_ERR_INTERNAL, node->line, "Unexpected AST node type %d in visitor_visit", (int)node->type);
        break;
    }
    return NULL;
}
