# Maintainer Guide: Tracked Memory Subsystem

> **Audience:** Developers modifying low-level memory allocation, leak tracking, or cleanup in `src/tracked_memory.c` and `src/include/tracked_memory.h`.

---

## 1. Struct: `alloc_hdr_t` (`struct ALLOC_HDR_STRUCT`)

Defined in `src/include/tracked_memory.h`:

```c
typedef struct ALLOC_HDR_STRUCT {
    struct ALLOC_HDR_STRUCT *next;
    struct ALLOC_HDR_STRUCT *previous;
} alloc_hdr_t;
```

### Memory Block Layout
The allocator uses an **intrusive header** prepended directly in front of every payload allocation:

```
┌───────────────────────────────┬──────────────────────────────────────────┐
│ alloc_hdr_t (next, previous)  │ User Payload Memory (returned to caller) │
└───────────────────────────────┴──────────────────────────────────────────┘
▲                               ▲
│                               │
Allocation pointer (from malloc) `(hdr + 1)` (pointer returned to caller)
```

| Field | Type | Purpose & Contribution to Logic |
| :--- | :--- | :--- |
| `next` | `alloc_hdr_t *` | Pointer to the next tracked block in the intrusive doubly linked list `*list`. |
| `previous` | `alloc_hdr_t *` | Pointer to the previous tracked block. Enables $O(1)$ removal from the list without traversing. |

---

## 2. Allocation & Deallocation Mechanics

All tracking functions accept an optional `alloc_hdr_t **list` parameter, allowing per-instance memory tracking (such as `&rt->alloc_list` or `&prog->alloc_list`), or fallback to the global tracking list `s_alloc_list` if `list == NULL`:

### 2.1. `tracked_malloc` / `tracked_calloc`
- Signatures:
  ```c
  void *tracked_malloc(alloc_hdr_t **list, size_t size);
  void *tracked_calloc(alloc_hdr_t **list, size_t num, size_t size);
  ```
1. Requests `sizeof(alloc_hdr_t) + payload_size` from system `malloc()`.
2. Inserts `hdr` at the head of `*list` ($O(1)$), setting `hdr->previous = NULL` and `hdr->next = *list`.
3. Returns `(void *)(hdr + 1)`, hiding the header completely from caller code.

### 2.2. `tracked_free(alloc_hdr_t **list, void *ptr)` (Header Recovery & $O(1)$ Unlinking)
1. Recovers the header by pointer arithmetic:
   ```c
   alloc_hdr_t *hdr = (alloc_hdr_t *)ptr - 1;
   ```
2. Unlinks `hdr` from `*list` via `list_remove(list, hdr)`:
   - Validates that `*list == hdr` before unlinking the head to prevent cross-list corruption.
   - Clears `hdr->previous = NULL` and `hdr->next = NULL` upon unlinking.
3. Calls system `free(hdr)`.

### 2.3. `tracked_realloc(alloc_hdr_t **list, void *ptr, size_t new_size)`
1. Shifts pointer back to find `hdr = (alloc_hdr_t *)ptr - 1`.
2. Calls system `realloc(hdr, sizeof(alloc_hdr_t) + new_size)`.
3. If the block was moved to a new address by `realloc`, updates the neighbor `previous` and `next` pointers to point to the new header address, maintaining doubly linked list integrity.

---

## 3. Bulk Cleanup (`free_all`)

When an isolated runtime or program context terminates, or during fatal script termination:
- `free_all(alloc_hdr_t **list)`: Traverses `*list` from head to tail, releasing every remaining allocation in memory in a single sweep and setting `*list = NULL`.
- Guarantees zero residual heap leaks even on abnormal script interruptions.
