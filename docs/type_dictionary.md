# Dictionary Type Architecture & Memory Mechanics

This document explains the internal mechanics of the `VAL_DICT` type within the TI Interpreter. It serves as a guide for C maintainers and developers looking to understand the memory lifecycle and the Abstract Syntax Tree (AST) evaluation of Dictionaries.

## 1. AST Architecture (L-Value vs R-Value)

The TI Interpreter handles Dictionary access via a unified `AST_ARRAY_ACCESS` node. To avoid ambiguity during parsing (Lookahead issues), the Evaluator distinguishes between setting and getting a value based on the node's position in the AST tree.

### R-Value (Getting a value)
When the parser encounters `print(dict["key"])`:
1. It generates an isolated `AST_ARRAY_ACCESS` node.
2. The `visitor_visit()` function dispatches this to `eval_array_access()`.
3. The evaluator looks up the dictionary in the context, evaluates the key, and calls `val_dict_get()` to return a deep copy of the stored value.

### L-Value (Setting a value)
When the parser encounters `dict["key"] = 10`:
1. It wraps the `AST_ARRAY_ACCESS` inside an `AST_ASSIGNMENT` node (as the `target`/LHS).
2. The `visitor_visit()` function dispatches this to `eval_assignment()`.
3. The evaluator manually inspects the target type. Because it is an `AST_ARRAY_ACCESS`, it evaluates the right-hand expression and pushes it directly into the dictionary using `val_dict_set()` (bypassing `eval_array_access`).

## 2. Memory Lifecycle & Garbage Collection

Dictionaries in TI use a hybrid **Caller-Owns** and **Reference Counting** architecture to prevent memory leaks and dangling pointers.

### The `refcount` Mechanic
When a Dictionary is born (either via `{"key": 10}` literal or a native C function `val_new_dict()`), its `refcount` is initialized to `1`.
- **Assignment (Ownership Transfer):** When assigned for the first time `dict a = {"key": 10}`, the context directly takes ownership. The `refcount` remains `1` (Zero-copy).
- **Copying (Shared Ownership):** When assigned to a secondary variable `dict b = a`, `val_copy()` invokes `dict_retain()`, increasing the `refcount` to `2`.

### Cascading Destruction (Memory Deallocation)
When a variable containing a dictionary is overwritten or destroyed, `val_free()` is called on it.
1. `val_free()` calls `dict_release()`, dropping the `refcount` by 1.
2. If `refcount` hits `0`, `chashmap_destroy()` is invoked.
3. The hashmap loop iterates through all surviving key-value pairs and triggers `dict_payload_free_cb`.
4. The callback explicitly invokes `val_free()` on every internal integer, string, float, or boolean, ensuring **100% memory wipe** without memory leaks.

## 3. Native C API for Developers

If you are writing built-in C functions, ALWAYS use the official APIs in `ti_type_value_dict.h` to interact with dictionaries. **Never interact with the raw `chashmap_t` directly.**

### Creating a Dictionary
```c
value_t *my_dict = val_new_dict();
```

### Setting a Value (Zero-Copy)
When inserting a value, the dictionary **takes ownership** of the memory pointer. Do NOT call `val_free()` on the value after passing it to `val_dict_set`.
```c
val_dict_set(my_dict->dict_val, "status", val_new_string("running"));
```

### Getting a Value (Deep Copy)
When retrieving a value, the dictionary returns a **deep copy**. The caller is responsible for freeing it when done.
```c
value_t *val = val_dict_get(my_dict->dict_val, "status");
// ... use val ...
val_free(val);
```

### Traversing Entries (Iteration)
To iterate over all key-value entries in a dictionary, define a callback matching `dict_foreach_cb` and pass it to `val_dict_foreach`. The traversal bridges the underlying generic hashmap payloads to strongly-typed `value_t *` pointers.

```c
static bool print_entry_cb(const char *key, value_t *value, void *user_context)
{
    // Process entry
    return true; // Return true to continue, false to break early
}

val_dict_foreach(my_dict->dict_val, print_entry_cb, NULL);
```
