#ifndef TI_RUNTIME_BUILTIN_H
#define TI_RUNTIME_BUILTIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- Core Built-in Functions -------------------- */

/**
 * @brief Register the built-in functions that belong to the language itself (not to a host).
 * Safe to call more than once: only the first call registers.
 * Called automatically by ti_create(), so every host gets these functions.
 */
void ti_register_core_builtins(void);

/* -------------------- Topic Registration (internal) -------------------- */

/** @brief Register to_string, to_int, to_float, to_bool. */
void ti_runtime_builtin_convert_register(void);

/** @brief Register bytes_to_string, string_to_bytes. */
void ti_runtime_builtin_bytes_register(void);

#ifdef __cplusplus
}
#endif

#endif /* !TI_RUNTIME_BUILTIN_H */
