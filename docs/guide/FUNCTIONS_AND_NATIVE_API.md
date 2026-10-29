# Developer Guide: Functions & Native C Extension API

> **Audience:** Developers writing TI functions or binding external C libraries, hardware drivers (GPIO, I2C, MQTT), and system services into the TI interpreter runtime.

---

## 1. User-Defined Functions in TI Scripts

Functions in TI use standard C syntax with mandatory type declarations for return values and parameters.

### 1.1 Declaring Functions
```c
/* Void function without return statement */
void show_greeting(string name) {
    print("Welcome, " + name);
}

/* Value-returning function */
int calculate_area(int width, int height) {
    return width * height;
}
```

### 1.2 Calling Functions
```c
show_greeting("ESP32 User");
int area = calculate_area(5, 10);
```

### 1.3 Execution Constraints & Invariants
* **Strict Type Enforcement:** Parameters and return values are verified at runtime. Passing incompatible types raises `TI_ERR_TYPE_MISMATCH`.
* **Arity Checking:** Calling a function with too few or too many arguments raises `TI_ERR_INVALID_ARG`.
* **Missing Return Guard:** Non-void functions that finish their body without hitting a `return` statement raise `TI_ERR_RUNTIME`.
* **Recursion Limit:** The runtime enforces a maximum call depth limit (`rt->max_call_depth`, default 64) to protect against stack overflows on resource-constrained embedded targets. Exceeding this raises `TI_ERR_LIMIT_EXCEEDED`.

---

## 2. Native C Functions: Architecture & Registration

Native functions bridge C code into the TI script environment, allowing scripts to call hardware drivers and system functions directly.

### 2.1 The Native Function Signature (`native_fn_t`)
Defined in `src/include/ti_type.h`:
```c
typedef value_t *(*native_fn_t)(ti_handle_t handle, value_t **args, int argc);
```
* **`handle` (`ti_handle_t`)**: An opaque, generation-checked reference representing the calling runtime instance. Native functions must never access `ti_runtime_t *` directly; all runtime interactions must go through this handle.
* **`args` (`value_t **`)**: Array of pointers to evaluated arguments.
* **`argc` (`int`)**: Number of arguments passed by the script.
* **Return Value (`value_t *`)**: Must return a dynamically allocated `value_t *` (e.g. via `val_new_void()`, `val_new_int()`, etc.). The interpreter takes ownership and frees it automatically.

---

## 3. Registering Built-in Functions

### 3.1 Registration API
```c
bool register_builtin_function(const char *name, 
                               val_type_t return_type, 
                               param_t *params, 
                               int param_count, 
                               native_fn_t function);
```
Defined in `src/include/ti_type_func.h`:
* `name`: Identifier used by scripts to invoke the function.
* `return_type`: Expected return value type (`VAL_INT`, `VAL_VOID`, `VAL_DICT`, etc.).
* `params`: Array of `param_t` (`{ type, name, element_type }`) specifying parameter names and expected types. Use `VAL_ANY` as a parameter's `type` to accept a value of **any** type there (see 4.3).
* `param_count`: Number of elements in `params`; the runtime rejects calls with a different argument count. Pass `-1` to declare a **fully variadic** function (neither count nor types are checked). `params` may be `NULL` only when `param_count` is `0` or `-1`; otherwise registration fails.
* `function`: Function pointer matching `native_fn_t`.

### 3.2 Registering Many Functions with a Table
A file that owns several built-ins lists them in a `ti_builtin_def_t` table (same five values as `register_builtin_function`, one row each) and registers them in one call:

```c
static param_t s_bytes_param[] = { { VAL_BYTES, "data", VAL_NULL } };

static const ti_builtin_def_t s_defs[] = {
    { "bytes_to_string", VAL_STRING, s_bytes_param, 1, builtin_bytes_to_string },
    { "crc16",           VAL_INT,    s_bytes_param, 1, builtin_crc16           },
};

void my_topic_register(void)
{
    ti_register_builtin_table(s_defs, (int)(sizeof(s_defs) / sizeof(s_defs[0])));
}
```
`ti_register_builtin_table` returns `false` if any row failed (for example a duplicate name) but still registers the other rows. Parameter tables must outlive the call, so keep them `static`.

**Where the core built-ins live** (flat files in `src/`, one per topic, each with one `ti_runtime_builtin_<topic>_register()` that `ti_register_core_builtins()` calls):

| File | Functions |
| :--- | :--- |
| `ti_runtime_builtin_convert.c` | `to_string`, `to_int`, `to_float`, `to_bool` |
| `ti_runtime_builtin_bytes.c` | `bytes_to_string`, `string_to_bytes` |
| `ti_runtime_builtin_math.c` | `pow`, `sqrt`, `exp`, `log`, `log10`, `sin`, `cos`, `tan`, `atan2`, `pi`, `floor`, `ceil`, `round`, `abs`, `min`, `max` |
| `ti_runtime_builtin_text.c/.h` | Internal text/JSON formatter shared by the topic files (not public API) |

To add a core topic: create `ti_runtime_builtin_<topic>.c` with its own table, declare `ti_runtime_builtin_<topic>_register()` in `include/ti_runtime_builtin.h`, and call it from `ti_register_core_builtins()`. Core built-ins must work on every host, so anything that needs the OS or hardware belongs in the host layer instead (for the CLI: `cli/builtin/`).

---

## 4. Practical Implementation Patterns

