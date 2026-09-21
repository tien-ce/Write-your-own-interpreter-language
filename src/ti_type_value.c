#include "include/ti_type_value.h"
#include "include/tracked_memory.h"
#include "TienInterpreter.h"
#include <string.h>

/* -------------------- Value Constructors & Destructors -------------------- */

/* Allocate a new value_t of the specified type */
value_t *val_init(alloc_hdr_t **list, int type)
{
    value_t *value = tracked_calloc(list, 1, sizeof(struct VALUE_STRUCT));
    if (value) {
        value->type = (val_type_t)type;
    }
    return value;
}

/* Create a null value_t */
value_t *val_new_null(alloc_hdr_t **list)
{
    return val_init(list, VAL_NULL);
}

/* Create an integer value_t */
value_t *val_new_int(alloc_hdr_t **list, int v)
{
    value_t *val = val_init(list, VAL_INT);
    if (val) {
        val->int_val = v;
    }
    return val;
}

/* Create a float value_t */
value_t *val_new_float(alloc_hdr_t **list, float v)
{
    value_t *val = val_init(list, VAL_FLOAT);
    if (val) {
        val->float_val = v;
    }
    return val;
}

/* Create a string value_t */
value_t *val_new_string(alloc_hdr_t **list, const char *s)
{
    value_t *val = val_init(list, VAL_STRING);
    if (val) {
        val->string_val = s ? tracked_strdup(list, s) : NULL;
    }
    return val;
}

/* Create a boolean value_t */
value_t *val_new_bool(alloc_hdr_t **list, bool b)
{
    value_t *val = val_init(list, VAL_BOOL);
    if (val) {
        val->bool_val = b;
    }
    return val;
}

/* Create a void value_t */
value_t *val_new_void(alloc_hdr_t **list)
{
    return val_init(list, VAL_VOID);
}

/* Free dynamically allocated payload inside value_t (e.g. string_val) */
void val_free_internal(alloc_hdr_t **list, value_t *value)
{
    if (value == NULL) {
        return;
    }
    if (value->type == VAL_STRING && value->string_val != NULL) {
        tracked_free(list, value->string_val);
        value->string_val = NULL;
    }
}

/* Free an entire value_t structure along with its dynamic payload */
void val_free(alloc_hdr_t **list, value_t *value)
{
    if (value == NULL) {
        return;
    }
    val_free_internal(list, value);
    tracked_free(list, value);
}

/**
 * @brief Create an independent deep copy of a value_t structure.
 */
value_t *val_copy(alloc_hdr_t **list, const value_t *val)
{
    if (val == NULL) {
        return NULL;
    }
    /* Create struct value_t with same value type */
    value_t *copy = val_init(list, val->type);
    if (!copy) {
        return NULL;
    }
    switch (val->type) {
    case VAL_INT:
        copy->int_val = val->int_val;
        break;

    case VAL_FLOAT:
        copy->float_val = val->float_val;
        break;

    case VAL_STRING:
        /* Deep copy string value */
        copy->string_val = val->string_val ? tracked_strdup(list, val->string_val) : NULL;
        break;

    case VAL_BOOL:
        copy->bool_val = val->bool_val;
        break;

    case VAL_VOID:
    case VAL_NULL:
        break;

    default:
        ti_log("[ERROR]: Unknown value type %d in val_copy\n", (int)val->type);
        ti_fatal();
        break;
    }
    return copy;
}
