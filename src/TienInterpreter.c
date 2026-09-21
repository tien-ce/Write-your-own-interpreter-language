#include "TienInterpreter.h"
#include "include/ti_runtime_context.h"
#include "include/ti_build_lexer.h"
#include "include/ti_build_parser.h"
#include "include/ti_type_ast.h"
#include "include/ti_runtime_visitor.h"
#include "include/ti_build_program.h"
#include "include/ti_runtime.h"
#include "include/tracked_memory.h"
#include <stdio.h>
#include <stdlib.h>

/* -------------------- Static Callback Storage -------------------- */

/* Platform-registered callbacks for diagnostics and fatal error handling */
static ti_fatal_callback_t s_fatal_cb = NULL;
static ti_log_callback_t s_log_cb = NULL;

/* -------------------- Platform & Logging Functions -------------------- */

/**
 * @brief Register custom fatal error callback (e.g. exit(1) for Desktop, halt for Arduino).
 */
void ti_register_fatal(ti_fatal_callback_t func)
{
    s_fatal_cb = func;
}

/**
 * @brief Register custom logging callback (e.g. stdout for Desktop, Serial for Arduino).
 */
void ti_register_log(ti_log_callback_t func)
{
    s_log_cb = func;
}

/**
 * @brief Print a line of source code up to newline or null terminator.
 * Used by parser and visitor error reporting to display offending lines.
 */
void ti_log_line(char *line)
{
    while (line != NULL && *line != '\n' && *line != '\0') {
        printf("%c", *line);
        line++;
    }
}

/**
 * @brief Printf-style logging function dispatched to registered log callback.
 * If no custom callback is registered, output is ignored to prevent unbuffered crashes.
 */
void ti_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (s_log_cb != NULL) {
        s_log_cb(fmt, args);
    }
    va_end(args);
}

/**
 * @brief Handles an unrecoverable fatal interpreter error.
 * Invokes the host platform's fatal callback if registered, or falls back to exit(1).
 */
void ti_fatal(void)
{
    if (s_fatal_cb != NULL) {
        s_fatal_cb();
    }
    /* Fallback exit if callback returns or was not registered by host */
    exit(1);
}

/* -------------------- Public Two-Phase Pipeline Functions -------------------- */

/**
 * @brief Phase 1 (Build-Time): Compile source text into an immutable AST program.
 *
 * Execution Logic:
 * 1. Validates source text input.
 * 2. Allocates a fresh `ti_program_t` container with its own isolated `alloc_list`.
 * 3. Duplicates source string into the program's allocation list so the lexer can
 *    index characters without mutating caller-owned memory.
 * 4. Initializes the lexical scanner (`lexer_init`) and recursive descent parser
 *    (`parser_init`), binding all transient token and AST node allocations directly
 *    to `prog->alloc_list`.
 * 5. Executes syntactic analysis via `parser_parse()`, constructing the AST tree.
 * 6. Deallocates transient frontend objects (`parser`, `lexer`, `contents`) to reclaim
 *    intermediate heap buffers, while leaving the resulting AST nodes intact on `prog->alloc_list`.
 * 7. Validates compilation success: If parsing encountered fatal syntax errors and produced
 *    no root AST, tears down the program container via `ti_program_free` and returns NULL.
 */
ti_program_t *ti_compile(const char *source_code)
{
    if (!source_code) {
        return NULL;
    }

    /* 1. Allocate compiled program container */
    ti_program_t *prog = ti_program_create();
    if (!prog) {
        return NULL;
    }

    /* 2. Create mutable source buffer tracked on program's build-time memory list */
    char *contents = tracked_strdup(&prog->alloc_list, source_code);
    if (!contents) {
        ti_program_free(prog);
        return NULL;
    }

    /* 3. Initialize frontend lexer and parser bound to program's memory list */
    lexer_t *lexer = lexer_init(&prog->alloc_list, contents);
    parser_t *parser = parser_init(&prog->alloc_list, lexer);

    /* 4. Construct the Abstract Syntax Tree (AST) */
    prog->root_ast = parser_parse(parser);

    /* 5. Reclaim transient frontend state machines (AST nodes remain on alloc_list) */
    tracked_free(&prog->alloc_list, parser);
    tracked_free(&prog->alloc_list, lexer);
    tracked_free(&prog->alloc_list, contents);

    /* 6. Verify compilation outcome */
    if (!prog->root_ast) {
        ti_program_free(prog);
        return NULL;
    }

    return prog;
}

