#include "include/ti_type_value.h"
#include "include/ti_type.h"
#include "include/ti_type_value_dict.h"
#include "include/ti_type_value_list.h"
#include "include/ti_type_value_bytes.h"
#include "include/ti_type_value_handle.h"
#include "include/ti_type_func.h"
#include "include/tracked_memory.h"
#include "include/debug.h"
#include "TienInterpreter.h"
#include <stdint.h>
#include <string.h>

/* Shared success sentinel (see TI_VAL_OK in ti_type_value.h): static storage, never freed */
value_t g_val_ok = { .type = VAL_NULL };

/* -------------------- Value Constructors & Destructors -------------------- */

/* Allocate a new value_t of the specified type */
value_t *val_init(int type)
{
    /* VAL_ANY only describes what a native parameter accepts; no value can have that type */
    if (type == VAL_ANY) {
        return NULL;
    }

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
    if (value == NULL || value == TI_VAL_OK) {
        return;
    }
    /* Release dynamically allocated string buffer if present */
    if (value->type == VAL_STRING && value->string_val != NULL) {
        ti_raw_free(value->string_val);
        value->string_val = NULL;
    }
    /* Release dictionary reference and free resources if refcount reaches 0 */
    else if (value->type == VAL_DICT && value->dict_val != NULL) {
        dict_release(value->dict_val);
        value->dict_val = NULL;
    }
    /* Release bytes reference and free the buffer if refcount reaches 0 */
    else if (value->type == VAL_BYTES && value->bytes_val != NULL) {
        bytes_release(value->bytes_val);
        value->bytes_val = NULL;
    }
    /* Release handle reference and destroy the native object if refcount reaches 0 */
    else if (value->type == VAL_HANDLE && value->handle_val != NULL) {
        handle_release(value->handle_val);
        value->handle_val = NULL;
    }
    /* Release list reference and free its items if refcount reaches 0 */
    else if (value->type == VAL_LIST && value->list_val != NULL) {
        list_release(value->list_val);
        value->list_val = NULL;
    }
}

/* Free an entire value_t structure along with its dynamic payload */
void val_free(value_t *value)
{
    /* Guard against null value pointer */
    if (value == NULL || value == TI_VAL_OK) {
        return;
    }
    /* Release internal dynamic payload */
    val_free_internal(value);
    /* Release outer value structure using raw memory allocator */
    ti_raw_free(value);
}

value_t *val_new_dict(void)
{
    /* Create new value struct */
    value_t *val = val_init(VAL_DICT);
    if (val == NULL) {
        return NULL;
    }

    /* Out of memory: discard the half-built value */
    val->dict_val = dict_create();
    if (val->dict_val == NULL) {
        ti_raw_free(val);
        return NULL;
    }
    return val;
}

/* Create an empty list value_t with the given element type */
value_t *val_new_list(val_type_t elem_type)
{
    value_t *val = val_init(VAL_LIST);
    if (val == NULL) {
        return NULL;
    }

    /* Invalid element type or out of memory: discard the half-built value */
    val->list_val = list_create(elem_type);
    if (val->list_val == NULL) {
        ti_raw_free(val);
        return NULL;
    }
    return val;
}
/* Check whether a value of type incoming may replace one of type current */
bool val_can_replace(val_type_t current, val_type_t incoming)
{
    /* A held null has lost its type: nothing may replace it */
    if (current == VAL_NULL) {
        return false;
    }

    /* null replaces any value; anything else must keep the type */
    return incoming == VAL_NULL || incoming == current;
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
    case VAL_DICT:
        /* Create new value point to the same dict of current dict */
        copy->dict_val = val->dict_val;
        dict_retain(copy->dict_val); // Increase the reference to dict value
        break;
    case VAL_LIST:
        /* Share the same list object (reference semantics, like dict) */
        copy->list_val = val->list_val;
        list_retain(copy->list_val);
        break;
    case VAL_BYTES:
        /* Share the same buffer (reference semantics, like list and dict) */
        copy->bytes_val = val->bytes_val;
        bytes_retain(copy->bytes_val);
        break;
    case VAL_HANDLE:
        /* Share the same native object: C objects cannot be copied, so even deep copies share it */
        copy->handle_val = val->handle_val;
        handle_retain(copy->handle_val);
        break;
    case VAL_MODULE:
        /* The module table is static const and never freed: copying the address is enough */
        copy->module_val = val->module_val;
        break;
    case VAL_FUNC:
        /* The function is borrowed from its namespace (never moves, lives with the runtime): copy the
         * pointer; NULL is the empty function ('func f;') */
        copy->func_val = val->func_val;
        break;
    case VAL_VOID:
    case VAL_NULL:
        /* No internal payload allocation needed for empty types */
        break;

    default:
        /* Unknown type tag is an interpreter invariant violation: pure helper, no rt, so signal with NULL */
        val_free(copy);
        return NULL;
    }
    return copy;
}

