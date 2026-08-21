#ifndef LEXER_H  
#define LEXER_H

#include "token.h"

/* -------------------- Lexer Structure -------------------- */

typedef struct LEXER_STRUCT {
  char c;           // Current character
  unsigned int i;   // Current index 
  char *contents;   // Pointer to input buffer
} lexer_t;

/* -------------------- Public Functions -------------------- */

/**
 * @brief Initialize a new lexer for the given source string.
 * @param str Source code buffer.
 * @return Pointer to newly allocated lexer_t.
 */
lexer_t *init_lexer(char *str);

/**
 * @brief Advance the lexer by one character.
 */
void lexer_advance(lexer_t *lexer);

/**
 * @brief Step back the lexer by one character.
 */
void lexer_go_back(lexer_t *lexer);

/**
 * @brief Skip whitespace characters in the source.
 */
void lexer_skip_whitespace(lexer_t *lexer);

/**
 * @brief Return current character as a newly allocated null-terminated string.
 */
char *lexer_get_current_char_as_string(lexer_t *lexer);

/**
 * @brief Tokenize and return the next token from source.
 * @return Pointer to next token_t.
 */
token_t *lexer_get_next_token(lexer_t *lexer);

/**
 * @brief Collect a string literal enclosed in double quotes.
 */
token_t *lexer_collect_string(lexer_t *lexer);

/**
 * @brief Collect an identifier or keyword token.
 */
token_t *lexer_collect_id(lexer_t *lexer);

/**
 * @brief Collect an integer or float literal token.
 */
token_t *lexer_collect_number(lexer_t *lexer);

#endif // LEXER_H 
