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
| `next` | `alloc_hdr_t *` | Pointer to the next tracked block in the global doubly linked list `s_alloc_list`. |
| `previous` | `alloc_hdr_t *` | Pointer to the previous tracked block. Enables $O(1)$ removal from the list without traversing. |

---

## 2. Allocation & Deallocation Mechanics

### 2.1. `tracked_malloc` / `tracked_calloc`
1. Requests `sizeof(alloc_hdr_t) + payload_size` from system `malloc()`.
2. Inserts `hdr` at the head of `s_alloc_list` ($O(1)$).
3. Returns `(void *)(hdr + 1)`, hiding the header completely from caller code.

### 2.2. `tracked_free(void *ptr)` (Header Recovery & $O(1)$ Unlinking)
1. Recovers the header by pointer arithmetic:
   ```c
   alloc_hdr_t *hdr = (alloc_hdr_t *)ptr - 1;
   ```
2. Unlinks `hdr` from `s_alloc_list` via `list_remove(hdr)`.
3. Calls system `free(hdr)`.

### 2.3. `tracked_realloc(void *ptr, size_t new_size)`
1. Shifts pointer back to find `hdr = (alloc_hdr_t *)ptr - 1`.
2. Calls system `realloc(hdr, sizeof(alloc_hdr_t) + new_size)`.
3. If the block was moved to a new address by `realloc`, updates the neighbor `previous` and `next` pointers to point to the new header address.

---

## 3. Global Bulk Cleanup (`free_all`)

When a fatal script error occurs (via `ti_fatal()`) or when the interpreter shuts down, `free_all()` is invoked:
- Traverses `s_alloc_list` from head to tail.
- Frees every remaining allocation in memory in a single loop.
- Guarantees zero residual heap leaks even on abnormal script crashes.
