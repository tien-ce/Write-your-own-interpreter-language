#include "include/AST.h"
#include <stdlib.h>

ast_t *init_ast(int type)
{
  ast_t *ast = calloc(1, sizeof(struct AST_STRUCT));
  ast->type = type;
  /* AST_VARIABLE_DEFINITON */ 
  return ast;
}
