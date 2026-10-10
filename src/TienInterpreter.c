#include "TienInterpreter.h"
#include "include/ti_runtime_context.h"
#include "include/ti_build_lexer.h"
#include "include/ti_build_parser.h"
#include "include/ti_type_ast.h"
#include "include/ti_runtime_visitor.h"
#include "include/ti_build_program.h"
#include "include/ti_runtime.h"
#include "include/ti_runtime_builtin.h"
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
 * - Validates source text input.
 * - Allocates a fresh `ti_program_t` container with its own isolated `alloc_list`.
 * - Duplicates source string into the program's allocation list so the lexer can
 *    index characters without mutating caller-owned memory.
 * - Initializes the lexical scanner (`lexer_init`) and recursive descent parser
 *    (`parser_init`), binding all transient token and AST node allocations directly
 *    to `prog->alloc_list`.
 * - Executes syntactic analysis via `parser_parse()`, constructing the AST tree.
 * - Deallocates transient frontend objects (`parser`, `lexer`, `contents`) to reclaim
 *    intermediate heap buffers, while leaving the resulting AST nodes intact on `prog->alloc_list`.
 * - Validates compilation success: If parsing encountered fatal syntax errors and produced
 *    no root AST, tears down the program container via `ti_program_free` and returns NULL.
 */
ti_program_t *ti_compile(const char *source_code)
{
    if (!source_code) {
        return NULL;
    }

    /* Allocate compiled program container */
    ti_program_t *prog = ti_program_create();
    if (!prog) {
        return NULL;
    }

    /* Create mutable source buffer tracked on program's build-time memory list */
    char *contents = tracked_strdup(&prog->alloc_list, source_code);
    if (!contents) {
        ti_program_free(prog);
        return NULL;
    }

    /* Initialize frontend lexer and parser bound to program's memory list */
    lexer_t *lexer = lexer_init(&prog->alloc_list, contents);
    parser_t *parser = parser_init(&prog->alloc_list, lexer);

    /* Construct the Abstract Syntax Tree (AST) */
    prog->root_ast = parser_parse(parser);

    /* Reclaim transient frontend state machines (AST nodes remain on alloc_list) */
    tracked_free(&prog->alloc_list, parser);
    tracked_free(&prog->alloc_list, lexer);
    tracked_free(&prog->alloc_list, contents);

    /* Verify compilation outcome */
    if (!prog->root_ast) {
        ti_program_free(prog);
        return NULL;
    }

    return prog;
}

/* Create a new runtime instance */
ti_handle_t ti_create(void)
{
    /* Language-level built-ins are registered here so every host gets them (only the first call does work) */
    ti_register_core_builtins();

    ti_runtime_t *rt = ti_runtime_create();
    return rt ? rt->handle : TI_INVALID_HANDLE;
}

/* Destroy a runtime, discard its pending events and invalidate its handle */
ti_status_t ti_destroy(ti_handle_t handle)
{
    /* A stale handle (e.g. double destroy) is rejected instead of double-freeing */
    ti_runtime_t *rt = ti_runtime_resolve(handle);
    if (rt == NULL) {
        return TI_ERR_STALE_HANDLE;
    }

    ti_runtime_destroy(rt);
    return TI_OK;
}

/**
 * @brief Phase 2 (Run-Time): Execute a compiled AST program on a runtime instance.
 *
 * Execution Logic:
 * - Validates preconditions: Ensures the compiled program AST exists and the handle
 *    resolves to a live runtime instance.
 * - Dispatches recursive tree evaluation from the root node (`prog->root_ast`) using
 *    the runtime's root variable scope (`rt->global_context`).
 * - Evaluates statement blocks, assignments, loops, and function invocations. All dynamic
 *    values (`value_t`) and local variable scopes (`context_t`) created during evaluation
 *    are tracked on `rt->alloc_list`.
 * - Inspects post-execution control flow state:
 *    - `FLOW_BREAK`: If unconsumed break escaped to top-level, raises runtime error
 *      ("break statement not within a loop").
 *    - `FLOW_CONTINUE`: If unconsumed continue escaped to top-level, raises runtime error
 *      ("continue statement not within a loop").
 *    - `FLOW_RETURN`: If top-level script returned a value, normalizes flow_state back to
 *      `FLOW_NORMAL` so subsequent executions on this runtime remain clean.
 */
