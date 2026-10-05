# TI Interpreter Quick Reference Guide

A concise guide to the syntax, supported data types, control flow, functions, and native C extension APIs of the TI Interpreter.

---

## 1. Supported Data Types

The TI Interpreter enforces **static typing**. Variables must be explicitly typed upon declaration:

### 1.1 Primitive Types
```c
int a = 10;
float b = 3.14;
string name = "ESP32";
bool is_active = true;
```

### 1.2 Collections (List & Dict)
* **Homogeneous Lists (`list <type>`)**:
  ```c
  list int numbers = [1, 2, 3, 4];
  numbers.push(5);
  int first = numbers[0];
  int count = numbers.len();
  ```
* **Dictionaries (`dict`)**:
  ```c
  dict config = {"host": "localhost", "port": 8080};
  string host = config["host"];
  config["timeout"] = 5000;
  ```

---

## 2. Control Flow

### 2.1 Conditional Statements (`if` / `else`)
```c
if (a > 5) {
    print("Greater than 5");
} else {
    print("5 or less");
}
```

### 2.2 While Loops (`while`)
Executes repeatedly as long as the condition evaluates to `true`. Supports `break` and `continue`:
```c
int i = 0;
while (i < 5) {
    i = i + 1;
    if (i == 2) {
        continue;
    }
    if (i == 4) {
        break;
    }
    print(i);
}
```

> **Note:** `for` loops are not yet implemented. Use `while` loops for all iterations.

---

## 3. User-Defined Functions

Functions require explicit return types and typed parameter lists.

### 3.1 Void Functions
```c
void log_status(string tag, int code) {
    print(tag);
    print(code);
}

log_status("STATUS_OK", 200);
```

### 3.2 Value-Returning Functions
```c
int calculate_sum(int a, int b) {
    return a + b;
}

int total = calculate_sum(15, 25);
```

---

## 4. Standard Built-in Functions

The interpreter comes with several standard built-in functions:
* `print(...)`: Prints arguments to standard output (variadic).
* `delay(ms)`: Pauses execution for `ms` milliseconds (yields in 10ms slices to service pending events).
* `register_event(func_name)`: Registers a script function as an asynchronous event callback.

---

## 5. Developer Guide: Registering Native C Functions

Native C functions can be registered into the global runtime table to expose hardware drivers or system services to scripts.

### 5.1 Native Callback Signature
Every native C function must match the `native_fn_t` signature:
```c
typedef value_t *(*native_fn_t)(ti_handle_t handle, value_t **args, int argc);
```

### 5.2 Registration Function
```c
bool register_builtin_function(const char *name, 
                               val_type_t return_type, 
                               param_t *params, 
                               int param_count, 
                               native_fn_t function);
```

### 5.3 Example: Registering a Custom C Function
```c
#include "src/TienInterpreter.h"
#include "src/include/ti_type.h"
#include "src/include/ti_type_value.h"

/* 1. Define the native callback */
static value_t *native_multiply(ti_handle_t handle, value_t **args, int argc)
{
    (void)handle;
    (void)argc;
    int a = args[0]->int_val;
    int b = args[1]->int_val;
    return val_new_int(a * b);
}

/* 2. Register with typed parameter metadata */
void register_custom_builtins(void)
{
    static param_t multiply_params[] = {
        { VAL_INT, "a" },
        { VAL_INT, "b" }
    };

    register_builtin_function("multiply", VAL_VOID, multiply_params, 2, native_multiply);
}
```

> **Further Reading:** Detailed architectural guides for memory tracking (`docs/tracked_memory.md`), parser mechanics (`docs/parser.md`), runtime contexts (`docs/context_value.md`), and error unwinding (`docs/ti_error.md`) are located in the `docs/` directory.
