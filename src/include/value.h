#ifndef TI_VALUE_H
#define TI_VALUE_H

#include "ti_type.h"
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
 * @param type Value type enum value.
 * @return Pointer to newly allocated value_t.
 */
value_t *val_init(int type);

/**
 * @brief Create a null value_t.
 * @return Newly allocated VAL_NULL value_t.
 */
value_t *val_new_null(void);

/**
 * @brief Create an integer value_t.
 * @param v Integer value.
 * @return Newly allocated VAL_INT value_t.
 */
value_t *val_new_int(int v);

/**
 * @brief Create a float value_t.
 * @param v Float value.
 * @return Newly allocated VAL_FLOAT value_t.
 */
value_t *val_new_float(float v);

/**
 * @brief Create a string value_t (duplicates string into tracked memory).
 * @param s String content (or NULL).
 * @return Newly allocated VAL_STRING value_t.
 */
value_t *val_new_string(const char *s);

/**
 * @brief Create a boolean value_t.
 * @param b Boolean value.
 * @return Newly allocated VAL_BOOL value_t.
 */
value_t *val_new_bool(bool b);

/**
 * @brief Create a void value_t (used for void function returns).
 * @return Newly allocated VAL_VOID value_t.
 */
value_t *val_new_void(void);

/**
 * @brief Free dynamically allocated payload inside value_t (e.g. string_val).
 * @param value Pointer to value_t.
 */
void val_free_internal(value_t *value);

#ifdef __cplusplus
}
#endif

#endif /* !TI_VALUE_H */
