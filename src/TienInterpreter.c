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

    /* Pin the runtime so a concurrent destroy waits for this run to finish */
    ti_runtime_t *rt = ti_runtime_acquire(handle);
    if (rt == NULL) {
        return TI_ERR_STALE_HANDLE;
    }

    /* Hold the execution lock for the whole run: ti_call callers get in only at safe points */
    ti_runtime_exec_lock(rt, TI_PORT_WAIT_FOREVER);

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

    /* Decide the outcome while the runtime is still locked and pinned */
    ti_status_t status = ti_runtime_is_interrupted(rt) ? TI_ERR_INTERRUPTED : TI_OK;
    bool fatal = (rt->status == TI_RT_ERROR);
    if (fatal) {
        ti_runtime_report_error(rt);
        status = rt->error.kind;
    }

    /* Release before ti_fatal: the fatal handler may block or never return, which must not wedge ti_call */
    ti_runtime_exec_unlock(rt);
    ti_runtime_release(rt);

    /* The single place that turns a raised runtime error into a fatal (host decides how to stop) */
    if (fatal) {
        ti_fatal();
    }
    return status;
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

    /* Callback errors are consumed by the dispatcher; only cancellation stops the calling native */
    return ti_runtime_is_interrupted(rt) ? TI_ERR_INTERRUPTED : TI_OK;
}

/* Sleep inside a native without blocking events or ti_call callers */
ti_status_t ti_wait(ti_handle_t handle, uint32_t ms)
{
    /* Caller is a native running on this runtime, so it stays alive for this call */
    ti_runtime_t *rt = ti_runtime_resolve(handle);
    if (rt == NULL) {
        return TI_ERR_STALE_HANDLE;
    }
    return ti_runtime_wait(rt, ms);
}

/* Call a script function synchronously from outside the script's execution context */
ti_status_t ti_call(ti_handle_t handle, const char *func_name, value_t **args, int argc,
                    value_t **result, uint32_t timeout_ms)
{
    if (result != NULL) {
        *result = NULL;
    }
    if (func_name == NULL || func_name[0] == '\0' || argc < 0 || (argc > 0 && args == NULL)) {
        return TI_ERR_INVALID_ARG;
    }
    for (int i = 0; i < argc; i++) {
        if (args[i] == NULL) {
            return TI_ERR_INVALID_ARG;
        }
    }

    /* Pin the runtime: a concurrent destroy waits until this call is done */
    ti_runtime_t *rt = ti_runtime_acquire(handle);
    if (rt == NULL) {
        return TI_ERR_STALE_HANDLE;
    }

    /* A call made by the lock holder itself (e.g. a native) already owns the runtime: run inline */
    bool reentrant = ti_runtime_exec_held(rt);
    ti_status_t status = TI_OK;

    if (!reentrant) {
        /* Register as a waiter so the script yields the lock at its next safe point */
        __atomic_add_fetch(&rt->lock_waiters, 1, __ATOMIC_RELAXED);
        bool locked = ti_runtime_exec_lock(rt, timeout_ms);
        __atomic_sub_fetch(&rt->lock_waiters, 1, __ATOMIC_RELAXED);

        if (!locked) {
            /* Wake a script that may be waiting for this caller before it re-takes the lock */
            ti_signal_give(rt->handoff);
            ti_runtime_release(rt);
            return TI_ERR_TIMEOUT;
        }
    }

    /* From here on this context is the only one running TI code on the runtime */
    if (ti_runtime_is_interrupted(rt)) {
        status = TI_ERR_INTERRUPTED;
    } else if (rt->status != TI_RT_OK) {
        status = TI_ERR_RUNTIME;
    } else {
        ti_error_t err;
        status = ti_runtime_invoke(rt, func_name, args, argc, result, &err);
        if (status != TI_OK && status != TI_ERR_INTERRUPTED && err.message[0] != '\0') {
            ti_log("[Call Error] %s at line %d\n", err.message, err.line);
        }
    }

    if (!reentrant) {
        ti_runtime_exec_unlock(rt);
        /* Let a script yielding to this call continue (also releases stale hand-offs harmlessly) */
        ti_signal_give(rt->handoff);
    }
    ti_runtime_release(rt);
    return status;
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
