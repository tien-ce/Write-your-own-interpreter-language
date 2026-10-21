#include "include/ti_runtime_builtin.h"
#include "chashmap.h"
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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------- Constants -------------------- */

/* Initial capacity of the text buffer */
#define TEXT_INITIAL_CAPACITY 32

/* -------------------- Static Types -------------------- */

/**
 * @brief Growable text buffer used to build the result of to_string().
 */
typedef struct {
    char  *data;     /* NUL-terminated text (NULL until the first append) */
    size_t length;   /* Characters in use, not counting the terminator */
    size_t capacity; /* Allocated bytes */
} text_buffer_t;

/**
 * @brief Dictionary keys collected for sorted JSON output.
 */
typedef struct {
    const char **items;    /* Key pointers (owned by the dictionary, not copied) */
    size_t       count;    /* Keys collected */
    size_t       capacity; /* Allocated pointer slots */
    ti_status_t  status;   /* First failure while collecting */
} json_keys_t;

/* -------------------- Static Variables -------------------- */

/* Set once the core built-ins have been registered */
static bool s_core_registered = false;

/* -------------------- Static Function Prototypes -------------------- */

static ti_status_t text_reserve(text_buffer_t *text, size_t min_capacity);
static ti_status_t text_append(text_buffer_t *text, const char *fmt, ...);
static ti_status_t text_append_raw(text_buffer_t *text, const char *chunk, size_t length);
static ti_status_t json_append_string(text_buffer_t *text, const char *str);
static ti_status_t json_append_float(text_buffer_t *text, float number);
static bool json_collect_key(const char *key, value_t *value, void *context);
static int json_compare_keys(const void *a, const void *b);
static ti_status_t json_append_dict(text_buffer_t *text, const value_t *dict);
static ti_status_t json_append_value(text_buffer_t *text, const value_t *value);
static ti_status_t text_append_value(text_buffer_t *text, const value_t *value);
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

/* -------------------- Static Functions -------------------- */

/**
 * @brief Make sure the buffer can hold at least min_capacity bytes, growing geometrically.
 * @param text Text buffer.
 * @param min_capacity Required number of bytes (including the terminator).
 * @return TI_OK or TI_ERR_NO_MEMORY (the buffer is left intact on failure).
 */
static ti_status_t text_reserve(text_buffer_t *text, size_t min_capacity)
{
    if (min_capacity <= text->capacity) {
        return TI_OK;
    }

    /* Double the capacity (starting small) until the request fits */
    size_t new_capacity = (text->capacity > 0) ? text->capacity : TEXT_INITIAL_CAPACITY;
    while (new_capacity < min_capacity) {
        new_capacity *= 2;
    }

    /* Assign to a temporary so the old block stays valid if realloc fails */
    char *new_data = (char *)ti_raw_realloc(text->data, new_capacity);
    if (new_data == NULL) {
        return TI_ERR_NO_MEMORY;
    }
    text->data = new_data;
    text->capacity = new_capacity;
    return TI_OK;
}

/**
 * @brief Append printf-style formatted text to the buffer.
 * @param text Text buffer.
 * @param fmt printf-style format.
 * @return TI_OK, TI_ERR_NO_MEMORY or TI_ERR_INTERNAL (formatting failed).
 */
static ti_status_t text_append(text_buffer_t *text, const char *fmt, ...)
{
    va_list args;
    va_list measure;
    va_start(args, fmt);

    /* First pass: how many characters would this need? (a copy of the argument list is consumed) */
    va_copy(measure, args);
    int needed = vsnprintf(NULL, 0, fmt, measure);
    va_end(measure);
    if (needed < 0) {
        va_end(args);
        return TI_ERR_INTERNAL;
    }

    /* Second pass: grow once, then write straight into the buffer */
    ti_status_t status = text_reserve(text, text->length + (size_t)needed + 1);
    if (status == TI_OK) {
        vsnprintf(text->data + text->length, (size_t)needed + 1, fmt, args);
        text->length += (size_t)needed;
    }
    va_end(args);
    return status;
}

/**
 * @brief Append raw characters to the buffer without any formatting.
 * @param text Text buffer.
 * @param chunk Characters to append (need not be NUL-terminated).
 * @param length Number of characters.
 * @return TI_OK or TI_ERR_NO_MEMORY.
 */
static ti_status_t text_append_raw(text_buffer_t *text, const char *chunk, size_t length)
{
    ti_status_t status = text_reserve(text, text->length + length + 1);
    if (status != TI_OK) {
        return status;
    }

    memcpy(text->data + text->length, chunk, length);
    text->length += length;
    text->data[text->length] = '\0';
    return TI_OK;
}

/**
 * @brief Append a string as a JSON string literal: quoted, with the required escapes.
 * Only the output text is escaped; the script's string value is never modified.
 * @param text Text buffer.
 * @param str String to write (NULL is written as an empty string).
 * @return TI_OK or TI_ERR_NO_MEMORY.
 */
