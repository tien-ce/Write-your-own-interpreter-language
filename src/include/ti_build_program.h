#ifndef TI_BUILD_PROGRAM_H
#define TI_BUILD_PROGRAM_H

#include "ti_type_ast.h"
#include "tracked_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TI_PROGRAM_STRUCT {
    ast_t       *root_ast;    /* Root AST node of the compiled program */
    alloc_hdr_t *alloc_list;  /* Dedicated allocation list tracking all build-time memory */
} ti_program_t;

ti_program_t *ti_program_create(void);
void          ti_program_free(ti_program_t *prog);

#ifdef __cplusplus
}
#endif

#endif /* !TI_BUILD_PROGRAM_H */