/**
 * @brief Phase 2 (Run-Time): Execute a compiled AST program on a runtime instance.
 *
 * Execution Logic:
 * 1. Validates preconditions: Ensures both runtime instance and compiled program AST exist.
 * 2. Dispatches recursive tree evaluation from the root node (`prog->root_ast`) using
 *    the runtime's root variable scope (`rt->global_context`).
 * 3. Evaluates statement blocks, assignments, loops, and function invocations. All dynamic
 *    values (`value_t`) and local variable scopes (`context_t`) created during evaluation
 *    are tracked on `rt->alloc_list`.
 * 4. Inspects post-execution control flow state:
 *    - `FLOW_BREAK`: If unconsumed break escaped to top-level, raises runtime error
 *      ("break statement not within a loop").
 *    - `FLOW_CONTINUE`: If unconsumed continue escaped to top-level, raises runtime error
 *      ("continue statement not within a loop").
 *    - `FLOW_RETURN`: If top-level script returned a value, normalizes flow_state back to
 *      `FLOW_NORMAL` so subsequent executions on this runtime remain clean.
 */
void ti_execute(ti_runtime_t *rt, ti_program_t *prog)
{
    if (!rt || !prog || !prog->root_ast) {
        return;
    }

    /* 1. Walk and evaluate the AST graph from root */
    visitor_visit(rt, rt->global_context, prog->root_ast);

    /* 2. Validate control flow invariants at script completion */
    if (rt->global_context->flow_state == FLOW_BREAK) {
        ti_log("[Runtime Error] 'break' statement not within a loop\n");
        ti_fatal();
    } else if (rt->global_context->flow_state == FLOW_CONTINUE) {
        ti_log("[Runtime Error] 'continue' statement not within a loop\n");
        ti_fatal();
    } else if (rt->global_context->flow_state == FLOW_RETURN) {
        /* Consume return signal at top-level script boundary */
        rt->global_context->flow_state = FLOW_NORMAL;
    }
}

/**
 * @brief High-level helper: Compile and execute a Ti script from raw text in one step.
 *
 * Execution Logic:
 * 1. Compiles source text via `ti_compile()` into an immutable `ti_program_t`.
 * 2. Creates a dedicated, isolated execution runtime via `ti_runtime_create()`.
 * 3. Executes the compiled program on the runtime instance via `ti_execute()`.
 * 4. Performs deterministic cleanup:
 *    - Destroys the runtime instance (`ti_runtime_destroy`), reclaiming all runtime
 *      variables, local context scopes, and dynamic value allocations.
 *    - Frees the compiled program (`ti_program_free`), reclaiming all AST nodes and tokens.
 * 5. Guarantees zero residual heap allocations on script completion.
 */
void ti_run_string(const char *source_code)
{
    if (!source_code) {
        return;
    }

    /* 1. Phase 1: Compile source text to AST */
    ti_program_t *prog = ti_compile(source_code);
    if (!prog) {
        return; /* Syntax error occurred and was logged; abort execution */
    }

    /* 2. Phase 2: Create execution runtime */
    ti_runtime_t *rt = ti_runtime_create();
    if (!rt) {
        ti_program_free(prog);
        return;
    }

    /* 3. Execute script */
    ti_execute(rt, prog);

    /* 4. Phase 3: Deterministic teardown of both execution environments */
    ti_runtime_destroy(rt);
    ti_program_free(prog);
}

/**
 * @brief Request execution cancellation to immediately halt running script.
 * Sets the volatile `is_interrupted` flag on the runtime instance, causing the
 * visitor master dispatcher to abort execution on the next statement.
 */
void ti_stop(ti_runtime_t *rt)
{
    ti_runtime_stop(rt);
}
