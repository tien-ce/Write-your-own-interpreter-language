#include "builtin.h"
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>

/* -------------------- Static Function Prototypes -------------------- */

static value_t *builtin_get_json(ti_handle_t handle, value_t **argv, int argc);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Built-in native get_json function using cJSON.
 * @param handle Calling runtime handle (unused).
 * @param argv Array of argument values (json_string, key).
 * @param argc Number of arguments passed.
 * @return Extracted value as value_t.
 */
static value_t *builtin_get_json(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle;
    if (argc < 2 || argv[0]->type != VAL_STRING || argv[1]->type != VAL_STRING) {
        ti_log("[ERROR]: get_json() expects (string json, string key)\n");
        ti_fatal();
        return val_new_null();
    }

    const char *json_str = argv[0]->string_val ? argv[0]->string_val : "";
    const char *key = argv[1]->string_val ? argv[1]->string_val : "";

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        return val_new_null();
    }

    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!item) {
        item = cJSON_GetObjectItem(root, key);
    }

    if (!item) {
        cJSON_Delete(root);
        return val_new_null();
    }

    value_t *result = NULL;
    if (cJSON_IsNumber(item)) {
        if (item->valuedouble != (double)item->valueint) {
            result = val_new_float((float)item->valuedouble);
        } else {
            const char *p = strstr(json_str, key);
            bool has_dot = false;
            if (p) {
                const char *colon = strchr(p, ':');
                if (colon) {
                    colon++;
                    while (*colon == ' ' || *colon == '\t' || *colon == '\n' || *colon == '\r') {
                        colon++;
                    }
                    while ((*colon >= '0' && *colon <= '9') || *colon == '.' || *colon == '-' || *colon == '+') {
                        if (*colon == '.') {
                            has_dot = true;
                        }
                        colon++;
                    }
                }
            }
            if (has_dot) {
                result = val_new_float((float)item->valuedouble);
            } else {
                result = val_new_int(item->valueint);
            }
        }
    } else if (cJSON_IsString(item)) {
        result = val_new_string(item->valuestring);
    } else if (cJSON_IsBool(item)) {
        result = val_new_bool(cJSON_IsTrue(item));
    } else if (cJSON_IsObject(item) || cJSON_IsArray(item)) {
        char *printed = cJSON_PrintUnformatted(item);
        result = val_new_string(printed ? printed : "");
        if (printed) {
            free(printed);
        }
    } else {
        result = val_new_null();
    }

    cJSON_Delete(root);
    return result ? result : val_new_null();
}

/* -------------------- Public Functions -------------------- */

/* Register get_json() */
void builtin_json_register(void)
{
    static param_t get_json_params[] = { { VAL_STRING, "json", VAL_NULL }, { VAL_STRING, "key", VAL_NULL } };

    register_builtin_function("get_json", VAL_STRING, get_json_params, 2, builtin_get_json);
}
