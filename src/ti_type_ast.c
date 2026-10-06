#include "include/ti_type_ast.h"
#include "include/tracked_memory.h"
#include "TienInterpreter.h"
#include <stdlib.h>
#include <stdio.h>

/* -------------------- Public Functions -------------------- */

/* Initialize AST node */
ast_t *ast_init(alloc_hdr_t **list, int type, int line)
{
    /* Allocate zero-initialized AST node container tracked on allocation list */
    ast_t *ast = tracked_calloc(list, 1, sizeof(struct AST_STRUCT));
    if (!ast) {
        return NULL;
    }
    /* Set node classification type tag and source line for error reporting */
    ast->type = type;
    ast->line = line;
    return ast;
}

/* Free AST node recursively */
void ast_free(ast_t *ast)
{
    if (!ast) {
        return;
    }

    /* Recursively free child nodes and dynamic payload buffers by node type */
    switch (ast->type) {
    case AST_COMPOUND:
        /* Free all nested statements and statement pointer array */
        for (int i = 0; i < ast->value.compound.statement_count; i++) {
            ast_free(ast->value.compound.statements[i]);
        }
        tracked_free(NULL, ast->value.compound.statements);
        break;

    case AST_VARIABLE_DEFINITION:
        /* Free variable name string and initializer expression */
        if (ast->value.variable_definition.variable_name) {
            tracked_free(NULL, ast->value.variable_definition.variable_name);
        }
        ast_free(ast->value.variable_definition.value);
        break;

    case AST_FUNCTION_DEFINITION: {
        /* Free parameter nodes, parameter pointer array, function name string, and body */
        int param_count = ast->value.function_definition.param_count;
        for (int i = 0; i < param_count; i++) {
            ast_free(ast->value.function_definition.params[i]);
        }
        if (ast->value.function_definition.params) {
            tracked_free(NULL, ast->value.function_definition.params);
        }
        if (ast->value.function_definition.func_name) 
        {
            tracked_free(NULL, ast->value.function_definition.func_name);
        }
        ast_free(ast->value.function_definition.body);
        break;
    }

    case AST_PARAM:
        /* Free parameter name string */
        if (ast->value.param.param_name) {
            tracked_free(NULL, ast->value.param.param_name);
        }
        break;

    case AST_FUNCTION_CALL: {
        /* Free target function name string, argument expression nodes, and args array */
        if (ast->value.function_call.func_name) {
            tracked_free(NULL, ast->value.function_call.func_name);
        }
        int arg_count = ast->value.function_call.arg_count;
        for (int i = 0; i < arg_count; i++) {
            ast_free(ast->value.function_call.args[i]);
        }
        tracked_free(NULL, ast->value.function_call.args);
        break;
    }

    case AST_BINARY_EXPR:
        /* Free left and right binary operand subtrees */
        ast_free(ast->value.binary_expr.left);
        ast_free(ast->value.binary_expr.right);
        break;

    case AST_UNARY_EXPR:
        /* Free unary operand subtree */
        ast_free(ast->value.unary_expr.operand);
        break;

    case AST_LIST_LITERAL:
        /* Free every element expression and the element pointer array */
        for (int i = 0; i < ast->value.list_literal.element_count; i++) {
            ast_free(ast->value.list_literal.elements[i]);
        }
        tracked_free(NULL, ast->value.list_literal.elements);
        break;

    case AST_ASSIGNMENT:
        /* Free assignment target and assigned value expression */
        ast_free(ast->value.assignment.target);
        ast_free(ast->value.assignment.value);
        break;

    case AST_BYTES_LITERAL:
        /* Free decoded bytes buffer */
        tracked_free(NULL, ast->value.bytes_literal.data);
        break;

    case AST_STRING_LITERAL:
        /* Free string literal text payload */
        tracked_free(NULL, ast->value.string_value);
        break;

    case AST_IDENTIFIER:
        /* Free identifier name string */
        tracked_free(NULL, ast->value.identifier);
        break;

    case AST_ARRAY_ACCESS:
        /* Free array identifier name string and index expression */
        if (ast->value.array_access.id) {
            tracked_free(NULL, ast->value.array_access.id);
        }
        ast_free(ast->value.array_access.index_expr);
        break;

    case AST_FOR_STATEMENT:
        /* Free the optional header parts and the body compound block */
        ast_free(ast->value.for_statement.init);
        ast_free(ast->value.for_statement.condition);
        ast_free(ast->value.for_statement.step);
        ast_free(ast->value.for_statement.body);
        break;

    case AST_WHILE_STATEMENT:
        /* Free loop condition and body compound block */
        ast_free(ast->value.while_statement.condition);
        ast_free(ast->value.while_statement.body);
        break;

    case AST_IF_STATEMENT:
        /* Free if condition, true body, and optional else body */
        ast_free(ast->value.if_statement.condition);
        ast_free(ast->value.if_statement.body);
        ast_free(ast->value.if_statement.else_body);
        break;
    case AST_RETURN_STATEMENT:
        /* Free return expression if present */
        ast_free(ast->value.return_statement.value);
        break;

    case AST_INT_LITERAL:
    case AST_FLOAT_LITERAL:
    case AST_BOOLEAN:
    case AST_NOOP:
    case AST_PROGRAM:
    case AST_BREAK_STATEMENT:
    case AST_CONTINUE_STATEMENT:
        /* Atomic literal or statement nodes with no suballocations */
        break;

    default:
        ti_log("[Error]: AST Free with unexpected type: %d\n", ast->type);
        break;
    }

    /* Free the node container itself */
    tracked_free(NULL, ast);
}

