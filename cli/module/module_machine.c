#include "module.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------- Constants -------------------- */

/* Serial frame formats, same values as the Arduino ESP32 core (data bits, parity, stop bits) */
#define MACHINE_SERIAL_8N1 0x800001c
#define MACHINE_SERIAL_8N2 0x800003c

#define MACHINE_UART_COUNT 3 /* Simulated ports 0..2, like an ESP32 */

/* -------------------- Static Types -------------------- */

/**
 * @brief Native state of one simulated UART port. Scripts only hold the handle that wraps it.
 */
typedef struct {
    int port;   /* Port number given to machine.UART */
    int baud;   /* Current baud rate (writable attribute) */
    int config; /* Frame format, e.g. MACHINE_SERIAL_8N1 (read-only attribute) */
} uart_cfg_t;

/* -------------------- Static Function Prototypes -------------------- */

static void uart_destroy(void *ptr);
static value_t *machine_uart_new(ti_handle_t handle, value_t **argv, int argc);
static value_t *uart_write(ti_handle_t handle, value_t **argv, int argc);
static value_t *uart_close(ti_handle_t handle, value_t **argv, int argc);
static ti_status_t uart_get_attr(void *ptr, const char *name, value_t **out);
static ti_status_t uart_set_attr(void *ptr, const char *name, const value_t *value);

/* -------------------- Static Variables -------------------- */

/* Forward declaration: the method parameter tables point to the type, and the type points to its methods */
static const ti_handle_type_t s_uart_type;

/* Parameter tables: static const, so they stay in flash like the method and module tables */
static const param_t s_uart_new_params[] = { PARAM("port", VAL_INT), PARAM("baud", VAL_INT), PARAM("config", VAL_INT) };
static const param_t s_uart_self_params[] = { PARAM_HANDLE("self", &s_uart_type) };
static const param_t s_uart_write_params[] = { PARAM_HANDLE("self", &s_uart_type), PARAM("text", VAL_STRING) };

/* Methods of a UART handle: u.write(text), u.close() */
static const ti_builtin_def_t s_uart_methods[] = {
    { "write", VAL_VOID, s_uart_write_params, 2, uart_write },
    { "close", VAL_VOID, s_uart_self_params,  1, uart_close },
};

/* Handle type returned by machine.UART: its address is the type identity */
static const ti_handle_type_t s_uart_type = {
    .name         = "uart",
    .destroy      = uart_destroy,
    .methods      = s_uart_methods,
    .method_count = (int)(sizeof(s_uart_methods) / sizeof(s_uart_methods[0])),
    .get_attr     = uart_get_attr, /* u.port, u.baud, u.config */
    .set_attr     = uart_set_attr, /* u.baud = 115200 (port and config are read-only) */
};

/* Functions of the module: machine.UART(port, baud, config) is the only way to get a UART handle */
static const ti_builtin_def_t s_machine_funcs[] = {
    { "UART", VAL_HANDLE, s_uart_new_params, 3, machine_uart_new },
};

/* Constants of the module: machine.SERIAL_8N1, machine.SERIAL_8N2 */
static const ti_module_const_t s_machine_consts[] = {
    { "SERIAL_8N1", { .type = VAL_INT, .int_val = MACHINE_SERIAL_8N1 } },
    { "SERIAL_8N2", { .type = VAL_INT, .int_val = MACHINE_SERIAL_8N2 } },
};

/* The module scripts get with 'import machine;' */
static const ti_module_t s_machine_module = {
    .name        = "machine",
    .funcs       = s_machine_funcs,
    .func_count  = (int)(sizeof(s_machine_funcs) / sizeof(s_machine_funcs[0])),
    .consts      = s_machine_consts,
    .const_count = (int)(sizeof(s_machine_consts) / sizeof(s_machine_consts[0])),
};

/* -------------------- Static Functions -------------------- */

/**
 * @brief Handle destructor: runs on u.close(), or when the script drops its last copy.
 * @param ptr The uart_cfg_t wrapped by the handle.
 */
static void uart_destroy(void *ptr)
{
    uart_cfg_t *cfg = (uart_cfg_t *)ptr;

    /* Visible in the CLI so the automatic release can be observed */
    printf("[uart] port %d released\n", cfg->port);
    free(cfg);
}

/**
 * @brief Module function machine.UART(int port, int baud, int config): open a simulated port.
 * @param handle Calling runtime handle, used to raise errors.
 * @param argv argv[0] port, argv[1] baud, argv[2] frame format (types checked by the runtime).
 * @param argc Number of arguments passed.
 * @return New handle of type s_uart_type, or NULL with an error raised.
 */
