#ifndef PARSER_H
#define PARSER_H

#include "token.h"
#include "AST.h"
#include "lexer.h"

/* -------------------- Parser Structure -------------------- */

typedef struct PARSER_STRUCT {
    lexer_t *lexer;
    token_t *current_token;
} parser_t;

/* -------------------- Public Functions -------------------- */

/**
 * @brief Initialize a new parser using the given lexer.
 * @param lexer Pointer to initialized lexer.
 * @return Pointer to newly allocated parser_t.
 */
parser_t *parser_init(lexer_t *lexer);

/**
 * @brief Parse the entire source code into a program AST.
 * @param parser Pointer to active parser instance.
 * @return Root compound AST node.
 */
ast_t *parser_parse(parser_t *parser);

#endif /* !PARSER_H */
