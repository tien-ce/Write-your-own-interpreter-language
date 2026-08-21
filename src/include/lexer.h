#ifndef LEXER_H  
#define LEXER_H

#include "token.h"
typedef struct LEXER_STRUCT{
  char c; // Current character
  unsigned int i; // Current index 
  char *contents; // Pointer to the input buffer
} lexer_t;

lexer_t *init_lexer(char *str);
void lexer_advance(lexer_t *lexer); // Go ahead
void lexer_go_back(lexer_t *lexer);
void lexer_skip_whitespace(lexer_t *lexer); // Skip white space between tokens
char *lexer_get_current_char_as_string(lexer_t *lexer); // Get the character that lexer points to and then change it to dynamic string char* with null-terminated
token_t *lexer_get_next_token(lexer_t *lexer);
token_t *lexer_collect_string(lexer_t *lexer); // Collect string from current index and return string token
token_t *lexer_collect_id(lexer_t *lexer);
token_t *lexer_collect_number(lexer_t *lexer);
#endif // LEXER_H 
