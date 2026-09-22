#ifndef TRACKED_MEMORY_H
#define TRACKED_MEMORY_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- Allocator Header -------------------- */

typedef struct ALLOC_HDR_STRUCT {
    struct ALLOC_HDR_STRUCT *next;
    struct ALLOC_HDR_STRUCT *previous;
} alloc_hdr_t;

/* -------------------- Custom Allocator Hook Types -------------------- */

typedef void *(*ti_malloc_fn_t)(size_t size);
typedef void  (*ti_free_fn_t)(void *ptr);
typedef void *(*ti_realloc_fn_t)(void *ptr, size_t size);

/**
 * @brief Register custom memory allocation hooks (e.g. for ESP32 PSRAM).
 * @param malloc_fn Custom malloc function pointer (or NULL for default).
 * @param free_fn Custom free function pointer (or NULL for default).
 * @param realloc_fn Custom realloc function pointer (or NULL for default).
 */
void ti_register_allocator(ti_malloc_fn_t malloc_fn, ti_free_fn_t free_fn, ti_realloc_fn_t realloc_fn);

/* -------------------- Raw Allocator API (Untracked) -------------------- */

/**
 * @brief Allocate raw unmanaged memory (respects custom allocator hooks if registered).
 * @param size Number of bytes to allocate.
 * @return Pointer to allocated memory, or NULL on failure.
 */
void *ti_raw_malloc(size_t size);

/**
 * @brief Allocate and zero-initialize raw unmanaged memory for an array.
 * @param num Number of elements.
 * @param size Size of each element in bytes.
 * @return Pointer to allocated zeroed memory, or NULL on failure.
 */
void *ti_raw_calloc(size_t num, size_t size);

/**
 * @brief Reallocate an existing raw unmanaged memory block.
 * @param ptr Pointer to existing memory block.
 * @param size New size in bytes.
 * @return Pointer to reallocated memory, or NULL on failure.
 */
void *ti_raw_realloc(void *ptr, size_t size);

/**
 * @brief Free a raw unmanaged memory block.
 * @param ptr Pointer to memory to release.
 */
void ti_raw_free(void *ptr);

/**
 * @brief Duplicate a null-terminated string into raw unmanaged memory.
 * @param s String to duplicate.
 * @return Newly allocated copy of string, or NULL on failure.
 */
char *ti_raw_strdup(const char *s);

/* -------------------- Public Tracked Allocator API -------------------- */

/**
 * @brief Allocate tracked memory block and link to the specified tracking list.
 * @param list Pointer to head of allocation list (can be NULL if unlinked).
 * @param size Number of bytes to allocate.
 * @return Pointer to allocated payload memory.
 */
void *tracked_malloc(alloc_hdr_t **list, size_t size);

/**
 * @brief Allocate and zero-initialize tracked memory for an array.
 * @param list Pointer to head of allocation list (can be NULL if unlinked).
 * @param num Number of elements.
 * @param size Size of each element in bytes.
 * @return Pointer to zero-initialized allocated memory.
 */
void *tracked_calloc(alloc_hdr_t **list, size_t num, size_t size);

/**
 * @brief Resize an existing tracked memory allocation.
 * @param list Pointer to head of allocation list (can be NULL if unlinked).
 * @param ptr Pointer to existing allocation.
 * @param new_size New requested size in bytes.
 * @return Pointer to reallocated memory.
 */
void *tracked_realloc(alloc_hdr_t **list, void *ptr, size_t new_size);

/**
 * @brief Free a tracked memory allocation and remove it from the tracking list.
 * @param list Pointer to head of allocation list (can be NULL).
 * @param ptr Pointer to allocated memory to free.
 */
void tracked_free(alloc_hdr_t **list, void *ptr);

/**
 * @brief Duplicate a null-terminated string using tracked allocation.
 * @param list Pointer to head of allocation list (can be NULL if unlinked).
 * @param s Source string to duplicate.
 * @return Newly allocated copy of string.
 */
char *tracked_strdup(alloc_hdr_t **list, const char *s);

/**
 * @brief Free all tracked allocations belonging to the specified list in one batch.
 * @param list Pointer to head of allocation list to clear.
 */
void tracked_free_all(alloc_hdr_t **list);

#ifdef __cplusplus
}
#endif

#endif /* !TRACKED_MEMORY_H */
