#include <stdio.h>
#include <stdlib.h>
#include "include/lexer.h"
#include "include/parser.h"

/*
 * AST for name.ti:
 *
 *   print("I love" + get_my_love() + "\n");
 *
 * root
 * AST_COMPOUND (compound.compound_size = 1)
 * |
 * +--[0] AST_FUNCTION_CALL                          (statement 1)
 *        function_call.func ---> AST_IDENTIFIER, identifier = "print"
 *        function_call.num_arg = 1
 *        function_call.args[0] ---> AST_BINARY_EXPR (op = OP_ADD)
 *                                   |
 *                                   +-- left  ---> AST_BINARY_EXPR (op = OP_ADD)
 *                                   |             |
 *                                   |             +-- left  ---> AST_STRING_LITERAL
 *                                   |             |             string_value = "I love"
 *                                   |             |
 *                                   |             +-- right ---> AST_FUNCTION_CALL
 *                                   |                           function_call.func ---> AST_IDENTIFIER
 *                                   |                                                   identifier = "get_my_love"
 *                                   |                           function_call.num_arg = 0
 *                                   |
 *                                   +-- right ---> AST_STRING_LITERAL
 *                                                 string_value = "\n"
 *
 * ("I love" + get_my_love()) + "\n"  -- '+' is left-associative
 *
 * NOTE: this tree is the TARGET shape, not what the parser produces
 * today. parser_parse_expr() still has no case for TOKEN_PLUS / no
 * AST_BINARY_EXPR construction, so parsing this file currently stops
 * with a clean "Unexpected value +" error right after "I love" — that
 * is expected until binary-expression (precedence climbing) parsing
 * is added.
 */
int main(int argc, char* argv[])
{
  lexer_t *lexer = init_lexer("print(\"I love\" + get_my_love() + \"\\n\");");
  token_t *token = (void*)0;
  parser_t *parser = init_parser(lexer);
  ast_t *root = parser_parse(parser);
  printf("%d\n",root->type);
  printf("%d\n",root->value.compound.compound_size);
  free((void*)lexer);
	return 0;
}
