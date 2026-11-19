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
* `VAL_NULL`: The script's `null` ("no value", e.g. a native that could not read its sensor); see 2.9. Internally it also marks "no type" (an unset `element_type`) and is the type of the `TI_VAL_OK` statement sentinel, which never reaches a script.
* `VAL_LIST`: Homogeneous, reference-counted array of elements (`list_t *`).
* `VAL_DICT`: String-keyed hash map (`dict_t *`).
* `VAL_HANDLE`: Opaque, reference-counted wrapper around a native C object (`handle_t *`). Scripts hold it and use the methods and attributes of its type; see 2.7.
* `VAL_MODULE`: A module bound by `import` (`ti_module_instance_t *module_val`, the runtime's instance of a native module table or of a script module; the instance lives until the runtime is destroyed, so copying or freeing the value does nothing else). See 2.8.
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
numbers[1] = 25;         // Subscript assignment (same type only, or null: see 2.9)
```
> **Note:** An element of the wrong type (`list int l = [1, "a"];`) is caught at runtime with `TI_ERR_TYPE_MISMATCH`, reported with the element index and the line.

#### Sized declaration: `list <elem_type> name[size];`
Creates `size` elements, each the default value of the element type (`0`, `0.0`, `false`, `""`, `{}`), like `int x;` does for one variable. Use it when the elements start empty; when the values are known, write a literal instead.
```c
list dict stations[config["station_count"]];  // N empty dicts
list int samples[8];                           // [0,0,0,0,0,0,0,0]

while (true) {
    list dict fresh[3];   // a new list every iteration: the body is a new scope
    dict first = fresh[0];
    first["id"] = 1;      // fills fresh[0] (a variable shares the element)
}
```
| Rule | Otherwise |
| :--- | :--- |
| Only for `list` | `int x[3];` is a parser error (`Only a list can be declared with a size`) |
| No initializer after it | `list int l[3] = [...];` is a parser error (`Expected token ';'`) |
| `size` is any `int` expression, evaluated at run time | Not an `int`: `TI_ERR_TYPE_MISMATCH` |
| `0 <= size <= TI_MAX_LIST_ITEMS` (1024) | `TI_ERR_VALUE_OUT_OF_RANGE` |
| The element type has a default | `list list l[2];` is `TI_ERR_TYPE_MISMATCH` (an inner list has no element type) |

Also allowed as a `for` init (`for (list int l[4]; ...; ...)`). The element storage is allocated once for all `size` elements.

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
Overwriting an existing key must keep its type (`device["id"] = "x";` is `TI_ERR_TYPE_MISMATCH`), except that any key may be set to `null`, after which it cannot be set again (2.9).

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
| `handle`, `module`, `func` | no JSON form: `to_string` of the container raises `TI_ERR_TYPE_MISMATCH` |
| `list` | array: `[...]` |
| `dict` | object: `{...}`, **keys sorted** alphabetically, so the same dict always gives the same text |
| empty list / dict | `[]` / `{}` |

Notes:
* The output is compact (no spaces or newlines), ready to publish over MQTT or HTTP.
* A float that is NaN or infinity has no JSON form and raises an error; the math built-ins never produce one.
* Because inserting copies (2.5), build the object **after** the values are final, or set the field again: changing `a` after `stations` was built does not change `payload`.

### 2.7 Native Objects (`handle`)
A `handle` holds a C object that has no TI type: a driver configuration, a socket, a sensor context. Natives create it; the script stores it and uses it through functions, **methods** and **attributes** that the handle's type provides:
```c
handle dev = device_open("/dev/ttyUSB0", 9600);   // native returns a handle of type "device_cfg"
device_set_baud(dev, 115200);                       // function style: the handle is an argument
int sample = dev.read();                            // method style: the handle is self
dev.baud = 57600;                                   // attribute write (the type's set_attr)
print(dev.port + " @ " + to_string(dev.baud));      // attribute read (the type's get_attr)
dev.baud += 100;                                    // read, add, write back
dev.close();
```
Full file: [`examples/ti/handle_device.ti`](../../examples/ti/handle_device.ti) (natives in `cli/builtin/builtin_device.c`).

Rules:
* **Type:** every handle has a type (`device_cfg`), defined by the native as one C descriptor. A native parameter declared with a type accepts only that type, so a UART config can never reach a Modbus function (`TI_ERR_TYPE_MISMATCH`, checked before the native runs). A script function takes a bare `handle` and accepts any type.
* **Methods:** `obj.name(args)` runs the method of the handle's type with `obj` as the first argument (self). Error messages count self: `dev.set_baud()` says `expects 2 argument(s), but received 1`. Any expression can be the receiver: `device_open("/dev/ttyS1", 4800).info()`.
* **Attributes:** `obj.name` and `obj.name = value` are answered by the type. An unknown name is `TI_ERR_UNDEFINED`, a fixed one (`dev.port = ...`) is `TI_ERR_READ_ONLY`, and the type checks the value (`dev.baud = "x"` is `TI_ERR_TYPE_MISMATCH`). `+=`, `-=`, ... work on attributes.
* **Always shared:** declaring from another variable, passing an argument, and even putting it in a list or dict share the **same** C object (a C object cannot be deep copied). A handle holds no TI values, so it can never create a reference cycle.
* **Lifetime:** the native object is destroyed when the last copy is released (end of scope, overwritten variable, freed container), or earlier when a native closes it (`dev.close()`). After a close every copy reads as closed: methods report an error and attributes raise `Cannot access attribute 'x': handle device_cfg is closed`, instead of crashing.
* **Empty handle:** `handle h;` starts empty (no object, no type). It cannot be passed to a typed parameter and has no methods or attributes. Assign a real one later: `h = device_open(...);`.
* **What a script can do with it:** assign, pass, store in a dict, call methods, use attributes, `==` / `!=` (same C object, or both empty), `to_string` / `print`. No arithmetic, no subscript, no JSON.

| Value | `to_string` / `print` |
| :--- | :--- |
| open handle | `<handle device_cfg>` |
| closed handle | `<handle device_cfg closed>` |
| empty handle (`handle h;`) | `<handle none>` |

### 2.8 Modules (`import`)
`import` binds a module to a variable. A module is one of two kinds; the script uses both the same way:

| Kind | Made of | Members |
| :--- | :--- | :--- |
| **Native module** | A table of C functions and constants registered by the host (for example `machine` in the CLI) | `m.f(args)` native functions, `m.NAME` constants |
| **Script module** | Another `.ti` file, provided by the host's module loader | `m.f(args)` its top-level functions, `m.name` its top-level variables |

When both exist for a name, the native module wins.

#### 2.8.1 Syntax
```c
import machine;                        // native module, variable 'machine'
import machine as m;                   // same module under another name
import config;                         // script module, variable 'config'
import sensors/temperature;            // variable 'temperature' (the last part)
import ../shared/math as m2;           // variable 'm2'
```
* The module name is a path written without quotes and without the file extension: `('./' | '../')* name ('/' name)*`. `./` and `../` may only come first; every `name` is an identifier.
* Any other token is a syntax error: no strings (`import "lib/a"`), no `.` inside a name (`import lib.a;`), no empty part (`import lib/;`), no `...` or `..` without `/`, no `..` in the middle (`import lib/../a;`), no number (`import lib/2;`). Spaces between the tokens are allowed by the lexer (`lib / a` is `lib/a`); `//` starts a comment.
* Without `as`, the variable is named after the **last** part (`../shared/math` binds `math`).
* The library gives the name no meaning: the host's loader (2.8.3) turns it into a file. The CLI reads it as a path relative to the **file that has the `import`** (main or a module), plus `.ti`.

#### 2.8.2 Using a module
```c
import machine;
handle u = machine.UART(1, 9600, machine.SERIAL_8N1);   // native function, native constant

import util/counter as c;
c.increase();                          // runs inside util/counter.ti: it changes that file's 'count'
int n = c.count;                       // reads a top-level variable of the module: a copy
```
Full example: [`examples/ti/project_1/main.ti`](../../examples/ti/project_1/main.ti) (script modules); a native module: `cli/module/module_machine.c`.

Rules:
* **Scope:** `import` declares a variable in the current scope. A top-level import is visible inside functions; an import inside a function or block is local to it. Importing the same name twice in one scope is an error.
* **Functions:** `m.f(args)` has no self; count and types are checked like any call. A script module's function runs in its own module: it sees the module's variables and calls the module's other functions, never main's (main and a module may both define `count` or `helper()` without clashing).
* **Constants and variables:** `m.NAME` gives a copy. Changing the copy never changes the module. A member cannot be assigned from outside: `machine.SERIAL_8N1 = 5;` and `c.count = 5;` raise `TI_ERR_READ_ONLY`. A script module changes its own variables through its functions (`c.increase()`).
* **One module per file per runtime:** the first import of a file loads it; every later import of that file, anywhere in the main script or in another module and under any name, gives the **same** module (`==` is true). A script module's top level runs once, on its first import. Identity is the path the loader returns: in the CLI, `sensors/temperature.ti` writes `import calibration;` and main writes `import sensors/calibration as ca;`, and both get the same module with one set of variables.
* **Modules can import modules.** A circular import (`a` imports `b`, `b` imports `a`) does not loop: the second import gets the module that is still running its top level, so variables defined after that point are not there yet.
* **What a script can do with a module:** call its functions, read its members, store it in a variable or a dict, `==` / `!=` (same module). There is no `module` type keyword, so it cannot be declared as a typed variable (`int x = machine;` is a type error). It has no text form: `to_string(machine)` raises `TI_ERR_TYPE_MISMATCH`, and it has no JSON form.

Errors:

| Error | Example message |
| :--- | :--- |
| No module with that name | `Unknown module 'util/missing'` |
| The loader failed for another reason | `Cannot load module 'x': <reason>` |
| The module's file does not compile | `Cannot import module 'lib/count': syntax error` (after the parser's own message, which names the module's file and line) |
| An error in the module's top level | That error, with the module's file and line |
| Unknown member | `Module machine has no function 'x'`, `Module machine has no constant 'x'`, `Module util/counter has no variable 'x'` |

A runtime error names the file whose code raised it: `[Runtime Error] Division by zero error at /home/me/project/sensors/calibration.ti:4` (the CLI's loader returns absolute paths; only the line when the script has no path).

#### 2.8.3 Where script modules come from
The library does not read files. The host registers a **module loader** callback that turns a module name into source text and a path; without one, only native modules exist. The loader gets the path of the file that has the `import`; for the main script that is the path the host gave when compiling it (`ti_compile(source, path)`, `ti_run_string(source, path)` or `ti_run_string_take(source, path)`). Writing a loader: [Functions & Native C Extension API](FUNCTIONS_AND_NATIVE_API.md), section 4.7.

Writing a native module in C: [Functions & Native C Extension API](FUNCTIONS_AND_NATIVE_API.md), section 4.6.

### 2.9 `null`
`null` is a value meaning "no value". Its main use is a native that returns its declared type, or `null` when it has nothing to give (a sensor that did not answer), so the script decides what to do instead of stopping on an error:
```c
int t = sensor_read();        // int, or null
if (t == null) {
    print("sensor offline");
} else {
    total = total + t;
}
```

**Comparing.** `==` and `!=` accept `null` on either side with **any** type, and `null` is equal only to `null`: `5 == null` is `false`, `5 != null` is `true`, `null == null` is `true`. An empty handle (`handle h;`), `0`, `""`, `[]` and `{}` are **not** `null`.

**Every other operator rejects it** with `TI_ERR_TYPE_MISMATCH`: `5 + null`, `null < null`, `null && true`, `x & null`, `!null`, `-null`. There is no truthiness: test `x == null`, never `!x`. A condition (`if`, `while`) must still be a `bool`.

**Storing it: one rule for variables, list elements and dict entries.** A slot that holds a value of type T accepts a value of type T or `null`; a slot that holds `null` accepts **nothing** more:

| Script | Result |
| :--- | :--- |
| `int a = null;` | ok: any declared type accepts `null` |
| `int a = 5;  a = null;` | ok: the `5` is freed, `a` holds `null` |
| `a = 7;` or `a = null;` while `a` holds `null` | `TI_ERR_TYPE_MISMATCH` (`Variable 'a' is null and cannot be assigned`) |
| `a += 1;` while `a` holds `null` | `TI_ERR_TYPE_MISMATCH` (from `a + 1`) |
| `l[0] = null;` (`list int l`) | ok; `l[0] = 2;` afterwards is `TI_ERR_TYPE_MISMATCH` |
| `d["k"] = null;` | ok for a new or an existing key; `d["k"] = 2;` afterwards is `TI_ERR_TYPE_MISMATCH` |
| `list int l = [1, null];`, `to_string([null, 1])` | `TI_ERR_TYPE_MISMATCH`: a literal (and a push) never takes `null`; a literal without a declared type cannot take its element type from a `null` first element |
| `int x = some_void_native();` | `TI_ERR_TYPE_MISMATCH` (`void` is not `null`) |

A list whose elements were set to `null` is copied with them (`[[null,2]]` when inserted into another container).

**Text.** `to_string(null)`, `print(null)` and JSON give `null`: `[null,2,30]`, `{"k":null}`.

**Function parameters and returns of script functions** do not accept `null` (`void f(int x)` called with `null` is a type error; an `int` function cannot `return null;`).

**C side.**
* A native returns `val_new_null()` for "no value"; declare the real return type (`VAL_INT`, ...), not `VAL_ANY`: the runtime does not check a native's return type, and the table then tells the reader what the function gives. Use `ti_raise_error` instead when the script must not continue. A `VAL_VOID` native returns `val_new_void()`, never `val_new_null()`.
* The storing rule is `val_can_replace(current, incoming)` (`ti_type_value.h`): `false` when `current` is `VAL_NULL`, otherwise `incoming == VAL_NULL || incoming == current`. `eval_variable_definition` (with the declared type as `current`), `eval_assignment`, `val_list_set` (with the element held now) and `val_dict_set` / `val_dict_set_raw` (existing key) all use it. `val_list_push_raw` also takes `null`, because `val_deep_copy` uses it to copy lists that hold `null`; `val_list_push` does not.

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
| `void` | `val_new_void()` | Allocates `VAL_VOID`: what every native declared `VAL_VOID` returns |
| `null` | `val_new_null()` | Allocates `VAL_NULL`: the script's `null` (never the result of a void native) |
| `dict` | `val_new_dict()` | Allocates empty `VAL_DICT` with refcount = 1 |
| `list` | `val_new_list(val_type_t elem_type)` | Allocates empty `VAL_LIST` of specified element type |
| `handle` | `val_new_handle(void *ptr, const ti_handle_type_t *type)` | Wraps a native object (`src/include/ti_type_value_handle.h`); see [Functions & Native C Extension API](FUNCTIONS_AND_NATIVE_API.md), section 4.5 |

---

## 4. Manipulating Collections in C

### 4.1 Working with Lists (`src/include/ti_type_value_list.h`)
```c
/* Create a list of integers */
value_t *list_val = val_new_list(VAL_INT);
list_t *list = list_val->list_val;

/* Optional: when the count is known, allocate the element storage once
 * (otherwise it starts at 4 slots and doubles with realloc when full) */
val_list_reserve(list, 2);   // TI_OK, TI_ERR_LIMIT_EXCEEDED (> TI_MAX_LIST_ITEMS), TI_ERR_NO_MEMORY

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
   - A handle is never copied: the container shares it (refcount), like every other copy of a handle.
   - `val_dict_set_raw` and `val_list_push_raw` store `val` as it is, without the copy. They exist for `val_deep_copy`, which already built a fresh tree; natives should use the normal functions.
   - **Rule:** Do NOT call `val_free(val)` after passing it into `val_dict_set` or `val_list_push`.

3. **Reading from Containers (`val_dict_get` and `val_list_get`):**
   - Both functions populate their `value_t **out` parameter with an independent **deep copy** of the stored value.
   - **Rule:** You MUST call `val_free(*out)` when you are finished processing the extracted value in C.
