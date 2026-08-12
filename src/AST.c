#include "include/AST.h"
#include <stdlib.h>

ast_t *init_ast(int type)
{
  ast_t *ast = calloc(1, sizeof(struct AST_STRUCT));
  ast->type = type;
  /* AST_VARIABLE_DEFINITON */ 
  char *variable_definition_variable_name = (void*)0;
  struct AST_STRUCT *variable_definition_value = (void*)0;

  /* AST_VARIABLE */ 
  char *variable = (void*)0;
  
  /* AST_FUNCTION_CALL */ 
  char *function_call_name = (void*)0;
  struct AST_STRUCT **function_call_arguments = (void*)0; // An array coinstain AST pointers
  size_t function_call_arguments_size = 0;

  /* AST_STRING */ 
  char *string_value = (void*)0;

  /* AST_COMPUND */ 
  struct AST_STRUCT **compound_value = (void*)0; // An array coinstain AST pointers point to statements
  size_t compound_size = 0;

  return ast;
}