ti_status_t ti_execute(ti_handle_t handle, ti_program_t *prog)
{
    if (!prog || !prog->root_ast) {
        return TI_ERR_INVALID_ARG;
    }

    /* The host owns the runtime lifecycle, so it cannot be destroyed while executing */
    ti_runtime_t *rt = ti_runtime_resolve(handle);
    if (rt == NULL) {
        return TI_ERR_STALE_HANDLE;
    }

    /* Walk and evaluate the AST graph from root */
    visitor_visit(rt, rt->global_context, prog->root_ast);

    /* Validate control flow invariants at script completion (only if nothing failed earlier) */
    if (rt->status == TI_RT_OK) {
        if (rt->global_context->flow_state == FLOW_BREAK) {
            ti_raise(rt, TI_ERR_RUNTIME, 0, "'break' statement not within a loop");
        } else if (rt->global_context->flow_state == FLOW_CONTINUE) {
            ti_raise(rt, TI_ERR_RUNTIME, 0, "'continue' statement not within a loop");
        } else if (rt->global_context->flow_state == FLOW_RETURN) {
            /* Consume return signal at top-level script boundary */
            rt->global_context->flow_state = FLOW_NORMAL;
        }
    }

    /* The single place that turns a raised runtime error into a fatal (host decides how to stop) */
    if (rt->status == TI_RT_ERROR) {
        ti_runtime_report_error(rt);
        ti_fatal();
        return rt->error.kind;
    }

    return ti_runtime_is_interrupted(rt) ? TI_ERR_INTERRUPTED : TI_OK;
}

/**
 * @brief High-level helper: Compile and execute a Ti script from raw text in one step.
 *
 * Execution Logic:
 * - Compiles source text via `ti_compile()` into an immutable `ti_program_t`.
 * - Creates a dedicated, isolated execution runtime via `ti_create()`.
 * - Executes the compiled program on the runtime instance via `ti_execute()`.
 * - Performs deterministic cleanup:
 *    - Destroys the runtime instance (`ti_destroy`), invalidating its handle and reclaiming
 *      all runtime variables, local context scopes, pending events and dynamic value allocations.
 *    - Frees the compiled program (`ti_program_free`), reclaiming all AST nodes and tokens.
 * - Guarantees zero residual heap allocations on script completion.
 */
void ti_run_string(const char *source_code)
{
    if (!source_code) {
        return;
    }

    /* Phase 1: Compile source text to AST */
    ti_program_t *prog = ti_compile(source_code);
    if (!prog) {
        return; /* Syntax error occurred and was logged; abort execution */
    }

    /* Phase 2: Create execution runtime */
    ti_handle_t handle = ti_create();
    if (handle == TI_INVALID_HANDLE) {
        ti_program_free(prog);
        return;
    }

    /* Execute script */
    ti_execute(handle, prog);

    /* Phase 3: Deterministic teardown of both execution environments */
    ti_destroy(handle);
    ti_program_free(prog);
}

/**
 * @brief Request execution cancellation to immediately halt running script.
 * Sets the volatile `is_interrupted` flag on the runtime instance, causing the
 * visitor master dispatcher to abort execution on the next statement.
 */
ti_status_t ti_stop(ti_handle_t handle)
{
    return ti_runtime_stop(handle);
}

/* -------------------- Event Bridge Functions -------------------- */

/* Queue an event that invokes a TI callback function at the next safe point */
ti_status_t ti_post_event(ti_handle_t handle, const char *func_name, value_t **args, int arg_count)
{
    return ti_runtime_post_event(handle, func_name, args, arg_count);
}

/* Execute the pending events of a runtime immediately */
ti_status_t ti_dispatch_events(ti_handle_t handle)
{
    /* Caller is a native running on this runtime, so it stays alive for this call */
    ti_runtime_t *rt = ti_runtime_resolve(handle);
    if (rt == NULL) {
        return TI_ERR_STALE_HANDLE;
    }

    ti_runtime_dispatch_pending_events(rt);

    /* A callback that raised an error stops the calling native (e.g. delay) so it can unwind */
    if (rt->status == TI_RT_ERROR) {
        return rt->error.kind;
    }
    return ti_runtime_is_interrupted(rt) ? TI_ERR_INTERRUPTED : TI_OK;
}

/* Raise a runtime error from native code */
void ti_raise_error(ti_handle_t handle, ti_status_t kind, const char *fmt, ...)
{
    /* The caller is a native running on this runtime, so it stays alive for this call */
    ti_runtime_t *rt = ti_runtime_resolve(handle);
    if (rt == NULL) {
        return;
    }

    /* Format first: ti_raise takes a ready-made message through "%s" */
    char message[TI_ERROR_MESSAGE_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    /* Line 0 means "unknown": run_function fills in the line of the call */
    ti_raise(rt, kind, 0, "%s", message);
}
