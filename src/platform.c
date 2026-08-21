#include "include/platform.h"
#include "include/tracked_memory.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(ARDUINO)

#include <Arduino.h>

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
  while (1)
  {
    delay(1000);
  }
}

#else

#define TI_PLATFORM_DESKTOP

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
