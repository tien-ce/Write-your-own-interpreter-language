#include "chashmap.h" /* Reference to the external generic hashmap */
#include "include/ti_type_value_dict.h"
#include "include/tracked_memory.h"
#include "include/debug.h"
#include <stdlib.h>

/* Declaration for runtime error handling (usually in debug.h or TienInterpreter.h) */
extern void ti_log(const char *fmt, ...);
extern void ti_fatal(void);

/* Internal callback triggered by chashmap to safely free a dictionary payload. */
static void dict_payload_free_cb(void *payload)
{
    if (payload != NULL) {
        value_t *val = (value_t *)payload;
        /* Delegate to the interpreter's native value destructor */
        val_free(val);
    }
}

/* Allocate and initialize a new dictionary reference object. */
dict_t *dict_create(void)
{
    dict_t *new_dict = tracked_calloc(NULL, 1, sizeof(dict_t));
    if (new_dict == NULL) {
        return NULL;
    }
    new_dict->refcount = 1;
    new_dict->map = chashmap_create(16, dict_payload_free_cb);
    return new_dict;
}

/* Increment the reference counter for shared ownership. */
void dict_retain(dict_t *dict)
{
    if (dict != NULL) {
        dict->refcount++;
    }
}

/* Decrement the reference counter. Destroy the dictionary if it reaches zero. */
void dict_release(dict_t *dict)
{
    if (dict != NULL) {
        dict->refcount--;
        if (dict->refcount <= 0) {
            if (dict->map != NULL) {
                chashmap_destroy(dict->map);
            }
            tracked_free(NULL, dict);
        }
    }
}

/* Function used for native C interaction to set a key-value pair with strict typing. */
/* Note: Evaluator transfers ownership (Zero-copy), val MUST NOT be freed by caller after. */
void val_dict_set(dict_t *dict, const char *key, value_t *val)
{
    if (dict == NULL || key == NULL || val == NULL) {
        return;
    }

    value_t *cur_val = (value_t*)chashmap_get(dict->map, key);
    if (cur_val != NULL && cur_val->type != val->type)
    {
        ti_log("[Runtime Error] Type mismatch in dict assignment. Expected type %s but got type %s.\n", val_type_to_str(cur_val->type), val_type_to_str(val->type));
        ti_fatal();
    }

    /* Transfer ownership directly without val_copy, based on interpreter conventions */
    chashmap_set(dict->map, key, val);
}

/* Retrieve a deep copy of the value associated with the key. */
value_t *val_dict_get(dict_t *dict, const char *key)
{
    if (dict == NULL || key == NULL) {
        return NULL;
    }
    
    value_t *cur_val = (value_t*)chashmap_get(dict->map, key);
    if (cur_val == NULL) {
        return NULL;
    }
    
    /* Return a deep copy to strictly maintain Caller-Owns memory semantics */
    return val_copy(cur_val);
}

/* Remove a key-value pair from the dictionary. */
bool val_dict_remove(dict_t *dict, const char *key)
{
    if (dict == NULL || key == NULL) {
        return false;
    }
    return chashmap_remove(dict->map, key);
}

/* Check if a key exists in the dictionary. */
bool val_dict_has_key(dict_t *dict, const char *key)
{
    if (dict == NULL || key == NULL) {
        return false;
    }
    return chashmap_get(dict->map, key) != NULL;
}

/**
 * @brief Context payload used to propagate both the user callback and user context
 * through the single opaque parameter expected by the hashmap iteration API.
 */
struct dict_iter_context {
    dict_foreach_cb user_callback;
    void *user_context;
};

/**
 * @brief Internal adapter callback bridging generic hashmap payloads to interpreter values.
 * Performs type conversion from opaque void* payload to value_t* before invoking user callback.
 */
static bool dict_callback(const char *key, void *value, void *hashmap_context)
{
    /* Unpack traversal context */
    struct dict_iter_context *ctx = (struct dict_iter_context *)hashmap_context;

    /* Downcast opaque payload pointer to runtime value pointer */
    value_t *val = (value_t *)value;

    /* Dispatch to caller-defined callback with cast value and user context */
    return ctx->user_callback(key, val, ctx->user_context);
}

/* Traverse all key-value entries in the dictionary and invoke the user callback */
void val_dict_foreach(dict_t *dict, dict_foreach_cb user_callback, void *user_context)
{
    /* Validate input arguments */
    if (dict == NULL || dict->map == NULL || user_callback == NULL) {
        return;
    }

    /* Aggregate user callback and caller state into a stack-allocated context frame */
    struct dict_iter_context ctx = {
        .user_callback = user_callback,
        .user_context = user_context
    };

    /* Execute traversal via underlying hashmap iteration interface */
    chashmap_foreach(dict->map, dict_callback, &ctx);
}
