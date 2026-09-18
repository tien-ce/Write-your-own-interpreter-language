#include "include/tracked_memory.h"
#include "TienInterpreter.h"
#include <string.h>
#include <stdio.h>

#ifndef CHECK_MEM_LEAK

/* -------------------- Static Variables -------------------- */

static _Thread_local alloc_hdr_t *s_alloc_list = NULL;

/* -------------------- Static Function Prototypes -------------------- */

static void list_insert_head(alloc_hdr_t *new_hdr);
static void list_remove(alloc_hdr_t *hdr);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Insert allocation header to head of doubly linked tracking list.
 * @param new_hdr Pointer to newly allocated header node.
 */
static void list_insert_head(alloc_hdr_t *new_hdr)
{
    new_hdr->previous = NULL;
    new_hdr->next = s_alloc_list;
    if (s_alloc_list != NULL) {
        s_alloc_list->previous = new_hdr;
    }
    s_alloc_list = new_hdr;
}

/**
 * @brief Remove allocation header from doubly linked tracking list.
 * @param hdr Pointer to header node being removed.
 */
static void list_remove(alloc_hdr_t *hdr)
{
    if (hdr->previous != NULL) {
        hdr->previous->next = hdr->next;
    } else {
        s_alloc_list = hdr->next;
    }

    if (hdr->next) {
        hdr->next->previous = hdr->previous;
    }
}

#endif /* !CHECK_MEM_LEAK */

/* -------------------- Public Functions -------------------- */

/* Allocate tracked memory block of the given size */
void *tracked_malloc(size_t size)
{
    alloc_hdr_t *hdr = (alloc_hdr_t *)malloc(sizeof(struct ALLOC_HDR_STRUCT) + size);
    if (hdr == NULL) {
        return NULL;
    }
#ifndef CHECK_MEM_LEAK
    list_insert_head(hdr);
#endif
    return (void *)(hdr + 1);
}

/* Allocate and zero-initialize tracked memory for an array */
void *tracked_calloc(size_t num, size_t size)
{
    size_t total_payload_size = num * size;
    size_t total_alloc_size = sizeof(struct ALLOC_HDR_STRUCT) + total_payload_size;
    alloc_hdr_t *hdr = (alloc_hdr_t *)malloc(total_alloc_size);
    if (hdr == NULL) {
        return NULL;
    }
#ifndef CHECK_MEM_LEAK
    list_insert_head(hdr);
#endif
    void *payload = (void *)(hdr + 1);
    memset(payload, 0, total_payload_size);
    return payload;
}

/* Resize an existing tracked memory allocation */
void *tracked_realloc(void *ptr, size_t new_size)
{
    if (ptr == NULL) {
        return tracked_malloc(new_size);
    }
    alloc_hdr_t *hdr = (alloc_hdr_t *)ptr - 1;
    alloc_hdr_t *new_ptr = (alloc_hdr_t *)realloc(hdr, sizeof(struct ALLOC_HDR_STRUCT) + new_size);
    if (new_ptr != NULL) {
#ifndef CHECK_MEM_LEAK
        if (new_ptr->next != NULL) {
            new_ptr->next->previous = new_ptr;
        }
        if (new_ptr->previous != NULL) {
            new_ptr->previous->next = new_ptr;
        } else {
            s_alloc_list = new_ptr;
        }
#endif
    } else {
        return NULL;
    }
    return (void *)(new_ptr + 1);
}

/* Duplicate a null-terminated string using tracked allocation */
char *tracked_strdup(const char *s)
{
    size_t total_payload_size = (strlen(s) + 1) * sizeof(char);
    alloc_hdr_t *hdr = (alloc_hdr_t *)malloc(sizeof(struct ALLOC_HDR_STRUCT) + total_payload_size);
    if (hdr == NULL) {
        return NULL;
    }
#ifndef CHECK_MEM_LEAK
    list_insert_head(hdr);
#endif
    char *payload = (char *)(hdr + 1);
    memcpy(payload, s, total_payload_size);
    return payload;
}

/* Free a tracked memory allocation and remove it from the tracking list */
void tracked_free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }
    alloc_hdr_t *hdr = (alloc_hdr_t *)ptr - 1;
#ifndef CHECK_MEM_LEAK
    list_remove(hdr);
#endif
    free(hdr);
}

/* Free all tracked allocations in one batch */
void free_all(void)
{
#ifndef CHECK_MEM_LEAK
    alloc_hdr_t *current = s_alloc_list;
    while (current != NULL) {
        alloc_hdr_t *next = current->next;
        free(current);
        current = next;
    }
    s_alloc_list = NULL;
#endif
}
