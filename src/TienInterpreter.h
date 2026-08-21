#ifndef TIEN_INTERPRETER_H
#define TIEN_INTERPRETER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "include/token.h"
#include "include/AST.h"
#include "include/lexer.h"
#include "include/parser.h"
#include "include/visitor.h"
#include "include/tracked_memory.h"
#include "include/platform.h"

/* Initialize built-in interpreter functions (e.g. print) */
void init_builtin(void);

/* High-level helper to execute a Ti script from a string buffer */
void ti_run_string(const char *source_code);

#ifdef __cplusplus
}
#endif

#endif // TIEN_INTERPRETER_H
