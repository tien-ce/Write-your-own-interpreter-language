#include "include/ti_runtime_registry.h"
#include "include/ti_port.h"
#include "TienInterpreter.h"
#include <stdint.h>

/* -------------------- Handle Encoding -------------------- */

/* Handle layout: [generation : 24 bits][slot index : 8 bits]; generation 0 is never issued */
#define HANDLE_SLOT_BITS 8u
#define HANDLE_SLOT_MASK 0xFFu
#define HANDLE_GEN_MASK  0x00FFFFFFu

#if TI_MAX_RUNTIMES < 1 || TI_MAX_RUNTIMES > 256
#error "TI_MAX_RUNTIMES must be in range [1, 256]"
#endif

/* -------------------- Static Types -------------------- */

/**
 * @brief One entry of the handle table.
 */
typedef struct {
    ti_runtime_t *rt;  /* Live runtime occupying the slot, NULL when free */
    uint32_t      gen; /* Generation of the current/last occupant (0 = never used) */
} slot_t;

/* -------------------- Static Variables -------------------- */

/* Handle table (guarded by s_registry_mutex) */
static slot_t s_slots[TI_MAX_RUNTIMES];

/* Registry mutex from the port layer: held for one whole external interaction (created once, never freed) */
static ti_mutex_t *s_registry_mutex;

/* -------------------- Static Functions -------------------- */

/**
 * @brief Return the registry mutex, creating it on first use without a race.
 * @return The mutex, or NULL if it could not be created (out of memory).
 */
static ti_mutex_t *registry_mutex(void)
{
    ti_mutex_t *m = __atomic_load_n(&s_registry_mutex, __ATOMIC_ACQUIRE);
    if (m != NULL) {
        return m;
    }

    /* Two first callers may both create one: the loser of the exchange destroys its own */
    ti_mutex_t *created = ti_mutex_create();
    if (created == NULL) {
        return NULL;
    }
    ti_mutex_t *expected = NULL;
    if (!__atomic_compare_exchange_n(&s_registry_mutex, &expected, created, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        ti_mutex_destroy(created);
        return expected;
    }
    return created;
}

/**
 * @brief Resolve a handle to its slot runtime; the caller must hold the registry mutex.
 * @param h Runtime handle.
 * @return Live runtime, or NULL if the handle is stale or invalid.
 */
static ti_runtime_t *slot_lookup_locked(ti_handle_t h)
{
    uint32_t index = h & HANDLE_SLOT_MASK;
    uint32_t gen = h >> HANDLE_SLOT_BITS;

    /* Generation 0 is never issued, so TI_INVALID_HANDLE always fails here */
    if (gen == 0 || index >= TI_MAX_RUNTIMES) {
        return NULL;
    }

    /* A mismatching generation means the runtime was destroyed (and maybe the slot reused) */
    if (s_slots[index].rt == NULL || s_slots[index].gen != gen) {
        return NULL;
    }
    return s_slots[index].rt;
}

/* -------------------- Public Functions -------------------- */

/* Resolve a handle and keep the registry mutex held on success */
ti_runtime_t *ti_registry_lock(ti_handle_t h)
{
    /* No mutex yet means no runtime was ever attached: every handle is stale */
    ti_mutex_t *m = __atomic_load_n(&s_registry_mutex, __ATOMIC_ACQUIRE);
    if (m == NULL) {
        return NULL;
    }

    ti_mutex_lock(m, TI_PORT_WAIT_FOREVER);

    /* Stale handle: release now so the caller only unlocks after a successful lookup */
    ti_runtime_t *rt = slot_lookup_locked(h);
    if (rt == NULL) {
        ti_mutex_unlock(m);
    }
    return rt;
}

/* Release the registry mutex taken by a successful ti_registry_lock */
void ti_registry_unlock(void)
{
    ti_mutex_unlock(__atomic_load_n(&s_registry_mutex, __ATOMIC_ACQUIRE));
}

/* Register a runtime in a free slot and assign rt->handle */
bool ti_registry_attach(ti_runtime_t *rt)
{
    if (rt == NULL) {
        return false;
    }

    /* The first attach creates the mutex */
    ti_mutex_t *m = registry_mutex();
    if (m == NULL) {
        return false;
    }

    bool attached = false;
    ti_mutex_lock(m, TI_PORT_WAIT_FOREVER);
    for (uint32_t i = 0; i < TI_MAX_RUNTIMES; i++) {
        if (s_slots[i].rt != NULL) {
            continue;
        }

        /* Advance the generation so handles of previous occupants stay stale (skip 0 on wrap) */
        uint32_t gen = (s_slots[i].gen + 1u) & HANDLE_GEN_MASK;
        if (gen == 0) {
            gen = 1;
        }

        s_slots[i].gen = gen;
        s_slots[i].rt = rt;
        rt->handle = (gen << HANDLE_SLOT_BITS) | i;
        attached = true;
        break;
    }
    ti_mutex_unlock(m);

    return attached;
}

/* Unregister the runtime referenced by a handle in one atomic step */
ti_runtime_t *ti_registry_detach(ti_handle_t h)
{
    /* Lookup and removal under one hold: a second detach of the same handle finds nothing */
    ti_runtime_t *rt = ti_registry_lock(h);
    if (rt == NULL) {
        return NULL;
    }

    s_slots[h & HANDLE_SLOT_MASK].rt = NULL;

    /* Atomic: ti_call and the script thread read it outside the registry as the "detached" flag */
    __atomic_store_n(&rt->handle, TI_INVALID_HANDLE, __ATOMIC_RELEASE);

    ti_registry_unlock();
    return rt;
}
