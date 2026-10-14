#include "builtin.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* -------------------- Static Function Prototypes -------------------- */

static void ti_log_callback(const char *fmt, va_list args);
static void ti_fatal_callback(void);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Default desktop logging callback routing formatted text to standard output.
 * @param fmt Format string.
 * @param args Variadic argument list.
 */
static void ti_log_callback(const char *fmt, va_list args)
{
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    printf("%s", buffer);
}

/**
 * @brief Default desktop fatal error callback calling exit(1).
 */
static void ti_fatal_callback(void)
{
    exit(1);
}

/* -------------------- Public Functions -------------------- */

/* Initialize built-in interpreter native functions */
void ti_init_builtin(void)
{
    /* Host callbacks first, so registration problems can already be logged */
    ti_register_log(ti_log_callback);
    ti_register_fatal(ti_fatal_callback);

    /* One register function per topic file */
    builtin_core_register();
    builtin_event_register();
    builtin_callback_register();
    builtin_relay_register();
    builtin_autonics_register();
    builtin_storage_register();
    builtin_http_register();
    builtin_json_register();
    builtin_system_register();
    builtin_bytes_register();
}

void init_builtin(void)
{
    ti_init_builtin();
}