### 4.1 Pattern 1: Strictly-Typed Native Function
When parameter types are explicitly declared, the interpreter's `run_function()` automatically validates both argument count and argument types **before** calling your C function. If there is a mismatch, the interpreter reports an informative error and unwinds cleanly.

```c
#include "src/TienInterpreter.h"
#include "src/include/ti_type.h"
#include "src/include/ti_type_value.h"

/* 1. Define the native callback */
static value_t *native_relay_set(ti_handle_t handle, value_t **args, int argc)
{
    (void)handle;
    (void)argc; /* Guaranteed to be 2 by the interpreter */

    int relay_id = args[0]->int_val;
    bool state   = args[1]->bool_val;

    /* Call hardware driver */
    hardware_set_relay(relay_id, state);

    return val_new_void();
}

/* 2. Register into interpreter table */
void register_relay_functions(void)
{
    static param_t relay_params[] = {
        { VAL_INT,  "id" },
        { VAL_BOOL, "state" }
    };

    register_builtin_function("relay_set", VAL_VOID, relay_params, 2, native_relay_set);
}
```

### 4.2 Pattern 2: Variadic Functions (`param_count = -1`)
For functions accepting a dynamic number of arguments of various types (such as `print`):
```c
static value_t *native_custom_log(ti_handle_t handle, value_t **args, int argc)
{
    (void)handle;
    for (int i = 0; i < argc; i++) {
        if (args[i]->type == VAL_STRING) {
            printf("%s ", args[i]->string_val);
        } else if (args[i]->type == VAL_INT) {
            printf("%d ", args[i]->int_val);
        }
    }
    printf("\n");
    return val_new_void();
}

void register_variadic_log(void)
{
    /* Pass NULL and -1 to accept arbitrary arguments */
    register_builtin_function("custom_log", VAL_VOID, NULL, -1, native_custom_log);
}
```

### 4.3 Pattern 3: Any-Typed Parameters (`VAL_ANY`)
Use `VAL_ANY` when a parameter may be of any type but the **argument count (and the other parameters' types) must still be enforced**. This avoids falling back to `-1` and re-checking `argc` by hand.

```c
/* log_value(string label, any value): label must be a string, value can be anything */
static value_t *native_log_value(ti_handle_t handle, value_t **args, int argc)
{
    (void)handle;
    (void)argc; /* Guaranteed to be 2 by the interpreter */

    /* args[0] is a string; args[1] has whatever type the script passed, so branch on it */
    switch (args[1]->type) {
    case VAL_INT:
        printf("%s = %d\n", args[0]->string_val, args[1]->int_val);
        break;
    case VAL_STRING:
        printf("%s = %s\n", args[0]->string_val, args[1]->string_val);
        break;
    default:
        printf("%s = <%s>\n", args[0]->string_val, val_type_to_str(args[1]->type));
        break;
    }
    return val_new_void();
}

void register_log_value(void)
{
    static param_t params[] = {
        { VAL_STRING, "label", VAL_NULL },
        { VAL_ANY,    "value", VAL_NULL },
    };
    register_builtin_function("log_value", VAL_VOID, params, 2, native_log_value);
}
```

Rules:
* `VAL_ANY` is for **native function parameters only**. Scripts have no `any` keyword, so a script function can never declare it, and no value, variable or return type is ever `VAL_ANY` (`val_init(VAL_ANY)` returns `NULL`). `run_function` skips the type check for such a parameter for every function, but only native signatures can contain one, so script functions behave exactly as before.
* The native must check `args[i]->type` itself before reading a union member.
* Use `-1` only when the argument **count** is also open (like `print`, or an optional argument such as `to_int(value [, base])`).
* A native's declared `return_type` is informational (the runtime does not check what a native returns). When the result type follows the arguments (`abs`, `min`, `max`), declare the return type as `VAL_ANY`.
* The core built-ins `to_string(any)`, `to_float(any)` and `to_bool(any)` are registered this way.

### 4.4 Reporting Errors from a Native (`ti_raise_error`)
The runtime checks argument **count** and **type**; a native must check **values** (ranges, lengths, which types of a `VAL_ANY` it supports) and report problems itself:

```c
if (args[1]->type != VAL_INT && args[1]->type != VAL_STRING) {
    ti_raise_error(handle, TI_ERR_TYPE_MISMATCH,
                   "log_value: 'value' must be int or string, got %s",
                   val_type_to_str(args[1]->type));
    return NULL;   /* mandatory: free temporaries first, then return NULL */
}
```

The error ends the script and is reported at the line of the call. Choosing the error kind, when to raise instead of returning a value, and the full contract are in [Native C and TI Scripts: How They Interact](NATIVE_SCRIPT_INTERACTION.md), section 4.

---

## 5. Summary of Best Practices for Native C Code

1. **Avoid Pointer Retaining:** Never store raw `value_t *` pointers from `args` across function calls. If you need to keep data, copy it using standard C functions (e.g. `strdup`) or `val_copy()`.
2. **Never Panic:** Do not call `exit()` or abort from native functions. Return `val_new_null()` or `val_new_void()`.
3. **Keep Execution Short:** A native runs while holding the runtime's execution lock, so a long C loop blocks events and `ti_call` callers. To wait for time, call `ti_wait(handle, ms)`; to wait for another thread's I/O, call `ti_wait_flag(handle, &done, timeout_ms)` and have that thread call `ti_give(handle)` (both release the lock while sleeping and service events and callbacks; see [Native C and TI Scripts](NATIVE_SCRIPT_INTERACTION.md), section 6.1). Never sleep with `sleep()`/`usleep()` or wait on a lock another thread may hold while it calls `ti_call`.
