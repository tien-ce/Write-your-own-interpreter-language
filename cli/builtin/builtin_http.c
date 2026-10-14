#include "builtin.h"

/* -------------------- Static Function Prototypes -------------------- */

static value_t *builtin_http_get(ti_handle_t handle, value_t **argv, int argc);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Built-in native http_get function (returns fixed JSON string).
 * @param handle Calling runtime handle (unused).
 * @param argv Array of argument values.
 * @param argc Number of arguments passed.
 * @return Response JSON string value_t.
 */
static value_t *builtin_http_get(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle;
    (void)argv;
    (void)argc;
    const char *fixed_response = "{\"code\": 200, \"payload\": \"{\\\"UPPER_TEMP_MAX\\\": 80.0, \\\"UPPER_TEMP_MIN\\\": 40.0}\"}";
    return val_new_string(fixed_response);
}

/* -------------------- Public Functions -------------------- */

/* Register http_get() */
void builtin_http_register(void)
{
    register_builtin_function("http_get", VAL_STRING, NULL, 0, builtin_http_get);
}
