#ifndef TI_BUILD_LEXER_H
#define TI_BUILD_LEXER_H

#include "ti_build_token.h"
#include "tracked_memory.h"
#include <setjmp.h>

/* -------------------- Lexer Structure -------------------- */

typedef struct LEXER_STRUCT {
    alloc_hdr_t **alloc_list; /* Dedicated allocation list pointer */
    jmp_buf *err_jmp;       // Where lexer_abort() jumps on a build error (set by ti_compile)
    char c;                 // Current character
    unsigned int i;         // Current index
    unsigned int line_num;  // Current line number (1-based)
    char *contents;         // Pointer to input buffer
    char *line;             // Pointer to start of current line
} lexer_t;

/* -------------------- Public Functions -------------------- */

/**
 * @brief Initialize a new lexer for the given source string.
 * @param list Pointer to head of allocation list (can be NULL if unlinked).
 * @param str Source code buffer.
 * @return Pointer to newly allocated lexer_t.
 */
lexer_t *lexer_init(alloc_hdr_t **list, char *str);

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

/**
 * @brief Abort the build after an error has been logged. Does not return.
 * Jumps back to the setjmp in ti_compile(), which frees the program and then calls ti_fatal().
 * lexer->err_jmp must be set by the code that created the lexer (only ti_compile does).
 * @param lexer Pointer to active lexer instance.
 */
void lexer_abort(lexer_t *lexer);

#endif /* !TI_BUILD_LEXER_H */
