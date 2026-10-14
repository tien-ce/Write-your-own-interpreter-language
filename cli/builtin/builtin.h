#ifndef CLI_BUILTIN_H
#define CLI_BUILTIN_H

#include "TienInterpreter.h"

/* -------------------- Built-in Registration (one function per topic file) -------------------- */

/**
 * @brief Register print() and delay() (builtin_core.c).
 */
void builtin_core_register(void);

/**
 * @brief Register register_event() (builtin_event.c).
 */
void builtin_event_register(void);

/**
 * @brief Register register_callback() (builtin_callback.c).
 */
void builtin_callback_register(void);

/**
 * @brief Register the relay_* functions (builtin_relay.c).
 */
void builtin_relay_register(void);

/**
 * @brief Register the autonics_tk_* functions (builtin_autonics.c).
 */
void builtin_autonics_register(void);

/**
 * @brief Register nvs_read() (builtin_storage.c).
 */
void builtin_storage_register(void);

/**
 * @brief Register http_get() (builtin_http.c).
 */
void builtin_http_register(void);

/**
 * @brief Register get_json() (builtin_json.c).
 */
void builtin_json_register(void);

/**
 * @brief Register get_system_info() (builtin_system.c).
 */
void builtin_system_register(void);

/**
 * @brief Register the bytes examples modbus_read_request() and crc16() (builtin_bytes.c).
 */
void builtin_bytes_register(void);

/* -------------------- CLI Event Simulation -------------------- */

/**
 * @brief Post a simulated input event carrying text to the callback registered by a script.
 * @param text Text delivered to the callback as its string argument.
 * @return TI_OK, or the status returned by ti_post_event (TI_ERR_STALE_HANDLE when no runtime is listening).
 */
ti_status_t cli_simulate_event(const char *text);

/**
 * @brief Call the handler registered with register_callback() and wait for its answer (ti_call).
 * @param text Text delivered to the handler as its string argument.
 * @param answer Buffer receiving the handler's string result (left empty otherwise); may be NULL.
 * @param answer_size Size of the answer buffer.
 * @return TI_OK, or the ti_call status (TI_ERR_TIMEOUT, TI_ERR_STALE_HANDLE when no runtime is listening,
 *         or the error kind the handler raised).
 */
ti_status_t cli_simulate_call(const char *text, char *answer, size_t answer_size);

#endif /* !CLI_BUILTIN_H */
