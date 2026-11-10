#ifndef TI_TYPE_MODULE_H
#define TI_TYPE_MODULE_H

#include "ti_type_value.h"
#include "ti_type_func.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- Module Types -------------------- */

/**
 * @brief One named constant of a module (e.g. machine.SERIAL_8N1), stored as a static const value.
 * Only types that can be initialized statically are allowed: int, float, bool and string (dict, list,
 * bytes and handle need the heap). Scripts never hold this value: reading it returns a copy.
 */
typedef struct {
    const char *name;  /* Name after the dot: "SERIAL_8N1" (an identifier: must not start with a digit) */
    value_t     value; /* Constant value, e.g. { .type = VAL_INT, .int_val = 0x800001c } */
} ti_module_const_t;

/* -------------------- Constant Table Helpers -------------------- */

/*
 * Build one ti_module_const_t row. They use designated initializers to pick the union member,
 * so they compile in C only (C++17 has no designated initializers): define constant tables in a .c file.
 * Usage: static const ti_module_const_t s_consts[] = { TI_CONST_INT("SERIAL_8N1", 0x800001c) };
 */

/**
 * @brief Build an int constant row.
 * @param name Constant name (string literal).
 * @param v Integer value.
 */
#define TI_CONST_INT(name, v)     { (name), { .type = VAL_INT,    .int_val    = (v) } }

/**
 * @brief Build a float constant row.
 * @param name Constant name (string literal).
 * @param v Float value (e.g. 0.5f).
 */
#define TI_CONST_FLOAT(name, v)   { (name), { .type = VAL_FLOAT,  .float_val  = (v) } }

/**
 * @brief Build a bool constant row.
 * @param name Constant name (string literal).
 * @param v true or false.
 */
#define TI_CONST_BOOL(name, v)    { (name), { .type = VAL_BOOL,   .bool_val   = (v) } }

/**
 * @brief Build a string constant row.
 * The cast only fits the char * field: the literal is never written or freed (a script read gets a copy).
 * @param name Constant name (string literal).
 * @param s String literal.
 */
#define TI_CONST_STRING(name, s)  { (name), { .type = VAL_STRING, .string_val = (char *)(s) } }

/**
 * @brief A built-in module: a named group of functions and constants, all in static const tables.
 * Define exactly one static const instance per module; it must live for the whole program
 * (scripts hold only its address, so it is never copied or freed).
 */
struct MODULE_STRUCT {
    const char              *name;        /* Name used by 'import' and printed: "machine" */
    const ti_builtin_def_t  *funcs;       /* Functions called as module.name(args), without self (NULL: none) */
    int                      func_count;  /* Number of rows in funcs */
    const ti_module_const_t *consts;      /* Constants read as module.name (NULL: none) */
    int                      const_count; /* Number of rows in consts */
};

/* -------------------- Module Registry Functions -------------------- */

/**
 * @brief Make a module importable by scripts ('import <name>;').
 * Call at host startup, before any runtime is created (the registry is not locked).
 * @param module Static const module descriptor (borrowed: must live for the whole program).
 * @return true on success; false if module is NULL, its name is NULL or empty, the name is already
 *         registered, or TI_MAX_MODULES modules are already registered.
 */
bool ti_register_module(const ti_module_t *module);

/**
 * @brief Find a registered module by name.
 * @param name Module name written after 'import'.
 * @return The module, or NULL if no module is registered under that name.
 */
const ti_module_t *ti_module_find(const char *name);

/**
 * @brief Find a function of a module by name (module.name(args)).
 * @param module Module to search (NULL finds nothing).
 * @param name Function name written after the dot.
 * @return The function's row in module->funcs, or NULL if the module has no such function.
 */
const ti_builtin_def_t *ti_module_find_func(const ti_module_t *module, const char *name);

/**
 * @brief Find a constant of a module by name (module.name).
 * @param module Module to search (NULL finds nothing).
 * @param name Constant name written after the dot.
 * @return The constant's row in module->consts, or NULL if the module has no such constant.
 */
const ti_module_const_t *ti_module_find_const(const ti_module_t *module, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* TI_TYPE_MODULE_H */
