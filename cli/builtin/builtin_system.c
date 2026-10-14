#include "builtin.h"

/* -------------------- Static Function Prototypes -------------------- */

static value_t *builtin_get_system_info(ti_handle_t handle, value_t **argv, int argc);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Built-in native function returning a Dictionary.
 * Usage in Ti script: dict info = get_system_info();
 */
static value_t *builtin_get_system_info(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle;
    (void)argv;
    (void)argc;

    /* Create an empty dictionary object */
    value_t *sys_dict = val_new_dict();

    /* Fill it with key-value pairs (ownership of each value transfers to the dictionary) */
    (void)val_dict_set(sys_dict->dict_val, "status", val_new_string("running"));
    (void)val_dict_set(sys_dict->dict_val, "uptime", val_new_int(3600));
    (void)val_dict_set(sys_dict->dict_val, "is_ready", val_new_bool(true));

    /* Hand the dictionary to the interpreter */
    return sys_dict;
}

/* -------------------- Public Functions -------------------- */

/* Register get_system_info() */
void builtin_system_register(void)
{
    register_builtin_function("get_system_info", VAL_DICT, NULL, 0, builtin_get_system_info);
}
