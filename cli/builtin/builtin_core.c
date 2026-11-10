#include "builtin.h"
#include "include/debug.h"
#include <stdio.h>
#include <stdint.h>

/* -------------------- Static Function Prototypes -------------------- */

static value_t *builtin_print(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_delay(ti_handle_t handle, value_t **argv, int argc);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Built-in native print function for Ti scripts.
 * @param handle Calling runtime handle (unused).
 * @param argv Array of argument values.
 * @param argc Number of arguments passed.
 * @return Null value_t.
 */
static value_t *builtin_print(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle;
    if (argc == 0) {
        ti_log("\n");
    }
    for (int i = 0; i < argc; i++) {
        switch (argv[i]->type) {
        case VAL_STRING:
            printf("%s", argv[i]->string_val);
            break;
        case VAL_INT:
            printf("%d", argv[i]->int_val);
            break;
        case VAL_FLOAT:
            printf("%.2f", argv[i]->float_val);
            break;
        case VAL_BOOL:
            printf("%s", argv[i]->bool_val ? "true" : "false");
            break;
        case VAL_FUNC:
            /* A function value prints as its name in angle brackets; an empty one ('func f;') as "none" */
            printf("<func %s>", argv[i]->func_name != NULL ? argv[i]->func_name : "none");
            break;
        case VAL_HANDLE: {
            /* Same form as to_string: the type name and whether it is still open, never the native object */
            const ti_handle_type_t *htype = val_handle_type(argv[i]);
            if (htype == NULL) {
                printf("<handle none>");
            } else {
                printf(val_handle_is_open(argv[i]) ? "<handle %s>" : "<handle %s closed>", htype->name);
            }
            break;
        }
        case VAL_MODULE:
            /* Same form as to_string: only the module name */
            printf("<module %s>", argv[i]->module_val->name);
            break;
        case VAL_BYTES: {
            /* Binary data has no text form: print it as hex digits */
            const uint8_t *data = val_bytes_data(argv[i]);
            for (int b = 0; b < val_bytes_len(argv[i]); b++) {
                printf("0x%02X\t", data[b]);
            }
            break;
        }
        default:
            printf("Unexpected type %s", val_type_to_str(argv[i]->type));
            break;
        }
    }
    printf("\n");
    return val_new_null();
}

/**
 * @brief Built-in native delay function for Ti scripts (milliseconds), servicing events while waiting.
 * @param handle Calling runtime handle, passed to ti_wait.
 * @param argv Array of argument values.
 * @param argc Number of arguments passed.
 * @return Null value_t.
 */
static value_t *builtin_delay(ti_handle_t handle, value_t **argv, int argc)
{
    if (argc != 1) {
        ti_log("[ERROR]: delay() expects 1 argument (milliseconds)\n");
        ti_fatal();
        return val_new_null();
    }

    int ms = 0;
    if (argv[0]->type == VAL_INT) {
        ms = argv[0]->int_val;
    } else if (argv[0]->type == VAL_FLOAT) {
        ms = (int)argv[0]->float_val;
    } else {
        ti_log("[ERROR]: delay() argument must be an integer or float\n");
        ti_fatal();
        return val_new_null();
    }

    /* The runtime sleeps with its lock released: events and ti_call callbacks run at once, then
     * the delay continues for the remaining time. A stop ends it early (the script unwinds next). */
    ti_wait(handle, ms > 0 ? (uint32_t)ms : 0);

    return val_new_null();
}

/* -------------------- Public Functions -------------------- */

/* Register print() and delay() */
void builtin_core_register(void)
{
    static const param_t delay_params[] = { { VAL_INT, "ms", VAL_NULL, NULL } };

    register_builtin_function("print", VAL_VOID, NULL, -1, builtin_print);
    register_builtin_function("delay", VAL_VOID, delay_params, 1, builtin_delay);
}
