#ifndef TIEN_INTERPRETER_H
#define TIEN_INTERPRETER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include "include/ti_type.h"
#include "include/ti_type_value.h"
#include "include/ti_type_value_dict.h"
#include "include/ti_type_value_list.h"
#include "include/ti_type_value_bytes.h"
#include "include/ti_type_func.h"
#include "include/ti_build_program.h"

/* -------------------- Platform & Callback Types -------------------- */

/**
 * @brief Callback type for interpreter log output (printf-style).
 */
typedef void (*ti_log_callback_t)(const char *fmt, va_list args);

/**
 * @brief Callback type for fatal unrecoverable interpreter error.
 */
typedef void (*ti_fatal_callback_t)(void);

/* -------------------- Platform & Logging API -------------------- */

/**
 * @brief Printf-style logging function dispatched to registered log callback.
 * @param fmt Format string.
 */
void ti_log(const char *fmt, ...);

/**
 * @brief Print a line of text from the given pointer up to newline or null terminator.
 * @param line Pointer to start of line.
 */
void ti_log_line(char *line);

/**
 * @brief Handles an unrecoverable fatal interpreter error (halts/exits).
 */
void ti_fatal(void);

/**
 * @brief Register custom logging callback (e.g. stdout for Desktop, Serial for Arduino).
 * @param func Pointer to callback function.
 */
void ti_register_log(ti_log_callback_t func);

/**
 * @brief Register custom fatal error callback (e.g. exit(1) for Desktop, halt for Arduino).
 * @param func Pointer to callback function.
 */
void ti_register_fatal(ti_fatal_callback_t func);

/* -------------------- Public Interpreter API -------------------- */

/**
 * @brief Initialize built-in interpreter functions (e.g. print).
 */
void ti_init_builtin(void);

/**
 * @brief Compile a Ti language source code string into an immutable AST program.
 * @param source_code Null-terminated source code string.
 * @return Newly allocated ti_program_t, or NULL on syntax error.
 */
ti_program_t *ti_compile(const char *source_code);

/**
 * @brief Create a new runtime instance.
 * @return Handle of the new runtime, or TI_INVALID_HANDLE if out of memory or no free slot.
 */
ti_handle_t ti_create(void);

/**
 * @brief Destroy a runtime, discard its pending events and invalidate its handle.
 * Every later call with this handle (from any driver or task) fails with TI_ERR_STALE_HANDLE.
 * Must not be called while ti_execute() is running on the same handle: call ti_stop() and
 * wait for ti_execute() to return first.
 * @param handle Runtime handle.
 * @return TI_OK, or TI_ERR_STALE_HANDLE if the handle is stale or invalid.
 */
ti_status_t ti_destroy(ti_handle_t handle);

/**
 * @brief Execute a compiled program on the specified runtime.
 * @param handle Runtime handle.
 * @param prog Pointer to compiled program.
 * @return TI_OK on completion, TI_ERR_INTERRUPTED if stopped, TI_ERR_STALE_HANDLE or TI_ERR_INVALID_ARG.
 */
ti_status_t ti_execute(ti_handle_t handle, ti_program_t *prog);

/**
 * @brief High-level helper to compile and execute a Ti script in one step.
 * @param source_code Null-terminated Ti language source code string.
 */
void ti_run_string(const char *source_code);

/**
 * @brief Request execution cancellation to immediately halt running script.
 * Safe to call from any task/thread (and from ISR on ESP32).
 * @param handle Runtime handle.
 * @return TI_OK, or TI_ERR_STALE_HANDLE if the handle is stale or invalid.
 */
ti_status_t ti_stop(ti_handle_t handle);

/* -------------------- Event Bridge API -------------------- */

/**
 * @brief Queue an event that invokes a TI callback function at the next safe point.
 * Ownership of every value in args is always transferred, whether or not the call succeeds.
 * Safe to call from any task/thread, but not from ISR (allocates memory).
 * @param handle Target runtime handle (typically stored by a native registration function).
 * @param func_name Name of the TI callback function (copied).
 * @param args Array of argument values (array is copied, values are owned by the call).
 * @param arg_count Number of arguments.
 * @return TI_OK, TI_ERR_STALE_HANDLE (runtime gone: drop the listener), TI_ERR_QUEUE_FULL,
 *         TI_ERR_NO_MEMORY or TI_ERR_INVALID_ARG.
 */
ti_status_t ti_post_event(ti_handle_t handle, const char *func_name, value_t **args, int arg_count);

/**
 * @brief Execute the pending events of a runtime immediately.
 * Intended for long-running native functions (e.g. a sliced delay) so callbacks are not blocked.
 * Must only be called from a native function executing on this runtime.
 * @param handle Runtime handle passed to the native function.
 * @return TI_OK, TI_ERR_INTERRUPTED if the runtime has been stopped, or TI_ERR_STALE_HANDLE.
 */
ti_status_t ti_dispatch_events(ti_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* !TIEN_INTERPRETER_H */
