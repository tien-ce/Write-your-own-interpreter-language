#include "include/ti_runtime_visitor.h"
#include "include/ti_type_func.h"
#include "TienInterpreter.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <cjson/cJSON.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/* -------------------- Static Variables -------------------- */

static int s_relay_state = 0;
static int s_autonics_slave_addr = 1;

/* -------------------- Static Function Prototypes -------------------- */

static void ti_log_callback(const char *fmt, va_list args);
static void ti_fatal_callback(void);
static value_t *built_in_print(value_t **argv, int argc);
static value_t *built_in_delay(value_t **argv, int argc);
static value_t *built_in_relay_get_state(value_t **argv, int argc);
static value_t *built_in_relay_set_state(value_t **argv, int argc);
static value_t *built_in_autonics_tk_set_slave_address(value_t **argv, int argc);
static value_t *built_in_autonics_tk_get_pv(value_t **argv, int argc);
static value_t *built_in_autonics_tk_get_sv(value_t **argv, int argc);
static value_t *built_in_nvs_read(value_t **argv, int argc);
static value_t *built_in_http_get(value_t **argv, int argc);
static value_t *built_in_get_json(value_t **argv, int argc);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Default desktop logging callback routing formatted text to standard output.
 * @param fmt Format string.
 * @param args Variadic argument list.
 */
static void ti_log_callback(const char *fmt, va_list args)
{
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    printf("%s", buffer);
}

/**
 * @brief Default desktop fatal error callback calling exit(1).
 */
static void ti_fatal_callback(void)
{
  pthread_exit(NULL);
}

/**
 * @brief Built-in native print function for Ti scripts.
 * @param argv Array of argument values.
 * @param argc Number of arguments passed.
 * @return Null value_t.
 */
static value_t *built_in_print(value_t **argv, int argc)
{
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
        default:
            printf("Unexpected type %d", argv[i]->type);
            break;
        }
    }
    printf("\n");
    return val_new_null();
}

/**
 * @brief Built-in native delay function for Ti scripts (milliseconds).
 * @param argv Array of argument values.
 * @param argc Number of arguments passed.
 * @return Null value_t.
 */
static value_t *built_in_delay(value_t **argv, int argc)
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

    if (ms > 0) {
#ifdef _WIN32
        Sleep(ms);
#else
        usleep((useconds_t)ms * 1000);
#endif
    }

    return val_new_null();
}

/**
 * @brief Built-in native relay_get_state function.
 * @param argv Array of argument values.
 * @param argc Number of arguments passed.
 * @return Current relay state integer value_t.
 */
static value_t *built_in_relay_get_state(value_t **argv, int argc)
{
    (void)argv;
    (void)argc;
    return val_new_int(s_relay_state);
}

/**
 * @brief Built-in native relay_set_state function.
 * @param argv Array of argument values.
 * @param argc Number of arguments passed.
 * @return Null value_t.
 */
static value_t *built_in_relay_set_state(value_t **argv, int argc)
{
    if (argc >= 2 && argv[1]->type == VAL_INT) {
        s_relay_state = argv[1]->int_val;
    }
    return val_new_null();
}

/**
 * @brief Built-in native autonics_tk_set_slave_address function.
 * @param argv Array of argument values.
 * @param argc Number of arguments passed.
 * @return Null value_t.
 */
static value_t *built_in_autonics_tk_set_slave_address(value_t **argv, int argc)
{
    if (argc >= 1 && argv[0]->type == VAL_INT) {
        s_autonics_slave_addr = argv[0]->int_val;
    }
    return val_new_null();
}

/**
 * @brief Built-in native autonics_tk_get_pv function.
 * @param argv Array of argument values.
 * @param argc Number of arguments passed.
 * @return Present temperature float value_t.
 */
static value_t *built_in_autonics_tk_get_pv(value_t **argv, int argc)
{
    (void)argv;
    (void)argc;
    return val_new_float(28.5f);
}

/**
 * @brief Built-in native autonics_tk_get_sv function.
 * @param argv Array of argument values.
 * @param argc Number of arguments passed.
 * @return Setpoint temperature float value_t.
 */
static value_t *built_in_autonics_tk_get_sv(value_t **argv, int argc)
{
    (void)argv;
    (void)argc;
    return val_new_float(85.0f);
}

/**
 * @brief Built-in native nvs_read function (returns default value).
 * @param argv Array of argument values.
 * @param argc Number of arguments passed.
 * @return Default value as value_t.
 */
static value_t *built_in_nvs_read(value_t **argv, int argc)
{
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

/**
 * @brief Built-in native http_get function (returns fixed JSON string).
 * @param argv Array of argument values.
 * @param argc Number of arguments passed.
 * @return Response JSON string value_t.
 */
static value_t *built_in_http_get(value_t **argv, int argc)
{
    (void)argv;
    (void)argc;
    const char *fixed_response = "{\"code\": 200, \"payload\": \"{\\\"UPPER_TEMP_MAX\\\": 80.0, \\\"UPPER_TEMP_MIN\\\": 40.0}\"}";
    return val_new_string(fixed_response);
}

/**
 * @brief Built-in native get_json function using cJSON.
 * @param argv Array of argument values (json_string, key).
 * @param argc Number of arguments passed.
 * @return Extracted value as value_t.
 */
static value_t *built_in_get_json(value_t **argv, int argc)
{
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

/* Initialize built-in interpreter native functions */
void ti_init_builtin(void)
{
    ti_register_log(ti_log_callback);
    ti_register_fatal(ti_fatal_callback);

    static param_t delay_params[] = { { VAL_INT, "ms" } };
    static param_t relay_set_params[] = { { VAL_INT, "id" }, { VAL_INT, "state" } };
    static param_t autonics_addr_params[] = { { VAL_INT, "address" } };
    static param_t nvs_read_params[] = { { VAL_STRING, "key" }, { VAL_INT, "default_val" } };
    static param_t get_json_params[] = { { VAL_STRING, "json" }, { VAL_STRING, "key" } };

    register_builtin_function("print", VAL_VOID, NULL, -1, built_in_print);
    register_builtin_function("delay", VAL_VOID, delay_params, 1, built_in_delay);
    register_builtin_function("relay_get_state", VAL_INT, NULL, 0, built_in_relay_get_state);
    register_builtin_function("relay_set_state", VAL_VOID, relay_set_params, 2, built_in_relay_set_state);
    register_builtin_function("autonics_tk_set_slave_address", VAL_VOID, autonics_addr_params, 1, built_in_autonics_tk_set_slave_address);
    register_builtin_function("autonics_tk_get_pv", VAL_FLOAT, NULL, 0, built_in_autonics_tk_get_pv);
    register_builtin_function("autonics_tk_get_sv", VAL_FLOAT, NULL, 0, built_in_autonics_tk_get_sv);
    register_builtin_function("nvs_read", VAL_INT, nvs_read_params, 2, built_in_nvs_read);
    register_builtin_function("http_get", VAL_STRING, NULL, 0, built_in_http_get);
    register_builtin_function("get_json", VAL_STRING, get_json_params, 2, built_in_get_json);
}

void init_builtin(void)
{
  ti_init_builtin();
}
