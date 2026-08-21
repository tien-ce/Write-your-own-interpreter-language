#include "include/visitor.h"
#include "TienInterpreter.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* -------------------- Static Functions -------------------- */

/**
 * @brief Default desktop logging callback routing formatted text to standard output.
 */
static void ti_log_callback(const char *fmt, va_list args)
{
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    printf("%s", buffer);
}

/**
 * @brief Default desktop fatal error callback calling exit(1).
 */
static void ti_fatal_callback(void)
{
    exit(1);
}

/**
 * @brief Built-in native print function for Ti scripts.
 */
static value_t *built_in_print(value_t **argv, int argc)
{
  if (argc == 0)
    ti_log("\n");
  for (int i = 0; i < argc; i++)
  {
    switch(argv[i]->type)
    {
      case VAL_STRING:
        ti_log("%s", argv[i]->string_val);
        break;
      case VAL_INT:
        ti_log("%d", argv[i]->int_val);
        break;
      case VAL_FLOAT:
        ti_log("%.2f", argv[i]->float_val);
        break;
      default:
        ti_log("Unexpected type %d", argv[i]->type);
        break;
    }
  }
  return init_val(VAL_NULL);
}

/* -------------------- Public Functions -------------------- */

void init_builtin(void)
{
  ti_register_log(ti_log_callback);
  ti_register_fatal(ti_fatal_callback);
  register_builtin_function("print", built_in_print);
}
