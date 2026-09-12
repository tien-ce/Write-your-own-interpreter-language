#ifndef LEXER_H
#define LEXER_H

#include "token.h"

/* -------------------- Lexer Structure -------------------- */

typedef struct LEXER_STRUCT {
    char c;                 // Current character
    unsigned int i;         // Current index
    unsigned int line_num;  // Current line number (1-based)
    char *contents;         // Pointer to input buffer
    char *line;             // Pointer to start of current line
} lexer_t;

/* -------------------- Public Functions -------------------- */

/**
 * @brief Initialize a new lexer for the given source string.
 * @param str Source code buffer.
 * @return Pointer to newly allocated lexer_t.
 */
lexer_t *lexer_init(char *str);

/** 
 * @brief Create a shallow copy of the given lexer state.
 * @param lexer Lexer to clone.
 * @return Pointer to cloned lexer_t.
 */
lexer_t *lexer_copy(lexer_t *lexer);

/**
 * @brief Tokenize and return the next token from source.
 * @param lexer Pointer to active lexer instance.
 * @return Pointer to next token_t.
 */
token_t *lexer_get_next_token(lexer_t *lexer);

#endif /* !LEXER_H */
