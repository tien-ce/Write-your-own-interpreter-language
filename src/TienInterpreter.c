#include "TienInterpreter.h"
#include <stdio.h>

/* -------------------- Variables -------------------- */

static ti_fatal_callback_t ti_fatal_cb = NULL;
static ti_log_callback_t ti_log_cb = NULL;

/* -------------------- Platform & Logging Functions -------------------- */

void ti_register_fatal(ti_fatal_callback_t func)
{
    ti_fatal_cb = func;
}

void ti_register_log(ti_log_callback_t func)
{
    ti_log_cb = func;
}

void ti_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (ti_log_cb != NULL)
        ti_log_cb(fmt, args);
    va_end(args);
}

void ti_fatal(void)
{
    free_all();
    if (ti_fatal_cb != NULL)
    {
        ti_fatal_cb();
    }
}

/* -------------------- Public Interpreter Functions -------------------- */

void ti_run_string(const char *source_code)
{
    if (!source_code)
    {
        return;
    }

    char *contents = tracked_strdup(source_code);
    if (!contents)
    {
        ti_fatal();
        return;
    }

    lexer_t *lexer = init_lexer(contents);
    parser_t *parser = init_parser(lexer);
    ast_t *root = parser_parse(parser);
    context_t *context = init_interpreter_context();

    visitor_visit(context, root);

    tracked_free(parser);
    free_context(context);
    tracked_free((void*)lexer);
    free_ast(root);
    tracked_free(contents);
}
