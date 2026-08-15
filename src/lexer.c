#include "include/lexer.h"
#include "include/token.h"
#include<stdio.h>
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
    
    // If start with digit 
    if (isdigit(lexer->c))
    {
      return lexer_collect_number(lexer);
    }
    
    // If is alpla or _ (allowed for name)
    if(isalpha(lexer->c))
    {
      return lexer_collect_id(lexer);
    }

    switch (lexer->c) {
      case '=': return lexer_advance_with_token(lexer, init_token(TOKEN_EQUALS, NULL));
      case '(': return lexer_advance_with_token(lexer, init_token(TOKEN_LPAREN, NULL));
      case ')': return lexer_advance_with_token(lexer, init_token(TOKEN_RPAREN, NULL));
      case ';': return lexer_advance_with_token(lexer, init_token(TOKEN_SEMI, NULL));
      case '+': return lexer_advance_with_token(lexer, init_token(TOKEN_PLUS, NULL));
      case ',': return lexer_advance_with_token(lexer, init_token(TOKEN_COMMA, NULL));
      default:
        printf("[Lexer Error] Unxepected character %c\n", lexer->c);
        exit(1);
    }
  }
  return init_token(TOKEN_EOF,(void*)0);
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
  while(isalnum(lexer->c) || lexer->c == '_') // is alphanumeric or '_'
  {
     char *s = lexer_get_current_char_as_string(lexer);
     value =  realloc(value,strlen(value) + strlen(s) + 1); // allocate for concatnation 
     strcat(value,s);
     free(s);
     lexer_advance(lexer);
  }

  // Check KEYWORDS (type alone tells us the value, so don't keep it)
  if (strcmp(value, "int") == 0)      { free(value); return init_token(TOKEN_KW_INT, NULL); }
  if (strcmp(value, "float") == 0)    { free(value); return init_token(TOKEN_KW_FLOAT, NULL); }
  if (strcmp(value, "string") == 0)   { free(value); return init_token(TOKEN_KW_STRING, NULL); }
  if (strcmp(value, "if") == 0)       { free(value); return init_token(TOKEN_KW_IF, NULL); }
  if (strcmp(value, "while") == 0)    { free(value); return init_token(TOKEN_KW_WHILE, NULL); }
  if (strcmp(value, "return") == 0)   { free(value); return init_token(TOKEN_KW_RETURN, NULL); }

  // If not keywork (variable or function)
  return init_token(TOKEN_ID, value);
}

token_t *lexer_collect_number(lexer_t *lexer)
{
  char *value = calloc(1,sizeof(char));
  while(isdigit(lexer->c))
  {
    char *s = lexer_get_current_char_as_string(lexer);
    value = realloc(value,strlen(value) + strlen(s) + 1);
    strcat(value,s);
    lexer_advance(lexer);
  }
  if(lexer->c == '.')
  {
    // Float number 
    while(isdigit(lexer->c))
    {
      char *s = lexer_get_current_char_as_string(lexer);
      value = realloc(value,strlen(value) + strlen(s) + 1);
      strcat(value,s);
      lexer_advance(lexer);
    }
    if (isalpha(lexer->c) || lexer->c == '_')
    {
        printf("[Lexer Error] Invalid suffix '%c' on float constant '%s'\n", lexer->c, value);
        exit(1);
    }
    return init_token(TOKEN_FLOAT, value);
  }
  // Interger number
  if (isalpha(lexer->c) || lexer->c == '_')
  {
    printf("[lexer error] invalid suffix '%c' on float constant '%s'\n", lexer->c, value);
    exit(1);
  } 
  return init_token(TOKEN_INT, value);
}

char *lexer_get_current_char_as_string(lexer_t *lexer)
{
  char *str = calloc(2,sizeof(char));
  str[0] = lexer->c;
  str[1] = '\0';
  return str;
}
