#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdarg.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ti_log_callback_t)(const char *fmt, va_list args);
typedef void (*ti_fatal_callback_t)(void);
/* printf-style logging. Desktop: stdout. Arduino/ESP32: Serial (TODO). */
void ti_log(const char *fmt, ...);
void ti_fatal(void);
/* Unrecoverable interpreter error. Desktop: free_all() + exit(1).
 * Arduino/ESP32: free_all() + halt (TODO). Never returns. */
void ti_register_log(ti_log_callback_t func);
void ti_register_fatal(ti_fatal_callback_t func);

#ifdef __cplusplus
}
#endif

#endif
