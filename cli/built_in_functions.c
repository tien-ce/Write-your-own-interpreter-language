#include "include/visitor.h"
#include "include/platform.h"
static value_t *built_in_print(value_t **argv, int argc)
{
  if (argc == 0)
    ti_log("\n");
  for (int i = 0; i < argc; i++)
  {
    switch(argv[i]->type)
    {
      case VAL_STRING:
        ti_log("%s",argv[i]->string_val);
        break;
      case VAL_INT:
        ti_log("%d",argv[i]->int_val);
        break;
      case VAL_FLOAT:
        ti_log("%.2f",argv[i]->float_val);
        break;
      default:
        ti_log("Unexpeted type %d", argv[i]->type);
        break;
    }
  }
  return init_val(VAL_NULL);
}

void init_builtin()
{
  register_bultin_function("print", built_in_print);
}
