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
 * Ownership of val is always transferred: on failure the value is freed.
 * @param dict Dictionary reference object.
 * @param key String identifier for the entry.
 * @param val Value to insert.
 * @return TI_OK, TI_ERR_TYPE_MISMATCH (key already holds another type) or TI_ERR_INVALID_ARG.
 */
ti_status_t val_dict_set(dict_t *dict, const char *key, value_t *val);

/**
 * @brief Retrieve a deep copy of the value associated with the key.
 * @param dict Dictionary reference object.
 * @param key String identifier for the entry.
 * @param out Receives the newly allocated copy (caller owns it) on success.
 * @return TI_OK, TI_ERR_KEY_NOT_FOUND, TI_ERR_NO_MEMORY or TI_ERR_INVALID_ARG.
 */
ti_status_t val_dict_get(dict_t *dict, const char *key, value_t **out);

/**
 * @brief Remove a key-value pair from the dictionary.
 * @param dict Dictionary reference object.
 * @param key String identifier for the entry.
 * @return TI_OK, TI_ERR_KEY_NOT_FOUND or TI_ERR_INVALID_ARG.
 */
ti_status_t val_dict_remove(dict_t *dict, const char *key);

/**
 * @brief Check if a key exists in the dictionary.
 * @param dict Dictionary reference object.
 * @param key String identifier for the entry.
 * @return true if key exists, false otherwise.
 */
bool val_dict_has_key(dict_t *dict, const char *key);

/**
 * @brief User Callback function signature for dictionary iteration.
 * @param key String identifier of the current entry.
 * @param value Pointer to the strongly-typed value object.
 * @param user_context Opaque pointer passed through to callback.
 * @return true to continue traversal, false to terminate early.
 */
typedef bool (*dict_foreach_cb)(const char *key, value_t *value, void *user_context);

/**
 * @brief Traverse all key-value entries in the dictionary and invoke the user callback.
 * @param dict Pointer to dictionary reference object.
 * @param user_callback Callback function invoked per entry.
 * @param user_context Opaque caller context passed directly to user_callback.
 */
void val_dict_foreach(dict_t *dict, dict_foreach_cb user_callback, void *user_context);

#ifdef __cplusplus
}
#endif

#endif /* TI_TYPE_VALUE_DICT_H */
