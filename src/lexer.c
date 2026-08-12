#include "include/lexer.h"
#include "include/token.h"
#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static token_t *lexer_advance_with_token(lexer_t *lexer, token_t *token)
{
  lexer_advance(lexer);
  return token;
}

lexer_t *init_lexer(char *str) 
{
  lexer_t *lexer = calloc(1, sizeof(struct LEXER_STRUCT));
  lexer->contents = str;
  lexer->i = 0;
  lexer->c = lexer->contents[lexer->i];
  return lexer;
}
void lexer_advance(lexer_t *lexer)
{
  uint32_t lenght = strlen(lexer->contents); 
  if(lexer->c != '\0' && lexer->i < lenght)
  {
      lexer->i ++;
      lexer->c = lexer->contents[lexer->i];
  }
}
void lexer_skip_whitespace(lexer_t *lexer)
{
  while(lexer->c == ' ' || lexer->c == '\n')
  {
    lexer_advance(lexer);
  }
}
token_t *lexer_get_next_token(lexer_t *lexer)
{
  while(lexer->c != '\0')
  {
    if (lexer->c == ' ' || lexer ->c == '\n')
    {
      lexer_skip_whitespace(lexer);
    }
    
    // If get the start of string
    if (lexer->c == '"')
    {
      return lexer_collect_string(lexer);
    }
    
    if (isalnum(lexer->c))
    {
      return lexer_collect_id(lexer);
    }
    
    switch (lexer->c) {
      case '=': return lexer_advance_with_token(lexer, init_token(TOKEN_EQUALS, lexer_get_current_char_as_string(lexer)));
      case '(': return lexer_advance_with_token(lexer, init_token(TOKEN_LPAREN, lexer_get_current_char_as_string(lexer)));
      case ')': return lexer_advance_with_token(lexer, init_token(TOKEN_RPAREN, lexer_get_current_char_as_string(lexer)));
      case ';': return lexer_advance_with_token(lexer, init_token(TOKEN_SEMI, lexer_get_current_char_as_string(lexer)));
    }
  }
  return (void*)0;
}

token_t *lexer_collect_string(lexer_t *lexer)
{        
  lexer_advance(lexer); // go thorugh open quote
  char *value = calloc(1, sizeof(char));
  value[0] = '\0';
  while(lexer->c != '"')
  {
     char *s = lexer_get_current_char_as_string(lexer);
     value =  realloc(value,strlen(value) + strlen(s) + 1); // allocate for s 
     strcat(value,s);
     free(s);
     lexer_advance(lexer);
  }
  lexer_advance(lexer); // Skip close quote
  return init_token(TOKEN_STRING, value);
}

token_t *lexer_collect_id(lexer_t *lexer)
{
  char *value = calloc(1,sizeof(char));
  while(isalnum(lexer->c)) // is alphanumeric
  {
     char *s = lexer_get_current_char_as_string(lexer);
     value =  realloc(value,strlen(value) + strlen(s) + 1); // allocate for concatnation 
     strcat(value,s);
     free(s);
     lexer_advance(lexer);
  }
  return init_token(TOKEN_ID, value);
}

char *lexer_get_current_char_as_string(lexer_t *lexer)
{
  char *str = calloc(2,sizeof(char));
  str[0] = lexer->c;
  str[1] = '\0';
  return str;
}
