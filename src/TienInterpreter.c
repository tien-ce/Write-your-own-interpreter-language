#include "TienInterpreter.h"
#include "include/ti_runtime_context.h"
#include "include/ti_build_lexer.h"
#include "include/ti_build_parser.h"
#include "include/ti_type_ast.h"
#include "include/ti_runtime_visitor.h"
#include "include/ti_build_program.h"
#include "include/ti_runtime.h"
#include "include/ti_runtime_registry.h"
#include "include/ti_runtime_builtin.h"
#include "include/tracked_memory.h"
#include <stdio.h>
#include <stdlib.h>

/* -------------------- Static Callback Storage -------------------- */

/* Platform-registered callback for diagnostics */
static ti_log_callback_t s_log_cb = NULL;

/* -------------------- Platform & Logging Functions -------------------- */

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
    if (line == NULL) {
        return;
    }

    /* The line ends at its newline or at the end of the source (the last line may have no newline) */
    int length = 0;
    while (line[length] != '\n' && line[length] != '\0') {
        length++;
    }

    /* Through the log callback like every other message, always closed by a newline */
    ti_log("%.*s\n", length, line);
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

/* -------------------- Public Two-Phase Pipeline Functions -------------------- */

/* Compile and record the source path; a build error (already logged) just returns NULL */
ti_program_t *ti_compile(const char *source_code, const char *path)
{
    return ti_program_compile(source_code, path, NULL);
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
    /* Detach is atomic: a stale handle (e.g. double destroy, even concurrent) is rejected, never double-freed */
    return ti_runtime_destroy(handle);
}

/* Run a program, then destroy its runtime; a raised error comes back as the status */
ti_status_t ti_execute(ti_handle_t handle, ti_program_t *prog)
{
    /* Run: a raised runtime error is already logged and returned as the status */
    ti_status_t status = ti_runtime_execute(handle, prog);

    /* The handle is always consumed, also when prog was invalid (handle destructors release their hardware);
     * a stale handle just fails here */
    (void)ti_runtime_destroy(handle);
    return status;
}

/* Compile, run on a fresh runtime, then free the program: nothing is left allocated on any path */
ti_status_t ti_run_string(const char *source_code, const char *path)
{
    /* Phase 1: compile (a NULL source, a syntax error or out of memory comes back as the status);
     * the program keeps its own copy of the path, read by the module loader on import */
    ti_status_t status = TI_OK;
    ti_program_t *prog = ti_program_compile(source_code, path, &status);
    if (!prog) {
        return status;
    }

    /* Phase 2: create the execution runtime (fails on out of memory or no free runtime slot) */
    ti_handle_t handle = ti_create();
    if (handle == TI_INVALID_HANDLE) {
        ti_program_free(prog);
        return TI_ERR_NO_MEMORY;
    }

    /* Phase 3: run (ti_execute also destroys the runtime), then free the program */
    status = ti_execute(handle, prog);
    ti_program_free(prog);
    return status;
}

