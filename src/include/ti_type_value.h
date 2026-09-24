#ifndef TI_TYPE_VALUE_H
#define TI_TYPE_VALUE_H

#include "ti_type.h"
#include "tracked_memory.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- Value Type -------------------- */

/* Forward declaration for the generic hash map struct from external library */
struct CHASHMAP_STRUCT;

typedef struct DICT_STRUCT dict_t;

/**
 * @brief Encapsulates the state and structure of a hash table based dictionary.
 * Uses the external chashmap library for internal storage.
 */
struct DICT_STRUCT {
    struct CHASHMAP_STRUCT *map; /* Pointer to the core generic hashmap */
    int refcount;                /* Counter tracking active references to manage memory deallocation */
};

typedef struct VALUE_STRUCT {
    val_type_t type;
    union {
        int int_val;
        float float_val;
        char *string_val;
        bool bool_val;
        dict_t *dict_val;
    };
} value_t;

/* -------------------- Value Constructors & Destructors -------------------- */

/**
 * @brief Allocate a new value_t of the specified type using raw memory allocation.
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
 * @brief Create a string value_t (duplicates string into raw memory).
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

value_t *val_new_dict(void);
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

/**
 * @brief Free an entire value_t structure along with its dynamic payload.
 * @param value Pointer to value_t to deallocate.
 */
void val_free(value_t *value);

/**
 * @brief Create an independent deep copy of a value_t structure.
 * @param val Source value pointer to clone.
 * @return Newly allocated value_t clone, or NULL if source is NULL.
 */
value_t *val_copy(const value_t *val);

/* -------------------- Dictionary Functions -------------------- */

/**
 * @brief Create a new dictionary value_t.
 * @return Newly allocated VAL_DICT value_t.
 */
value_t *val_new_dict(void);

/**
 * @brief Set a key-value pair in a dictionary.
 * @param dict Pointer to dictionary.
 * @param key Key string.
 * @param value Value to insert.
 */
void dict_set(dict_t *dict, const char *key, struct VALUE_STRUCT *value);

/**
 * @brief Get a value from a dictionary by key.
 * @param dict Pointer to dictionary.
 * @param key Key string.
 * @return Value associated with key, or NULL if not found.
 */
struct VALUE_STRUCT *dict_get(dict_t *dict, const char *key);

/**
 * @brief Remove a key-value pair from a dictionary.
 * @param dict Pointer to dictionary.
 * @param key Key string.
 */
void dict_remove(dict_t *dict, const char *key);
#ifdef __cplusplus
}
#endif

#endif /* !TI_TYPE_VALUE_H */
