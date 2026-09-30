#include "include/ti_type_value_list.h"
#include "include/tracked_memory.h"
#include <stdlib.h>

/* -------------------- Constants -------------------- */

/* Number of slots allocated on the first insertion */
#define LIST_INITIAL_CAPACITY 4

/* -------------------- Static Function Prototypes -------------------- */

static bool list_is_valid_elem_type(val_type_t type);
static bool list_reserve(list_t *list, int min_capacity);
static void list_move_in(value_t *slot, value_t *val);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Check whether a type may be stored as a list element.
 * @param type Candidate element type.
 * @return true for scalar and string types, false for containers and void/null.
 */
static bool list_is_valid_elem_type(val_type_t type)
{
    /* Containers are excluded: refcounting cannot reclaim reference cycles */
    return type == VAL_INT || type == VAL_FLOAT || type == VAL_STRING || type == VAL_BOOL;
}

/**
 * @brief Ensure the list can hold at least min_capacity elements, growing geometrically.
 * @param list List reference object.
 * @param min_capacity Required number of slots.
 * @return true if capacity is sufficient, false on limit or out of memory.
 */
static bool list_reserve(list_t *list, int min_capacity)
{
    if (min_capacity <= list->capacity) {
        return true;
    }
    if (min_capacity > TI_MAX_LIST_ITEMS) {
        return false;
    }

    /* Double the capacity (starting from the initial size) and clamp to the configured limit */
    int new_capacity = (list->capacity > 0) ? list->capacity * 2 : LIST_INITIAL_CAPACITY;
    if (new_capacity < min_capacity) {
        new_capacity = min_capacity;
    }
    if (new_capacity > TI_MAX_LIST_ITEMS) {
        new_capacity = TI_MAX_LIST_ITEMS;
    }

    /* Keep the old block intact if reallocation fails */
    value_t *new_items = (value_t *)ti_raw_realloc(list->items, (size_t)new_capacity * sizeof(value_t));
    if (new_items == NULL) {
        return false;
    }
    list->items = new_items;
    list->capacity = new_capacity;
    return true;
}

/**
 * @brief Move a heap value into an inline slot and release the emptied value_t shell.
 * @param slot Destination slot inside the items array.
 * @param val Source value whose payload ownership moves to the slot.
 */
static void list_move_in(value_t *slot, value_t *val)
{
    /* Shallow struct copy transfers payload ownership (e.g. string_val) to the slot */
    *slot = *val;
    ti_raw_free(val);
}

/* -------------------- List Lifecycle Operations -------------------- */

/* Allocate and initialize a new empty list reference object */
list_t *list_create(val_type_t elem_type)
{
    if (!list_is_valid_elem_type(elem_type)) {
        return NULL;
    }

    list_t *list = (list_t *)ti_raw_calloc(1, sizeof(list_t));
    if (list == NULL) {
        return NULL;
    }

    /* Storage is allocated lazily so empty lists cost only the header */
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
    list->elem_type = elem_type;
    list->refcount = 1;
    return list;
}

/* Increment the reference counter for shared ownership */
void list_retain(list_t *list)
{
    if (list != NULL) {
        list->refcount++;
    }
}

/* Decrement the reference counter. Destroy the list if it reaches zero */
void list_release(list_t *list)
{
    if (list == NULL) {
        return;
    }

    list->refcount--;
    if (list->refcount > 0) {
        return;
    }

    /* Release each item payload (items are inline, so only their contents are freed) */
    for (int i = 0; i < list->count; i++) {
        val_free_internal(&list->items[i]);
    }
    ti_raw_free(list->items);
    ti_raw_free(list);
}

/* -------------------- List Element Operations -------------------- */

/* Append a value to the end of the list with strict type checking */
ti_status_t val_list_push(list_t *list, value_t *val)
{
    if (val == NULL) {
        return TI_ERR_INVALID_ARG;
    }

    /* Every rejection below still consumes the value */
    ti_status_t status = TI_OK;
    if (list == NULL) {
        status = TI_ERR_INVALID_ARG;
    } else if (val->type != list->elem_type) {
        status = TI_ERR_TYPE_MISMATCH;
    } else if (list->count + 1 > TI_MAX_LIST_ITEMS) {
        status = TI_ERR_LIMIT_EXCEEDED;
    } else if (!list_reserve(list, list->count + 1)) {
        status = TI_ERR_NO_MEMORY;
    }

    if (status != TI_OK) {
        val_free(val);
        return status;
    }

    list_move_in(&list->items[list->count], val);
    list->count++;
    return TI_OK;
}

/* Remove the last element of the list and hand it to the caller */
ti_status_t val_list_pop(list_t *list, value_t **out)
{
    if (list == NULL || out == NULL) {
        return TI_ERR_INVALID_ARG;
    }
    if (list->count == 0) {
        return TI_ERR_INDEX_OUT_OF_RANGE;
    }

    /* Allocate the result shell first so a failure leaves the list unchanged */
    value_t *result = (value_t *)ti_raw_malloc(sizeof(value_t));
    if (result == NULL) {
        return TI_ERR_NO_MEMORY;
    }

    /* Move the last item payload out of the list */
    list->count--;
    *result = list->items[list->count];
    *out = result;
    return TI_OK;
}

/* Retrieve a deep copy of the element at the given index */
ti_status_t val_list_get(list_t *list, int index, value_t **out)
{
    if (list == NULL || out == NULL) {
        return TI_ERR_INVALID_ARG;
    }
    if (index < 0 || index >= list->count) {
        return TI_ERR_INDEX_OUT_OF_RANGE;
    }

    /* Return a deep copy to strictly maintain Caller-Owns memory semantics */
    value_t *copy = val_copy(&list->items[index]);
    if (copy == NULL) {
        return TI_ERR_NO_MEMORY;
    }
    *out = copy;
    return TI_OK;
}

/* Replace the element at the given index with strict type checking */
ti_status_t val_list_set(list_t *list, int index, value_t *val)
{
    if (val == NULL) {
        return TI_ERR_INVALID_ARG;
    }

    /* Every rejection below still consumes the value */
    ti_status_t status = TI_OK;
    if (list == NULL) {
        status = TI_ERR_INVALID_ARG;
    } else if (index < 0 || index >= list->count) {
        status = TI_ERR_INDEX_OUT_OF_RANGE;
    } else if (val->type != list->elem_type) {
        status = TI_ERR_TYPE_MISMATCH;
    }

    if (status != TI_OK) {
        val_free(val);
        return status;
    }

    /* Drop the old payload, then move the new one into the same slot */
    val_free_internal(&list->items[index]);
    list_move_in(&list->items[index], val);
    return TI_OK;
}

/* Get the number of elements in the list */
int val_list_count(list_t *list)
{
    return (list != NULL) ? list->count : 0;
}
