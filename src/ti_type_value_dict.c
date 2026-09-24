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
