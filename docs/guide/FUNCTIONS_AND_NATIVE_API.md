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
                               const param_t *params, 
                               int param_count, 
                               native_fn_t function);
```
Defined in `src/include/ti_type_func.h`:
* `name`: Identifier used by scripts to invoke the function.
* `return_type`: Expected return value type (`VAL_INT`, `VAL_VOID`, `VAL_DICT`, etc.).
* `params`: Array of `param_t` (`{ type, name, element_type, handle_type }`) specifying parameter names and expected types. Build each entry with a macro from `src/include/ti_type_func.h`; the fields a macro does not take get their "accept any" value:

  | Macro | Accepts |
  | :--- | :--- |
  | `PARAM("ms", VAL_INT)` | a value of that type (`VAL_ANY` accepts any type, see 4.3) |
  | `PARAM_INT("ms")`, `PARAM_FLOAT`, `PARAM_BOOL`, `PARAM_STRING`, `PARAM_BYTES`, `PARAM_DICT`, `PARAM_FUNC`, `PARAM_ANY` | same as `PARAM` with that type |
  | `PARAM_LIST("names", VAL_STRING)` | a list whose elements have that type (`VAL_NULL`: any list) |
  | `PARAM_HANDLE("dev", &s_device_type)` | a handle of that type descriptor (`NULL`: any handle), see 4.5 |
* `param_count`: Number of elements in `params`; the runtime rejects calls with a different argument count. Pass `-1` to declare a **fully variadic** function (neither count nor types are checked). `params` may be `NULL` only when `param_count` is `0` or `-1`; otherwise registration fails.
* `function`: Function pointer matching `native_fn_t`.

### 3.2 Registering Many Functions with a Table
A file that owns several built-ins lists them in a `ti_builtin_def_t` table (same five values as `register_builtin_function`, one row each) and registers them in one call:

```c
static const param_t s_bytes_param[] = { PARAM("data", VAL_BYTES) };

static const ti_builtin_def_t s_defs[] = {
    { "bytes_to_string", VAL_STRING, s_bytes_param, 1, builtin_bytes_to_string },
    { "crc16",           VAL_INT,    s_bytes_param, 1, builtin_crc16           },
};

