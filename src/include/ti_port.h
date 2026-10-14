#ifndef TI_PORT_H
#define TI_PORT_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @file ti_port.h
 * @brief Platform abstraction for the runtime's execution lock and signals.
 *
 * The interpreter core only sees neutral primitives: a mutex, a binary
 * signal and an opaque caller identity. Nothing here names the underlying
 * scheduling model, so the core stays platform-agnostic. All blocking
 * functions take a timeout in milliseconds.
 */

/** Timeout value meaning "block until the operation succeeds". */
#define TI_PORT_WAIT_FOREVER UINT32_MAX

/** Opaque identity of whoever is currently executing (never interpreted). */
typedef uintptr_t ti_caller_t;

/** Value that never equals a real caller identity. */
#define TI_CALLER_NONE ((ti_caller_t)0)

/** Opaque non-recursive mutex. Priority inheritance where the platform has it. */
typedef struct ti_mutex ti_mutex_t;

/** Opaque binary signal: multiple gives before a take collapse into one. */
typedef struct ti_signal ti_signal_t;

/**
 * @brief Return the identity of the current execution context.
 * @return Stable non-zero opaque id, equal across calls from the same context.
 */
ti_caller_t ti_current_caller(void);

/**
 * @brief Read a monotonic millisecond counter (wraps at 2^32; use unsigned differences).
 * @return Milliseconds since an arbitrary fixed point.
 */
uint32_t ti_millis(void);

/**
 * @brief Create a mutex (initially unlocked).
 * @return New mutex, or NULL on allocation failure.
 */
ti_mutex_t *ti_mutex_create(void);

/**
 * @brief Destroy a mutex. It must be unlocked and unused.
 * @param m Mutex to destroy; NULL is ignored.
 */
void ti_mutex_destroy(ti_mutex_t *m);

/**
 * @brief Lock a mutex, waiting up to timeout_ms.
 * @param m          Mutex.
 * @param timeout_ms Wait limit in ms; 0 = try only; TI_PORT_WAIT_FOREVER = no limit.
 * @return true if the lock was acquired, false on timeout or invalid argument.
 */
bool ti_mutex_lock(ti_mutex_t *m, uint32_t timeout_ms);

/**
 * @brief Unlock a mutex held by the calling context.
 * @param m Mutex.
 */
void ti_mutex_unlock(ti_mutex_t *m);

/**
 * @brief Create a binary signal (initially not set).
 * @return New signal, or NULL on allocation failure.
 */
ti_signal_t *ti_signal_create(void);

/**
 * @brief Destroy a signal. No context may be waiting on it.
 * @param s Signal to destroy; NULL is ignored.
 */
void ti_signal_destroy(ti_signal_t *s);

/**
 * @brief Set the signal, waking one waiter (or the next waiter to arrive).
 * Safe from interrupt context on platforms that have one (the port picks the right primitive).
 * @param s Signal.
 */
void ti_signal_give(ti_signal_t *s);

/**
 * @brief Wait for the signal and consume it.
 * @param s          Signal.
 * @param timeout_ms Wait limit in ms; 0 = poll; TI_PORT_WAIT_FOREVER = no limit.
 * @return true if the signal was consumed, false on timeout or invalid argument.
 */
bool ti_signal_take(ti_signal_t *s, uint32_t timeout_ms);

#endif /* TI_PORT_H */
