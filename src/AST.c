#include "include/AST.h"
#include <stdlib.h>
#include <stdio.h>

ast_t *init_ast(int type)
{
  ast_t *ast = calloc(1, sizeof(struct AST_STRUCT));
  ast->type = type;
  /* AST_VARIABLE_DEFINITON */ 
  return ast;
}

void free_ast(ast_t *ast)
{
  if(!ast)
    return;
  switch(ast->type)
  {
    case AST_COMPOUND:
        for(int i = 0; i < ast->value.compound.compound_size; i ++)
        {
          free_ast(ast->value.compound.compound_value[i]);
        }
        free(ast->value.compound.compound_value);
        break;
    case AST_VARIABLE_DEFINITION:
        if(ast->value.variable_definition.variable_name)
          free(ast->value.variable_definition.variable_name);
        free_ast(ast->value.variable_definition.value);
        break;
    case AST_FUNCTION_CALL:
        if(ast->value.function_call.func)
          free(ast->value.function_call.func);
        int argc = ast->value.function_call.num_arg;
        for(int i = 0; i < argc; i++)
          free_ast(ast->value.function_call.args[i]);
        free(ast->value.function_call.args);
        break;
    case AST_BINARY_EXPR:
        free_ast(ast->value.binary_expr.left);
        free_ast(ast->value.binary_expr.right);
        break;
    case AST_UNARY_EXPR:
        free_ast(ast->value.unary_expr.operand);
        break;
    case AST_ASSIGNMENT:
        free_ast(ast->value.assignment.id);
        free_ast(ast->value.assignment.value);
        break;
    case AST_STRING_LITERAL:
        free(ast->value.string_value);
        break;
    case AST_IDENTIFIER:
        free(ast->value.identifier);
        break;
    default:
        printf("[Error]: AST Free with unexpected type: %d", ast->type);
        break;
  }
  free(ast); 
}
