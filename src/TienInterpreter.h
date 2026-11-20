#ifndef TIEN_INTERPRETER_H
#define TIEN_INTERPRETER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include "include/ti_type.h"
#include "include/ti_port.h"
#include "include/ti_type_value.h"
#include "include/ti_type_value_dict.h"
#include "include/ti_type_value_list.h"
#include "include/ti_type_value_bytes.h"
#include "include/ti_type_value_handle.h"
#include "include/ti_type_func.h"
#include "include/ti_type_module.h"


/* -------------------- Runtime Configuration -------------------- */
/* Maximum number of simultaneously live runtimes (handle slot table size, at most 256) */
#ifndef TI_MAX_RUNTIMES
#define TI_MAX_RUNTIMES 10
#endif

/* Maximum number of events queued per runtime before posting is rejected */
#ifndef TI_MAX_PENDING_EVENTS
#define TI_MAX_PENDING_EVENTS 32
#endif

/* Maximum number of registered built-in modules (one pointer of RAM each) */
#ifndef TI_MAX_MODULES
#define TI_MAX_MODULES 16
#endif

/* -------------------- Platform & Callback Types -------------------- */

/**
 * @brief Callback type for interpreter log output (printf-style).
 */
typedef void (*ti_log_callback_t)(const char *fmt, va_list args);

/**
 * @brief Callback type that loads the source of a script module for 'import <name>;'.
 * The library gives the name no meaning: the loader resolves it, for example relative to importer_path.
 * The returned path identifies the module: names resolved to the same path are one module (loaded,
 * compiled and run once per runtime), so return one spelling per file. Out parameters are read only
 * when TI_OK is returned; the library then owns both buffers and frees them with ti_raw_free.
 * @param importer_path Path of the file that runs the import: the main script's path given to ti_compile
 *                      or ti_run_string*, or the path this loader returned for a module (may be NULL);
 *                      borrowed, read only.
 * @param name Module name as written after 'import' ("lib", "lib/count", "../lib/count"); borrowed, read only.
 * @param text_out Receives the module source text (null-terminated), allocated with ti_raw_malloc.
 * @param path_out Receives the path of the module's file, allocated with ti_raw_malloc (required).
 * @return TI_OK, TI_ERR_UNDEFINED if there is no such module, or another status if loading failed.
 */
typedef ti_status_t (*ti_module_loader_t)(const char *importer_path, const char *name,
                                          char **text_out, char **path_out);

/* -------------------- Platform & Logging API -------------------- */

/**
 * @brief Printf-style logging function dispatched to registered log callback.
 * @param fmt Format string.
 */
void ti_log(const char *fmt, ...);

/**
 * @brief Log a line of text from the given pointer up to newline or null terminator, then a newline
 * (through the registered log callback, like ti_log).
 * @param line Pointer to start of line.
 */
void ti_log_line(char *line);

/**
 * @brief Register custom logging callback (e.g. stdout for Desktop, Serial for Arduino).
 * @param func Pointer to callback function.
 */
void ti_register_log(ti_log_callback_t func);

/**
 * @brief Register the callback that loads script modules ('import <name>;' of a name no native module has).
 * Call at host startup, before any runtime is created (the loader is not locked).
 * @param loader Loader callback, or NULL to offer no script modules (import then fails with 'Unknown module').
 */
void ti_register_module_loader(ti_module_loader_t loader);

/* -------------------- Public Interpreter API -------------------- */

/**
 * @brief Compiled program (opaque): hosts only hold the pointer and hand it back to the library.
 * Its fields are private to the library (defined in include/ti_build_program.h).
 */
typedef struct TI_PROGRAM_STRUCT ti_program_t;

/**
 * @brief Initialize built-in interpreter functions (e.g. print).
 */
void ti_init_builtin(void);

/**
 * @brief Compile a Ti language source code string into an immutable AST program.
 * @param source_code Null-terminated source code string (only read: the caller keeps it).
 * @param path Path of the script (copied; may be NULL). Errors name the file with it, and the module loader
 *             receives it as importer_path for every 'import' written in this script.
 * @return Newly allocated ti_program_t, or NULL on a syntax error (already logged), NULL source or out of memory.
 */
ti_program_t *ti_compile(const char *source_code, const char *path);

/**
 * @brief Free a compiled program and all its build-time memory (AST included).
 * Call only when no ti_execute is running it (its functions borrow the AST during the run).
 * @param prog Program from ti_compile (NULL does nothing).
 */
void ti_program_free(ti_program_t *prog);

