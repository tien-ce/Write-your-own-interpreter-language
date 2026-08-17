#include "include/platform.h"
#include "include/tracked_memory.h"
#include <stdarg.h>

#if defined(ARDUINO)

/* TODO: ESP32/Arduino implementation (Serial.printf + halt/reset) */

#else

#define TI_PLATFORM_DESKTOP

#include <stdio.h>
#include <stdlib.h>

void ti_log(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}

void ti_fatal(void)
{
  free_all();
  exit(1);
}

#endif
