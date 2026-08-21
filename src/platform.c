#include "include/platform.h"
#include "include/tracked_memory.h"
#include <stdio.h>
ti_fatal_callback_t ti_fatal_cb = NULL;
ti_log_callback_t ti_log_cb = NULL;

void ti_register_fatal(ti_fatal_callback_t func)
{
    ti_fatal_cb = func;
}

void ti_register_log(ti_log_callback_t func)
{
    ti_log_cb = func;
}

void ti_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if(ti_log_cb != NULL)
        ti_log_cb(fmt, args);
    va_end(args);
}

void ti_fatal(void)
{
  free_all();
  if(ti_fatal_cb != NULL)
  {
    ti_fatal_cb();
  }
}
