#ifndef TRACKED_MEMORY_H
#define TRACKED_MEMORY_H

#include <stdlib.h>

typedef struct ALLOC_HDR_STRUCT {
    struct ALLOC_HDR_STRUCT *next;
    struct ALLOC_HDR_STRUCT *previous;
} alloc_hdr_t;

void *tracked_malloc (size_t size);
void *tracked_calloc (size_t num, size_t size);
void *tracked_realloc (void *ptr, size_t new_size);
void tracked_free (void *ptr);
char *tracked_strdup(const char *s);
void free_all();

#endif
