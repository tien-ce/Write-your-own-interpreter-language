#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

/* printf-style logging. Desktop: stdout. Arduino/ESP32: Serial (TODO). */
void ti_log(const char *fmt, ...);

/* Unrecoverable interpreter error. Desktop: free_all() + exit(1).
 * Arduino/ESP32: free_all() + halt (TODO). Never returns. */
void ti_fatal(void);

#ifdef __cplusplus
}
#endif

#endif
