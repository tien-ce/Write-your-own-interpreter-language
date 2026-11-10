#ifndef CLI_MODULE_H
#define CLI_MODULE_H

#include "TienInterpreter.h"

/* -------------------- Module Registration (one function per module file) -------------------- */

/**
 * @brief Register every CLI example module so scripts can 'import' it.
 * Call once at startup, before any runtime is created.
 */
void ti_init_modules(void);

/**
 * @brief Register the 'machine' example module: machine.UART(...) and serial constants (module_machine.c).
 */
void module_machine_register(void);

#endif /* CLI_MODULE_H */
