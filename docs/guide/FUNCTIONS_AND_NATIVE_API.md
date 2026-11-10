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
* `params`: Array of `param_t` (`{ type, name, element_type, handle_type }`) specifying parameter names and expected types. Build each entry with a macro from `src/include/ti_type_func.h`; the fields a macro does not take get their "accept any" value:

  | Macro | Accepts |
  | :--- | :--- |
  | `PARAM("ms", VAL_INT)` | a value of that type (`VAL_ANY` accepts any type, see 4.3) |
  | `PARAM_LIST("names", VAL_STRING)` | a list whose elements have that type (`VAL_NULL`: any list) |
  | `PARAM_HANDLE("dev", &s_device_type)` | a handle of that type descriptor (`NULL`: any handle), see 4.5 |
* `param_count`: Number of elements in `params`; the runtime rejects calls with a different argument count. Pass `-1` to declare a **fully variadic** function (neither count nor types are checked). `params` may be `NULL` only when `param_count` is `0` or `-1`; otherwise registration fails.
* `function`: Function pointer matching `native_fn_t`.

### 3.2 Registering Many Functions with a Table
A file that owns several built-ins lists them in a `ti_builtin_def_t` table (same five values as `register_builtin_function`, one row each) and registers them in one call:

```c
static param_t s_bytes_param[] = { PARAM("data", VAL_BYTES) };

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
    static param_t params[] = {
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

### 4.5 Pattern 4: Native Objects Passed Through Many Calls (`VAL_HANDLE`)
When a C object has no TI type (a driver config, a socket, a context struct), wrap it in a **handle** instead of flattening it into ints and strings. The script keeps one variable and uses it with functions (`device_read(dev)`), methods (`dev.read()`) and attributes (`dev.baud`). API: `src/include/ti_type_value_handle.h`.

#### 4.5.1 The type descriptor
Each C type gets **one** `static const ti_handle_type_t`. Its **address** is the type identity: two descriptors with the same name are still different types.

| Field | Meaning |
| :--- | :--- |
| `name` | Shown by `print`, `to_string` and error messages (`<handle device_cfg>`). Must not be empty. |
| `destroy` | `void (*)(void *ptr)`: called once, when the last copy is released or when a native closes the handle. `NULL` if the native keeps ownership (e.g. a static object). |
| `methods`, `method_count` | Table of `ti_builtin_def_t` rows called as `obj.name(...)`. `params[0]` is the receiver (self). `NULL` / `0`: no methods. |
| `get_attr` | `ti_status_t (*)(void *ptr, const char *name, value_t **out)`: reads `obj.name`. `NULL`: no readable attributes (a read reports "has no attribute"). |
| `set_attr` | `ti_status_t (*)(void *ptr, const char *name, const value_t *value)`: writes `obj.name = value`. `NULL`: no writable attributes (a write reports "has no attribute"). |

#### 4.5.2 Example
```c
static const ti_handle_type_t s_device_type;   /* forward: the tables below point to it */

/* Called once, when the last copy is released or when a native closes the handle */
static void device_destroy(void *ptr) { free(ptr); }

/* handle device_open(string port, int baud) */
static value_t *device_open(ti_handle_t handle, value_t **argv, int argc)
{
    device_cfg_t *cfg = calloc(1, sizeof(device_cfg_t));
    if (cfg == NULL) {
        ti_raise_error(handle, TI_ERR_NO_MEMORY, "device_open: out of memory");
        return NULL;
    }
    /* ... fill cfg from argv ... */

    value_t *result = val_new_handle(cfg, &s_device_type);
    if (result == NULL) {
        free(cfg);                       /* on failure cfg still belongs to the native */
        ti_raise_error(handle, TI_ERR_NO_MEMORY, "device_open: out of memory");
    }
    return result;
}

/* int device_read(handle dev), also dev.read(): the runtime already checked the type */
static value_t *device_read(ti_handle_t handle, value_t **argv, int argc)
{
    device_cfg_t *cfg = val_handle_get(argv[0], &s_device_type);
    if (cfg == NULL) {                   /* only possible if the device was closed */
        ti_raise_error(handle, TI_ERR_INVALID_VALUE, "device_read: device is closed");
        return NULL;
    }
    return val_new_int(read_sample(cfg));
}

/* void device_close(handle dev), also dev.close(): destroy now; every copy reads as closed */
static value_t *device_close(ti_handle_t handle, value_t **argv, int argc)
{
    if (val_handle_close(argv[0], &s_device_type) != TI_OK) {
        ti_raise_error(handle, TI_ERR_INVALID_VALUE, "device_close: already closed");
        return NULL;
    }
    return val_new_void();
}

/* dev.baud (int), dev.port (string): never raises, returns a status */
static ti_status_t device_get_attr(void *ptr, const char *name, value_t **out)
{
    device_cfg_t *cfg = ptr;
    if (strcmp(name, "baud") == 0) {
        *out = val_new_int(cfg->baud);
    } else if (strcmp(name, "port") == 0) {
        *out = val_new_string(cfg->port);
    } else {
        return TI_ERR_UNDEFINED;         /* the interpreter reports "has no attribute" */
    }
    return (*out != NULL) ? TI_OK : TI_ERR_NO_MEMORY;
}

/* dev.baud = int; dev.port is read-only */
static ti_status_t device_set_attr(void *ptr, const char *name, const value_t *value)
{
    device_cfg_t *cfg = ptr;
    if (strcmp(name, "baud") == 0) {
        if (value->type != VAL_INT)  return TI_ERR_TYPE_MISMATCH;   /* the native checks the type */
        if (value->int_val <= 0)     return TI_ERR_VALUE_OUT_OF_RANGE;
        cfg->baud = value->int_val;
        return TI_OK;
    }
    return (strcmp(name, "port") == 0) ? TI_ERR_READ_ONLY : TI_ERR_UNDEFINED;
}