/* Compile and run a script, freeing the host's source buffer right after compiling */
ti_status_t ti_run_string_take(char *source_code, const char *path)
{
    /* Phase 1: compile (a NULL source, a syntax error or out of memory comes back as the status);
     * the program keeps its own copy of the path, read by the module loader on import */
    ti_status_t status = TI_OK;
    ti_program_t *prog = ti_program_compile(source_code, path, &status);

    /* The program keeps no pointer into the source (the compiler works on its own copy): release it now,
     * with the allocator the host was told to use (ti_raw_malloc) */
    ti_raw_free(source_code);

    if (!prog) {
        return status;
    }

    /* Phase 2: create the execution runtime (fails on out of memory or no free runtime slot) */
    ti_handle_t handle = ti_create();
    if (handle == TI_INVALID_HANDLE) {
        ti_program_free(prog);
        return TI_ERR_NO_MEMORY;
    }

    /* Phase 3: run (ti_execute also destroys the runtime), then free the program */
    status = ti_execute(handle, prog);
    ti_program_free(prog);
    return status;
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
ti_status_t ti_post_event(ti_handle_t handle, const value_t *func, value_t **args, int arg_count)
{
    return ti_runtime_post_event(handle, func, args, arg_count);
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

/* Block a native until another thread reports completion, without blocking events or ti_call callers */
ti_status_t ti_wait_flag(ti_handle_t handle, bool *is_done, uint32_t timeout_ms)
{
    /* Caller is a native running on this runtime, so it stays alive for this call */
    ti_runtime_t *rt = ti_runtime_resolve(handle);
    if (rt == NULL) {
        return TI_ERR_STALE_HANDLE;
    }
    return ti_runtime_wait_flag(rt, is_done, timeout_ms);
}

/* Wake a native blocked in ti_wait_flag (or ti_wait) from another thread */
ti_status_t ti_give(ti_handle_t handle)
{
    return ti_runtime_give(handle);
}

/* Call a script function synchronously from outside the script's execution context */
ti_status_t ti_call(ti_handle_t handle, const value_t *func, value_t **args, int argc,
                    value_t **result, uint32_t timeout_ms)
{
    if (result != NULL) {
        *result = NULL;
    }
    /* Only the value is read here; the function it points to is checked by ti_runtime_invoke */
    if (func == NULL || func->type != VAL_FUNC || func->func_val == NULL ||
        argc < 0 || (argc > 0 && args == NULL)) {
        return TI_ERR_INVALID_ARG;
    }
    for (int i = 0; i < argc; i++) {
        if (args[i] == NULL) {
            return TI_ERR_INVALID_ARG;
        }
    }

    /* Step 1: resolve under the registry so destroy cannot detach the runtime meanwhile */
    ti_runtime_t *rt = ti_registry_lock(handle);
    if (rt == NULL) {
        return TI_ERR_STALE_HANDLE;
    }

    /* Step 2: a call made by the lock holder itself (e.g. a native) already owns the runtime: run inline */
    bool reentrant = ti_runtime_exec_held(rt);

    /*
     * Step 3: register as a waiter while the registry is still held, so a destroy that detaches
     * after this point is guaranteed to see the waiter and wait for it. Never block on exec_lock
     * while holding the registry (the script takes the registry at its safe points).
     */
    if (!reentrant) {
        __atomic_add_fetch(&rt->lock_waiters, 1, __ATOMIC_ACQ_REL);
    }
    ti_registry_unlock();

    ti_status_t status = TI_OK;
    if (!reentrant) {
        /* Step 4: wait for the lock; the script yields it at its next safe point */
        if (!ti_runtime_exec_lock(rt, timeout_ms)) {
            /* Wake a script (or destroy) waiting for this caller, then leave; rt is not touched afterwards */
            ti_signal_give(rt->handoff);
            __atomic_sub_fetch(&rt->lock_waiters, 1, __ATOMIC_ACQ_REL);
            return TI_ERR_TIMEOUT;
        }

        /* Holding the lock: destroy cannot free rt until it is released, so leave the waiter count now */
        __atomic_sub_fetch(&rt->lock_waiters, 1, __ATOMIC_ACQ_REL);

        /* Step 5: detached while waiting: destroy is draining waiters, so leave without running */
        if (__atomic_load_n(&rt->handle, __ATOMIC_ACQUIRE) == TI_INVALID_HANDLE) {
            ti_signal_give(rt->handoff);
            ti_runtime_exec_unlock(rt);
            return TI_ERR_STALE_HANDLE;
        }
    }

    /* Step 6: from here on this context is the only one running TI code on the runtime */
    if (ti_runtime_is_interrupted(rt)) {
        status = TI_ERR_INTERRUPTED;
    } else if (rt->status != TI_RT_OK) {
        status = TI_ERR_RUNTIME;
    } else {
        ti_error_t err;
        status = ti_runtime_invoke(rt, func->func_val, args, argc, result, &err);
        if (status != TI_OK && status != TI_ERR_INTERRUPTED && err.message[0] != '\0') {
            ti_log("[Call Error] %s at line %d\n", err.message, err.line);
        }
    }

    /*
     * Step 7: give the hand-off BEFORE unlocking: once the lock is released, destroy may free rt.
     * The yielding script wakes, then blocks on the lock until the unlock below.
     */
    if (!reentrant) {
        ti_signal_give(rt->handoff);
        ti_runtime_exec_unlock(rt);
    }
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
