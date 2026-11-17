#ifndef TI_BUILD_PROGRAM_H
#define TI_BUILD_PROGRAM_H

#include "ti_type_ast.h"
#include "ti_type.h"
#include "tracked_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TI_PROGRAM_STRUCT {
    ast_t       *root_ast;    /* Root AST node of the compiled program */
    alloc_hdr_t *alloc_list;  /* Dedicated allocation list tracking all build-time memory */
    char        *path;        /* Source path (owned copy on alloc_list), NULL if unknown (e.g. the main script) */
} ti_program_t;

ti_program_t *ti_program_create(void);
void          ti_program_free(ti_program_t *prog);

/**
 * @brief Compile source text into a program and record its path.
 * ti_compile is this with no path; script modules will pass the path their loader resolved.
 * @param source_code Null-terminated source code string (only read: the caller keeps it).
 * @param path Source path copied into prog->path; NULL if unknown.
 * @param status Optional out (may be NULL): TI_OK, TI_ERR_SYNTAX (build error, already logged),
 *               TI_ERR_NO_MEMORY or TI_ERR_INVALID_ARG (NULL source).
 * @return The program, or NULL on failure (nothing is left allocated).
 */
ti_program_t *ti_program_compile(const char *source_code, const char *path, ti_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* !TI_BUILD_PROGRAM_H */