/* Parameter tables: params[0] of a method is self, so a free function's table is reused */
static param_t s_open_params[] = { PARAM("port", VAL_STRING), PARAM("baud", VAL_INT) };
static param_t s_dev_params[]  = { PARAM_HANDLE("dev", &s_device_type) };

static const ti_builtin_def_t s_device_methods[] = {
    { "read",  VAL_INT,  s_dev_params, 1, device_read },
    { "close", VAL_VOID, s_dev_params, 1, device_close },
};

static const ti_handle_type_t s_device_type = {
    .name         = "device_cfg",
    .destroy      = device_destroy,
    .methods      = s_device_methods,
    .method_count = 2,
    .get_attr     = device_get_attr,
    .set_attr     = device_set_attr,
};

void device_register(void)
{
    register_builtin_function("device_open",  VAL_HANDLE, s_open_params, 2, device_open);
    register_builtin_function("device_read",  VAL_INT,    s_dev_params,  1, device_read);
    register_builtin_function("device_close", VAL_VOID,   s_dev_params,  1, device_close);
}
```
Full working version: `cli/builtin/builtin_device.c`, script: `examples/ti/handle_device.ti`.

#### 4.5.3 C API
| Function | Use |
| :--- | :--- |
| `val_new_handle(ptr, &type)` | Return a new handle. On success the value owns `ptr`; on `NULL` it is still yours. |
| `val_handle_get(value, &type)` | Get `ptr` back. `NULL` if not a handle, wrong type, empty, or closed. The type is mandatory. |
| `val_handle_close(value, &type)` | Destroy the object now. `TI_ERR_TYPE_MISMATCH` for a wrong type, `TI_ERR_INVALID_VALUE` if already closed. |
| `val_handle_type(value)`, `val_handle_is_open(value)` | For logging and checks (`val_handle_type(v)->name`). A closed handle keeps its type. |
| `handle_type_find_method(&type, name)` | The method row the interpreter runs for `obj.name(...)`. |

#### 4.5.4 How a method call runs
`dev.read()` looks up `"read"` in `s_device_type.methods`, puts `dev` in `argv[0]` and runs the row exactly like a native function call. So:
* Count and type errors count self: `dev.set_baud()` reports `expects 2 argument(s), but received 1`, and the first written argument is parameter 2.
* `self` is type-checked like any `PARAM_HANDLE`: the native only has to handle "closed".
* The receiver can be any expression: `device_open("/dev/ttyS1", 4800).info()`, `devices["a"].read()`.
* A missing method raises `TI_ERR_UNDEFINED` (`Handle device_cfg has no method 'x'`); a method on a non-handle or on an empty handle raises `TI_ERR_TYPE_MISMATCH`.

#### 4.5.5 How an attribute access runs
`get_attr` and `set_attr` are **layer 1** functions: they return a status and never call `ti_raise_error`. The interpreter turns the status into the error the script sees:

| Status returned | Script error |
| :--- | :--- |
| `TI_ERR_UNDEFINED` (or `get_attr` / `set_attr` is `NULL`) | `Handle device_cfg has no attribute 'x'` |
| `TI_ERR_READ_ONLY` | `Cannot set attribute 'port' of handle device_cfg: read-only` |
| any other status | `Cannot get/set attribute 'baud' of handle device_cfg: <status text>` |

* A closed or empty handle is rejected **before** `get_attr` / `set_attr` run, so `ptr` is never `NULL` there.
* `get_attr` returns a **new** value in `*out` (owned by the interpreter). `set_attr` receives a **borrowed** value: copy what you keep. Its type is not checked by the interpreter, so check it.
* `dev.baud += 1` is rewritten as `dev.baud = dev.baud + 1`: one `get_attr`, then one `set_attr`. The receiver expression is evaluated twice (`devices[f()].baud += 1` calls `f()` twice).

#### 4.5.6 Rules
* **Declare the type in the parameter table** (`PARAM_HANDLE`): the runtime then rejects a wrong type or an empty handle before the native runs, so `val_handle_get` returning `NULL` only means "closed".
* **The descriptor must outlive every handle**: make it `static const` and never free it.
* **`destroy` runs on the script's context** (end of scope, overwritten variable, `close`), or inside `ti_destroy` for handles still held by globals. Keep it short and never call TI functions from it.
* **Reference counts are not atomic.** Like every `value_t`, never keep a handle value in another thread. A driver thread may keep the raw `ptr` only if it manages that object's lifetime itself.

---

## 5. Summary of Best Practices for Native C Code

1. **Avoid Pointer Retaining:** Never store raw `value_t *` pointers from `args` across function calls. If you need to keep data, copy it using standard C functions (e.g. `strdup`) or `val_copy()`.
2. **Never Panic:** Do not call `exit()` or abort from native functions. Return `val_new_null()` or `val_new_void()`.
3. **Keep Execution Short:** A native runs while holding the runtime's execution lock, so a long C loop blocks events and `ti_call` callers. To wait for time, call `ti_wait(handle, ms)`; to wait for another thread's I/O, call `ti_wait_flag(handle, &done, timeout_ms)` and have that thread call `ti_give(handle)` (both release the lock while sleeping and service events and callbacks; see [Native C and TI Scripts](NATIVE_SCRIPT_INTERACTION.md), section 6.1). Never sleep with `sleep()`/`usleep()` or wait on a lock another thread may hold while it calls `ti_call`.
