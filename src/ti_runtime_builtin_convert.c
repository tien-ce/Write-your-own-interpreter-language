#include "include/ti_runtime_builtin.h"
#include "ti_runtime_builtin_text.h"
#include "include/ti_type_value_dict.h"
#include "include/ti_type_value_list.h"
#include "include/ti_type_value_bytes.h"
#include "include/tracked_memory.h"
#include "include/debug.h"
#include "TienInterpreter.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------- Static Function Prototypes -------------------- */

static ti_status_t parse_int_text(const char *text, int base, int *out);
static ti_status_t parse_float_text(const char *text, float *out);
static ti_status_t convert_to_int(const value_t *src, int base, int *out);
static ti_status_t convert_to_float(const value_t *src, float *out);
static ti_status_t convert_to_bool(const value_t *src, bool *out);
static void raise_conversion_error(ti_handle_t handle, const char *name, const value_t *src, ti_status_t status);
static value_t *builtin_to_int(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_to_float(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_to_bool(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_to_string(ti_handle_t handle, value_t **argv, int argc);

/* -------------------- Static Variables -------------------- */

/* One argument of any type: VAL_ANY keeps the type open while the runtime still checks the count */
static param_t s_any_param[] = { { VAL_ANY, "value", VAL_NULL } };

/* to_int stays variadic (-1) only because of the optional base: to_int(value) or to_int(text, base) */
static const ti_builtin_def_t s_defs[] = {
    { "to_string", VAL_STRING, s_any_param, 1,  builtin_to_string },
    { "to_float",  VAL_FLOAT,  s_any_param, 1,  builtin_to_float  },
    { "to_bool",   VAL_BOOL,   s_any_param, 1,  builtin_to_bool   },
    { "to_int",    VAL_INT,    NULL,        -1, builtin_to_int    },
};

/* -------------------- Static Functions -------------------- */


/**
 * @brief Parse a whole string as an int in the given base.
 * Leading and trailing whitespace is allowed; any other leftover character is an error.
 * @param text NUL-terminated text.
 * @param base Numeric base 2..36.
 * @param out Receives the value on success.
 * @return TI_OK, TI_ERR_INVALID_VALUE (not a number) or TI_ERR_VALUE_OUT_OF_RANGE (does not fit an int).
 */
static ti_status_t parse_int_text(const char *text, int base, int *out)
{
    if (text == NULL) {
        return TI_ERR_INVALID_VALUE;
    }

    /* strtol reports overflow through errno, so clear it first */
    errno = 0;
    char *end = NULL;
    long parsed = strtol(text, &end, base);

    /* Nothing was consumed: the text does not start with a number */
    if (end == text) {
        return TI_ERR_INVALID_VALUE;
    }

    /* Only whitespace may follow the number */
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (*end != '\0') {
        return TI_ERR_INVALID_VALUE;
    }

    /* long may be wider than int, so range-check explicitly */
    if (errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX) {
        return TI_ERR_VALUE_OUT_OF_RANGE;
    }
    *out = (int)parsed;
    return TI_OK;
}

/**
 * @brief Parse a whole string as a finite float.
 * @param text NUL-terminated text.
 * @param out Receives the value on success.
 * @return TI_OK, TI_ERR_INVALID_VALUE (not a number, or "nan"/"inf") or TI_ERR_VALUE_OUT_OF_RANGE.
 */
static ti_status_t parse_float_text(const char *text, float *out)
{
    if (text == NULL) {
        return TI_ERR_INVALID_VALUE;
    }

    errno = 0;
    char *end = NULL;
    float parsed = strtof(text, &end);
    if (end == text) {
        return TI_ERR_INVALID_VALUE;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (*end != '\0') {
        return TI_ERR_INVALID_VALUE;
    }

    /* Overflow gives +-inf with ERANGE; the literal text "nan" or "inf" is rejected as well */
    if (errno == ERANGE) {
        return TI_ERR_VALUE_OUT_OF_RANGE;
    }
    if (!isfinite(parsed)) {
        return TI_ERR_INVALID_VALUE;
    }
    *out = parsed;
    return TI_OK;
}

/**
 * @brief Convert a value to an int (the conversion behind to_int).
 * A float is truncated toward zero, exactly like a C cast; values that do not fit are an error
 * (in C they would be undefined behaviour).
 * @param src Value to convert.
 * @param base Base used when src is a string (2..36).
 * @param out Receives the int on success.
 * @return TI_OK, TI_ERR_TYPE_MISMATCH, TI_ERR_INVALID_VALUE or TI_ERR_VALUE_OUT_OF_RANGE.
 */
static ti_status_t convert_to_int(const value_t *src, int base, int *out)
{
    switch (src->type) {
    case VAL_INT:
        *out = src->int_val;
        return TI_OK;

    case VAL_FLOAT:
        /* The negated comparison also rejects NaN. 2147483648.0f is exactly representable. */
        if (!(src->float_val >= -2147483648.0f && src->float_val < 2147483648.0f)) {
            return TI_ERR_VALUE_OUT_OF_RANGE;
        }
        *out = (int)src->float_val; /* truncation toward zero: 3.9 -> 3, -3.9 -> -3 */
        return TI_OK;

    case VAL_BOOL:
        *out = src->bool_val ? 1 : 0;
        return TI_OK;

    case VAL_STRING:
        return parse_int_text(src->string_val, base, out);

    default:
        return TI_ERR_TYPE_MISMATCH;
    }
}

/**
 * @brief Convert a value to a float (the conversion behind to_float).
 * @param src Value to convert.
 * @param out Receives the float on success.
 * @return TI_OK, TI_ERR_TYPE_MISMATCH, TI_ERR_INVALID_VALUE or TI_ERR_VALUE_OUT_OF_RANGE.
 */
static ti_status_t convert_to_float(const value_t *src, float *out)
{
    switch (src->type) {
    case VAL_INT:
        *out = (float)src->int_val;
        return TI_OK;

    case VAL_FLOAT:
        *out = src->float_val;
        return TI_OK;

    case VAL_BOOL:
        *out = src->bool_val ? 1.0f : 0.0f;
        return TI_OK;

    case VAL_STRING:
        return parse_float_text(src->string_val, out);

    default:
        return TI_ERR_TYPE_MISMATCH;
    }
}

/**
 * @brief Convert a value to a bool (the conversion behind to_bool).
 * Numbers are true when non-zero. Strings must be exactly "true" or "false": the language is
 * strictly typed, so there is no "non-empty means true" rule.
 * @param src Value to convert.
 * @param out Receives the bool on success.
 * @return TI_OK, TI_ERR_TYPE_MISMATCH or TI_ERR_INVALID_VALUE.
 */
static ti_status_t convert_to_bool(const value_t *src, bool *out)
{
    switch (src->type) {
    case VAL_BOOL:
        *out = src->bool_val;
        return TI_OK;

    case VAL_INT:
        *out = (src->int_val != 0);
        return TI_OK;

    case VAL_FLOAT:
        *out = (src->float_val != 0.0f);
        return TI_OK;

    case VAL_STRING:
        if (src->string_val != NULL && strcmp(src->string_val, "true") == 0) {
            *out = true;
            return TI_OK;
        }
        if (src->string_val != NULL && strcmp(src->string_val, "false") == 0) {
            *out = false;
            return TI_OK;
        }
        return TI_ERR_INVALID_VALUE;

    default:
        return TI_ERR_TYPE_MISMATCH;
    }
}

/**
 * @brief Raise a conversion error from a to_* built-in, naming the offending value.
 * @param handle Calling runtime handle.
 * @param name Built-in name for the message (e.g. "to_int").
 * @param src The value that could not be converted.
 * @param status Why it failed.
 */
static void raise_conversion_error(ti_handle_t handle, const char *name, const value_t *src, ti_status_t status)
{
    if (src->type == VAL_STRING) {
        /* Show (a prefix of) the text so the user can see what failed to parse */
        ti_raise_error(handle, status, "%s cannot convert \"%.32s\": %s", name,
                       src->string_val != NULL ? src->string_val : "", ti_err_to_str(status));
    } else {
        ti_raise_error(handle, status, "%s cannot convert %s: %s", name,
                       val_type_to_str(src->type), ti_err_to_str(status));
    }
}

/**
 * @brief Built-in to_int(value [, base]): converts a value to an int.
 * A float is truncated toward zero like a C cast (to_int(3.9) is 3, to_int(-3.9) is -3).
 * The optional base (2..36) applies to strings: to_int("FF", 16) is 255.
 * @param handle Calling runtime handle, used to report errors.
 * @param argv Array of argument values.
 * @param argc Number of arguments passed (1 or 2).
 * @return Newly allocated int value, or NULL after raising an error.
 */
static value_t *builtin_to_int(ti_handle_t handle, value_t **argv, int argc)
{
    if (argc < 1 || argc > 2) {
        ti_raise_error(handle, TI_ERR_INVALID_ARG, "to_int expects 1 or 2 arguments, but received %d", argc);
        return NULL;
    }

    /* The base is optional, must be an int, and only means something for strings */
    int base = 10;
    if (argc == 2) {
        if (argv[1]->type != VAL_INT || argv[0]->type != VAL_STRING) {
            ti_raise_error(handle, TI_ERR_TYPE_MISMATCH, "to_int(value, base): base is an int and needs a string value");
            return NULL;
        }
        base = argv[1]->int_val;
        if (base < 2 || base > 36) {
            ti_raise_error(handle, TI_ERR_VALUE_OUT_OF_RANGE, "to_int: base must be 2..36, got %d", base);
            return NULL;
        }
    }

    int converted = 0;
    ti_status_t status = convert_to_int(argv[0], base, &converted);
    if (status != TI_OK) {
        raise_conversion_error(handle, "to_int", argv[0], status);
        return NULL;
    }

    value_t *result = val_new_int(converted);
    if (result == NULL) {
        ti_raise_error(handle, TI_ERR_NO_MEMORY, "to_int: cannot allocate the result");
    }
    return result;
}

/**
 * @brief Built-in to_float(value): converts a value to a float.
 * @param handle Calling runtime handle, used to report errors.
 * @param argv Array of argument values (exactly one).
 * @param argc Number of arguments passed.
 * @return Newly allocated float value, or NULL after raising an error.
 */
static value_t *builtin_to_float(ti_handle_t handle, value_t **argv, int argc)
{
    if (argc != 1) {
        ti_raise_error(handle, TI_ERR_INVALID_ARG, "to_float expects 1 argument, but received %d", argc);
        return NULL;
    }

    float converted = 0.0f;
    ti_status_t status = convert_to_float(argv[0], &converted);
    if (status != TI_OK) {
        raise_conversion_error(handle, "to_float", argv[0], status);
        return NULL;
    }

    value_t *result = val_new_float(converted);
    if (result == NULL) {
        ti_raise_error(handle, TI_ERR_NO_MEMORY, "to_float: cannot allocate the result");
    }
    return result;
}

/**
 * @brief Built-in to_bool(value): converts a number or "true"/"false" text to a bool.
 * @param handle Calling runtime handle, used to report errors.
 * @param argv Array of argument values (exactly one).
 * @param argc Number of arguments passed.
 * @return Newly allocated bool value, or NULL after raising an error.
 */
static value_t *builtin_to_bool(ti_handle_t handle, value_t **argv, int argc)
{
    if (argc != 1) {
        ti_raise_error(handle, TI_ERR_INVALID_ARG, "to_bool expects 1 argument, but received %d", argc);
        return NULL;
    }

    bool converted = false;
    ti_status_t status = convert_to_bool(argv[0], &converted);
    if (status != TI_OK) {
        raise_conversion_error(handle, "to_bool", argv[0], status);
        return NULL;
    }

    value_t *result = val_new_bool(converted);
    if (result == NULL) {
        ti_raise_error(handle, TI_ERR_NO_MEMORY, "to_bool: cannot allocate the result");
    }
    return result;
}

/**
 * @brief Built-in to_string(value): converts any value to its text form.
 * Usage in a Ti script: string s = to_string(42);
 * @param handle Calling runtime handle, used to report errors.
 * @param argv Array of argument values (exactly one, of any type).
 * @param argc Number of arguments passed.
 * @return Newly allocated string value, or NULL after raising an error.
 */
static value_t *builtin_to_string(ti_handle_t handle, value_t **argv, int argc)
{
    /* The VAL_ANY signature already enforces one argument; this guards direct calls from C */
    if (argc != 1) {
        ti_raise_error(handle, TI_ERR_INVALID_ARG, "to_string expects 1 argument, but received %d", argc);
        return NULL;
    }

    ti_text_buffer_t text = { NULL, 0, 0 };
    value_t *result = NULL;

    /* Render into the growable buffer, then hand the text to a new string value */
    ti_status_t status = ti_text_append_value(&text, argv[0]);
    if (status == TI_OK) {
        result = val_new_string(text.data != NULL ? text.data : "");
        if (result == NULL) {
            status = TI_ERR_NO_MEMORY;
        }
    }

    if (status != TI_OK) {
        ti_raise_error(handle, status, "to_string cannot convert %s: %s",
                       val_type_to_str(argv[0]->type), ti_err_to_str(status));
    }

    /* The buffer is only a temporary; the string value owns its own copy */
    ti_raw_free(text.data);
    return result;
}

/* -------------------- Public Functions -------------------- */

/* Register to_string, to_int, to_float, to_bool */
void ti_runtime_builtin_convert_register(void)
{
    ti_register_builtin_table(s_defs, (int)(sizeof(s_defs) / sizeof(s_defs[0])));
}