static ti_status_t json_append_string(text_buffer_t *text, const char *str)
{
    ti_status_t status = text_append_raw(text, "\"", 1);

    for (const char *p = (str != NULL) ? str : ""; status == TI_OK && *p != '\0'; p++) {
        unsigned char ch = (unsigned char)*p;
        switch (ch) {
        case '"':  status = text_append_raw(text, "\\\"", 2); break;
        case '\\': status = text_append_raw(text, "\\\\", 2); break;
        case '\n': status = text_append_raw(text, "\\n", 2);  break;
        case '\r': status = text_append_raw(text, "\\r", 2);  break;
        case '\t': status = text_append_raw(text, "\\t", 2);  break;
        case '\b': status = text_append_raw(text, "\\b", 2);  break;
        case '\f': status = text_append_raw(text, "\\f", 2);  break;
        default:
            if (ch < 0x20) {
                /* Remaining control characters must be written as \u00XX */
                status = text_append(text, "\\u%04X", ch);
            } else {
                /* Printable ASCII and UTF-8 bytes are copied as they are */
                status = text_append_raw(text, p, 1);
            }
            break;
        }
    }
    return (status == TI_OK) ? text_append_raw(text, "\"", 1) : status;
}

/**
 * @brief Append a float as a JSON number that keeps its float-ness.
 * About 7 significant digits are written; a whole number gets ".0" (3 becomes 3.0), so a reader
 * can tell a float from an int.
 * @param text Text buffer.
 * @param number Value to write.
 * @return TI_OK, TI_ERR_NO_MEMORY or TI_ERR_INVALID_VALUE (NaN and infinity have no JSON form).
 */
static ti_status_t json_append_float(text_buffer_t *text, float number)
{
    if (!isfinite(number)) {
        return TI_ERR_INVALID_VALUE;
    }

    char digits[32];
    int length = snprintf(digits, sizeof(digits), "%.7g", (double)number);
    if (length < 0 || (size_t)length >= sizeof(digits)) {
        return TI_ERR_INTERNAL;
    }

    ti_status_t status = text_append_raw(text, digits, (size_t)length);

    /* No decimal point and no exponent means it looks like an int: mark it as a float */
    if (status == TI_OK && strpbrk(digits, ".eE") == NULL) {
        status = text_append_raw(text, ".0", 2);
    }
    return status;
}

/**
 * @brief Dictionary iteration callback: remember each key so the keys can be sorted afterwards.
 * @param key Entry key (owned by the dictionary, valid while it is not modified).
 * @param value Entry value (unused here).
 * @param context Pointer to the json_keys_t being filled.
 * @return true to continue, false to stop after an allocation failure.
 */
static bool json_collect_key(const char *key, value_t *value, void *context)
{
    (void)value;
    json_keys_t *keys = (json_keys_t *)context;

    /* Grow the pointer array geometrically */
    if (keys->count == keys->capacity) {
        size_t new_capacity = (keys->capacity > 0) ? keys->capacity * 2 : 8;
        const char **grown = (const char **)ti_raw_realloc((void *)keys->items, new_capacity * sizeof(const char *));
        if (grown == NULL) {
            keys->status = TI_ERR_NO_MEMORY;
            return false;
        }
        keys->items = grown;
        keys->capacity = new_capacity;
    }
    keys->items[keys->count++] = key;
    return true;
}

/**
 * @brief qsort comparator ordering two key pointers alphabetically.
 * @param a Pointer to a const char * element.
 * @param b Pointer to a const char * element.
 * @return Negative, zero or positive like strcmp.
 */
