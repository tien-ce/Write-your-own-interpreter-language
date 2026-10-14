#include "builtin.h"

/* -------------------- Static Variables -------------------- */

static int s_relay_state = 0;

/* -------------------- Static Function Prototypes -------------------- */

static value_t *builtin_relay_get_state(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_relay_set_state(ti_handle_t handle, value_t **argv, int argc);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Built-in native relay_get_state function.
 * @param handle Calling runtime handle (unused).
 * @param argv Array of argument values.
 * @param argc Number of arguments passed.
 * @return Current relay state integer value_t.
 */
static value_t *builtin_relay_get_state(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle;
    (void)argv;
    (void)argc;
    return val_new_int(s_relay_state);
}

/**
 * @brief Built-in native relay_set_state function.
 * @param handle Calling runtime handle (unused).
 * @param argv Array of argument values.
 * @param argc Number of arguments passed.
 * @return Null value_t.
 */
static value_t *builtin_relay_set_state(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle;
    if (argc >= 2 && argv[1]->type == VAL_INT) {
        s_relay_state = argv[1]->int_val;
    }
    return val_new_null();
}

/* -------------------- Public Functions -------------------- */

/* Register the relay_* functions */
void builtin_relay_register(void)
{
    static param_t relay_set_params[] = { { VAL_INT, "id", VAL_NULL }, { VAL_INT, "state", VAL_NULL } };

    register_builtin_function("relay_get_state", VAL_INT, NULL, 0, builtin_relay_get_state);
    register_builtin_function("relay_set_state", VAL_VOID, relay_set_params, 2, builtin_relay_set_state);
}
