#ifndef TI_TYPE_VALUE_H
#define TI_TYPE_VALUE_H

#include "ti_type.h"
#include "tracked_memory.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- Value Type -------------------- */

typedef struct VALUE_STRUCT {
    val_type_t type;
    union {
        int int_val;
        float float_val;
        char *string_val;
        bool bool_val;
    };
} value_t;

/* -------------------- Value Constructors & Destructors -------------------- */

/**
 * @brief Allocate a new value_t of the specified type.
 * @param list Pointer to head of allocation list (or NULL).
 * @param type Value type enum value.
 * @return Pointer to newly allocated value_t.
 */
value_t *val_init(alloc_hdr_t **list, int type);

/**
 * @brief Create a null value_t.
 * @param list Pointer to head of allocation list (or NULL).
 * @return Newly allocated VAL_NULL value_t.
 */
value_t *val_new_null(alloc_hdr_t **list);

/**
 * @brief Create an integer value_t.
 * @param list Pointer to head of allocation list (or NULL).
 * @param v Integer value.
 * @return Newly allocated VAL_INT value_t.
 */
value_t *val_new_int(alloc_hdr_t **list, int v);

/**
 * @brief Create a float value_t.
 * @param list Pointer to head of allocation list (or NULL).
 * @param v Float value.
 * @return Newly allocated VAL_FLOAT value_t.
 */
value_t *val_new_float(alloc_hdr_t **list, float v);

/**
 * @brief Create a string value_t (duplicates string into tracked memory).
 * @param list Pointer to head of allocation list (or NULL).
 * @param s String content (or NULL).
 * @return Newly allocated VAL_STRING value_t.
 */
value_t *val_new_string(alloc_hdr_t **list, const char *s);

/**
 * @brief Create a boolean value_t.
 * @param list Pointer to head of allocation list (or NULL).
 * @param b Boolean value.
 * @return Newly allocated VAL_BOOL value_t.
 */
value_t *val_new_bool(alloc_hdr_t **list, bool b);

/**
 * @brief Create a void value_t (used for void function returns).
 * @param list Pointer to head of allocation list (or NULL).
 * @return Newly allocated VAL_VOID value_t.
 */
value_t *val_new_void(alloc_hdr_t **list);

/**
 * @brief Free dynamically allocated payload inside value_t (e.g. string_val).
 * @param list Pointer to head of allocation list (or NULL).
 * @param value Pointer to value_t.
 */
void val_free_internal(alloc_hdr_t **list, value_t *value);

/**
 * @brief Free an entire value_t structure along with its dynamic payload.
 * @param list Pointer to head of allocation list (or NULL).
 * @param value Pointer to value_t to deallocate.
 */
void val_free(alloc_hdr_t **list, value_t *value);

/**
 * @brief Create an independent deep copy of a value_t structure.
 * @param list Pointer to head of allocation list (or NULL).
 * @param val Source value pointer to clone.
 * @return Newly allocated value_t clone, or NULL if source is NULL.
 */
value_t *val_copy(alloc_hdr_t **list, const value_t *val);
#ifdef __cplusplus
}
#endif

#endif /* !TI_TYPE_VALUE_H */
