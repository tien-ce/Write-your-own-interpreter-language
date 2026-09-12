#include "TienInterpreter.h"
#include <stdio.h>
#include <stdlib.h>

/* -------------------- Static Variables -------------------- */

static ti_fatal_callback_t s_fatal_cb = NULL;
static ti_log_callback_t s_log_cb = NULL;

/* -------------------- Platform & Logging Functions -------------------- */

/* Register custom fatal error callback */
void ti_register_fatal(ti_fatal_callback_t func)
{
    s_fatal_cb = func;
}

/* Register custom logging callback */
void ti_register_log(ti_log_callback_t func)
{
    s_log_cb = func;
}

/* Print a line of source code up to newline or null terminator */
void ti_log_line(char *line)
{
    while (line != NULL && *line != '\n' && *line != '\0') {
        printf("%c", *line);
        line++;
    }
}

/* Printf-style logging function dispatched to registered log callback */
void ti_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (s_log_cb != NULL) {
        s_log_cb(fmt, args);
    }
    va_end(args);
}

/* Handles an unrecoverable fatal interpreter error */
void ti_fatal(void)
{
    free_all();
    if (s_fatal_cb != NULL) {
        s_fatal_cb();
    }
    /* Fallback exit if callback returns or was not registered */
    exit(1);
}

/* -------------------- Public Interpreter Functions -------------------- */

/* High-level helper to execute a Ti script from a source string */
void ti_run_string(const char *source_code)
{
    if (!source_code) {
        return;
    }

    char *contents = tracked_strdup(source_code);
    if (!contents) {
        ti_fatal();
        return;
    }

    lexer_t *lexer = lexer_init(contents);
    parser_t *parser = parser_init(lexer);
    ast_t *root = parser_parse(parser);
    context_t *context = context_init();

    visitor_visit(context, root);

    tracked_free(parser);
    context_free(context);
    tracked_free(lexer);
    ast_free(root);
    tracked_free(contents);
}
