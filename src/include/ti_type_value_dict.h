#ifndef TI_TYPE_VALUE_DICT_H
#define TI_TYPE_VALUE_DICT_H

#include "ti_type_value.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Allocate and initialize a new dictionary reference object.
 * @return Pointer to the newly allocated dict_t struct.
 */
dict_t *dict_create(void);

/**
 * @brief Increment the reference counter for the dictionary (Shared Ownership).
 * @param dict Pointer to the dictionary object.
 */
void dict_retain(dict_t *dict);

/**
 * @brief Decrement the reference counter. If it reaches zero, destroys the dictionary.
 * @param dict Pointer to the dictionary object.
 */
void dict_release(dict_t *dict);

/**
 * @brief Insert or update a key-value pair in a dictionary with strict type checking.
 * @param dict Dictionary reference object.
 * @param key String identifier for the entry.
 * @param val Value to insert (ownership is transferred).
 */
void val_dict_set(dict_t *dict, const char *key, value_t *val);

/**
 * @brief Retrieve a deep copy of the value associated with the key.
 * @param dict Dictionary reference object.
 * @param key String identifier for the entry.
 * @return A newly allocated value_t* (copy), or NULL if key does not exist.
 */
value_t *val_dict_get(dict_t *dict, const char *key);

/**
 * @brief Remove a key-value pair from the dictionary.
 * @param dict Dictionary reference object.
 * @param key String identifier for the entry.
 * @return true if successful, false if key does not exist.
 */
bool val_dict_remove(dict_t *dict, const char *key);

/**
 * @brief Check if a key exists in the dictionary.
 * @param dict Dictionary reference object.
 * @param key String identifier for the entry.
 * @return true if key exists, false otherwise.
 */
bool val_dict_has_key(dict_t *dict, const char *key);

#ifdef __cplusplus
}
#endif

#endif /* TI_TYPE_VALUE_DICT_H */
