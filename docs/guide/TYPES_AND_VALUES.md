# Developer Guide: Types, Values & Memory Ownership

> **Audience:** Developers writing TI scripts and C native built-in functions interacting with values, lists, and dictionaries.

---

## 1. Overview & Type System

The `TienInterpreter` features a **static, strongly typed** value system. Every runtime value is encapsulated in a tagged union structure: `value_t` (defined in `src/include/ti_type_value.h`).

### Supported Type Enumeration (`val_type_t`)
Defined in `src/include/ti_type.h`:
* `VAL_INT`: Signed 32-bit integer (`int`).
* `VAL_FLOAT`: Single-precision floating point (`float`).
* `VAL_STRING`: Null-terminated dynamically allocated string (`char *`).
* `VAL_BOOL`: Boolean (`bool`: `true` or `false`).
* `VAL_VOID`: Void sentinel, returned by void functions.
* `VAL_NULL`: Null sentinel, indicates no payload or uninitialized value.
* `VAL_LIST`: Homogeneous, reference-counted array of elements (`list_t *`).
* `VAL_DICT`: String-keyed hash map (`dict_t *`).

---

## 2. Using Types in TI Scripts

### 2.1 Primitive Types
```c
int count = 100;
float temperature = 26.5;
string topic = "sensors/temperature";
bool is_online = true;
```

### 2.2 Homogeneous Lists (`list <elem_type>`)
Lists in TI are **strictly homogeneous** (every element must share the same type):
```c
/* Declaration & Literal initialization */
list int numbers = [10, 20, 30];
list string names = ["Alice", "Bob"];

/* Methods */
numbers.push(40);        // Appends 40 to the end
int first = numbers[0];  // Index access (0-based)
numbers[1] = 25;         // Subscript assignment
int total = numbers.len(); // Returns element count
```
> **Note:** Type mismatches such as `numbers.push("hello")` or `list string s = numbers;` are caught at runtime with `TI_ERR_TYPE_MISMATCH`.

### 2.3 Dictionaries (`dict`)
Dictionaries store key-value pairs where keys are always strings:
```c
/* Declaration & Literal initialization */
dict device = {
    "id": 101,
    "name": "Relay_1",
    "active": true
};

/* Read and Update */
string dev_name = device["name"];
device["status"] = "OK";
```

---

## 3. C Native API: Interacting with Values

When implementing native C functions (`native_fn_t`), arguments are received as an array of `value_t **argv`, and the function returns a `value_t *`.

### 3.1 Extracting Values from Arguments
```c
/* Inspect argument type before accessing union payload */
if (argv[0]->type == VAL_INT) {
    int val = argv[0]->int_val;
} else if (argv[0]->type == VAL_FLOAT) {
    float val = argv[0]->float_val;
} else if (argv[0]->type == VAL_STRING) {
    const char *str = argv[0]->string_val;
} else if (argv[0]->type == VAL_BOOL) {
    bool flag = argv[0]->bool_val;
} else if (argv[0]->type == VAL_LIST) {
    list_t *list = argv[0]->list_val;
} else if (argv[0]->type == VAL_DICT) {
    dict_t *dict = argv[0]->dict_val;
}
```

### 3.2 Creating Return Values
Always use the dedicated constructors in `src/include/ti_type_value.h`:

| Type | C Constructor | Description |
| :--- | :--- | :--- |
| `int` | `val_new_int(int v)` | Allocates `VAL_INT` |
| `float` | `val_new_float(float v)` | Allocates `VAL_FLOAT` |
| `string` | `val_new_string(const char *s)` | Allocates `VAL_STRING` (duplicates string internally) |
| `bool` | `val_new_bool(bool b)` | Allocates `VAL_BOOL` |
| `void` | `val_new_void()` | Allocates `VAL_VOID` (used for void builtins) |
| `null` | `val_new_null()` | Allocates `VAL_NULL` |
| `dict` | `val_new_dict()` | Allocates empty `VAL_DICT` with refcount = 1 |
| `list` | `val_new_list(val_type_t elem_type)` | Allocates empty `VAL_LIST` of specified element type |

---

## 4. Manipulating Collections in C

### 4.1 Working with Lists (`src/include/ti_type_value_list.h`)
```c
/* Create a list of integers */
value_t *list_val = val_new_list(VAL_INT);
list_t *list = list_val->list_val;

/* Append elements (Ownership of val_new_int is transferred to list) */
val_list_push(list, val_new_int(10));
val_list_push(list, val_new_int(20));

/* Query count */
int count = val_list_count(list);

/* Read an element (Returns a deep copy into out) */
value_t *elem = NULL;
ti_status_t st = val_list_get(list, 0, &elem);
if (st == TI_OK) {
    printf("First element: %d\n", elem->int_val);
    val_free(elem); // Free the retrieved copy when done
}

return list_val; // Return list to script
```

### 4.2 Working with Dictionaries (`src/include/ti_type_value_dict.h`)
```c
/* Create a new dictionary */
value_t *dict_val = val_new_dict();
dict_t *dict = dict_val->dict_val;

/* Insert key-value pairs (Ownership of values is transferred to dict) */
val_dict_set(dict, "ip", val_new_string("192.168.1.50"));
val_dict_set(dict, "port", val_new_int(1883));
val_dict_set(dict, "enabled", val_new_bool(true));

/* Read a key (Returns a deep copy into out) */
value_t *ip_val = NULL;
if (val_dict_get(dict, "ip", &ip_val) == TI_OK) {
    printf("IP is: %s\n", ip_val->string_val);
    val_free(ip_val); // Free the retrieved copy
}

return dict_val; // Return dictionary to script
```

---

## 5. Memory Ownership Rules (Strict Invariants)

1. **Returning Values from C Native Functions:**
   - Any `value_t *` returned by a native function transfers ownership to the interpreter.
   - If the script assigns the result to a variable (`int x = my_func();`), the variable owns it.
   - If the script ignores the return value (`my_func();`), the interpreter's compound statement evaluator automatically calls `val_free(result)` immediately at the statement boundary.
   - **Rule:** Never call `val_free()` on the pointer you return from a native function.

2. **Inserting into Containers (`val_dict_set` and `val_list_push`):**
   - Both functions take ownership of the inserted `value_t *val`.
   - On success, the container retains the value.
   - On failure (e.g. type mismatch or out of memory), the function automatically calls `val_free(val)` to prevent memory leaks.
   - **Rule:** Do NOT call `val_free(val)` after passing it into `val_dict_set` or `val_list_push`.

3. **Reading from Containers (`val_dict_get` and `val_list_get`):**
   - Both functions populate their `value_t **out` parameter with an independent **deep copy** of the stored value.
   - **Rule:** You MUST call `val_free(*out)` when you are finished processing the extracted value in C.
