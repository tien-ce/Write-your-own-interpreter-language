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
* `VAL_ANY`: **Not a value type.** A wildcard used only in the parameter table of a native C function (`param_t.type`) meaning "any type is accepted here". No `value_t` ever carries it (`val_init(VAL_ANY)` returns `NULL`), scripts cannot declare it, and `val_type_to_str` prints it as `any`. See [Functions & Native C Extension API](FUNCTIONS_AND_NATIVE_API.md), section 4.3.

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
Lists in TI are **strictly homogeneous** (every element must share the same type). The element type is `int`, `float`, `string`, `bool`, `dict` or `list`:
```c
/* Declaration & Literal initialization: elements may be any expression */
int base = 10;
list int numbers = [base, base * 2, 30];
list string names = ["Alice", "Bob"];
list int empty = [];

int first = numbers[0];  // Index access (0-based)
numbers[1] = 25;         // Subscript assignment (same type only)
```
> **Note:** An element of the wrong type (`list int l = [1, "a"];`) is caught at runtime with `TI_ERR_TYPE_MISMATCH`, reported with the element index and the line.

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
Overwriting an existing key must keep its type (`device["id"] = "x";` is `TI_ERR_TYPE_MISMATCH`).

### 2.4 Nesting Containers
A list can hold dictionaries or lists, and a dictionary value can be a list or a dictionary:
```c
dict a = {"key": "value"};
list dict stations = [a, {"name": "tien"}];          // list of dict
dict payload = {"stations": stations, "count": 2};    // dict holding a list
list list grid = [[1, 2], [3, 4]];                   // list of lists

list int row = grid[1];                               // [3,4]
```
* `list dict` requires every element to be a `dict`; `list list` requires every element to be a `list` (the inner lists may have different element types).
* A dictionary does not check what its values are, only that an overwritten key keeps its type.
* **Depth limit:** at most `TI_MAX_CONTAINER_DEPTH` (4) container levels, counted from the outermost one. Going deeper raises `TI_ERR_LIMIT_EXCEEDED`:
  ```c
  dict l1 = {"x": 1};
  dict l2 = {"l": l1};
  dict l3 = {"l": l2};
  dict l4 = {"l": l3};     // 4 levels: OK
  dict l5 = {"l": l4};     // 5 levels: runtime error, limit exceeded
  ```
* Subscripts cannot be chained: `b["cfg"]["a"] = 5;` and `ld[0]["k"]` are parser errors. Take the inner container out first (see 2.5).

### 2.5 Copy Rules: Insert Copies, Variables Share
**Putting a container into another container stores a deep copy** (a snapshot of the whole tree). This happens in a list or dict literal, in `list[i] = value` and in `dict[key] = value`. Changing the original afterwards does not change the copy:
```c
dict a = {"key": "vala"};
list dict ld = [a];        // ld[0] is a copy of a
a["key"] = "changed";
print(to_string(ld));      // [{"key":"vala"}]

ld[0] = a;                 // copied again: ld[0] is {"key":"changed"}
```
Because every inserted value is a fresh tree that shares nothing with its container, a container can never end up inside itself, so reference counting never leaks a cycle:
```c
dict cfg = {"a": 1};
cfg["self"] = cfg;         // inserts a snapshot of cfg
print(to_string(cfg));     // {"a":1,"self":{"a":1}}
```

**Everything else shares the same container** (no copy): declaring a variable from another, plain assignment, passing an argument, and reading an element with a subscript. A change through either name is seen through both:
```c
dict p = {"n": 1};
dict q = p;                // q and p are the same dict
q["n"] = 2;                // p["n"] is 2 too

void reset(dict d) { d["n"] = 0; }
reset(p);                  // p["n"] is 0: the function changed the caller's dict

list dict ld = [{"key": "v"}];
dict item = ld[0];         // item is the element inside ld
item["key"] = "x";         // ld is now [{"key":"x"}]
```
Use this to edit a nested element in place (chained subscripts are not supported):
```c
dict cfg = payload["config"];   // shared with payload
cfg["retry"] = 3;               // payload["config"]["retry"] is now 3
```
To get an independent copy of a container, put it through a literal: `dict copy = {"v": p};` then use `copy["v"]`.

### 2.6 Building JSON with `to_string`
`to_string` of a list gives a **JSON array**, `to_string` of a dict gives a **JSON object**, and nested containers give nested JSON. Scalars on their own are written plainly (`to_string("hi")` is `hi`, `to_string(3.0)` is `3.00`); inside a container they follow JSON rules.

```c
list int nums = [1, 2, 3];
print(to_string(nums));          // [1,2,3]

dict d = {"id": 7, "name": "relay", "on": true, "t": 26.5};
print(to_string(d));             // {"id":7,"name":"relay","on":true,"t":26.5}

dict a = {"key": "value"};
list dict stations = [a, {"name": "tien"}];
dict payload = {"stations": stations, "count": 2};
print(to_string(payload));       // {"count":2,"stations":[{"key":"value"},{"name":"tien"}]}

list list grid = [[1, 2], [3, 4]];
print(to_string(grid));          // [[1,2],[3,4]]
```

| TI value inside a container | JSON output |
| :--- | :--- |
| `int` | number: `7` |
| `float` | number with up to 7 significant digits, always marked as a float: `26.5`, `3.0`, `3.141593` |
| `bool` | `true` / `false` |
| `string` | quoted and escaped: `"he said \"hi\""`, `"line\nnext"` (control characters as `\n`, `\t`, `\uXXXX`) |
| `bytes` | quoted hex string: `"4142"` |
| `list` | array: `[...]` |
| `dict` | object: `{...}`, **keys sorted** alphabetically, so the same dict always gives the same text |
| empty list / dict | `[]` / `{}` |

Notes:
* The output is compact (no spaces or newlines), ready to publish over MQTT or HTTP.
* A float that is NaN or infinity has no JSON form and raises an error; the math built-ins never produce one.
* Because inserting copies (2.5), build the object **after** the values are final, or set the field again: changing `a` after `stations` was built does not change `payload`.

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
   - If `val` is a list or dict, the container stores a **deep copy** of it (up to `TI_MAX_CONTAINER_DEPTH` levels) and frees `val`. A tree that is too deep fails with `TI_ERR_LIMIT_EXCEEDED`. This is what keeps containers free of reference cycles (see 2.5).
   - `val_dict_set_raw` and `val_list_push_raw` store `val` as it is, without the copy. They exist for `val_deep_copy`, which already built a fresh tree; natives should use the normal functions.
   - **Rule:** Do NOT call `val_free(val)` after passing it into `val_dict_set` or `val_list_push`.

3. **Reading from Containers (`val_dict_get` and `val_list_get`):**
   - Both functions populate their `value_t **out` parameter with an independent **deep copy** of the stored value.
   - **Rule:** You MUST call `val_free(*out)` when you are finished processing the extracted value in C.
