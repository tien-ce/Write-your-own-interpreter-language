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
    /* Store external allocator function pointers provided by the host embedder */
    s_custom_malloc  = malloc_fn;
    s_custom_free    = free_fn;
    s_custom_realloc = realloc_fn;
}

/* -------------------- Raw Allocator API Implementation -------------------- */

void *ti_raw_malloc(size_t size)
{
    /* Prioritize custom allocator hook if registered by the embedder, otherwise fallback to libc malloc */
    return s_custom_malloc ? s_custom_malloc(size) : malloc(size);
}

void *ti_raw_calloc(size_t num, size_t size)
{
    /* Calculate contiguous memory requirement for array elements */
    size_t total = num * size;
    void *ptr = ti_raw_malloc(total);
    if (ptr) {
        /* Zero-initialize the entire memory buffer upon successful allocation */
        memset(ptr, 0, total);
    }
    return ptr;
}

void *ti_raw_realloc(void *ptr, size_t size)
{
    /* Delegate reallocation to custom hook if registered, otherwise fallback to libc realloc */
    return s_custom_realloc ? s_custom_realloc(ptr, size) : realloc(ptr, size);
}

void ti_raw_free(void *ptr)
{
    /* Guard against releasing null pointers */
    if (ptr == NULL) {
        return;
    }
    /* Delegate release to custom hook if registered, otherwise fallback to libc free */
    if (s_custom_free) {
        s_custom_free(ptr);
    } else {
        free(ptr);
    }
}

char *ti_raw_strdup(const char *s)
{
    /* Guard against null source string */
    if (!s) {
        return NULL;
    }
    /* Include null terminator byte when computing allocation size */
    size_t len = strlen(s) + 1;
    char *copy = (char *)ti_raw_malloc(len);
    if (copy) {
        /* Copy string bytes including null terminator into allocated memory */
        memcpy(copy, s, len);
    }
    return copy;
}

static inline void *raw_malloc(size_t size)
{
    return ti_raw_malloc(size);
}

static inline void raw_free(void *ptr)
{
    ti_raw_free(ptr);
}

static inline void *raw_realloc(void *ptr, size_t size)
{
    return ti_raw_realloc(ptr, size);
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
    /* Guard against invalid list pointer or null header */
    if (!list || !new_hdr) {
        return;
    }
    /* Set new header as the new list head */
    new_hdr->previous = NULL;
    new_hdr->next = *list;
    /* Update backward link of the previous head if list was non-empty */
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
    /* Guard against null header node */
    if (!hdr) {
        return;
    }
    /* Unlink node from previous neighbor or update list head pointer */
    if (hdr->previous != NULL) {
        hdr->previous->next = hdr->next;
    } else if (list && *list == hdr) {
        *list = hdr->next;
    }

    /* Unlink node from next neighbor */
    if (hdr->next != NULL) {
        hdr->next->previous = hdr->previous;
    }
    /* Clear detached node pointers to prevent dangling references */
    hdr->previous = NULL;
    hdr->next = NULL;
}

#endif /* !CHECK_MEM_LEAK */

/* -------------------- Public Functions -------------------- */

/* Allocate tracked memory block of the given size */
void *tracked_malloc(alloc_hdr_t **list, size_t size)
{
    /* Allocate contiguous memory block large enough to hold header followed by payload */
    alloc_hdr_t *hdr = (alloc_hdr_t *)raw_malloc(sizeof(struct ALLOC_HDR_STRUCT) + size);
    if (hdr == NULL) {
        return NULL;
    }
    /* Initialize header links before registration */
    hdr->previous = NULL;
    hdr->next = NULL;
#ifndef CHECK_MEM_LEAK
    /* Insert header into caller tracking list if tracking list provided */
    if (list) {
        list_insert_head(list, hdr);
    }
#endif
    /* Advance pointer past tracking header to return usable payload address */
    return (void *)(hdr + 1);
}

