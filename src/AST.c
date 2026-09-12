#include "include/AST.h"
#include "include/tracked_memory.h"
#include "TienInterpreter.h"
#include <stdlib.h>
#include <stdio.h>

/* -------------------- Public Functions -------------------- */

/* Initialize AST node */
ast_t *ast_init(int type)
{
    ast_t *ast = tracked_calloc(1, sizeof(struct AST_STRUCT));
    ast->type = type;
    return ast;
}

/* Free AST node recursively */
void ast_free(ast_t *ast)
{
    if (!ast) {
        return;
    }

    switch (ast->type) {
    case AST_COMPOUND:
        for (int i = 0; i < ast->value.compound.compound_size; i++) {
            ast_free(ast->value.compound.compound_value[i]);
        }
        tracked_free(ast->value.compound.compound_value);
        break;

    case AST_VARIABLE_DEFINITION:
        if (ast->value.variable_definition.variable_name) {
            tracked_free(ast->value.variable_definition.variable_name);
        }
        ast_free(ast->value.variable_definition.value);
        break;

    case AST_FUNCTION_DEFINITION: {
        int argc = ast->value.function_definition.num_args;
        for (int i = 0; i < argc; i++) {
            ast_free(ast->value.function_definition.args[i]);
        }
        if (ast->value.function_definition.args) {
            tracked_free(ast->value.function_definition.args);
        }
        ast_free(ast->value.function_definition.body);
        break;
    }

    case AST_FUNCTION_CALL: {
        if (ast->value.function_call.func) {
            tracked_free(ast->value.function_call.func);
        }
        int argc = ast->value.function_call.num_arg;
        for (int i = 0; i < argc; i++) {
            ast_free(ast->value.function_call.args[i]);
        }
        tracked_free(ast->value.function_call.args);
        break;
    }

    case AST_BINARY_EXPR:
        ast_free(ast->value.binary_expr.left);
        ast_free(ast->value.binary_expr.right);
        break;

    case AST_UNARY_EXPR:
        ast_free(ast->value.unary_expr.operand);
        break;

    case AST_ASSIGNMENT:
        ast_free(ast->value.assignment.id);
        ast_free(ast->value.assignment.value);
        break;

    case AST_STRING_LITERAL:
        tracked_free(ast->value.string_value);
        break;

    case AST_IDENTIFIER:
        tracked_free(ast->value.identifier);
        break;

    case AST_ARRAY_ACCESS:
        if (ast->value.array_access.id) {
            tracked_free(ast->value.array_access.id);
        }
        ast_free(ast->value.array_access.index_expr);
        break;

    case AST_WHILE_STATEMENT:
        ast_free(ast->value.while_statement.condition);
        ast_free(ast->value.while_statement.body);
        break;

    case AST_IF_STATEMENT:
        ast_free(ast->value.if_statement.condition);
        ast_free(ast->value.if_statement.body);
        ast_free(ast->value.if_statement.else_body);
        break;

    case AST_INT_LITERAL:
    case AST_FLOAT_LITERAL:
    case AST_BOOLEAN:
    case AST_NOOP:
    case AST_PROGRAM:
    case AST_FOR_STATEMENT:
    case AST_RETURN_STATEMENT:
        break;

    default:
        ti_log("[Error]: AST Free with unexpected type: %d\n", ast->type);
        break;
    }

    tracked_free(ast);
}
