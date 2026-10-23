#include "include/ti_runtime_builtin.h"
#include "include/ti_type_value_bytes.h"
#include "include/tracked_memory.h"
#include "TienInterpreter.h"
#include <stdint.h>
#include <string.h>

/* -------------------- Static Function Prototypes -------------------- */

static value_t *builtin_bytes_to_string(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_string_to_bytes(ti_handle_t handle, value_t **argv, int argc);

/* -------------------- Static Variables -------------------- */

static param_t s_bytes_param[]  = { { VAL_BYTES,  "data", VAL_NULL } };
static param_t s_string_param[] = { { VAL_STRING, "text", VAL_NULL } };

static const ti_builtin_def_t s_defs[] = {
    { "bytes_to_string", VAL_STRING, s_bytes_param,  1, builtin_bytes_to_string },
    { "string_to_bytes", VAL_BYTES,  s_string_param, 1, builtin_string_to_bytes },
};

/* -------------------- Static Functions -------------------- */

/**
 * @brief Built-in bytes_to_string(bytes): copies the raw bytes into a string value.
 * Usage in a Ti script: string s = bytes_to_string(rx);
 * The bytes are not changed, only reinterpreted as characters. A 0x00 inside the data stays in
 * the buffer; functions that read the string as text stop at it.
 * @param handle Calling runtime handle, used to report errors.
 * @param argv Array of argument values (one bytes value).
 * @param argc Number of arguments passed.
 * @return Newly allocated string value, or NULL after raising an error.
 */
static value_t *builtin_bytes_to_string(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;

    const uint8_t *data = val_bytes_data(argv[0]);
    int length = val_bytes_len(argv[0]);

    /* One extra byte for the terminator the string type requires */
    value_t *result = val_init(VAL_STRING);
    char *text = (result != NULL) ? (char *)ti_raw_malloc((size_t)length + 1) : NULL;
    if (text == NULL) {
        val_free(result);
        ti_raise_error(handle, TI_ERR_NO_MEMORY, "bytes_to_string: cannot allocate %d byte(s)", length + 1);
        return NULL;
    }

    /* Raw copy (data may be NULL only for an empty buffer), then terminate */
    if (length > 0) {
        memcpy(text, data, (size_t)length);
    }
    text[length] = '\0';
    result->string_val = text;
    return result;
}

/**
 * @brief Built-in string_to_bytes(string): copies the characters (without the terminator) into bytes.
 * Usage in a Ti script: bytes tx = string_to_bytes("AT\r\n");
 * @param handle Calling runtime handle, used to report errors.
 * @param argv Array of argument values (one string value).
 * @param argc Number of arguments passed.
 * @return Newly allocated bytes value, or NULL after raising an error.
 */
static value_t *builtin_string_to_bytes(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;

    const char *text = argv[0]->string_val != NULL ? argv[0]->string_val : "";
    size_t length = strlen(text);

    if (length > TI_MAX_BYTES_LEN) {
        ti_raise_error(handle, TI_ERR_LIMIT_EXCEEDED, "string_to_bytes: %zu byte(s) exceeds the limit of %d",
                       length, TI_MAX_BYTES_LEN);
        return NULL;
    }

    value_t *result = val_new_bytes((const uint8_t *)text, (int)length);
    if (result == NULL) {
        ti_raise_error(handle, TI_ERR_NO_MEMORY, "string_to_bytes: cannot allocate %zu byte(s)", length);
    }
    return result;
}

/* -------------------- Public Functions -------------------- */

/* Register bytes_to_string, string_to_bytes */
void ti_runtime_builtin_bytes_register(void)
{
    ti_register_builtin_table(s_defs, (int)(sizeof(s_defs) / sizeof(s_defs[0])));
}