void my_topic_register(void)
{
    ti_register_builtin_table(s_defs, (int)(sizeof(s_defs) / sizeof(s_defs[0])));
}
```
`ti_register_builtin_table` returns `false` if any row failed (for example a duplicate name) but still registers the other rows. Parameter tables are borrowed, never copied: they must outlive the call, so keep them `static const` (see 3.3).

**Where the core built-ins live** (flat files in `src/`, one per topic, each with one `ti_runtime_builtin_<topic>_register()` that `ti_register_core_builtins()` calls):

| File | Functions |
| :--- | :--- |
| `ti_runtime_builtin_convert.c` | `to_string`, `to_int`, `to_float`, `to_bool` |
| `ti_runtime_builtin_bytes.c` | `bytes_to_string`, `string_to_bytes` |
| `ti_runtime_builtin_math.c` | `pow`, `sqrt`, `exp`, `log`, `log10`, `sin`, `cos`, `tan`, `atan2`, `pi`, `floor`, `ceil`, `round`, `abs`, `min`, `max` |
| `ti_runtime_builtin_text.c/.h` | Internal text/JSON formatter shared by the topic files (not public API) |

To add a core topic: create `ti_runtime_builtin_<topic>.c` with its own table, declare `ti_runtime_builtin_<topic>_register()` in `include/ti_runtime_builtin.h`, and call it from `ti_register_core_builtins()`. Core built-ins must work on every host, so anything that needs the OS or hardware belongs in the host layer instead (for the CLI: `cli/builtin/`).

### 3.3 Parameter Tables Are `const`
`params` is `const param_t *` everywhere (`register_builtin_function`, `ti_builtin_def_t.params`, `function_t.params`), and `param_t.name` is `const char *`. Declare every table `static const`:

```c
static const param_t s_params[] = { PARAM("port", VAL_INT), PARAM_HANDLE("self", &s_uart_type) };
```

* **Why:** a `static const` table goes to read-only memory (flash on ESP32, `.rodata` on Linux) instead of RAM, and the interpreter never writes to it: it only reads the types and names. Method tables (4.5) and module tables (4.6) are `static const` too, so a whole native type or module can live in flash.
* **C++ (ESP32 sketches):** `PARAM("name", ...)` stores a string literal; with `const char *name` this compiles in C++ too.
* **Migration from v2.2.0 and earlier:** `static param_t` tables still compile (non-`const` converts to `const`), but code that writes to `func->params[i]` or assigns `params[i].name` to a `char *` must be changed.

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
    static const param_t relay_params[] = {
        PARAM("id",    VAL_INT),
        PARAM("state", VAL_BOOL)
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
    static const param_t params[] = {
        PARAM("label", VAL_STRING),
        PARAM("value", VAL_ANY),
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

### 4.5 Pattern 4: Native Objects (`VAL_HANDLE`)
Wrap a C object that has no TI type (driver config, socket, context struct) in a **handle**. API: `src/include/ti_type_value_handle.h`. Script usage: [Types & Values](TYPES_AND_VALUES.md), section 2.7.

**Reference code:** `cli/builtin/builtin_device.c` (functions, methods and attributes of one type), `cli/module/module_machine.c` (a handle returned by a module function).

#### 4.5.1 The type descriptor (`ti_handle_type_t`)
One `static const` descriptor per C type. Its **address** is the type identity: two descriptors with the same name are different types.

| Field | Meaning |
| :--- | :--- |
| `name` | Shown by `print`, `to_string` and error messages. Must not be empty. |
| `destroy` | `void (*)(void *ptr)`: called once, when the last copy is released or on close. `NULL`: the native keeps ownership. |
| `methods`, `method_count` | `ti_builtin_def_t` rows called as `obj.name(...)`. `params[0]` is self, so a free function's row can be reused. |
| `get_attr` | `ti_status_t (*)(void *ptr, const char *name, value_t **out)`: reads `obj.name`. `NULL`: no readable attribute. |
| `set_attr` | `ti_status_t (*)(void *ptr, const char *name, const value_t *value)`: writes `obj.name = value`. `NULL`: no writable attribute. |

#### 4.5.2 Functions
| Function | Use |
| :--- | :--- |
| `val_new_handle(ptr, &type)` | New handle owning `ptr`. On `NULL` (bad argument, out of memory) `ptr` is still yours: free it. |
| `val_handle_get(value, &type)` | `ptr` back, or `NULL` if not a handle, wrong type, empty or closed. |
| `val_handle_close(value, &type)` | Destroy now; every copy reads as closed. `TI_ERR_TYPE_MISMATCH` (wrong type), `TI_ERR_INVALID_VALUE` (already closed). |
| `val_handle_type(value)` | The descriptor (also for a closed handle), for messages: `val_handle_type(v)->name`. |
| `val_handle_is_open(value)` | `true` while the object exists. |
| `handle_type_find_method(&type, name)` | The row the interpreter runs for `obj.name(...)`. |
| `PARAM_HANDLE("dev", &type)` | Parameter entry that accepts only that type (checked before the native runs). |

```c
device_cfg_t *cfg = val_handle_get(argv[0], &s_device_type);   /* NULL here only means "closed" */
```

#### 4.5.3 What the interpreter does for you
* **Method call** `obj.m(a)`: the row runs like any native with `argv[0] = obj`, `argv[1] = a`. Count and type errors count self.
* **Attribute** `obj.x` / `obj.x = v`: a closed or empty handle is rejected first, so `ptr` is never `NULL`. `get_attr` / `set_attr` return a status and **never raise**; the interpreter reports it:

| Status | Script error |
| :--- | :--- |
| `TI_ERR_UNDEFINED` (or the function is `NULL`) | `Handle <type> has no attribute 'x'` |
| `TI_ERR_READ_ONLY` | `Cannot set attribute 'x' of handle <type>: read-only` |
| other | `Cannot get/set attribute 'x' of handle <type>: <status>` |

* `get_attr` stores a **new** value in `*out`; `set_attr` receives a **borrowed** value and must check its type.

#### 4.5.4 Rules
* Declare the type in the parameter table (`PARAM_HANDLE`), so `val_handle_get` returning `NULL` only means "closed".
* The descriptor must outlive every handle: `static const`, never freed.
* `destroy` runs on the script's context (end of scope, close) or inside `ti_destroy`. Keep it short; never call TI functions from it.
* Reference counts are not atomic: never keep a handle value in another thread.

### 4.6 Pattern 5: Modules (`ti_module_t`)
A module groups native functions and constants under one name that scripts `import`. Nothing is copied into the builtin table: the module stays in its `static const` tables, and the registry keeps one pointer per module. API: `src/include/ti_type_module.h`. Script usage: [Types & Values](TYPES_AND_VALUES.md), section 2.8.

**Reference code:** `cli/module/module_machine.c` (module `machine`), registered by `ti_init_modules()` in `cli/module/module.c`.

#### 4.6.1 The descriptor (`ti_module_t`)
| Field | Meaning |
| :--- | :--- |
| `name` | Name used by `import`. Must not be empty and must be unique. |
| `funcs`, `func_count` | `ti_builtin_def_t` rows called as `module.f(args)`, without self (`argv[0]` is the first argument). |
| `consts`, `const_count` | `ti_module_const_t { name, value }` rows read as `module.NAME`. |

Build each constant row with a helper (`val_new_*` allocates, so it cannot be used in a static table):

| Helper | Constant |
| :--- | :--- |
| `TI_CONST_INT("SERIAL_8N1", 0x800001c)` | `int` |
| `TI_CONST_FLOAT("VREF", 3.3f)` | `float` |
| `TI_CONST_BOOL("HAS_PSRAM", true)` | `bool` |
| `TI_CONST_STRING("VERSION", "1.0")` | `string` (the literal is never written or freed) |

* Only these four types are possible: a list, dict, bytes or handle needs the heap. A script read gets a copy; the table is never written or freed.
* The helpers pick the union member with a designated initializer (`.float_val = ...`), which C++17 does not have: **define constant tables in a `.c` file**. A `.cpp` file can still register the module (the headers are `extern "C"`).
* Writing `{ VAL_FLOAT, 3.3f }` by hand is wrong: without a member name the value goes into the first union member (`int_val`), so C stores `3` and reads it back as a tiny float, and C++ rejects it as a narrowing conversion.

#### 4.6.2 Functions
| Function | Use |
| :--- | :--- |
| `ti_register_module(&module)` | Make it importable. `false` for `NULL`, an empty name, a duplicate name, or a full registry (`TI_MAX_MODULES`, default 16). |
| `ti_module_find(name)` | Registered module or `NULL`. |
| `ti_module_find_func(module, name)` | Function row or `NULL`. |
| `ti_module_find_const(module, name)` | Constant row or `NULL`. |

```c
void module_machine_register(void) { ti_register_module(&s_machine_module); }
```

#### 4.6.3 Rules
* Register at startup, from one thread, before any runtime is created: the registry is not locked.
* Use a module for an API only some scripts need (`machine`, `modbus`); keep `register_builtin_function` for functions every script uses (`print`, `delay`).
* Module natives raise errors like any native (4.4).

---

## 5. Summary of Best Practices for Native C Code

1. **Avoid Pointer Retaining:** Never store raw `value_t *` pointers from `args` across function calls. If you need to keep data, copy it using standard C functions (e.g. `strdup`) or `val_copy()`.
2. **Never Panic:** Do not call `exit()` or abort from native functions. Return `val_new_null()` or `val_new_void()`.
3. **Keep Execution Short:** A native runs while holding the runtime's execution lock, so a long C loop blocks events and `ti_call` callers. To wait for time, call `ti_wait(handle, ms)`; to wait for another thread's I/O, call `ti_wait_flag(handle, &done, timeout_ms)` and have that thread call `ti_give(handle)` (both release the lock while sleeping and service events and callbacks; see [Native C and TI Scripts](NATIVE_SCRIPT_INTERACTION.md), section 6.1). Never sleep with `sleep()`/`usleep()` or wait on a lock another thread may hold while it calls `ti_call`.
