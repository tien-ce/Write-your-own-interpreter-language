#include <stdio.h>
#include <stdlib.h>
#include "include/lexer.h"
int main(int argc, char* argv[])
{
  lexer_t *lexer = init_lexer("name = \"van anh\"\nprint(name);");
  token_t *token = (void*)0;
  while((token = lexer_get_next_token(lexer)) != (void*)0)
  {
    printf("TOKEN: %d, %s\n",token->type, token->value);
  }
  free((void*)lexer);
	return 0;
}
