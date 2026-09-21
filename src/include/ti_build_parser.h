#ifndef TI_BUILD_PARSER_H
#define TI_BUILD_PARSER_H

#include "ti_build_token.h"
#include "ti_type_ast.h"
#include "ti_build_lexer.h"
#include "tracked_memory.h"

/* -------------------- Parser Structure -------------------- */

typedef struct PARSER_STRUCT {
    alloc_hdr_t **alloc_list; /* Dedicated allocation list pointer */
    lexer_t *lexer;
    token_t *current_token;
} parser_t;

/* -------------------- Public Functions -------------------- */

/**
 * @brief Initialize a new parser using the given lexer.
 * @param list Pointer to head of allocation list (can be NULL if unlinked).
 * @param lexer Pointer to initialized lexer.
 * @return Pointer to newly allocated parser_t.
 */
parser_t *parser_init(alloc_hdr_t **list, lexer_t *lexer);

/**
 * @brief Parse the entire source code into a program AST.
 * @param parser Pointer to active parser instance.
 * @return Root compound AST node.
 */
ast_t *parser_parse(parser_t *parser);

#endif /* !TI_BUILD_PARSER_H */
