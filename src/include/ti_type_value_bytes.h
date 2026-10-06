#ifndef TI_TYPE_VALUE_BYTES_H
#define TI_TYPE_VALUE_BYTES_H

#include "ti_type_value.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- Bytes Configuration -------------------- */

/* Maximum number of bytes a single buffer may hold (protects the heap on embedded targets) */
#ifndef TI_MAX_BYTES_LEN
#define TI_MAX_BYTES_LEN 4096
#endif

/* -------------------- Bytes Lifecycle Functions -------------------- */

/**
 * @brief Allocate a zero-filled byte buffer reference object (refcount = 1).
 * @param length Number of bytes (0 to TI_MAX_BYTES_LEN).
 * @return Pointer to the new bytes_t, or NULL on invalid length or out of memory.
 */
bytes_t *bytes_create(int length);

/**
 * @brief Increment the reference counter for the buffer (Shared Ownership).
 * @param bytes Pointer to the bytes object.
 */
void bytes_retain(bytes_t *bytes);

/**
 * @brief Decrement the reference counter. If it reaches zero, frees the data and the object.
 * @param bytes Pointer to the bytes object.
 */
void bytes_release(bytes_t *bytes);

/* -------------------- Native (C) Bridge Constructors -------------------- */

/**
 * @brief Create a bytes value holding a copy of the given data (safe default for native code).
 * @param src Source data to copy (may be NULL only when length is 0).
 * @param length Number of bytes to copy (0 to TI_MAX_BYTES_LEN).
 * @return Newly allocated VAL_BYTES value_t, or NULL on invalid arguments or out of memory.
 */
value_t *val_new_bytes(const uint8_t *src, int length);

/**
 * @brief Create a zero-filled bytes value and expose its storage so native code can fill it
 * directly (e.g. read from a UART straight into the buffer without a second copy).
 * @param length Number of bytes (0 to TI_MAX_BYTES_LEN).
 * @param out_data Receives a writable pointer to the storage (NULL when length is 0).
 * @return Newly allocated VAL_BYTES value_t, or NULL on invalid arguments or out of memory.
 */
value_t *val_new_bytes_alloc(int length, uint8_t **out_data);

/**
 * @brief Create a bytes value that adopts a buffer already allocated with ti_raw_malloc.
 * On success the value owns buf and frees it with ti_raw_free; on failure the caller keeps it.
 * @param buf Buffer allocated with ti_raw_malloc/ti_raw_calloc (may be NULL only when length is 0).
 * @param length Number of valid bytes in buf (0 to TI_MAX_BYTES_LEN).
 * @return Newly allocated VAL_BYTES value_t, or NULL on invalid arguments or out of memory.
 */
value_t *val_new_bytes_take(uint8_t *buf, int length);

/**
 * @brief Get a read-only pointer to the bytes of a value, without copying.
 * Valid only while the value is alive and unmodified: a native that needs the data after the
 * call returns must copy it.
 * @param value A VAL_BYTES value.
 * @return Pointer to the data (NULL if value is not VAL_BYTES or the buffer is empty).
 */
const uint8_t *val_bytes_data(const value_t *value);

/**
 * @brief Get the number of bytes of a value.
 * @param value A VAL_BYTES value.
 * @return Length in bytes (0 if value is not VAL_BYTES).
 */
int val_bytes_len(const value_t *value);

/* -------------------- Bytes Element Functions -------------------- */

/**
 * @brief Read the byte at the given index.
 * @param bytes Bytes object.
 * @param index Zero-based index.
 * @param out Receives the byte as an int in 0..255.
 * @return TI_OK, TI_ERR_INDEX_OUT_OF_RANGE or TI_ERR_INVALID_ARG.
 */
ti_status_t val_bytes_get(const bytes_t *bytes, int index, int *out);

/**
 * @brief Overwrite the byte at the given index.
 * @param bytes Bytes object.
 * @param index Zero-based index.
 * @param byte Value to store (must be in 0..255).
 * @return TI_OK, TI_ERR_INDEX_OUT_OF_RANGE, TI_ERR_VALUE_OUT_OF_RANGE or TI_ERR_INVALID_ARG.
 */
ti_status_t val_bytes_set(bytes_t *bytes, int index, int byte);

/**
 * @brief Append one byte, growing the buffer if needed.
 * @param bytes Bytes object.
 * @param byte Value to append (must be in 0..255).
 * @return TI_OK, TI_ERR_VALUE_OUT_OF_RANGE, TI_ERR_LIMIT_EXCEEDED, TI_ERR_NO_MEMORY or TI_ERR_INVALID_ARG.
 */
ti_status_t val_bytes_push(bytes_t *bytes, int byte);

/**
 * @brief Append a block of bytes, growing the buffer if needed.
 * @param bytes Bytes object.
 * @param src Data to append (may be NULL only when length is 0).
 * @param length Number of bytes to append.
 * @return TI_OK, TI_ERR_LIMIT_EXCEEDED, TI_ERR_NO_MEMORY or TI_ERR_INVALID_ARG.
 */
ti_status_t val_bytes_append(bytes_t *bytes, const uint8_t *src, int length);

/* -------------------- Bytes Whole-Buffer Functions -------------------- */

/**
 * @brief Create an independent deep copy of a buffer (val_copy only shares the reference).
 * @param bytes Source bytes object.
 * @param out Receives a newly allocated VAL_BYTES value (caller owns it) on success.
 * @return TI_OK, TI_ERR_NO_MEMORY or TI_ERR_INVALID_ARG.
 */
ti_status_t val_bytes_clone(const bytes_t *bytes, value_t **out);

/**
 * @brief Copy the range [from, to) into a new buffer.
 * @param bytes Source bytes object.
 * @param from First index (inclusive).
 * @param to End index (exclusive), must satisfy from <= to <= length.
 * @param out Receives a newly allocated VAL_BYTES value (caller owns it) on success.
 * @return TI_OK, TI_ERR_INDEX_OUT_OF_RANGE, TI_ERR_NO_MEMORY or TI_ERR_INVALID_ARG.
 */
ti_status_t val_bytes_slice(const bytes_t *bytes, int from, int to, value_t **out);

/**
 * @brief Concatenate two buffers into a new one.
 * @param left First buffer.
 * @param right Second buffer.
 * @param out Receives a newly allocated VAL_BYTES value (caller owns it) on success.
 * @return TI_OK, TI_ERR_LIMIT_EXCEEDED, TI_ERR_NO_MEMORY or TI_ERR_INVALID_ARG.
 */
ti_status_t val_bytes_concat(const bytes_t *left, const bytes_t *right, value_t **out);

/**
 * @brief Compare two buffers for equal length and content.
 * @param left First buffer.
 * @param right Second buffer.
 * @return true if both are non-NULL and identical, false otherwise.
 */
bool val_bytes_equal(const bytes_t *left, const bytes_t *right);

#ifdef __cplusplus
}
#endif

#endif /* !TI_TYPE_VALUE_BYTES_H */
