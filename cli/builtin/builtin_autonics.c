#include "builtin.h"

/* -------------------- Static Variables -------------------- */

static int s_autonics_slave_addr = 1;

/* -------------------- Static Function Prototypes -------------------- */

static value_t *builtin_autonics_tk_set_slave_address(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_autonics_tk_get_pv(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_autonics_tk_get_sv(ti_handle_t handle, value_t **argv, int argc);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Built-in native autonics_tk_set_slave_address function.
 * @param handle Calling runtime handle (unused).
 * @param argv Array of argument values.
 * @param argc Number of arguments passed.
 * @return Null value_t.
 */
static value_t *builtin_autonics_tk_set_slave_address(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle;
    if (argc >= 1 && argv[0]->type == VAL_INT) {
        s_autonics_slave_addr = argv[0]->int_val;
    }
    return val_new_null();
}

/**
 * @brief Built-in native autonics_tk_get_pv function.
 * @param handle Calling runtime handle (unused).
 * @param argv Array of argument values.
 * @param argc Number of arguments passed.
 * @return Present temperature float value_t.
 */
static value_t *builtin_autonics_tk_get_pv(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle;
    (void)argv;
    (void)argc;
    return val_new_float(28.5f);
}

/**
 * @brief Built-in native autonics_tk_get_sv function.
 * @param handle Calling runtime handle (unused).
 * @param argv Array of argument values.
 * @param argc Number of arguments passed.
 * @return Setpoint temperature float value_t.
 */
static value_t *builtin_autonics_tk_get_sv(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle;
    (void)argv;
    (void)argc;
    return val_new_float(85.0f);
}

/* -------------------- Public Functions -------------------- */

/* Register the autonics_tk_* functions */
void builtin_autonics_register(void)
{
    static param_t autonics_addr_params[] = { { VAL_INT, "address", VAL_NULL } };

    register_builtin_function("autonics_tk_set_slave_address", VAL_VOID, autonics_addr_params, 1, builtin_autonics_tk_set_slave_address);
    register_builtin_function("autonics_tk_get_pv", VAL_FLOAT, NULL, 0, builtin_autonics_tk_get_pv);
    register_builtin_function("autonics_tk_get_sv", VAL_FLOAT, NULL, 0, builtin_autonics_tk_get_sv);
}