static int json_compare_keys(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/**
 * @brief Append a dictionary as a JSON object with its keys in alphabetical order.
 * @param text Text buffer.
 * @param dict A VAL_DICT value.
 * @return TI_OK or the first error from rendering a member.
 */
static ti_status_t json_append_dict(text_buffer_t *text, const value_t *dict)
{
    /* The hash map has no useful order, so collect the keys and sort them for stable output */
    json_keys_t keys = { NULL, 0, 0, TI_OK };
    val_dict_foreach(dict->dict_val, json_collect_key, &keys);
    ti_status_t status = keys.status;

    if (status == TI_OK && keys.count > 1) {
        qsort((void *)keys.items, keys.count, sizeof(const char *), json_compare_keys);
    }
    if (status == TI_OK) {
        status = text_append_raw(text, "{", 1);
    }

    for (size_t i = 0; status == TI_OK && i < keys.count; i++) {
        if (i > 0) {
            status = text_append_raw(text, ",", 1);
        }
        if (status == TI_OK) {
            status = json_append_string(text, keys.items[i]);
        }
        if (status == TI_OK) {
            status = text_append_raw(text, ":", 1);
        }
        if (status == TI_OK) {
            const value_t *member = (const value_t *)chashmap_get(dict->dict_val->map, keys.items[i]);
            status = (member != NULL) ? json_append_value(text, member) : TI_ERR_INTERNAL;
        }
    }

    ti_raw_free((void *)keys.items);
    return (status == TI_OK) ? text_append_raw(text, "}", 1) : status;
}

/**
 * @brief Append a value as JSON.
 * @param text Text buffer.
 * @param value Value to write.
 * @return TI_OK, TI_ERR_NO_MEMORY, TI_ERR_INVALID_VALUE (NaN/infinity) or TI_ERR_TYPE_MISMATCH
 *         (a function value has no JSON form).
 */
static ti_status_t json_append_value(text_buffer_t *text, const value_t *value)
{
    switch (value->type) {
    case VAL_INT:
        return text_append(text, "%d", value->int_val);

    case VAL_FLOAT:
        return json_append_float(text, value->float_val);

    case VAL_BOOL:
        return text_append_raw(text, value->bool_val ? "true" : "false", value->bool_val ? 4 : 5);

    case VAL_STRING:
        return json_append_string(text, value->string_val);

    case VAL_NULL:
    case VAL_VOID:
        return text_append_raw(text, "null", 4);

    case VAL_BYTES: {
        /* Binary data becomes a JSON string of hex digits */
        ti_status_t status = text_append_raw(text, "\"", 1);
        for (int i = 0; status == TI_OK && i < value->bytes_val->length; i++) {
            status = text_append(text, "%02X", value->bytes_val->data[i]);
        }
        return (status == TI_OK) ? text_append_raw(text, "\"", 1) : status;
    }

    case VAL_LIST: {
        ti_status_t status = text_append_raw(text, "[", 1);
        for (int i = 0; status == TI_OK && i < value->list_val->count; i++) {
            if (i > 0) {
                status = text_append_raw(text, ",", 1);
            }
            if (status == TI_OK) {
                status = json_append_value(text, &value->list_val->items[i]);
            }
        }
        return (status == TI_OK) ? text_append_raw(text, "]", 1) : status;
    }

    case VAL_DICT:
        return json_append_dict(text, value);

    default:
        return TI_ERR_TYPE_MISMATCH;
    }
}

/**
 * @brief Append the text form of a value (the conversion behind to_string).
 * Scalars are written plainly (a string without quotes). Lists and dictionaries are written as JSON.
 * @param text Text buffer.
 * @param value Value to render.
 * @return TI_OK, TI_ERR_NO_MEMORY, TI_ERR_INVALID_VALUE or TI_ERR_TYPE_MISMATCH.
 */
static ti_status_t text_append_value(text_buffer_t *text, const value_t *value)
{
    switch (value->type) {
    case VAL_INT:
        return text_append(text, "%d", value->int_val);

    case VAL_FLOAT:
        /* Same precision as print() */
        return text_append(text, "%.2f", value->float_val);

    case VAL_BOOL:
        return text_append(text, "%s", value->bool_val ? "true" : "false");

    case VAL_STRING:
        return text_append(text, "%s", value->string_val ? value->string_val : "");

    case VAL_NULL:
    case VAL_VOID:
        return text_append(text, "null");

    case VAL_FUNC:
        return text_append(text, "<func %s>", value->func_name);

    case VAL_BYTES: {
        /* Binary data has no text form: render it as hex digits */
        const uint8_t *data = value->bytes_val->data;
        for (int i = 0; i < value->bytes_val->length; i++) {
            ti_status_t status = text_append(text, "%02X", data[i]);
            if (status != TI_OK) {
                return status;
            }
        }
        return TI_OK;
    }

    case VAL_LIST:
    case VAL_DICT:
        return json_append_value(text, value);

    default:
        return TI_ERR_TYPE_MISMATCH;
    }
}


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

    text_buffer_t text = { NULL, 0, 0 };
    value_t *result = NULL;

    /* Render into the growable buffer, then hand the text to a new string value */
    ti_status_t status = text_append_value(&text, argv[0]);
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

/* Register the language-level built-in functions */
void ti_register_core_builtins(void)
{
    /* Registration happens once, whichever host or runtime asks first */
    if (s_core_registered) {
        return;
    }
    s_core_registered = true;

    /* One argument of any type: VAL_ANY keeps the type open while the runtime still checks the count */
    static param_t any_param[] = { { VAL_ANY, "value", VAL_NULL } };

    register_builtin_function("to_string", VAL_STRING, any_param, 1, builtin_to_string);
    register_builtin_function("to_float", VAL_FLOAT, any_param, 1, builtin_to_float);
    register_builtin_function("to_bool", VAL_BOOL, any_param, 1, builtin_to_bool);

    /* Variadic (-1) only because of the optional base: to_int(value) or to_int(text, base) */
    register_builtin_function("to_int", VAL_INT, NULL, -1, builtin_to_int);
}
