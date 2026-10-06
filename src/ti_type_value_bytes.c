#include "include/ti_type_value_bytes.h"
#include "include/tracked_memory.h"
#include <stdlib.h>
#include <string.h>

/* -------------------- Constants -------------------- */

/* Number of bytes allocated on the first growth of an empty buffer */
#define BYTES_INITIAL_CAPACITY 16

/* -------------------- Static Function Prototypes -------------------- */

static bool bytes_is_valid_length(int length);
static ti_status_t bytes_reserve(bytes_t *bytes, int min_capacity);
static value_t *bytes_wrap(bytes_t *bytes);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Check that a length is within the configured limits.
 * @param length Candidate length in bytes.
 * @return true if 0 <= length <= TI_MAX_BYTES_LEN.
 */
static bool bytes_is_valid_length(int length)
{
    /* Negative lengths are bugs in the caller; anything above the limit would exhaust the heap */
    return length >= 0 && length <= TI_MAX_BYTES_LEN;
}

/**
 * @brief Ensure the buffer can hold at least min_capacity bytes, growing geometrically.
 * @param bytes Bytes object.
 * @param min_capacity Required number of bytes.
 * @return TI_OK, TI_ERR_LIMIT_EXCEEDED or TI_ERR_NO_MEMORY (the buffer is left intact on failure).
 */
static ti_status_t bytes_reserve(bytes_t *bytes, int min_capacity)
{
    /* Fast path: enough room already, nothing to allocate */
    if (min_capacity <= bytes->capacity) {
        return TI_OK;
    }

    /* The request itself is beyond the hard limit, so growing can never satisfy it */
    if (min_capacity > TI_MAX_BYTES_LEN) {
        return TI_ERR_LIMIT_EXCEEDED;
    }

    /* Geometric growth keeps repeated pushes amortised O(1); an empty buffer starts at a small size */
    int new_capacity = (bytes->capacity > 0) ? bytes->capacity * 2 : BYTES_INITIAL_CAPACITY;

    /* Doubling may undershoot a large single request, or overshoot the limit: clamp both ways */
    if (new_capacity < min_capacity) {
        new_capacity = min_capacity;
    }
    if (new_capacity > TI_MAX_BYTES_LEN) {
        new_capacity = TI_MAX_BYTES_LEN;
    }

    /* Assign to a temporary so the old block stays valid and owned if realloc fails */
    uint8_t *new_data = (uint8_t *)ti_raw_realloc(bytes->data, (size_t)new_capacity);
    if (new_data == NULL) {
        return TI_ERR_NO_MEMORY;
    }
    bytes->data = new_data;
    bytes->capacity = new_capacity;
    return TI_OK;
}

/**
 * @brief Wrap a bytes object in a new VAL_BYTES value, taking over the caller's reference.
 * @param bytes Bytes object (refcount 1) to wrap.
 * @return The value, or NULL if the value shell cannot be allocated (bytes is released then).
 */
static value_t *bytes_wrap(bytes_t *bytes)
{
    /* The value shell is a separate allocation, so it can fail independently of the buffer */
    value_t *val = val_init(VAL_BYTES);
    if (val == NULL) {
        /* The caller handed us its only reference: drop it so nothing leaks on this path */
        bytes_release(bytes);
        return NULL;
    }

    /* The value now owns the single reference */
    val->bytes_val = bytes;
    return val;
}

/* -------------------- Bytes Lifecycle Operations -------------------- */

/* Allocate a zero-filled byte buffer reference object */
bytes_t *bytes_create(int length)
{
    /* Reject impossible sizes before touching the heap */
    if (!bytes_is_valid_length(length)) {
        return NULL;
    }

    /* Allocate the header (zeroed: data = NULL, length = capacity = refcount = 0) */
    bytes_t *bytes = (bytes_t *)ti_raw_calloc(1, sizeof(bytes_t));
    if (bytes == NULL) {
        return NULL;
    }

    /* Storage is allocated only when there is something to hold, so empty buffers cost no data block */
    if (length > 0) {
        bytes->data = (uint8_t *)ti_raw_calloc((size_t)length, 1);
        if (bytes->data == NULL) {
            /* Undo the header allocation so a failed create leaks nothing */
            ti_raw_free(bytes);
            return NULL;
        }
    }

    /* The caller receives the first (and only) reference */
    bytes->length = length;
    bytes->capacity = length;
    bytes->refcount = 1;
    return bytes;
}

/* Increment the reference counter for shared ownership */
void bytes_retain(bytes_t *bytes)
{
    if (bytes != NULL) {
        bytes->refcount++;
    }
}

