#ifndef TI_RUNTIME_BUILTIN_TEXT_H
#define TI_RUNTIME_BUILTIN_TEXT_H

/*
 * Internal helper for the core built-in topic files (not public API: kept beside the sources,
 * outside src/include, and not to be included by hosts).
 */

#include "include/ti_type.h"
#include "include/ti_type_value.h"
#include <stddef.h>

/**
 * @brief Growable text buffer. Start from { NULL, 0, 0 } and release data with ti_raw_free().
 */
typedef struct {
    char  *data;     /* NUL-terminated text (NULL until the first append) */
    size_t length;   /* Characters in use, not counting the terminator */
    size_t capacity; /* Allocated bytes */
} ti_text_buffer_t;

/**
 * @brief Append the text form of a value (what to_string shows).
 * @param text Text buffer to append to.
 * @param value Value to render.
 * @return TI_OK, TI_ERR_NO_MEMORY, TI_ERR_INVALID_VALUE or TI_ERR_TYPE_MISMATCH.
 */
ti_status_t ti_text_append_value(ti_text_buffer_t *text, const value_t *value);

#endif /* !TI_RUNTIME_BUILTIN_TEXT_H */