/* Allocate and zero-initialize tracked memory for an array */
void *tracked_calloc(alloc_hdr_t **list, size_t num, size_t size)
{
    /* Calculate payload size and total block size including tracking header */
    size_t total_payload_size = num * size;
    size_t total_alloc_size = sizeof(struct ALLOC_HDR_STRUCT) + total_payload_size;
    alloc_hdr_t *hdr = (alloc_hdr_t *)raw_malloc(total_alloc_size);
    if (hdr == NULL) {
        return NULL;
    }
    /* Initialize header links */
    hdr->previous = NULL;
    hdr->next = NULL;
#ifndef CHECK_MEM_LEAK
    /* Register block into tracking list */
    if (list) {
        list_insert_head(list, hdr);
    }
#endif
    /* Offset pointer to payload and zero out entire allocated payload area */
    void *payload = (void *)(hdr + 1);
    memset(payload, 0, total_payload_size);
    return payload;
}

/* Resize an existing tracked memory allocation */
void *tracked_realloc(alloc_hdr_t **list, void *ptr, size_t new_size)
{
    /* Handle initial allocation case when existing pointer is null */
    if (ptr == NULL) {
        return tracked_malloc(list, new_size);
    }
    /* Rewind pointer to access the intrusive allocation header */
    alloc_hdr_t *hdr = (alloc_hdr_t *)ptr - 1;
    /* Reallocate block with expanded or shrunk size including header overhead */
    alloc_hdr_t *new_ptr = (alloc_hdr_t *)raw_realloc(hdr, sizeof(struct ALLOC_HDR_STRUCT) + new_size);
    if (new_ptr != NULL) {
#ifndef CHECK_MEM_LEAK
        /* Repair intrusive list links if block address shifted in memory */
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
    /* Return address offset past header */
    return (void *)(new_ptr + 1);
}

/* Duplicate a null-terminated string using tracked allocation */
char *tracked_strdup(alloc_hdr_t **list, const char *s)
{
    /* Guard against null source string */
    if (!s) {
        return NULL;
    }
    /* Calculate string size including null terminator plus header overhead */
    size_t total_payload_size = (strlen(s) + 1) * sizeof(char);
    alloc_hdr_t *hdr = (alloc_hdr_t *)raw_malloc(sizeof(struct ALLOC_HDR_STRUCT) + total_payload_size);
    if (hdr == NULL) {
        return NULL;
    }
    /* Initialize header links */
    hdr->previous = NULL;
    hdr->next = NULL;
#ifndef CHECK_MEM_LEAK
    /* Register into tracking list */
    if (list) {
        list_insert_head(list, hdr);
    }
#endif
    /* Copy string characters and terminator into payload */
    char *payload = (char *)(hdr + 1);
    memcpy(payload, s, total_payload_size);
    return payload;
}

/* Free a tracked memory allocation and remove it from the tracking list */
void tracked_free(alloc_hdr_t **list, void *ptr)
{
    /* Guard against freeing null pointer */
    if (ptr == NULL) {
        return;
    }
    /* Rewind pointer to access intrusive allocation header */
    alloc_hdr_t *hdr = (alloc_hdr_t *)ptr - 1;
#ifndef CHECK_MEM_LEAK
    /* Unlink header from tracking list before releasing */
    if (list) {
        list_remove(list, hdr);
    }
#endif
    /* Release full memory block back to raw allocator */
    raw_free(hdr);
}

/* Free all tracked allocations belonging to the specified list in one batch */
void tracked_free_all(alloc_hdr_t **list)
{
#ifndef CHECK_MEM_LEAK
    /* Guard against uninitialized or empty list */
    if (!list || !*list) {
        return;
    }
    /* Traverse intrusive doubly-linked list freeing each block */
    alloc_hdr_t *current = *list;
    while (current != NULL) {
        alloc_hdr_t *next = current->next;
        raw_free(current);
        current = next;
    }
    /* Reset list head pointer to indicate empty list */
    *list = NULL;
#endif
}
