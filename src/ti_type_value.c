#include "include/ti_type_value.h"
#include "include/tracked_memory.h"
#include "TienInterpreter.h"
#include <string.h>

/* -------------------- Value Constructors & Destructors -------------------- */

/* Allocate a new value_t of the specified type */
value_t *val_init(int type)
{
    /* Allocate zero-initialized memory for value structure using raw memory allocator */
    value_t *value = (value_t *)ti_raw_calloc(1, sizeof(struct VALUE_STRUCT));
    if (value) {
        /* Set runtime type tag */
        value->type = (val_type_t)type;
    }
    return value;
}

/* Create a null value_t */
value_t *val_new_null(void)
{
    /* Initialize value tagged as VAL_NULL */
    return val_init(VAL_NULL);
}

/* Create an integer value_t */
value_t *val_new_int(int v)
{
    /* Allocate and initialize integer value container */
    value_t *val = val_init(VAL_INT);
    if (val) {
        /* Store integer payload */
        val->int_val = v;
    }
    return val;
}

/* Create a float value_t */
value_t *val_new_float(float v)
{
    /* Allocate and initialize floating-point value container */
    value_t *val = val_init(VAL_FLOAT);
    if (val) {
        /* Store floating-point payload */
        val->float_val = v;
    }
    return val;
}

/* Create a string value_t */
value_t *val_new_string(const char *s)
{
    /* Allocate and initialize string value container */
    value_t *val = val_init(VAL_STRING);
    if (val) {
        /* Duplicate string payload using raw memory allocator */
        val->string_val = s ? ti_raw_strdup(s) : NULL;
    }
    return val;
}

/* Create a boolean value_t */
value_t *val_new_bool(bool b)
{
    /* Allocate and initialize boolean value container */
    value_t *val = val_init(VAL_BOOL);
    if (val) {
        /* Store boolean payload */
        val->bool_val = b;
    }
    return val;
}

/* Create a void value_t */
value_t *val_new_void(void)
{
    /* Initialize value tagged as VAL_VOID */
    return val_init(VAL_VOID);
}

/* Free dynamically allocated payload inside value_t (e.g. string_val) */
void val_free_internal(value_t *value)
{
    /* Guard against null value pointer */
    if (value == NULL) {
        return;
    }
    /* Release dynamically allocated string buffer if present */
    if (value->type == VAL_STRING && value->string_val != NULL) {
        ti_raw_free(value->string_val);
        value->string_val = NULL;
    }
}

/* Free an entire value_t structure along with its dynamic payload */
void val_free(value_t *value)
{
    /* Guard against null value pointer */
    if (value == NULL) {
        return;
    }
    /* Release internal dynamic payload */
    val_free_internal(value);
    /* Release outer value structure using raw memory allocator */
    ti_raw_free(value);
}

/**
 * @brief Create an independent deep copy of a value_t structure.
 */
value_t *val_copy(const value_t *val)
{
    /* Guard against null source value */
    if (val == NULL) {
        return NULL;
    }
    /* Create struct value_t with same value type */
    value_t *copy = val_init(val->type);
    if (!copy) {
        return NULL;
    }
    switch (val->type) {
    case VAL_INT:
        /* Copy integer scalar payload */
        copy->int_val = val->int_val;
        break;

    case VAL_FLOAT:
        /* Copy floating-point scalar payload */
        copy->float_val = val->float_val;
        break;

    case VAL_STRING:
        /* Deep copy string payload into raw memory */
        copy->string_val = val->string_val ? ti_raw_strdup(val->string_val) : NULL;
        break;

    case VAL_BOOL:
        /* Copy boolean scalar payload */
        copy->bool_val = val->bool_val;
        break;

    case VAL_VOID:
    case VAL_NULL:
        /* No internal payload allocation needed for empty types */
        break;

    default:
        /* Log error and terminate on unhandled type */
        ti_log("[ERROR]: Unknown value type %d in val_copy\n", (int)val->type);
        ti_fatal();
        break;
    }
    return copy;
}
