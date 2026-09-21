#include "include/ti_build_program.h"
#include "include/tracked_memory.h"
#include <stdlib.h>

/* -------------------- Public Program Functions -------------------- */

ti_program_t *ti_program_create(void)
{
    ti_program_t *prog = (ti_program_t *)tracked_calloc(NULL, 1, sizeof(ti_program_t));
    if (!prog) {
        return NULL;
    }
    prog->alloc_list = NULL;
    prog->root_ast = NULL;
    return prog;
}

void ti_program_free(ti_program_t *prog)
{
    if (!prog) {
        return;
    }
    tracked_free_all(&prog->alloc_list);
    tracked_free(NULL, prog);
}
