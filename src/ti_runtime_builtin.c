#include "include/ti_runtime_builtin.h"
#include <stdbool.h>

/* -------------------- Static Variables -------------------- */

/* Set once the core built-ins have been registered */
static bool s_core_registered = false;

/* -------------------- Public Functions -------------------- */

/* Register the language-level built-in functions */
void ti_register_core_builtins(void)
{
    /* Registration happens once, whichever host or runtime asks first */
    if (s_core_registered) {
        return;
    }
    s_core_registered = true;

    /* One register function per topic file */
    ti_runtime_builtin_convert_register();
    ti_runtime_builtin_bytes_register();
    ti_runtime_builtin_math_register();
}
