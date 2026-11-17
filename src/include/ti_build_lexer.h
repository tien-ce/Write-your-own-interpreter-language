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
    const char *path;       // Path of the source being built (NULL: none); borrowed from the program
} lexer_t;

/* -------------------- Build Error Location -------------------- */

/*
 * Where a build error is, for the "at ..." part of a lexer or parser message:
 * "<path>:<line>" when the source has a path, "line <line>" when it has none.
 * Usage: ti_log("... at " TI_BUILD_AT_FMT "\n", TI_BUILD_AT_ARGS(lexer, lexer->line_num));
 */

/** @brief Format of the location: three conversions, filled by TI_BUILD_AT_ARGS. */
#define TI_BUILD_AT_FMT "%s%s%d"

/**
 * @brief Arguments matching TI_BUILD_AT_FMT.
 * @param lexer Lexer of the build (its path names the file).
 * @param line_num Line of the error.
 */
#define TI_BUILD_AT_ARGS(lexer, line_num) \
    ((lexer)->path != NULL ? (lexer)->path : "line "), ((lexer)->path != NULL ? ":" : ""), (int)(line_num)

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
 * Jumps back to the setjmp in ti_program_compile(), which frees the program and reports the build error.
 * lexer->err_jmp must be set by the code that created the lexer (only ti_program_compile does).
 * @param lexer Pointer to active lexer instance.
 */
void lexer_abort(lexer_t *lexer);

#endif /* !TI_BUILD_LEXER_H */
