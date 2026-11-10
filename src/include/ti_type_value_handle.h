#ifndef TI_TYPE_VALUE_HANDLE_H
#define TI_TYPE_VALUE_HANDLE_H

#include "ti_type_value.h"
#include "ti_type_func.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- Handle Types -------------------- */

/**
 * @brief Destructor of the native object wrapped by a handle.
 * @param ptr The native object pointer stored in the handle (never NULL when called).
 */
typedef void (*handle_destroy_fn)(void *ptr);

/**
 * @brief Read attribute obj.name of a handle's native object (layer 1: never raises, the interpreter does).
 * @param ptr The native object (never NULL: a closed handle is rejected before the call).
 * @param name Attribute name written in the script.
 * @param out Receives a new value_t owned by the caller (only on TI_OK).
 * @return TI_OK, TI_ERR_UNDEFINED if the type has no such attribute, or any other status on failure
 *         (e.g. TI_ERR_NO_MEMORY).
 */
typedef ti_status_t (*handle_get_attr_fn)(void *ptr, const char *name, value_t **out);

/**
 * @brief Write attribute obj.name = value of a handle's native object (layer 1: never raises, the interpreter does).
 * @param ptr The native object (never NULL: a closed handle is rejected before the call).
 * @param name Attribute name written in the script.
 * @param value New value (borrowed: copy what must be kept). Its type is checked here, not by the interpreter.
 * @return TI_OK, TI_ERR_UNDEFINED if the type has no such attribute, TI_ERR_READ_ONLY, or any other status
 *         on failure (e.g. TI_ERR_TYPE_MISMATCH, TI_ERR_VALUE_OUT_OF_RANGE).
 */
typedef ti_status_t (*handle_set_attr_fn)(void *ptr, const char *name, const value_t *value);

/**
 * @brief Describes one kind of native object a handle can wrap (e.g. a device config).
 * Define exactly one static const instance per C type and pass its address; type checks compare
 * the address, so the descriptor must outlive every handle of its type (normally static const).
 */
struct HANDLE_TYPE_STRUCT {
    const char        *name;    /* Type name shown by print, to_string and error messages (not empty) */
    handle_destroy_fn  destroy; /* Called with ptr when the last reference is released or on close
                                 * (NULL when the native keeps ownership itself, e.g. a static object) */
    const ti_builtin_def_t *methods; /* Method table called as obj.name(...) (NULL: no methods). Same rows as a
                                      * built-in table; params[0] is self, so a free function's row can be reused */
    int                method_count; /* Number of rows in methods */
    handle_get_attr_fn get_attr;     /* Reads obj.name (NULL: no readable attributes) */
    handle_set_attr_fn set_attr;     /* Writes obj.name = value (NULL: no writable attributes) */
};

/* -------------------- Handle Lifecycle Functions -------------------- */

/**
 * @brief Allocate a handle reference object wrapping a native object (refcount = 1).
 * @param ptr Native object pointer (must not be NULL).
 * @param type Type descriptor (must not be NULL and its name must not be empty; borrowed, never freed).
 * @return Pointer to the new handle_t, or NULL on invalid arguments or out of memory
 *         (ptr is not destroyed then: it still belongs to the caller).
 */
handle_t *handle_create(void *ptr, const ti_handle_type_t *type);

/**
 * @brief Increment the reference counter of the handle (Shared Ownership).
 * @param handle Pointer to the handle object (NULL is ignored).
 */
void handle_retain(handle_t *handle);

/**
 * @brief Decrement the reference counter. If it reaches zero, destroys the native object with the type's
 * destructor (unless it was already closed) and frees the handle.
 * @param handle Pointer to the handle object (NULL is ignored).
 */
void handle_release(handle_t *handle);

/* -------------------- Native (C) Bridge Functions -------------------- */

/**
 * @brief Create a VAL_HANDLE value wrapping a native object.
 * On success the value owns ptr and destroys it with type->destroy when the last copy is freed.
 * @param ptr Native object pointer (must not be NULL).
 * @param type Type descriptor (must not be NULL and its name must not be empty).
 * @return Newly allocated VAL_HANDLE value_t, or NULL on invalid arguments or out of memory
 *         (ptr is not destroyed then: it still belongs to the caller).
 */
value_t *val_new_handle(void *ptr, const ti_handle_type_t *type);

/**
 * @brief Get the native object of a handle value after checking its type.
 * @param value Value received from a script (any type).
 * @param type Expected type descriptor (compared by address; NULL never matches).
 * @return The native pointer, or NULL if value is not a handle, the type differs, or the handle was closed.
 */
void *val_handle_get(const value_t *value, const ti_handle_type_t *type);

/**
 * @brief Get the type descriptor of a handle value (e.g. val_handle_type(v)->name for messages).
 * @param value A VAL_HANDLE value.
 * @return The type, also for a closed handle, or NULL if value is not a handle or is empty ('handle h;').
 */
const ti_handle_type_t *val_handle_type(const value_t *value);

/**
 * @brief Check whether a handle value still holds its native object.
 * @param value A VAL_HANDLE value.
 * @return true if value is an open handle, false if it is closed or not a handle.
 */
bool val_handle_is_open(const value_t *value);

/**
 * @brief Close a handle now: destroy the native object and mark every copy as closed.
 * Later val_handle_get calls on any copy return NULL, so use after close is detected instead of crashing.
 * @param value A VAL_HANDLE value.
 * @param type Expected type descriptor (prevents closing an object of another type; NULL never matches).
 * @return TI_OK, TI_ERR_TYPE_MISMATCH (not a handle or wrong type) or TI_ERR_INVALID_VALUE (already closed).
 */
ti_status_t val_handle_close(value_t *value, const ti_handle_type_t *type);

/* -------------------- Handle Type Method Lookup -------------------- */

/**
 * @brief Find a method of a handle type by name (used when a script calls obj.name(...)).
 * @param type Type descriptor of the receiver (NULL finds nothing).
 * @param method_name Method name written in the script.
 * @return The method's row in type->methods, or NULL if the type has no method with that name.
 */
const ti_builtin_def_t *handle_type_find_method(const ti_handle_type_t *type, const char *method_name);

#ifdef __cplusplus
}
#endif

#endif /* TI_TYPE_VALUE_HANDLE_H */
