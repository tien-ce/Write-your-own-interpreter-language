# TI Interpreter Guide

Welcome to the comprehensive guide for the TI Interpreter. This manual covers the core mechanics, strict static typing system, C-style syntax constructs, and developer APIs to extend the interpreter with custom C functions.

## 1. Supported Data Types & Strict Typing

The TI Interpreter enforces **strict static typing** identical to C. A variable's type is explicitly defined upon declaration and cannot change dynamically.

*   **Integer (`int`)**: Whole numbers without a fractional component. Example: `int a = 5;`
*   **Float (`float`)**: Numbers with a fractional component. Example: `float b = 3.14;`
*   **String (`string`)**: A sequence of characters enclosed in double quotes. Example: `string name = "Alice";`
*   **Dictionary (`dict`)**: Key-value mappings for structured data. Example: `dict map = ...;`
*   **Boolean (`bool`)**: Logical values (`true` or `false`). Example: `bool is_valid = true;`
*   **Void (`void`)**: Represents the absence of a value, typically used for function return types.

## 2. Functions

Functions in the TI Interpreter use absolute C syntax. You must explicitly declare the return type and the types of all parameters.

### Declaring a Function

```c
void greet(string name) {
    print("Hello, " + name);
}

int add(int a, int b) {
    return a + b;
}
```

### Calling a Function

```c
greet("Alice");
int result = add(5, 10);
```

## 3. Loop Constructs

The interpreter supports standard C control flow mechanisms to iterate over data or execute code repeatedly.

### While Loop

Executes a block of code as long as a specified condition evaluates to true.

```c
int i = 0;
while (i < 5) {
    print(i);
    i = i + 1;
}
```

### For Loop

Standard C-style for loop iteration.

```c
for (int i = 0; i < 5; i = i + 1) {
    print(i);
}
```

## 4. Developer Guide: Registering Built-in C Functions

One of the most powerful features of the TI Interpreter is the ability to extend its runtime by registering custom built-in functions written in C.

### Memory Lifecycle & Guidelines (Mandatory)
*   Always use the interpreter's tracked memory allocators (`tracked_calloc`, `tracked_realloc`, `tracked_strdup`, `tracked_free`) for dynamic heap allocations to ensure memory safety.
*   Do NOT use numbered steps in comments. Use descriptive words or thematic phrases.

### Registering a Native Function

The TI Interpreter provides a robust C API to declare parameters, enforce types, and handle variadic arguments for built-in functions. The core registration function is:

```c
bool register_builtin_function(const char *name, val_type_t return_type, param_t *params, int param_count, native_fn_t function);
```

#### Example 1: Standard Function Registration
To register a native function that requires strict parameter typing:

```c
#include "src/TienInterpreter.h"
#include "src/include/ti_type_func.h"

/* 1. Define the native C callback (matches native_fn_t) */
value_t *builtin_add(value_t **args, int argc) {
    int a = args[0]->int_val;
    int b = args[1]->int_val;
    return val_new_int(a + b);
}

/* 2. Register globally */
void init_builtins(void) {
    /* Define parameter metadata (dynamically allocated via tracker) */
    param_t *params = tracked_calloc(NULL, 2, sizeof(param_t));
    params[0].name = tracked_strdup(NULL, "a");
    params[0].type = VAL_INT;
    params[1].name = tracked_strdup(NULL, "b");
    params[1].type = VAL_INT;

    /* Register into the global function table */
    register_builtin_function("add", VAL_INT, params, 2, builtin_add);
}
```

#### Example 2: Variadic Functions
If your built-in function accepts an arbitrary number of arguments (e.g., a custom `print` function), pass `-1` for `param_count` and `NULL` for the `params` array. But the interpreter can't check the type and number of param so if you variadic, the responsible is on you.

```c
/* Native handler for variadic arguments */
value_t *builtin_custom_print(value_t **args, int argc) {
    for (int i = 0; i < argc; i++) {
        /* Check NULL pointer and print error before redefrent avoid the GURU meditation */
        /* Process variadic args */
        if (args[i]->type == VAL_STRING) {
            printf("%s ", args[i]->string_val);
        }
    }
    printf("\n");
    return val_new_void();
}

void init_variadic_builtins(void) {
    /* Set param_count to -1 for variadic validation bypass */
    register_builtin_function("custom_print", VAL_VOID, NULL, -1, builtin_custom_print);
}
```

#### Example 3: Native Function Returning a Dictionary
To return a native `dict` object instead of a JSON string, use the `val_new_dict()` API and populate it using `val_dict_set()`. This employs zero-copy ownership transfer, meaning you do not need to call `val_free()` on the values you insert.

```c
/**
 * @brief Native C function returning a Dictionary
 * Usage in Ti script: dict info = get_system_info();
 */
value_t *builtin_get_system_info(value_t **args, int argc) {
    (void)args; (void)argc;

    /* Allocate a new dictionary wrapper */
    value_t *sys_dict = val_new_dict();

    /* Populate the dictionary with key-value pairs (Ownership Transfer) */
    val_dict_set(sys_dict->dict_val, "status", val_new_string("running"));
    val_dict_set(sys_dict->dict_val, "uptime", val_new_int(3600));
    val_dict_set(sys_dict->dict_val, "is_ready", val_new_bool(true));

    return sys_dict;
}

void init_dict_builtins(void) {
    /* Register with VAL_DICT return type */
    register_builtin_function("get_system_info", VAL_DICT, NULL, 0, builtin_get_system_info);
}
```
