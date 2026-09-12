#ifndef TRACKED_MEMORY_H
#define TRACKED_MEMORY_H

#include <stdlib.h>

/* -------------------- Allocator Header -------------------- */

typedef struct ALLOC_HDR_STRUCT {
    struct ALLOC_HDR_STRUCT *next;
    struct ALLOC_HDR_STRUCT *previous;
} alloc_hdr_t;

/* -------------------- Public Allocator API -------------------- */

/**
 * @brief Allocate tracked memory block of the given size.
 * @param size Number of bytes to allocate.
 * @return Pointer to allocated payload memory.
 */
void *tracked_malloc(size_t size);

/**
 * @brief Allocate and zero-initialize tracked memory for an array.
 * @param num Number of elements.
 * @param size Size of each element in bytes.
 * @return Pointer to zero-initialized allocated memory.
 */
void *tracked_calloc(size_t num, size_t size);

/**
 * @brief Resize an existing tracked memory allocation.
 * @param ptr Pointer to existing allocation.
 * @param new_size New requested size in bytes.
 * @return Pointer to reallocated memory.
 */
void *tracked_realloc(void *ptr, size_t new_size);

/**
 * @brief Free a tracked memory allocation and remove it from the tracking list.
 * @param ptr Pointer to allocated memory to free.
 */
void tracked_free(void *ptr);

/**
 * @brief Duplicate a null-terminated string using tracked allocation.
 * @param s Source string to duplicate.
 * @return Newly allocated copy of string.
 */
char *tracked_strdup(const char *s);

/**
 * @brief Free all tracked allocations in one batch (used on exit or fatal errors).
 */
void free_all(void);

#endif /* !TRACKED_MEMORY_H */
