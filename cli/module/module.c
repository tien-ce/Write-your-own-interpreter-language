#include "module.h"

/* -------------------- Public Functions -------------------- */

/* Register every CLI example module */
void ti_init_modules(void)
{
    /* One register function per module file */
    module_machine_register();
}
