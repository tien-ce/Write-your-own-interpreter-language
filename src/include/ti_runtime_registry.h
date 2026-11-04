#ifndef TI_RUNTIME_REGISTRY_H
#define TI_RUNTIME_REGISTRY_H

#include "ti_runtime.h"
#include "ti_type.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file ti_runtime_registry.h
 * @brief Handle table: the only bridge between external callers (host threads) and live runtimes.
 *
 * Every short external interaction runs inside one hold of the registry mutex:
 * lock (resolve the handle), use the runtime, unlock. A runtime cannot be detached,
 * and therefore not freed, while the mutex is held. The mutex never guards script
 * execution itself; that is the job of the per-runtime execution lock.
 */

/**
 * @brief Resolve a handle and keep the registry mutex held on success.
 * @param h Runtime handle.
 * @return Live runtime with the mutex held (release it with ti_registry_unlock),
 *         or NULL if the handle is stale or invalid (the mutex is already released).
 */
ti_runtime_t *ti_registry_lock(ti_handle_t h);

/**
 * @brief Release the registry mutex taken by a successful ti_registry_lock.
 */
void ti_registry_unlock(void);

/**
 * @brief Register a runtime in a free slot and assign rt->handle.
 * @param rt Runtime to register (not registered yet).
 * @return true on success, false if every slot is occupied.
 */
bool ti_registry_attach(ti_runtime_t *rt);

/**
 * @brief Unregister the runtime referenced by a handle in one atomic step.
 * After this call no external caller can reach the runtime anymore; its handle is reset
 * to TI_INVALID_HANDLE.
 * @param h Runtime handle.
 * @return The detached runtime, or NULL if the handle is stale or invalid (e.g. a second destroy).
 */
ti_runtime_t *ti_registry_detach(ti_handle_t h);

#ifdef __cplusplus
}
#endif

#endif /* TI_RUNTIME_REGISTRY_H */
