#include "include/ti_type_module.h"
#include "TienInterpreter.h"
#include <string.h>

/* -------------------- Static Variables -------------------- */

/* Registered modules: only pointers to static const descriptors (filled at host startup, never freed) */
static const ti_module_t *s_modules[TI_MAX_MODULES];
static int s_module_count = 0;

/* -------------------- Module Registry Operations -------------------- */

/* Make a module importable by scripts */
bool ti_register_module(const ti_module_t *module)
{
    /* A module without a name could never be imported */
    if (module == NULL || module->name == NULL || module->name[0] == '\0') {
        return false;
    }

    /* Names must be unique: import would always find the first one */
    if (ti_module_find(module->name) != NULL) {
        return false;
    }
    if (s_module_count >= TI_MAX_MODULES) {
        return false;
    }

    /* Keep only the address: the descriptor itself stays where the native defined it */
    s_modules[s_module_count] = module;
    s_module_count++;
    return true;
}

/* Find a registered module by name */
const ti_module_t *ti_module_find(const char *name)
{
    if (name == NULL) {
        return NULL;
    }

    /* Few modules: a linear scan needs no extra memory */
    for (int i = 0; i < s_module_count; i++) {
        if (strcmp(s_modules[i]->name, name) == 0) {
            return s_modules[i];
        }
    }
    return NULL;
}

/* Find a function of a module by name */
const ti_builtin_def_t *ti_module_find_func(const ti_module_t *module, const char *name)
{
    /* A module may have no functions at all */
    if (module == NULL || name == NULL || module->funcs == NULL) {
        return NULL;
    }

    for (int i = 0; i < module->func_count; i++) {
        if (module->funcs[i].name != NULL && strcmp(module->funcs[i].name, name) == 0) {
            return &module->funcs[i];
        }
    }
    return NULL;
}

/* Find a constant of a module by name */
const ti_module_const_t *ti_module_find_const(const ti_module_t *module, const char *name)
{
    /* A module may have no constants at all */
    if (module == NULL || name == NULL || module->consts == NULL) {
        return NULL;
    }

    for (int i = 0; i < module->const_count; i++) {
        if (module->consts[i].name != NULL && strcmp(module->consts[i].name, name) == 0) {
            return &module->consts[i];
        }
    }
    return NULL;
}
