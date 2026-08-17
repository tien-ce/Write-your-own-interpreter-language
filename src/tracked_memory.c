#include "include/tracked_memory.h"
#include "include/platform.h"
#include <string.h>
#include <stdio.h>

/* Forward declarations of static functions */
static void list_insert_head(alloc_hdr_t *new_hdr);
static void list_remove(alloc_hdr_t *hdr);

static alloc_hdr_t *g_alloc_list = NULL;
static void list_insert_head(alloc_hdr_t *new_hdr)
{
    new_hdr->previous = NULL;
    new_hdr->next = g_alloc_list;
    if(g_alloc_list != NULL)
    {
        // Already constain node 
        g_alloc_list->previous = new_hdr;
    }
    g_alloc_list= new_hdr;
}

static void list_remove(alloc_hdr_t *hdr)
{
    if(hdr->previous != NULL)
    {
        hdr->previous->next = hdr->next;
    }
    else 
    {
        // First node
        g_alloc_list = hdr->next;
    }
    
    if(hdr->next)
    {
        hdr->next->previous = hdr->previous;
    }
}

void *tracked_malloc(size_t size)
{
    alloc_hdr_t *hdr = (alloc_hdr_t *)malloc(sizeof(struct ALLOC_HDR_STRUCT) + size);
    if(hdr == NULL)
        return NULL;
    // Inser to list 
    list_insert_head(hdr);
    return (void*)(hdr+1); // Return payload
}

void *tracked_calloc(size_t num, size_t size)
{
    size_t total_payload_size = num * size;
    size_t total_alloc_size = sizeof(struct ALLOC_HDR_STRUCT) + total_payload_size;
    alloc_hdr_t *hdr = (alloc_hdr_t *)malloc(total_alloc_size); 
    if(hdr == NULL)
        return NULL;
    // Inser to list 
    list_insert_head(hdr);
    // Get address of payload 
    void *payload = (void*)(hdr + 1); // payload += sizeof(hdr)
    // Return payload to all 0
    memset(payload,0,total_payload_size);
    return payload; // Return payload
}

/* Hold the old pointer before call this function */
void *tracked_realloc(void *ptr, size_t new_size)
{
    if(ptr == NULL)
    {
        // Realloc in NULL pointer
        ti_log("Realloc the NULL pointer, you are crazy\n");
        return tracked_malloc(new_size);
    }
    alloc_hdr_t *hdr = (alloc_hdr_t*)ptr - 1;
    alloc_hdr_t *new_ptr = (alloc_hdr_t*)realloc(hdr, sizeof(struct ALLOC_HDR_STRUCT) + new_size);
    if(new_ptr != NULL)
    {
        // Reassing link
        if (new_ptr->next != NULL) // Not the end
            new_ptr->next->previous = new_ptr;
        if (new_ptr->previous != NULL) // Not the head
        {
            new_ptr->previous->next = new_ptr;
        }    
        else
        {
            // First node
            g_alloc_list = new_ptr;
        }
    }
    else
    {
        return NULL;
    }
    void *payload = (void*)(new_ptr + 1);
    return payload;
}

char *tracked_strdup(const char *s)
{
    size_t total_payload_size = (strlen(s) + 1) * sizeof(char);
    alloc_hdr_t *hdr = (alloc_hdr_t*) malloc(sizeof(struct ALLOC_HDR_STRUCT) + total_payload_size);
    if(hdr == NULL)
        return NULL;
    list_insert_head(hdr);
    char *payload = (char*)(hdr + 1);
    /* Dont't ask me how about with the string with none '\0', that's your falure*/
    memcpy(payload,s,total_payload_size);
    return payload;
}

void tracked_free(void *ptr)
{
    if(ptr == NULL)
    {
        return;
    }
    alloc_hdr_t *hdr = (alloc_hdr_t*)(ptr) - 1; // ptr -= sizeof(hdr_t)
    list_remove(hdr);
    free(hdr);
}

void free_all()
{
    alloc_hdr_t *current = g_alloc_list;
    while(current != NULL)
    {
        alloc_hdr_t *next = current->next;
        free(current);
        current = next;
    }
    g_alloc_list = NULL;
}
