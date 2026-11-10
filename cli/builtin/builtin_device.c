#include "builtin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------- Constants -------------------- */

#define DEVICE_PORT_MAX_LEN 32 /* Longest port name kept in the config */

/* -------------------- Static Types -------------------- */

/**
 * @brief Native configuration of one simulated serial device.
 * Scripts never see this struct: they only hold the handle that wraps it.
 */
typedef struct {
    char port[DEVICE_PORT_MAX_LEN]; /* Port name given to device_open */
    int  baud;                      /* Current baud rate */
    int  reads;                     /* Number of device_read calls, used to simulate changing data */
} device_cfg_t;

/* -------------------- Static Function Prototypes -------------------- */

static void device_destroy(void *ptr);
static value_t *builtin_device_open(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_device_set_baud(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_device_read(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_device_info(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_device_close(ti_handle_t handle, value_t **argv, int argc);
static ti_status_t device_get_attr(void *ptr, const char *name, value_t **out);
static ti_status_t device_set_attr(void *ptr, const char *name, const value_t *value);

/* -------------------- Static Variables -------------------- */

/* Forward declaration: the parameter tables below point to the type, and the type points to its methods */
static const ti_handle_type_t s_device_type;

/* Parameter tables shared by the device_* functions and the device methods (params[0] is self) */
static const param_t s_open_params[] = { PARAM("port", VAL_STRING), PARAM("baud", VAL_INT) };
static const param_t s_dev_params[]  = { PARAM_HANDLE("dev", &s_device_type) };
static const param_t s_baud_params[] = { PARAM_HANDLE("dev", &s_device_type), PARAM("baud", VAL_INT) };

/* Methods called as dev.name(...): the same natives as the device_* functions */
static const ti_builtin_def_t s_device_methods[] = {
    { "info",     VAL_STRING, s_dev_params,  1, builtin_device_info },
    { "read",     VAL_INT,    s_dev_params,  1, builtin_device_read },
    { "set_baud", VAL_VOID,   s_baud_params, 2, builtin_device_set_baud },
    { "close",    VAL_VOID,   s_dev_params,  1, builtin_device_close },
};

/* Handle type of every device: its address is the type identity */
static const ti_handle_type_t s_device_type = {
    .name         = "device_cfg",
    .destroy      = device_destroy,
    .methods      = s_device_methods,
    .method_count = (int)(sizeof(s_device_methods) / sizeof(s_device_methods[0])),
    .get_attr     = device_get_attr,  /* dev.baud, dev.port */
    .set_attr     = device_set_attr,  /* dev.baud = 9600 (port is read-only) */
};

/* -------------------- Static Functions -------------------- */

/**
 * @brief Handle destructor: runs on device_close, or when the script drops its last copy.
 * @param ptr The device_cfg_t wrapped by the handle.
 */
static void device_destroy(void *ptr)
{
    device_cfg_t *cfg = (device_cfg_t *)ptr;

    /* Visible in the CLI so the automatic release can be observed */
    printf("[device] %s released\n", cfg->port);
    free(cfg);
}

/**
 * @brief Attribute reader of device_cfg: dev.baud (int) and dev.port (string). Never raises.
 * @param ptr The device_cfg_t (never NULL: the interpreter rejects a closed handle first).
 * @param name Attribute name written in the script.
 * @param out Receives the new value on TI_OK.
 * @return TI_OK, TI_ERR_UNDEFINED for an unknown name, or TI_ERR_NO_MEMORY.
 */
static ti_status_t device_get_attr(void *ptr, const char *name, value_t **out)
{
    device_cfg_t *cfg = (device_cfg_t *)ptr;

    /* Each known name builds its value; the interpreter reports an unknown one */
    if (strcmp(name, "baud") == 0) {
        *out = val_new_int(cfg->baud);
    } else if (strcmp(name, "port") == 0) {
        *out = val_new_string(cfg->port);
    } else {
        return TI_ERR_UNDEFINED;
    }
    return (*out != NULL) ? TI_OK : TI_ERR_NO_MEMORY;
}

/**
 * @brief Attribute writer of device_cfg: dev.baud = int; dev.port is read-only. Never raises.
 * @param ptr The device_cfg_t (never NULL: the interpreter rejects a closed handle first).
 * @param name Attribute name written in the script.
 * @param value New value (borrowed; its type is checked here).
 * @return TI_OK, TI_ERR_UNDEFINED, TI_ERR_READ_ONLY, TI_ERR_TYPE_MISMATCH or TI_ERR_VALUE_OUT_OF_RANGE.
 */
static ti_status_t device_set_attr(void *ptr, const char *name, const value_t *value)
{
    device_cfg_t *cfg = (device_cfg_t *)ptr;

    if (strcmp(name, "baud") == 0) {
        /* Same rules as device_set_baud */
        if (value->type != VAL_INT) {
            return TI_ERR_TYPE_MISMATCH;
        }
        if (value->int_val <= 0) {
            return TI_ERR_VALUE_OUT_OF_RANGE;
        }
        cfg->baud = value->int_val;
        return TI_OK;
    }

    /* The port is fixed at device_open */
    if (strcmp(name, "port") == 0) {
        return TI_ERR_READ_ONLY;
    }
    return TI_ERR_UNDEFINED;
}

/**
 * @brief Built-in native device_open(string port, int baud): create a device and return its handle.
 * @param handle Calling runtime handle, used to raise errors.
 * @param argv argv[0] port name (string), argv[1] baud rate (int), both checked by the runtime.
 * @param argc Number of arguments passed.
 * @return New handle of type s_device_type, or NULL with an error raised.
 */
static value_t *builtin_device_open(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    const char *port = argv[0]->string_val;
    int baud = argv[1]->int_val;

    /* Values are the native's job: the runtime only checked the types */
    if (port == NULL || port[0] == '\0' || strlen(port) >= DEVICE_PORT_MAX_LEN) {
        ti_raise_error(handle, TI_ERR_INVALID_VALUE, "device_open: port name must be 1..%d characters", DEVICE_PORT_MAX_LEN - 1);
        return NULL;
    }
    if (baud <= 0) {
        ti_raise_error(handle, TI_ERR_VALUE_OUT_OF_RANGE, "device_open: baud must be positive, got %d", baud);
        return NULL;
    }

    device_cfg_t *cfg = calloc(1, sizeof(device_cfg_t));
    if (cfg == NULL) {
        ti_raise_error(handle, TI_ERR_NO_MEMORY, "device_open: out of memory");
        return NULL;
    }
    strcpy(cfg->port, port);
    cfg->baud = baud;

    /* On success the value owns cfg; on failure cfg is still ours and must be freed here */
    value_t *result = val_new_handle(cfg, &s_device_type);
    if (result == NULL) {
        free(cfg);
        ti_raise_error(handle, TI_ERR_NO_MEMORY, "device_open: out of memory");
        return NULL;
    }
    return result;
}

/**
 * @brief Built-in native device_set_baud(handle dev, int baud).
 * @param handle Calling runtime handle, used to raise errors.
 * @param argv argv[0] device handle (type checked by the runtime), argv[1] new baud rate.
 * @param argc Number of arguments passed.
 * @return Void value_t, or NULL with an error raised.
 */
static value_t *builtin_device_set_baud(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;

    /* The type was checked before the call; NULL here only means the device was closed */
    device_cfg_t *cfg = val_handle_get(argv[0], &s_device_type);
    if (cfg == NULL) {
        ti_raise_error(handle, TI_ERR_INVALID_VALUE, "device_set_baud: device is closed");
        return NULL;
    }
    if (argv[1]->int_val <= 0) {
        ti_raise_error(handle, TI_ERR_VALUE_OUT_OF_RANGE, "device_set_baud: baud must be positive, got %d", argv[1]->int_val);
        return NULL;
    }
    cfg->baud = argv[1]->int_val;
    return val_new_void();
}

/**
 * @brief Built-in native device_read(handle dev): read a simulated sample.
 * @param handle Calling runtime handle, used to raise errors.
 * @param argv argv[0] device handle (type checked by the runtime).
 * @param argc Number of arguments passed.
 * @return Sample int value_t, or NULL with an error raised.
 */
static value_t *builtin_device_read(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    device_cfg_t *cfg = val_handle_get(argv[0], &s_device_type);
    if (cfg == NULL) {
        ti_raise_error(handle, TI_ERR_INVALID_VALUE, "device_read: device is closed");
        return NULL;
    }

    /* Simulated data that changes on every read */
    cfg->reads++;
    return val_new_int(100 + cfg->reads);
}

/**
 * @brief Built-in native device_info(handle dev): describe the device as "port@baud".
 * @param handle Calling runtime handle, used to raise errors.
 * @param argv argv[0] device handle (type checked by the runtime).
 * @param argc Number of arguments passed.
 * @return String value_t, or NULL with an error raised.
 */
static value_t *builtin_device_info(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    device_cfg_t *cfg = val_handle_get(argv[0], &s_device_type);
    if (cfg == NULL) {
        ti_raise_error(handle, TI_ERR_INVALID_VALUE, "device_info: device is closed");
        return NULL;
    }

    char text[DEVICE_PORT_MAX_LEN + 16];
    snprintf(text, sizeof(text), "%s@%d", cfg->port, cfg->baud);
    return val_new_string(text);
}

/**
 * @brief Built-in native device_close(handle dev): release the device now.
 * Every copy of the handle sees it as closed afterwards.
 * @param handle Calling runtime handle, used to raise errors.
 * @param argv argv[0] device handle (type checked by the runtime).
 * @param argc Number of arguments passed.
 * @return Void value_t, or NULL with an error raised (already closed).
 */
static value_t *builtin_device_close(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    ti_status_t status = val_handle_close(argv[0], &s_device_type);
    if (status != TI_OK) {
        ti_raise_error(handle, status, "device_close: device is already closed");
        return NULL;
    }
    return val_new_void();
}

/* -------------------- Public Functions -------------------- */

/* Register the device_* functions (the methods need no registration: they live in s_device_type) */
void builtin_device_register(void)
{
    register_builtin_function("device_open", VAL_HANDLE, s_open_params, 2, builtin_device_open);
    register_builtin_function("device_set_baud", VAL_VOID, s_baud_params, 2, builtin_device_set_baud);
    register_builtin_function("device_read", VAL_INT, s_dev_params, 1, builtin_device_read);
    register_builtin_function("device_info", VAL_STRING, s_dev_params, 1, builtin_device_info);
    register_builtin_function("device_close", VAL_VOID, s_dev_params, 1, builtin_device_close);
}