/* Deep-copy an expression AST node */
ast_t *ast_clone(alloc_hdr_t **list, const ast_t *ast)
{
    if (!ast) {
        return NULL;
    }

    ast_t *copy = ast_init(list, ast->type, ast->line);
    if (!copy) {
        return NULL;
    }

    switch (ast->type) {
    case AST_INT_LITERAL:
    case AST_FLOAT_LITERAL:
    case AST_BOOLEAN:
        /* Plain scalar payloads copy by value */
        copy->value = ast->value;
        return copy;

    case AST_STRING_LITERAL:
        copy->value.string_value = tracked_strdup(list, ast->value.string_value);
        if (!copy->value.string_value) {
            break;
        }
        return copy;

    case AST_IDENTIFIER:
        copy->value.identifier = tracked_strdup(list, ast->value.identifier);
        if (!copy->value.identifier) {
            break;
        }
        return copy;

    case AST_ARRAY_ACCESS:
        copy->value.array_access.id = tracked_strdup(list, ast->value.array_access.id);
        copy->value.array_access.index_expr = ast_clone(list, ast->value.array_access.index_expr);
        if (!copy->value.array_access.id || !copy->value.array_access.index_expr) {
            break;
        }
        return copy;

    case AST_BINARY_EXPR:
        copy->value.binary_expr.op = ast->value.binary_expr.op;
        copy->value.binary_expr.left = ast_clone(list, ast->value.binary_expr.left);
        copy->value.binary_expr.right = ast_clone(list, ast->value.binary_expr.right);
        if (!copy->value.binary_expr.left || !copy->value.binary_expr.right) {
            break;
        }
        return copy;

    case AST_UNARY_EXPR:
        copy->value.unary_expr.op = ast->value.unary_expr.op;
        copy->value.unary_expr.operand = ast_clone(list, ast->value.unary_expr.operand);
        if (!copy->value.unary_expr.operand) {
            break;
        }
        return copy;

    case AST_FUNCTION_CALL: {
        int arg_count = ast->value.function_call.arg_count;
        copy->value.function_call.func_name = tracked_strdup(list, ast->value.function_call.func_name);
        copy->value.function_call.args = tracked_calloc(list, arg_count > 0 ? arg_count : 1, sizeof(struct AST_STRUCT *));
        if (!copy->value.function_call.func_name || !copy->value.function_call.args) {
            break;
        }
        /* Count grows as children are cloned so a partial copy can be freed safely */
        for (int i = 0; i < arg_count; i++) {
            copy->value.function_call.args[i] = ast_clone(list, ast->value.function_call.args[i]);
            if (!copy->value.function_call.args[i]) {
                break;
            }
            copy->value.function_call.arg_count++;
        }
        if (copy->value.function_call.arg_count != arg_count) {
            break;
        }
        return copy;
    }

    default:
        /* Statement, definition and container-literal nodes are never cloned */
        tracked_free(NULL, copy); /* bare node: no children to release */
        return NULL;
    }

    /* Failure: release the partial copy (ast_free tolerates NULL children) */
    ast_free(copy);
    return NULL;
}