/* Decrement the reference counter. Free the buffer if it reaches zero */
void bytes_release(bytes_t *bytes)
{
    if (bytes == NULL) {
        return;
    }

    /* Other owners still exist: only drop this reference */
    bytes->refcount--;
    if (bytes->refcount > 0) {
        return;
    }

    /* Last reference gone: free the data block first, then the header that points to it */
    ti_raw_free(bytes->data);
    ti_raw_free(bytes);
}

/* -------------------- Native (C) Bridge Constructors -------------------- */

/* Create a bytes value holding a copy of the given data */
value_t *val_new_bytes(const uint8_t *src, int length)
{
    /* A non-empty copy needs a source; a NULL source is only valid for an empty buffer */
    if (length > 0 && src == NULL) {
        return NULL;
    }

    bytes_t *bytes = bytes_create(length);
    if (bytes == NULL) {
        return NULL;
    }

    /* Copy in: the buffer is independent of the caller's memory from now on */
    if (length > 0) {
        memcpy(bytes->data, src, (size_t)length);
    }
    return bytes_wrap(bytes);
}

/* Create a zero-filled bytes value and expose its storage for native code to fill */
value_t *val_new_bytes_alloc(int length, uint8_t **out_data)
{
    bytes_t *bytes = bytes_create(length);
    if (bytes == NULL) {
        return NULL;
    }

    /* Read the pointer before wrapping: bytes_wrap releases (frees) the object if it fails */
    uint8_t *data = bytes->data;
    value_t *val = bytes_wrap(bytes);

    /* Only publish the pointer when the value really exists, so the caller never sees a dangling one */
    if (val != NULL && out_data != NULL) {
        *out_data = data;
    }
    return val;
}

/* Create a bytes value that adopts a buffer allocated with ti_raw_malloc */
value_t *val_new_bytes_take(uint8_t *buf, int length)
{
    /* Validate first: on any failure the caller must still own buf, so nothing may be freed here */
    if (!bytes_is_valid_length(length) || (length > 0 && buf == NULL)) {
        return NULL;
    }

    /* Allocate only the header; the data block already exists and is adopted, not copied */
    bytes_t *bytes = (bytes_t *)ti_raw_calloc(1, sizeof(bytes_t));
    if (bytes == NULL) {
        return NULL;
    }
    bytes->data = (length > 0) ? buf : NULL;
    bytes->length = length;
    bytes->capacity = length;
    bytes->refcount = 1;

    /* Ownership of buf transfers only once the value shell exists, so a failure here can still give it back */
    value_t *val = val_init(VAL_BYTES);
    if (val == NULL) {
        /* Free just the header: bytes->data is buf, which still belongs to the caller */
        ti_raw_free(bytes);
        return NULL;
    }
    val->bytes_val = bytes;
    return val;
}

/* Get a read-only pointer to the bytes of a value, without copying */
const uint8_t *val_bytes_data(const value_t *value)
{
    /* A wrong-typed or empty value yields NULL instead of reading the wrong union member */
    if (value == NULL || value->type != VAL_BYTES || value->bytes_val == NULL) {
        return NULL;
    }
    return value->bytes_val->data;
}

/* Get the number of bytes of a value */
int val_bytes_len(const value_t *value)
{
    /* Same type guard as val_bytes_data: anything that is not bytes has length 0 */
    if (value == NULL || value->type != VAL_BYTES || value->bytes_val == NULL) {
        return 0;
    }
    return value->bytes_val->length;
}

/* -------------------- Bytes Element Operations -------------------- */

/* Read the byte at the given index */
ti_status_t val_bytes_get(const bytes_t *bytes, int index, int *out)
{
    if (bytes == NULL || out == NULL) {
        return TI_ERR_INVALID_ARG;
    }

    /* Bound check on both sides; negative indices are not supported */
    if (index < 0 || index >= bytes->length) {
        return TI_ERR_INDEX_OUT_OF_RANGE;
    }

    /* Widen the unsigned byte to int: scripts see 0..255, never a negative value */
    *out = bytes->data[index];
    return TI_OK;
}

/* Overwrite the byte at the given index */
ti_status_t val_bytes_set(bytes_t *bytes, int index, int byte)
{
    if (bytes == NULL) {
        return TI_ERR_INVALID_ARG;
    }

    /* Position first, then value, so the error tells which part of the request was wrong */
    if (index < 0 || index >= bytes->length) {
        return TI_ERR_INDEX_OUT_OF_RANGE;
    }
    if (byte < 0 || byte > 255) {
        return TI_ERR_VALUE_OUT_OF_RANGE;
    }

    bytes->data[index] = (uint8_t)byte;
    return TI_OK;
}

