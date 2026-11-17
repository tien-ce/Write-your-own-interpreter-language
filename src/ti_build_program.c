#include "include/ti_build_program.h"
#include "include/tracked_memory.h"
#include "include/ti_build_lexer.h"
#include "include/ti_build_parser.h"
#include "TienInterpreter.h"
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
    prog->path = NULL;
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

/* Compile source text into a program and record its path; the outcome is reported through status */
ti_program_t *ti_program_compile(const char *source_code, const char *path, ti_status_t *status)
{
    ti_status_t outcome = TI_ERR_NO_MEMORY; /* Every failure below is an allocation failure unless set otherwise */
    ti_program_t *prog = NULL;
    char *contents = NULL;
    lexer_t *lexer = NULL;
    parser_t *parser = NULL;
    bool failed = false;
    jmp_buf err_jmp;

    if (!source_code) {
        outcome = TI_ERR_INVALID_ARG;
        goto out;
    }

    /* Step 1: allocate the program container (all build memory lives on its own alloc list) */
    prog = ti_program_create();
    if (!prog) {
        goto out;
    }

    /* Step 2: record where the source came from: an owned copy, freed with the program */
    if (path != NULL) {
        prog->path = tracked_strdup(&prog->alloc_list, path);
        if (prog->path == NULL) {
            goto fail;
        }
    }

    /* Step 3: the lexer reads its own copy of the source, so the caller's text is never kept */
    contents = tracked_strdup(&prog->alloc_list, source_code);
    if (!contents) {
        goto fail;
    }
    lexer = lexer_init(&prog->alloc_list, contents);
    parser = parser_init(&prog->alloc_list, lexer);
    if (!lexer || !parser) {
        goto fail;
    }

    /* Step 4: parse; a build error is logged (with the path, so it names its file), then lexer_abort
     * jumps back here */
    lexer->path = prog->path;
    lexer->err_jmp = &err_jmp;
    if (setjmp(err_jmp) == 0) {
        prog->root_ast = parser_parse(parser);
    } else {
        failed = true;
        prog->root_ast = NULL;
    }

    /* Step 5: the transient frontend is no longer needed (AST nodes stay on the alloc list) */
    tracked_free(&prog->alloc_list, parser);
    tracked_free(&prog->alloc_list, lexer);
    tracked_free(&prog->alloc_list, contents);

    /* Step 6: no root means nothing to run */
    if (prog->root_ast == NULL) {
        outcome = failed ? TI_ERR_SYNTAX : TI_ERR_NO_MEMORY;
        goto fail;
    }
    outcome = TI_OK;
    goto out;

fail:
    /* Release all build memory: nothing is left allocated on failure */
    ti_program_free(prog);
    prog = NULL;

out:
    if (status != NULL) {
        *status = outcome;
    }
    return prog;
}
