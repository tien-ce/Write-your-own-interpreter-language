#include "include/tracked_memory.h"
#include "TienInterpreter.h"
#include <string.h>
#include <stdio.h>

/* -------------------- Custom Allocator Hooks -------------------- */

static ti_malloc_fn_t  s_custom_malloc  = NULL;
static ti_free_fn_t    s_custom_free    = NULL;
static ti_realloc_fn_t s_custom_realloc = NULL;

void ti_register_allocator(ti_malloc_fn_t malloc_fn, ti_free_fn_t free_fn, ti_realloc_fn_t realloc_fn)
{
    s_custom_malloc  = malloc_fn;
    s_custom_free    = free_fn;
    s_custom_realloc = realloc_fn;
}

static inline void *raw_malloc(size_t size)
{
    return s_custom_malloc ? s_custom_malloc(size) : malloc(size);
}

static inline void raw_free(void *ptr)
{
    if (s_custom_free) {
        s_custom_free(ptr);
    } else {
        free(ptr);
    }
}

static inline void *raw_realloc(void *ptr, size_t size)
{
    return s_custom_realloc ? s_custom_realloc(ptr, size) : realloc(ptr, size);
}

#ifndef CHECK_MEM_LEAK

/* -------------------- Intrusive List Operations -------------------- */

/**
 * @brief Insert allocation header to head of doubly linked tracking list.
 * @param list Pointer to head of list pointer.
 * @param new_hdr Pointer to newly allocated header node.
 */
static void list_insert_head(alloc_hdr_t **list, alloc_hdr_t *new_hdr)
{
    if (!list || !new_hdr) {
        return;
    }
    new_hdr->previous = NULL;
    new_hdr->next = *list;
    if (*list != NULL) {
        (*list)->previous = new_hdr;
    }
    *list = new_hdr;
}

/**
 * @brief Remove allocation header from doubly linked tracking list.
 * @param list Pointer to head of list pointer.
 * @param hdr Pointer to header node being removed.
 */
static void list_remove(alloc_hdr_t **list, alloc_hdr_t *hdr)
{
    if (!hdr) {
        return;
    }
    if (hdr->previous != NULL) {
        hdr->previous->next = hdr->next;
    } else if (list && *list == hdr) {
        *list = hdr->next;
    }

    if (hdr->next != NULL) {
        hdr->next->previous = hdr->previous;
    }
    hdr->previous = NULL;
    hdr->next = NULL;
}

#endif /* !CHECK_MEM_LEAK */

/* -------------------- Public Functions -------------------- */

/* Allocate tracked memory block of the given size */
void *tracked_malloc(alloc_hdr_t **list, size_t size)
{
    alloc_hdr_t *hdr = (alloc_hdr_t *)raw_malloc(sizeof(struct ALLOC_HDR_STRUCT) + size);
    if (hdr == NULL) {
        return NULL;
    }
    hdr->previous = NULL;
    hdr->next = NULL;
#ifndef CHECK_MEM_LEAK
    if (list) {
        list_insert_head(list, hdr);
    }
#endif
    return (void *)(hdr + 1);
}

/* Allocate and zero-initialize tracked memory for an array */
void *tracked_calloc(alloc_hdr_t **list, size_t num, size_t size)
{
    size_t total_payload_size = num * size;
    size_t total_alloc_size = sizeof(struct ALLOC_HDR_STRUCT) + total_payload_size;
    alloc_hdr_t *hdr = (alloc_hdr_t *)raw_malloc(total_alloc_size);
    if (hdr == NULL) {
        return NULL;
    }
    hdr->previous = NULL;
    hdr->next = NULL;
#ifndef CHECK_MEM_LEAK
    if (list) {
        list_insert_head(list, hdr);
    }
#endif
    void *payload = (void *)(hdr + 1);
    memset(payload, 0, total_payload_size);
    return payload;
}

/* Resize an existing tracked memory allocation */
void *tracked_realloc(alloc_hdr_t **list, void *ptr, size_t new_size)
{
    if (ptr == NULL) {
        return tracked_malloc(list, new_size);
    }
    alloc_hdr_t *hdr = (alloc_hdr_t *)ptr - 1;
    alloc_hdr_t *new_ptr = (alloc_hdr_t *)raw_realloc(hdr, sizeof(struct ALLOC_HDR_STRUCT) + new_size);
    if (new_ptr != NULL) {
#ifndef CHECK_MEM_LEAK
        if (new_ptr->next != NULL) {
            new_ptr->next->previous = new_ptr;
        }
        if (new_ptr->previous != NULL) {
            new_ptr->previous->next = new_ptr;
        } else if (list) {
            *list = new_ptr;
        }
#endif
    } else {
        return NULL;
    }
    return (void *)(new_ptr + 1);
}

/* Duplicate a null-terminated string using tracked allocation */
char *tracked_strdup(alloc_hdr_t **list, const char *s)
{
    if (!s) {
        return NULL;
    }
    size_t total_payload_size = (strlen(s) + 1) * sizeof(char);
    alloc_hdr_t *hdr = (alloc_hdr_t *)raw_malloc(sizeof(struct ALLOC_HDR_STRUCT) + total_payload_size);
    if (hdr == NULL) {
        return NULL;
    }
    hdr->previous = NULL;
    hdr->next = NULL;
#ifndef CHECK_MEM_LEAK
    if (list) {
        list_insert_head(list, hdr);
    }
#endif
    char *payload = (char *)(hdr + 1);
    memcpy(payload, s, total_payload_size);
    return payload;
}

/* Free a tracked memory allocation and remove it from the tracking list */
void tracked_free(alloc_hdr_t **list, void *ptr)
{
    if (ptr == NULL) {
        return;
    }
    alloc_hdr_t *hdr = (alloc_hdr_t *)ptr - 1;
#ifndef CHECK_MEM_LEAK
    if (list) {
        list_remove(list, hdr);
    }
#endif
    raw_free(hdr);
}

/* Free all tracked allocations belonging to the specified list in one batch */
void tracked_free_all(alloc_hdr_t **list)
{
#ifndef CHECK_MEM_LEAK
    if (!list || !*list) {
        return;
    }
    alloc_hdr_t *current = *list;
    while (current != NULL) {
        alloc_hdr_t *next = current->next;
        raw_free(current);
        current = next;
    }
    *list = NULL;
#endif
}