/* Append one byte, growing the buffer if needed */
ti_status_t val_bytes_push(bytes_t *bytes, int byte)
{
    if (bytes == NULL) {
        return TI_ERR_INVALID_ARG;
    }

    /* Validate the value before growing so a bad byte does not cost an allocation */
    if (byte < 0 || byte > 255) {
        return TI_ERR_VALUE_OUT_OF_RANGE;
    }

    /* Make room for one more byte (may reallocate, fail, or hit the length limit) */
    ti_status_t status = bytes_reserve(bytes, bytes->length + 1);
    if (status != TI_OK) {
        return status;
    }

    bytes->data[bytes->length] = (uint8_t)byte;
    bytes->length++;
    return TI_OK;
}

/* Append a block of bytes, growing the buffer if needed */
ti_status_t val_bytes_append(bytes_t *bytes, const uint8_t *src, int length)
{
    if (bytes == NULL || length < 0 || (length > 0 && src == NULL)) {
        return TI_ERR_INVALID_ARG;
    }

    /* Appending nothing succeeds without touching the buffer */
    if (length == 0) {
        return TI_OK;
    }

    /* Compare against the remaining room instead of adding, so the sum can never overflow int */
    if (length > TI_MAX_BYTES_LEN - bytes->length) {
        return TI_ERR_LIMIT_EXCEEDED;
    }

    ti_status_t status = bytes_reserve(bytes, bytes->length + length);
    if (status != TI_OK) {
        return status;
    }

    /* Copy after the current end, then extend the length */
    memcpy(bytes->data + bytes->length, src, (size_t)length);
    bytes->length += length;
    return TI_OK;
}

/* -------------------- Bytes Whole-Buffer Operations -------------------- */

/* Create an independent deep copy of a buffer */
ti_status_t val_bytes_clone(const bytes_t *bytes, value_t **out)
{
    if (bytes == NULL || out == NULL) {
        return TI_ERR_INVALID_ARG;
    }

    /* val_new_bytes copies the data, which is what makes the result independent of the source */
    value_t *copy = val_new_bytes(bytes->data, bytes->length);
    if (copy == NULL) {
        return TI_ERR_NO_MEMORY;
    }
    *out = copy;
    return TI_OK;
}

/* Copy the range [from, to) into a new buffer */
ti_status_t val_bytes_slice(const bytes_t *bytes, int from, int to, value_t **out)
{
    if (bytes == NULL || out == NULL) {
        return TI_ERR_INVALID_ARG;
    }

    /* One check covers all bad ranges: negative start, start after end, end past the data.
     * from == to is allowed and yields an empty buffer. */
    if (from < 0 || from > to || to > bytes->length) {
        return TI_ERR_INDEX_OUT_OF_RANGE;
    }

    /* The slice is a copy, not a view, so later writes to either buffer do not affect the other */
    value_t *copy = val_new_bytes(bytes->data + from, to - from);
    if (copy == NULL) {
        return TI_ERR_NO_MEMORY;
    }
    *out = copy;
    return TI_OK;
}

/* Concatenate two buffers into a new one */
ti_status_t val_bytes_concat(const bytes_t *left, const bytes_t *right, value_t **out)
{
    if (left == NULL || right == NULL || out == NULL) {
        return TI_ERR_INVALID_ARG;
    }

    /* Check the combined size without adding, to stay clear of int overflow */
    if (left->length > TI_MAX_BYTES_LEN - right->length) {
        return TI_ERR_LIMIT_EXCEEDED;
    }

    /* Allocate the result once at its final size, then fill it directly (no intermediate copy) */
    uint8_t *data = NULL;
    value_t *val = val_new_bytes_alloc(left->length + right->length, &data);
    if (val == NULL) {
        return TI_ERR_NO_MEMORY;
    }

    /* Empty operands have a NULL data pointer, so skip memcpy for them */
    if (left->length > 0) {
        memcpy(data, left->data, (size_t)left->length);
    }
    if (right->length > 0) {
        memcpy(data + left->length, right->data, (size_t)right->length);
    }
    *out = val;
    return TI_OK;
}

/* Compare two buffers for equal length and content */
bool val_bytes_equal(const bytes_t *left, const bytes_t *right)
{
    /* Different lengths can never be equal, so memcmp only ever sees equal-sized ranges */
    if (left == NULL || right == NULL || left->length != right->length) {
        return false;
    }

    /* Two empty buffers are equal; skip memcmp because their data pointers may be NULL */
    return left->length == 0 || memcmp(left->data, right->data, (size_t)left->length) == 0;
}