static value_t *machine_uart_new(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    int port = argv[0]->int_val;
    int baud = argv[1]->int_val;

    /* Values are the native's job: the runtime only checked the types */
    if (port < 0 || port >= MACHINE_UART_COUNT) {
        ti_raise_error(handle, TI_ERR_VALUE_OUT_OF_RANGE, "UART: port must be 0..%d, got %d", MACHINE_UART_COUNT - 1, port);
        return NULL;
    }
    if (baud <= 0) {
        ti_raise_error(handle, TI_ERR_VALUE_OUT_OF_RANGE, "UART: baud must be positive, got %d", baud);
        return NULL;
    }

    uart_cfg_t *cfg = calloc(1, sizeof(uart_cfg_t));
    if (cfg == NULL) {
        ti_raise_error(handle, TI_ERR_NO_MEMORY, "UART: out of memory");
        return NULL;
    }
    cfg->port = port;
    cfg->baud = baud;
    cfg->config = argv[2]->int_val;

    /* On success the value owns cfg; on failure cfg is still ours and must be freed here */
    value_t *result = val_new_handle(cfg, &s_uart_type);
    if (result == NULL) {
        free(cfg);
        ti_raise_error(handle, TI_ERR_NO_MEMORY, "UART: out of memory");
        return NULL;
    }
    return result;
}

/**
 * @brief Method u.write(string text): send text on the simulated port (printed in the CLI).
 * @param handle Calling runtime handle, used to raise errors.
 * @param argv argv[0] self (type checked by the runtime), argv[1] text.
 * @param argc Number of arguments passed.
 * @return Void value_t, or NULL with an error raised.
 */
static value_t *uart_write(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;

    /* The type was checked before the call; NULL here only means the port was closed */
    uart_cfg_t *cfg = val_handle_get(argv[0], &s_uart_type);
    if (cfg == NULL) {
        ti_raise_error(handle, TI_ERR_INVALID_VALUE, "uart.write: port is closed");
        return NULL;
    }
    printf("[uart %d @%d] %s\n", cfg->port, cfg->baud, argv[1]->string_val);
    return val_new_void();
}

/**
 * @brief Method u.close(): release the port now; every copy of the handle sees it as closed.
 * @param handle Calling runtime handle, used to raise errors.
 * @param argv argv[0] self (type checked by the runtime).
 * @param argc Number of arguments passed.
 * @return Void value_t, or NULL with an error raised (already closed).
 */
static value_t *uart_close(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    ti_status_t status = val_handle_close(argv[0], &s_uart_type);
    if (status != TI_OK) {
        ti_raise_error(handle, status, "uart.close: port is already closed");
        return NULL;
    }
    return val_new_void();
}

/**
 * @brief Attribute reader of uart: u.port, u.baud, u.config (all int). Never raises.
 * @param ptr The uart_cfg_t (never NULL: the interpreter rejects a closed handle first).
 * @param name Attribute name written in the script.
 * @param out Receives the new value on TI_OK.
 * @return TI_OK, TI_ERR_UNDEFINED for an unknown name, or TI_ERR_NO_MEMORY.
 */
static ti_status_t uart_get_attr(void *ptr, const char *name, value_t **out)
{
    uart_cfg_t *cfg = (uart_cfg_t *)ptr;

    /* Each known name builds its value; the interpreter reports an unknown one */
    if (strcmp(name, "port") == 0) {
        *out = val_new_int(cfg->port);
    } else if (strcmp(name, "baud") == 0) {
        *out = val_new_int(cfg->baud);
    } else if (strcmp(name, "config") == 0) {
        *out = val_new_int(cfg->config);
    } else {
        return TI_ERR_UNDEFINED;
    }
    return (*out != NULL) ? TI_OK : TI_ERR_NO_MEMORY;
}

/**
 * @brief Attribute writer of uart: u.baud = int; u.port and u.config are read-only. Never raises.
 * @param ptr The uart_cfg_t (never NULL: the interpreter rejects a closed handle first).
 * @param name Attribute name written in the script.
 * @param value New value (borrowed; its type is checked here).
 * @return TI_OK, TI_ERR_UNDEFINED, TI_ERR_READ_ONLY, TI_ERR_TYPE_MISMATCH or TI_ERR_VALUE_OUT_OF_RANGE.
 */
static ti_status_t uart_set_attr(void *ptr, const char *name, const value_t *value)
{
    uart_cfg_t *cfg = (uart_cfg_t *)ptr;

    if (strcmp(name, "baud") == 0) {
        /* Same rule as machine.UART */
        if (value->type != VAL_INT) {
            return TI_ERR_TYPE_MISMATCH;
        }
        if (value->int_val <= 0) {
            return TI_ERR_VALUE_OUT_OF_RANGE;
        }
        cfg->baud = value->int_val;
        return TI_OK;
    }

    /* Port and frame format are fixed when the port is opened */
    if (strcmp(name, "port") == 0 || strcmp(name, "config") == 0) {
        return TI_ERR_READ_ONLY;
    }
    return TI_ERR_UNDEFINED;
}

/* -------------------- Public Functions -------------------- */

/* Register the 'machine' module: the only registration call, every table stays static const */
void module_machine_register(void)
{
    ti_register_module(&s_machine_module);
}
