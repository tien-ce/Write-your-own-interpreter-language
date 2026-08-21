#ifndef TIEN_INTERPRETER_H
#define TIEN_INTERPRETER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include "include/token.h"
#include "include/AST.h"
#include "include/lexer.h"
#include "include/parser.h"
#include "include/visitor.h"
#include "include/tracked_memory.h"

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
 * @brief Handles an unrecoverable fatal interpreter error (frees memory and halts/exits).
 */
void ti_fatal(void);

/**
 * @brief Register custom logging callback (e.g. stdout for Desktop, Serial for Arduino).
 */
void ti_register_log(ti_log_callback_t func);

/**
 * @brief Register custom fatal error callback (e.g. exit(1) for Desktop, halt for Arduino).
 */
void ti_register_fatal(ti_fatal_callback_t func);

/* -------------------- Public Interpreter API -------------------- */

/**
 * @brief Initialize built-in interpreter functions (e.g. print).
 */
void init_builtin(void);

/**
 * @brief High-level helper to execute a Ti script from a source string.
 * @param source_code Null-terminated Ti language source code string.
 */
void ti_run_string(const char *source_code);

#ifdef __cplusplus
}
#endif

#endif // TIEN_INTERPRETER_H
