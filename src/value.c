#include "include/value.h"
#include "include/tracked_memory.h"
#include <string.h>

/* -------------------- Value Constructors & Destructors -------------------- */

/* Allocate a new value_t of the specified type */
value_t *val_init(int type)
{
    value_t *value = tracked_calloc(1, sizeof(struct VALUE_STRUCT));
    value->type = (val_type_t)type;
    return value;
}

/* Create a null value_t */
value_t *val_new_null(void)
{
    return val_init(VAL_NULL);
}

/* Create an integer value_t */
value_t *val_new_int(int v)
{
    value_t *val = val_init(VAL_INT);
    val->int_val = v;
    return val;
}

/* Create a float value_t */
value_t *val_new_float(float v)
{
    value_t *val = val_init(VAL_FLOAT);
    val->float_val = v;
    return val;
}

/* Create a string value_t */
value_t *val_new_string(const char *s)
{
    value_t *val = val_init(VAL_STRING);
    val->string_val = s ? tracked_strdup(s) : NULL;
    return val;
}

/* Create a boolean value_t */
value_t *val_new_bool(bool b)
{
    value_t *val = val_init(VAL_BOOL);
    val->bool_val = b;
    return val;
}

/* Create a void value_t */
value_t *val_new_void(void)
{
    return val_init(VAL_VOID);
}

/* Free dynamically allocated payload inside value_t (e.g. string_val) */
void val_free_internal(value_t *value)
{
    if (value == NULL) {
        return;
    }
    if (value->type == VAL_STRING && value->string_val != NULL) {
        tracked_free(value->string_val);
        value->string_val = NULL;
    }
}