/**
 * @brief Create a new runtime instance.
 * The calling thread becomes the runtime's owner: ti_create, ti_execute and ti_destroy must be
 * called on that thread, one after another. Other threads may only use ti_post_event, ti_stop,
 * ti_give and ti_call.
 * @return Handle of the new runtime, or TI_INVALID_HANDLE if out of memory or no free slot.
 */
ti_handle_t ti_create(void);

/**
 * @brief Destroy a runtime that will not be executed (ti_execute already destroys the runtime it runs).
 * Owner thread only. Every later call with this handle (from any thread) fails with
 * TI_ERR_STALE_HANDLE; a ti_call already waiting for the runtime returns TI_ERR_STALE_HANDLE without running.
 * @param handle Runtime handle.
 * @return TI_OK, or TI_ERR_STALE_HANDLE if the handle is stale or invalid.
 */
ti_status_t ti_destroy(ti_handle_t handle);

/**
 * @brief Execute a compiled program on the specified runtime, then destroy that runtime.
 * Owner thread only. The handle is always consumed (also on error or invalid prog): once this
 * returns, every call with it fails with TI_ERR_STALE_HANDLE, and handle destructors have run.
 * A runtime error stops the script, is logged and is returned; it never ends the host program.
 * To run the program again, create a new runtime.
 * @param handle Runtime handle from ti_create.
 * @param prog Pointer to compiled program. Keep it alive until this returns: functions it defines
 *             borrow their name, parameters and body from its AST. It may be executed again.
 * @return TI_OK on completion, TI_ERR_INTERRUPTED if stopped, the raised error kind (e.g. TI_ERR_UNDEFINED),
 *         TI_ERR_STALE_HANDLE or TI_ERR_INVALID_ARG.
 */
ti_status_t ti_execute(ti_handle_t handle, ti_program_t *prog);

/**
 * @brief High-level helper to compile and execute a Ti script in one step (nothing is left allocated).
 * @param source_code Null-terminated Ti language source code string (only read: the caller keeps it).
 * @param path Path of the script (copied; may be NULL), as in ti_compile.
 * @return TI_OK, TI_ERR_SYNTAX (build error, logged), TI_ERR_NO_MEMORY (or no free runtime slot),
 *         TI_ERR_INVALID_ARG (NULL source), or what ti_execute returns.
 */
ti_status_t ti_run_string(const char *source_code, const char *path);

/**
 * @brief Compile and run a script like ti_run_string, taking ownership of the source buffer.
 * The buffer is freed with ti_raw_free right after compiling (before the script runs), in every case,
 * so the source text and the running script never occupy memory at the same time.
 * @param source_code Source text allocated with ti_raw_malloc (never a literal or a flash string); freed here.
 * @param path Path of the script (copied; may be NULL), as in ti_compile.
 * @return Same as ti_run_string.
 */
ti_status_t ti_run_string_take(char *source_code, const char *path);

/**
 * @brief Request execution cancellation to immediately halt running script.
 * Safe to call from any task/thread, but not from ISR (takes the registry mutex).
 * @param handle Runtime handle.
 * @return TI_OK, or TI_ERR_STALE_HANDLE if the handle is stale or invalid.
 */
ti_status_t ti_stop(ti_handle_t handle);

/* -------------------- Event Bridge API -------------------- */

/**
 * @brief Queue an event that invokes a TI callback function at the next safe point.
 * Ownership of every value in args is always transferred, whether or not the call succeeds.
 * Safe to call from any task/thread, but not from ISR (allocates memory).
 * The callback is a function value the script handed to a native (e.g. register_event(on_rx)); the
 * native keeps its own copy (val_copy) together with the handle. A function of another runtime is
 * dropped at dispatch (logged as undefined), never run.
 * @param handle Target runtime handle (typically stored by a native registration function).
 * @param func VAL_FUNC value of the TI callback (borrowed: the caller keeps and frees it).
 * @param args Array of argument values (array is copied, values are owned by the call).
 * @param arg_count Number of arguments.
 * @return TI_OK, TI_ERR_STALE_HANDLE (runtime gone: drop the listener), TI_ERR_QUEUE_FULL,
 *         TI_ERR_NO_MEMORY or TI_ERR_INVALID_ARG (func NULL, not VAL_FUNC or the empty function).
 */
ti_status_t ti_post_event(ti_handle_t handle, const value_t *func, value_t **args, int arg_count);

