#include "builtin.h"

/* -------------------- Static Function Prototypes -------------------- */

static value_t *builtin_nvs_read(ti_handle_t handle, value_t **argv, int argc);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Built-in native nvs_read function (returns default value).
 * @param handle Calling runtime handle (unused).
 * @param argv Array of argument values.
 * @param argc Number of arguments passed.
 * @return Default value as value_t.
 */
static value_t *builtin_nvs_read(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle;
    if (argc < 2) {
        return val_new_null();
    }

    value_t *def = argv[1];
    switch (def->type) {
    case VAL_INT:
        return val_new_int(def->int_val);
    case VAL_FLOAT:
        return val_new_float(def->float_val);
    case VAL_STRING:
        return val_new_string(def->string_val);
    case VAL_BOOL:
        return val_new_bool(def->bool_val);
    default:
        return val_new_null();
    }
}

/* -------------------- Public Functions -------------------- */

/* Register nvs_read() */
void builtin_storage_register(void)
{
    static param_t nvs_read_params[] = { { VAL_STRING, "key", VAL_NULL }, { VAL_INT, "default_val", VAL_NULL } };

    register_builtin_function("nvs_read", VAL_INT, nvs_read_params, 2, builtin_nvs_read);
}
