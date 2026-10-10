#include "include/ti_runtime_builtin.h"
#include "include/ti_type_value_dict.h"
#include "include/ti_type_value_list.h"
#include "include/ti_type_value_bytes.h"
#include "include/tracked_memory.h"
#include "include/debug.h"
#include "TienInterpreter.h"
#include <stdarg.h>
#include <stdio.h>
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
 * @brief State shared with the dictionary iteration callback.
 */
typedef struct {
    text_buffer_t *text;   /* Buffer being filled */
    ti_status_t    status; /* First failure seen while iterating */
    bool           first;  /* true until the first entry is written (no separator before it) */
} dict_text_context_t;

/* -------------------- Static Variables -------------------- */

/* Set once the core built-ins have been registered */
static bool s_core_registered = false;

/* -------------------- Static Function Prototypes -------------------- */

static ti_status_t text_reserve(text_buffer_t *text, size_t min_capacity);
static ti_status_t text_append(text_buffer_t *text, const char *fmt, ...);
static bool text_dict_entry(const char *key, value_t *value, void *context);
static ti_status_t text_append_value(text_buffer_t *text, const value_t *value, bool quote_strings);
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
 * @brief Dictionary iteration callback: append '"key": value' (with a separator after the first).
 * @param key Entry key.
 * @param value Entry value.
 * @param context Pointer to the dict_text_context_t being filled.
 * @return true to continue, false to stop after a failure.
 */
static bool text_dict_entry(const char *key, value_t *value, void *context)
{
    dict_text_context_t *ctx = (dict_text_context_t *)context;

    /* Entries are separated by a comma; the first one has no separator */
    ctx->status = text_append(ctx->text, "%s\"%s\": ", ctx->first ? "" : ", ", key);
    if (ctx->status != TI_OK) {
        return false;
    }
    ctx->first = false;

    ctx->status = text_append_value(ctx->text, value, true);
    return ctx->status == TI_OK;
}

/**
 * @brief Append the text form of a value.
 * @param text Text buffer.
 * @param value Value to render.
 * @param quote_strings true to wrap strings in double quotes (used inside lists and dicts).
 * @return TI_OK, TI_ERR_NO_MEMORY or TI_ERR_TYPE_MISMATCH (a type with no text form).
 */
static ti_status_t text_append_value(text_buffer_t *text, const value_t *value, bool quote_strings)
{
    switch (value->type) {
    case VAL_INT:
        return text_append(text, "%d", value->int_val);

    case VAL_FLOAT:
        /* Same precision as print() */
        return text_append(text, "%.2f", value->float_val);

    case VAL_BOOL:
        return text_append(text, "%s", value->bool_val ? "true" : "false");

    case VAL_STRING: {
        const char *str = value->string_val ? value->string_val : "";
        return text_append(text, quote_strings ? "\"%s\"" : "%s", str);
    }

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

    case VAL_LIST: {
        /* [a, b, c] with each element rendered recursively (strings quoted) */
        ti_status_t status = text_append(text, "[");
        for (int i = 0; status == TI_OK && i < value->list_val->count; i++) {
            if (i > 0) {
                status = text_append(text, ", ");
            }
            if (status == TI_OK) {
                status = text_append_value(text, &value->list_val->items[i], true);
            }
        }
        return (status == TI_OK) ? text_append(text, "]") : status;
    }

    case VAL_DICT: {
        /* {"key": value, ...}: the order follows the hash map, not insertion */
        dict_text_context_t ctx = { text, TI_OK, true };
        ti_status_t status = text_append(text, "{");
        if (status != TI_OK) {
            return status;
        }
        val_dict_foreach(value->dict_val, text_dict_entry, &ctx);
        return (ctx.status == TI_OK) ? text_append(text, "}") : ctx.status;
    }

    default:
        return TI_ERR_TYPE_MISMATCH;
    }
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
    /* The function is variadic so it can take any type; the argument count is checked here */
    if (argc != 1) {
        ti_raise_error(handle, TI_ERR_INVALID_ARG, "to_string expects 1 argument, but received %d", argc);
        return NULL;
    }

    text_buffer_t text = { NULL, 0, 0 };
    value_t *result = NULL;

    /* Render into the growable buffer, then hand the text to a new string value */
    ti_status_t status = text_append_value(&text, argv[0], false);
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

    /* Variadic (-1) because the single argument may be of any type */
    register_builtin_function("to_string", VAL_STRING, NULL, -1, builtin_to_string);
}
