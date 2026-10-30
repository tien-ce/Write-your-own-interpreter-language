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
  int first = numbers[0];
  numbers[1] = 20;
  ```
* **Dictionaries (`dict`)**:
  ```c
  dict config = {"host": "localhost", "port": 8080};
  string host = config["host"];
  config["timeout"] = 5000;
  ```
* **Nesting and JSON**: lists and dicts can hold each other (`list dict`, `list list`, a dict value that is a list), up to 4 levels. Inserting a container stores a copy; a variable, an argument or `x[i]` shares it. `to_string` turns them into JSON:
  ```c
  list dict stations = [{"name": "tien"}];
  dict payload = {"stations": stations, "count": 1};
  print(to_string(payload));   // {"count":1,"stations":[{"name":"tien"}]}
  ```
  Details: [Types & Values](TYPES_AND_VALUES.md), sections 2.4 to 2.6.

### 1.3 Bitwise Operators (`int` only)

| Operator | Meaning |
|---|---|
| `a & b`, `a \| b`, `a ^ b` | AND, OR, XOR |
| `~a` | Complement |
| `a << n`, `a >> n` | Shift left; shift right (arithmetic: the sign is kept) |

* Operands must be `int` (32-bit signed). `bool`, `float`, `string` and `bytes` are a type error; use `&&` / `||` for `bool`.
* **Precedence** (high to low): `~ ! + -` (unary), `* /`, `+ -`, `<< >>`, `&`, `^`, `\|`, comparisons, `&& \|\|`. Unlike C, bitwise operators bind tighter than comparisons, so `x & 0x80 == 0x80` means `(x & 0x80) == 0x80`.
* A shift count must be `0..31`, otherwise `TI_ERR_VALUE_OUT_OF_RANGE`. Bits shifted past the sign bit wrap (`1 << 31` is `-2147483648`).
* Hexadecimal literals: `0x1F`, `0xFF`, up to 8 digits. A hex literal is an ordinary 32-bit `int` (not limited to one byte): `0xFFF` is `4095`, and `0xFFFFFFFF` wraps to `-1`. Decimal literals are unchanged (`010` is still `10`).
* Shift operators bind tighter than `&`, so `a & b << 1` means `a & (b << 1)`.
* The result is a plain `int`, never limited to a byte. Only **storing** into a `bytes` element is range-checked (`0..255`, else `TI_ERR_VALUE_OUT_OF_RANGE`), so mask explicitly when you need a byte:
  ```c
  bytes b = x"01 FF";
  b[0] = (b[1] << 4) & 0xFF;   // OK: 0xF0
  b[0] = b[1] << 4;            // runtime error: 4080 does not fit a byte
  int hi = (b[1] >> 4) & 0x0F; // nibbles
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
* `to_string(value)`, `to_int(value [, base])`, `to_float(value)`, `to_bool(value)`: Type conversions. `to_string`, `to_float` and `to_bool` take exactly one argument of any type (`VAL_ANY` native parameter), so a wrong argument count is reported by the runtime.
* **Math** (arguments may be `int` or `float`; any other type is `TI_ERR_TYPE_MISMATCH`):

  | Function | Result | Notes |
  | :--- | :--- | :--- |
  | `pow(x, y)`, `sqrt(x)`, `exp(x)` | `float` | `pow(2, 10)` is `1024.00`; use `to_int` for a whole number |
  | `log(x)`, `log10(x)` | `float` | natural / base 10 |
  | `sin(x)`, `cos(x)`, `tan(x)`, `atan2(y, x)` | `float` | radians; `atan2` takes `y` first like C |
  | `pi()` | `float` | 3.14159... |
  | `floor(x)`, `ceil(x)`, `round(x)` | `int` | `round` sends halves away from zero (`2.5` -> `3`, `-2.5` -> `-3`); an `int` argument is returned unchanged |
  | `abs(x)` | same type as `x` | `abs` of the smallest `int` is an error |
  | `min(x, y)`, `max(x, y)` | same type as the arguments | both arguments must be the same type: `min(1, 2.0)` is an error, write `min(to_float(1), 2.0)` |

  A result that is not a finite number (`sqrt(-1)`, `log(0)`, `pow(0, -1)`, an overflow) raises `TI_ERR_VALUE_OUT_OF_RANGE`, so a script never holds NaN or infinity. `floor`/`ceil`/`round` raise the same error when the value does not fit an `int`. Math runs in 32-bit `float`, so an `int` above 2^24 can lose low digits when mixed into a float function. These names are reserved: a script cannot define its own function called `min`, `max`, `abs`, `round`, ...
* `bytes_to_string(bytes)`, `string_to_bytes(string)`: Raw conversion for binary links such as a serial port, so a native only has to return `bytes`. `bytes_to_string` copies the bytes into a string as characters (the bytes are not changed, only read differently); a `0x00` stays in the buffer, but string functions such as `print` and `==` stop at it, so `bytes_to_string(x"48 49 00 5A")` reads as `"HI"`. Copying that string into a variable also keeps only the text before the `0x00`. `string_to_bytes` copies the characters without the terminator (at most 4096). Note that `to_string(bytes)` is different: it gives the **hex** text (`"4142"`) for logging.
* `delay(ms)`: Pauses execution for `ms` milliseconds. Queued events and synchronous callbacks run immediately during the pause, then the delay continues for the remaining time; only a stop request ends it early.
* `register_event(func_name)`: Registers a script function as an asynchronous event handler (fire and forget).
* `register_callback(func_name)`: Registers a script function as a synchronous handler that a native driver calls and gets an answer from.

See [Asynchronous Events & Synchronous Callbacks](ASYNC_EVENTS_AND_CALLBACKS.md) for when to use which.

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

---

## 6. Detailed Feature & Developer Guides

For comprehensive, in-depth architectural and developer documentation, see the specialized guides:
* [Native C and TI Scripts: How They Interact](NATIVE_SCRIPT_INTERACTION.md): **Start here.** Map of every channel between scripts and native code (arguments, return values, errors, events, callbacks, waiting) with the rules and a checklist for new natives.
* [Types, Values & Memory Ownership](TYPES_AND_VALUES.md): Complete reference on primitive types, lists, dictionaries, C native value manipulation, and ownership invariants.
* [Functions & Native C Extension API](FUNCTIONS_AND_NATIVE_API.md): Guide to writing TI functions, registering native C drivers, parameter validation, and variadic handlers.
* [Asynchronous Events & Synchronous Callbacks](ASYNC_EVENTS_AND_CALLBACKS.md): Integrating background threads, MQTT/Web handlers and hardware interrupts with `ti_post_event` (async) and `ti_call` (sync), generation handles, and the execution lock.
* [Structured Error Handling & Unwinding](../ti_error.md): Runtime error status codes, `ti_raise`, and leak-free stack unwinding.