/* Context pass to call back of dict for each to save the result and depth */
typedef struct {
    dict_t *dst_dcit;
    int max_depth;
    bool success;
}dict_copy_ctx_t;
/* Call back is called each "key": "value", pair */
static bool copy_dict_entry_cb(const char *key, value_t *value, void *user_context)
{
    dict_copy_ctx_t *ctx = (dict_copy_ctx_t*) user_context;
    // Deep copy element value
    value_t *cloned_item = val_deep_copy(value, ctx->max_depth - 1);
    if (cloned_item == NULL)
    {
        ctx->success = false;
        return false;
    }
    // Add new value to dict directly without second-pass cloning
    ti_status_t status = val_dict_set_raw(ctx->dst_dcit, key, cloned_item);
    if (status != TI_OK)
    {
        ctx->success = false;
        return false;
    }
    return true; // Continue iterate
}

value_t *val_deep_copy(const value_t *src, uint8_t max_depth)
{
    value_t *result = NULL;
    // Base case 
    if (max_depth == 0 || src == NULL)
    {
        return NULL;
    }
    // If the container, try to deep copy
    if(src->type == VAL_LIST)
    {
        result = val_new_list(src->list_val->elem_type);
        if(result == NULL)
        {
            val_free(result);
            return NULL;
        }
        int count = src->list_val->count;
        for (int i = 0; i < count; i++)
        {
            // Get ith element at source
            value_t *item_src = &src->list_val->items[i];
            // Recusivelly deep copy the element with depth - 1
            value_t *cloned_item = val_deep_copy(item_src, max_depth - 1);
            if(cloned_item == NULL)
            {
                val_free(result);
                return NULL;
            }
            ti_status_t status = val_list_push_raw(result->list_val, cloned_item);
            if (status != TI_OK)
            {
                val_free(result);
                return NULL;
            }
        }
    }
    else if (src->type == VAL_DICT)
    {
        // For each key val and deep copy the value to key
        result = val_new_dict();
        dict_copy_ctx_t ctx = {result->dict_val, max_depth, true};
        val_dict_foreach(src->dict_val, copy_dict_entry_cb, &ctx);
        if(!ctx.success)
        {
            val_free(result);
            return NULL;
        }
    }
    else
    {
        // If the scalar type, use val_copy
        result = val_copy(src);
    }
    return result;
}

/* Create a function value pointing to a script function */
value_t *val_new_func(function_t *func)
{
    /* The empty function is made by val_init(VAL_FUNC) (declaration default), never here */
    if (func == NULL) {
        return NULL;
    }

    value_t *val = val_init(VAL_FUNC);
    if (val == NULL) {
        return NULL;
    }

    /* Borrow the pointer: a namespace entry never moves and lives until the runtime is destroyed */
    val->func_val = func;
    return val;
}

/* Get the name of the function held by a VAL_FUNC value */
const char *val_func_name(const value_t *value)
{
    if (value == NULL || value->type != VAL_FUNC || value->func_val == NULL) {
        return NULL;
    }
    return value->func_val->name;
}
