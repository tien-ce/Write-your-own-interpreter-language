#ifndef TI_TYPE_VALUE_LIST_H
#define TI_TYPE_VALUE_LIST_H

#include "ti_type_value.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- List Configuration -------------------- */

/* Maximum number of elements a single list may hold (protects the heap on embedded targets) */
#ifndef TI_MAX_LIST_ITEMS
#define TI_MAX_LIST_ITEMS 1024
#endif

/* -------------------- List Lifecycle Functions -------------------- */

/**
 * @brief Allocate and initialize a new empty list reference object (refcount = 1).
 * No element storage is allocated until the first insertion.
 * @param elem_type Element type (VAL_INT, VAL_FLOAT, VAL_STRING or VAL_BOOL).
 * @return Pointer to the newly allocated list_t, or NULL on invalid type or out of memory.
 */
list_t *list_create(val_type_t elem_type);

/**
 * @brief Increment the reference counter for the list (Shared Ownership).
 * @param list Pointer to the list object.
 */
void list_retain(list_t *list);

/**
 * @brief Decrement the reference counter. If it reaches zero, destroys the list and its items.
 * @param list Pointer to the list object.
 */
void list_release(list_t *list);

/* -------------------- List Element Functions -------------------- */

/**
 * @brief Append a value to the end of the list with strict type checking.
 * Ownership of val is always transferred: on failure the value is freed.
 * @param list List reference object.
 * @param val Value to append (must match the list element type).
 * @return TI_OK, TI_ERR_TYPE_MISMATCH, TI_ERR_LIMIT_EXCEEDED, TI_ERR_NO_MEMORY or TI_ERR_INVALID_ARG.
 */
ti_status_t val_list_push(list_t *list, value_t *val);

/**
 * @brief Remove the last element of the list and hand it to the caller.
 * @param list List reference object.
 * @param out Receives the removed value (caller owns it) on success.
 * @return TI_OK, TI_ERR_INDEX_OUT_OF_RANGE (empty list), TI_ERR_NO_MEMORY or TI_ERR_INVALID_ARG.
 */
ti_status_t val_list_pop(list_t *list, value_t **out);

/**
 * @brief Retrieve a deep copy of the element at the given index.
 * @param list List reference object.
 * @param index Zero-based element index.
 * @param out Receives the newly allocated copy (caller owns it) on success.
 * @return TI_OK, TI_ERR_INDEX_OUT_OF_RANGE, TI_ERR_NO_MEMORY or TI_ERR_INVALID_ARG.
 */
ti_status_t val_list_get(list_t *list, int index, value_t **out);

/**
 * @brief Replace the element at the given index with strict type checking.
 * Ownership of val is always transferred: on failure the value is freed.
 * @param list List reference object.
 * @param index Zero-based element index.
 * @param val New value (must match the list element type).
 * @return TI_OK, TI_ERR_INDEX_OUT_OF_RANGE, TI_ERR_TYPE_MISMATCH or TI_ERR_INVALID_ARG.
 */
ti_status_t val_list_set(list_t *list, int index, value_t *val);

/**
 * @brief Get the number of elements in the list.
 * @param list List reference object.
 * @return Element count (0 if list is NULL).
 */
int val_list_count(list_t *list);

#ifdef __cplusplus
}
#endif

#endif /* !TI_TYPE_VALUE_LIST_H */
