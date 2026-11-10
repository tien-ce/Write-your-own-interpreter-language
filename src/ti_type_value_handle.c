#include "include/ti_type_value_handle.h"
#include "include/tracked_memory.h"
#include <stdlib.h>
#include <string.h>

/* -------------------- Static Function Prototypes -------------------- */

static handle_t *handle_of(const value_t *value, const ti_handle_type_t *type);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Get the handle object of a value if it is a handle of the expected type.
 * @param value Value of any type (may be NULL).
 * @param type Expected type descriptor, or NULL to accept any type.
 * @return The handle object (open or closed), or NULL if value is not a matching handle.
 */
static handle_t *handle_of(const value_t *value, const ti_handle_type_t *type)
{
    /* A wrong-typed value yields NULL instead of reading the wrong union member */
    if (value == NULL || value->type != VAL_HANDLE || value->handle_val == NULL) {
        return NULL;
    }

    /* One descriptor object per type, so its address is the type identity */
    if (type != NULL && value->handle_val->type != type) {
        return NULL;
    }
    return value->handle_val;
}

/* -------------------- Handle Lifecycle Operations -------------------- */

/* Allocate a handle reference object wrapping a native object */
handle_t *handle_create(void *ptr, const ti_handle_type_t *type)
{
    /* A handle without an object or a named type could never be used safely */
    if (ptr == NULL || type == NULL || type->name == NULL || type->name[0] == '\0') {
        return NULL;
    }

    handle_t *handle = (handle_t *)ti_raw_calloc(1, sizeof(handle_t));
    if (handle == NULL) {
        return NULL;
    }

    /* The type is borrowed (static descriptor); the caller receives the first reference */
    handle->ptr = ptr;
    handle->type = type;
    handle->refcount = 1;
    return handle;
}

/* Increment the reference counter of the handle */
void handle_retain(handle_t *handle)
{
    if (handle != NULL) {
        handle->refcount++;
    }
}

/* Decrement the reference counter; destroy the native object and free the handle at zero */
void handle_release(handle_t *handle)
{
    if (handle == NULL) {
        return;
    }

    handle->refcount--;
    if (handle->refcount > 0) {
        return;
    }

    /* A handle closed earlier has no object left to destroy; a NULL destructor means the native owns it */
    if (handle->ptr != NULL && handle->type->destroy != NULL) {
        handle->type->destroy(handle->ptr);
    }
    handle->ptr = NULL;
    ti_raw_free(handle);
}

/* -------------------- Native (C) Bridge Operations -------------------- */

/* Create a VAL_HANDLE value wrapping a native object */
value_t *val_new_handle(void *ptr, const ti_handle_type_t *type)
{
    handle_t *handle = handle_create(ptr, type);
    if (handle == NULL) {
        return NULL;
    }

    value_t *val = val_init(VAL_HANDLE);
    if (val == NULL) {
        /* Free just the header: ptr still belongs to the caller, so destroy must not run */
        ti_raw_free(handle);
        return NULL;
    }

    /* Ownership of ptr transfers only now that the value exists */
    val->handle_val = handle;
    return val;
}

/* Get the native object of a handle value after checking its type */
void *val_handle_get(const value_t *value, const ti_handle_type_t *type)
{
    /* NULL type would skip the check: the caller must always state what it expects */
    if (type == NULL) {
        return NULL;
    }

    handle_t *handle = handle_of(value, type);
    return (handle != NULL) ? handle->ptr : NULL;
}

/* Get the type descriptor of a handle value */
const ti_handle_type_t *val_handle_type(const value_t *value)
{
    handle_t *handle = handle_of(value, NULL);
    return (handle != NULL) ? handle->type : NULL;
}

/* Check whether a handle value still holds its native object */
bool val_handle_is_open(const value_t *value)
{
    handle_t *handle = handle_of(value, NULL);
    return handle != NULL && handle->ptr != NULL;
}

/* Close a handle now: destroy the native object and mark every copy as closed */
ti_status_t val_handle_close(value_t *value, const ti_handle_type_t *type)
{
    /* Same rule as val_handle_get: closing requires the expected type */
    if (type == NULL) {
        return TI_ERR_TYPE_MISMATCH;
    }

    handle_t *handle = handle_of(value, type);
    if (handle == NULL) {
        return TI_ERR_TYPE_MISMATCH;
    }
    if (handle->ptr == NULL) {
        return TI_ERR_INVALID_VALUE;
    }

    /* Clear ptr before destroying: every copy shares this handle, so all of them now read as closed */
    void *ptr = handle->ptr;
    handle->ptr = NULL;
    if (handle->type->destroy != NULL) {
        handle->type->destroy(ptr);
    }
    return TI_OK;
}

/* -------------------- Handle Type Method Lookup -------------------- */

/* Find a method of a handle type by name */
const ti_builtin_def_t *handle_type_find_method(const ti_handle_type_t *type, const char *method_name)
{
    /* An empty handle has no type, and a type may have no methods at all */
    if (type == NULL || method_name == NULL || type->methods == NULL) {
        return NULL;
    }

    /* Types have only a few methods: a linear scan is cheaper than a hashmap on the target */
    for (int i = 0; i < type->method_count; i++) {
        if (type->methods[i].name != NULL && strcmp(type->methods[i].name, method_name) == 0) {
            return &type->methods[i];
        }
    }
    return NULL;
}

