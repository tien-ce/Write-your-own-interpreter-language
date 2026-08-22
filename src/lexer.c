#include "include/lexer.h"
#include "include/token.h"
#include "include/tracked_memory.h"
#include "TienInterpreter.h"
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* -------------------- Static Functions -------------------- */

/**
 * @brief Advance the lexer and return the given token.
 */
static token_t *lexer_advance_with_token(lexer_t *lexer, token_t *token)
{
  lexer_advance(lexer);
  return token;
}

/* -------------------- Public Functions -------------------- */

lexer_t *init_lexer(char *str) 
{
  lexer_t *lexer = tracked_calloc(1, sizeof(struct LEXER_STRUCT));
  lexer->contents = str;
  lexer->i = 0;
  lexer->c = lexer->contents[lexer->i];
  return lexer;
}

void lexer_go_back(lexer_t *lexer)
{
  if(lexer->i != 0)
  {
    lexer->i --;
    lexer->c = lexer->contents[lexer->i];
  }
}

void lexer_advance(lexer_t *lexer)
{
  uint32_t length = strlen(lexer->contents); 
  if(lexer->c != '\0' && lexer->i < length)
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
  while(lexer->c != '\0' && lexer->c != EOF && lexer->c != '\000')
  {
    if (lexer->c == ' ' || lexer ->c == '\n')
    {
      lexer_skip_whitespace(lexer);
      continue;
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
    
    // If is alpha or _ (allowed for name)
    if(isalpha(lexer->c))
    {
      return lexer_collect_id(lexer);
    }

    switch (lexer->c) {
      case '(': return lexer_advance_with_token(lexer, init_token(TOKEN_LPAREN, NULL));
      case ')': return lexer_advance_with_token(lexer, init_token(TOKEN_RPAREN, NULL));
      case ';': return lexer_advance_with_token(lexer, init_token(TOKEN_SEMI, NULL));
      case '+': return lexer_advance_with_token(lexer, init_token(TOKEN_PLUS, NULL));
      case ',': return lexer_advance_with_token(lexer, init_token(TOKEN_COMMA, NULL));
      case '=':
      {
          lexer_advance(lexer);
          if(lexer->c == '=')
          {
              return lexer_advance_with_token(lexer, init_token(TOKEN_DEQUALS, NULL));
          }
          lexer_go_back(lexer);
          return lexer_advance_with_token(lexer, init_token(TOKEN_EQUALS, NULL));
      }
      case '!':
      {
          lexer_advance(lexer);
          if(lexer->c == '=')
          {
              return lexer_advance_with_token(lexer, init_token(TOKEN_NOT_EQUALS, NULL));
          }
          lexer_go_back(lexer);
          return lexer_advance_with_token(lexer, init_token(TOKEN_NOT ,NULL));
      }
      case '<':
      {
        lexer_advance(lexer);
        if (lexer->c == '=')
        {
          return lexer_advance_with_token(lexer,  init_token(TOKEN_LTE,NULL));
        }
        lexer_go_back(lexer);
        return lexer_advance_with_token(lexer,  init_token(TOKEN_LT,NULL));
      }
      case '>':
      {
        lexer_advance(lexer);
        if (lexer->c == '=')
        {
          return lexer_advance_with_token(lexer,  init_token(TOKEN_GTE,NULL));
        }
        lexer_go_back(lexer);
        return lexer_advance_with_token(lexer,  init_token(TOKEN_GT,NULL));
      }
      case '{': return lexer_advance_with_token(lexer, init_token(TOKEN_LBRACE, NULL));
      case '}': return lexer_advance_with_token(lexer, init_token(TOKEN_RBRACE, NULL));
      default:
        ti_log("[Lexer Error] Unexpected character %c\n", lexer->c);
        ti_fatal();
    }
  }
  return init_token(TOKEN_EOF,(void*)0);
}

token_t *lexer_collect_string(lexer_t *lexer)
{        
  lexer_advance(lexer); // go through open quote
  char *value = tracked_calloc(1, sizeof(char));
  value[0] = '\0';
  while(lexer->c != '"')
  {
     if (lexer->c == '\\')
     {
       lexer_advance(lexer);
       switch(lexer->c)
       {
         case 'n':
           lexer->c = '\n';
           break;
        case 't':
           lexer->c = '\t';
           break;
         case 'r':
           lexer->c = '\r';
           break;
       }
     }
     char *s = lexer_get_current_char_as_string(lexer);
     value =  tracked_realloc(value,strlen(value) + strlen(s) + 1); // allocate for s
     strcat(value,s);
     tracked_free(s);
     lexer_advance(lexer);
  }
  lexer_advance(lexer); // Skip close quote
  return init_token(TOKEN_STRING, value);
}

token_t *lexer_collect_id(lexer_t *lexer)
{
  char *value = tracked_calloc(1,sizeof(char));
  while(isalnum(lexer->c) || lexer->c == '_') // is alphanumeric or '_'
  {
     char *s = lexer_get_current_char_as_string(lexer);
     value =  tracked_realloc(value,strlen(value) + strlen(s) + 1); // allocate for concatenation
     strcat(value,s);
     tracked_free(s);
     lexer_advance(lexer);
  }

  // Check KEYWORDS (type alone tells us the value, so don't keep it)
  if (strcmp(value, "int") == 0)      { tracked_free(value); return init_token(TOKEN_KW_INT, NULL); }
  if (strcmp(value, "float") == 0)    { tracked_free(value); return init_token(TOKEN_KW_FLOAT, NULL); }
  if (strcmp(value, "string") == 0)   { tracked_free(value); return init_token(TOKEN_KW_STRING, NULL); }
  if (strcmp(value, "if") == 0)       { tracked_free(value); return init_token(TOKEN_KW_IF, NULL); }
  if (strcmp(value, "else") == 0)     { tracked_free(value); return init_token(TOKEN_KW_ELSE, NULL); }
  if (strcmp(value, "while") == 0)    { tracked_free(value); return init_token(TOKEN_KW_WHILE, NULL); }
  if (strcmp(value, "return") == 0)   { tracked_free(value); return init_token(TOKEN_KW_RETURN, NULL); }

  // If not keyword (variable or function)
  return init_token(TOKEN_ID, value);
}

token_t *lexer_collect_number(lexer_t *lexer)
{
  char *value = tracked_calloc(1,sizeof(char));
  while(isdigit(lexer->c))
  {
    char *s = lexer_get_current_char_as_string(lexer);
    value = tracked_realloc(value,strlen(value) + strlen(s) + 1);
    strcat(value,s);
    lexer_advance(lexer);
  }
  if(lexer->c == '.')
  {
    // Float number
    while(isdigit(lexer->c))
    {
      char *s = lexer_get_current_char_as_string(lexer);
      value = tracked_realloc(value,strlen(value) + strlen(s) + 1);
      strcat(value,s);
      lexer_advance(lexer);
    }
    if (isalpha(lexer->c) || lexer->c == '_')
    {
        ti_log("[Lexer Error] Invalid suffix '%c' on float constant '%s'\n", lexer->c, value);
        ti_fatal();
    }
    return init_token(TOKEN_FLOAT, value);
  }
  // Integer number
  if (isalpha(lexer->c) || lexer->c == '_')
  {
    ti_log("[Lexer Error] Invalid suffix '%c' on integer constant '%s'\n", lexer->c, value);
    ti_fatal();
  } 
  return init_token(TOKEN_INT, value);
}

char *lexer_get_current_char_as_string(lexer_t *lexer)
{
  char *str = tracked_calloc(2,sizeof(char));
  str[0] = lexer->c;
  str[1] = '\0';
  return str;
}