/**
 * @brief Execute the pending events of a runtime immediately.
 * Intended for long-running native functions (e.g. a sliced delay) so callbacks are not blocked.
 * Must only be called from a native function executing on this runtime.
 * @param handle Runtime handle passed to the native function.
 * @return TI_OK, TI_ERR_INTERRUPTED if the runtime has been stopped, or TI_ERR_STALE_HANDLE.
 */
ti_status_t ti_dispatch_events(ti_handle_t handle);

/**
 * @brief Sleep for ms milliseconds inside a native (e.g. delay) while staying responsive.
 * The execution lock is released during the sleep, so queued events and ti_call callbacks run
 * immediately; the sleep then continues for the remaining time. Only ti_stop ends it early.
 * Must only be called from a native function executing on this runtime.
 * @param handle Runtime handle passed to the native function.
 * @param ms Duration in milliseconds.
 * @return TI_OK when the time has elapsed, TI_ERR_INTERRUPTED if stopped, or TI_ERR_STALE_HANDLE.
 */
ti_status_t ti_wait(ti_handle_t handle, uint32_t ms);

/**
 * @brief Block a native until another thread sets *is_done, while staying responsive like ti_wait.
 * Queued events and ti_call callbacks keep running during the wait. The producer thread must write
 * its result, then set the flag with release ordering, then call ti_give:
 *   result = ...; __atomic_store_n(&done, true, __ATOMIC_RELEASE); ti_give(handle);
 * The flag and any result buffer must stay valid until the producer is finished with them, also
 * after a timeout or a stop (never on the native's stack unless the I/O is cancelled first).
 * Must only be called from a native function executing on this runtime.
 * @param handle Runtime handle passed to the native function.
 * @param is_done Completion flag; the caller clears it before starting the I/O.
 * @param timeout_ms Wait limit in ms (TI_PORT_WAIT_FOREVER for no limit).
 * @return TI_OK when the flag is set, TI_ERR_TIMEOUT, TI_ERR_INTERRUPTED if stopped,
 *         TI_ERR_INVALID_ARG (is_done is NULL) or TI_ERR_STALE_HANDLE.
 */
ti_status_t ti_wait_flag(ti_handle_t handle, bool *is_done, uint32_t timeout_ms);

/**
 * @brief Wake a native blocked in ti_wait_flag (or ti_wait) so it re-checks its flag now.
 * Only wakes: setting the flag is the producer's job. Safe to call from any task/thread, but not from ISR.
 * @param handle Runtime handle.
 * @return TI_OK, or TI_ERR_STALE_HANDLE if the handle is stale or invalid.
 */
ti_status_t ti_give(ti_handle_t handle);

/**
 * @brief Call a script function synchronously from outside the script (e.g. a driver task).
 * The script is paused at its next safe point (statement or loop-iteration boundary, or inside a
 * delay) and the callback runs to completion on the caller's stack before the script resumes.
 * An error raised by the callback is consumed here: it is returned as the status and the script
 * keeps running. Arguments are borrowed (the caller keeps ownership); *result is owned by the
 * caller. Never call from an ISR, and never while holding a lock a native may wait for.
 * The function is a function value the script handed to a native (e.g. register_callback(on_ask)),
 * kept by the native as its own copy (val_copy) together with the handle.
 * @param handle Runtime handle.
 * @param func VAL_FUNC value of a script-defined function (borrowed).
 * @param args Argument values (may be NULL if argc is 0).
 * @param argc Number of arguments.
 * @param result Receives the return value, or NULL; may be NULL to discard it.
 * @param timeout_ms Maximum wait for the execution lock (TI_PORT_WAIT_FOREVER for no limit).
 * @return TI_OK, TI_ERR_TIMEOUT, TI_ERR_STALE_HANDLE, TI_ERR_INVALID_ARG (func NULL, not VAL_FUNC or
 *         the empty function), TI_ERR_UNDEFINED (func belongs to another runtime), TI_ERR_INTERRUPTED
 *         (runtime stopped) or the error kind the callback raised.
 */
ti_status_t ti_call(ti_handle_t handle, const value_t *func, value_t **args, int argc,
                    value_t **result, uint32_t timeout_ms);

/**
 * @brief Raise a runtime error from native code and unwind the running script.
 * The native must return NULL right after calling this. The error is reported at the line of the
 * call, and only the first error of a run is kept.
 * @param handle Runtime handle passed to the native function.
 * @param kind Error category.
 * @param fmt printf-style message format.
 */
void ti_raise_error(ti_handle_t handle, ti_status_t kind, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* !TIEN_INTERPRETER_H */
