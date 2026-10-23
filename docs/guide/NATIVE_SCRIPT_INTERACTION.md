# Developer Guide: How Native C and TI Scripts Interact

> **Audience:** Developers who write native C functions or drivers (GPIO, Modbus, MQTT, HTTP...) and the TI scripts that use them. Read this first: it is the map. The other guides go deeper on each part.

---

## 1. The Whole Picture

Two sides talk to each other. Every channel is one of the rows below; pick the row that matches what you need.

| I want to... | Direction | Mechanism | Section |
| :--- | :--- | :--- | :--- |
| Run C code from a script | script → native | Call a registered native function | 2 |
| Send data into C | script → native | Arguments (`value_t **args`) | 2 |
| Get data back from C | native → script | Return value (`value_t *`) | 3 |
| Tell the script "this call failed" | native → script | `ti_raise_error` + `return NULL` | 4 |
| Let a native run a script function | native → script | Function-name argument + `ti_call` / `ti_post_event` | 5 |
| Push an external event into the script (fire and forget) | native → script | `ti_post_event` | 5.1 |
| Ask the script a question and wait for the answer | native → script | `ti_call` | 5.2 |
| Wait inside a native without freezing the script's events | native | `ti_wait` | 6 |

What the two sides **cannot** do:

* A native cannot read or write script variables directly. Data crosses only through arguments, return values, and event/callback arguments. This keeps ownership simple.
* A script cannot catch an error. A raised error ends the script (see section 4.3). A callback error is the only one that does not (section 5.3).
* Native code never receives a `ti_runtime_t *`, only an opaque `ti_handle_t`.

---

## 2. Script Calls a Native (Arguments)

The native is registered once with a signature the runtime enforces:

```c
static param_t relay_params[] = {
    { VAL_INT,  "id",    VAL_NULL },
    { VAL_BOOL, "state", VAL_NULL },
};
register_builtin_function("relay_set", VAL_VOID, relay_params, 2, native_relay_set);
```

Before your C function runs, the runtime already checked, for every call:

| Check | Done by runtime? | Error raised |
| :--- | :--- | :--- |
| Argument **count** equals `param_count` | Yes, unless `param_count == -1` | `TI_ERR_INVALID_ARG` |
| Argument **type** equals `params[i].type` | Yes, unless that parameter is `VAL_ANY` (or the function is variadic) | `TI_ERR_TYPE_MISMATCH` |
| `list` element type equals `params[i].element_type` | Yes, when `element_type` is not `VAL_NULL` | `TI_ERR_TYPE_MISMATCH` |
| **Value** is acceptable (range, length, protocol rule) | **No, this is your job** | you raise it (section 4) |

Choosing the signature:

| Situation | Declare |
| :--- | :--- |
| Fixed count, known types | `param_count = N` with exact types |
| Fixed count, one parameter accepts several types | `VAL_ANY` for that parameter, then check `args[i]->type` yourself and raise `TI_ERR_TYPE_MISMATCH` for the types you refuse |
| Count is open (`print`) or an argument is optional (`to_int(value [, base])`) | `param_count = -1` (nothing is checked, check `argc` and types yourself) |

Arguments are **borrowed**: they are valid until your function returns. Never free them, and copy anything you need to keep (`strdup`, `val_copy`).

### Script-side view

```c
relay_set(1, true);       // OK
relay_set("1", true);     // TI_ERR_TYPE_MISMATCH, raised by the runtime
relay_set(1);             // TI_ERR_INVALID_ARG, raised by the runtime
relay_set(99, true);      // OK for the runtime; your native decides if 99 is a valid relay
```

---

## 3. Native Answers the Script (Return Values)

Return a freshly allocated `value_t *`; the interpreter owns it from then on:

```c
return val_new_int(42);            // script: int x = my_func();
return val_new_void();             // void function
return val_new_string("ok");
```

Rules:

* Return the type you declared as `return_type`. The runtime does **not** re-check a native's return type, so a mismatch will surface later as a confusing error in the script.
* Never `val_free` what you return. Never return a pointer you got from `args`.
* To return several fields use a `dict`, a `list` or `bytes`. Do **not** use this to report failure (no `{"ok": false, "code": 3}`): use an error (section 4) so the script cannot forget to check.
* Returning `NULL` means **"I failed"**, and only after `ti_raise_error` (section 4.1). Never return `NULL` without raising: the runtime has no error to report.

**Binary links (serial, Modbus...):** return the received frame as one `bytes` value (`val_new_bytes(buf, len)`) instead of writing a native per data type. The script then converts it with its own tools: `b[i]` and slices for fields, `bytes_to_string(b)` for text, `to_string(b)` for a hex dump, and `string_to_bytes(s)` / `x"..."` for the frame to send back.

For the full list of constructors and the list/dict/bytes helpers see [Types, Values & Memory Ownership](TYPES_AND_VALUES.md).

---

## 4. Native Reports an Error to the Script

### 4.1 The API

```c
void ti_raise_error(ti_handle_t handle, ti_status_t kind, const char *fmt, ...);
```

The pattern is always the same two steps, in this order:

```c
if (slave < 1 || slave > MODBUS_MAX_SLAVE) {
    ti_raise_error(handle, TI_ERR_VALUE_OUT_OF_RANGE,
                   "modbus_read_request: slave must be 1..%d, got %d", MODBUS_MAX_SLAVE, slave);
    return NULL;                     /* mandatory: the raise does not jump out of your function */
}
```

* `ti_raise_error` does **not** return control to the interpreter by itself (no `longjmp`). It records the error; your `return NULL` is what unwinds.
* Free every temporary you allocated **before** `return NULL` (the interpreter frees only the arguments and values it owns).
* The message is formatted with `printf` rules into a fixed buffer (`TI_ERROR_MESSAGE_SIZE`) and truncated if longer. Put the function name first so the log is searchable.
* The error line is the line of the **call in the script**. You do not pass one.
* Only the **first** error of a run is kept. If you raise twice, the second is ignored.

### 4.2 Choosing `kind`

| Situation | `kind` |
| :--- | :--- |
| Argument has the right type but a value outside the allowed range (slave id, address, count, shift) | `TI_ERR_VALUE_OUT_OF_RANGE` |
| `VAL_ANY` parameter received a type you do not support | `TI_ERR_TYPE_MISMATCH` |
| Argument is empty, malformed, or otherwise unusable (`NULL` name, empty string) | `TI_ERR_INVALID_ARG` |
| `val_new_*` or `malloc` returned `NULL` | `TI_ERR_NO_MEMORY` |
| A script-visible name does not exist (unknown device, unknown function name) | `TI_ERR_UNDEFINED` |
| A dict key you require is missing | `TI_ERR_KEY_NOT_FOUND` |
| Hardware or I/O failure (bus timeout, device NACK, HTTP failure) | `TI_ERR_RUNTIME` |
| A configured limit was reached | `TI_ERR_LIMIT_EXCEEDED` |

### 4.3 What the script sees

A raised error **ends the script**. The interpreter unwinds every call frame, frees all temporaries, and `ti_execute` finishes with the error: it logs the message and line, then calls the host's fatal handler (`ti_register_fatal`; `exit(1)` on desktop by default). There is no `try` in TI.

Therefore raise only for a real failure the script cannot continue after:

* **Raise** when the **caller made a mistake** (bad type, out-of-range value) or the **system failed** (no memory, hardware dead).
* **Do not raise** for an expected outcome ("no data yet", "not found, which is fine"). Return a value the script can test instead, for example an empty `list`/`bytes`, `-1`, or a `bool`.

```c
/* Expected "miss": return a value */
if (!found) return val_new_bool(false);

/* Caller bug: raise */
ti_raise_error(handle, TI_ERR_INVALID_ARG, "storage_get: key must not be empty");
return NULL;
```

### 4.4 A complete example with `VAL_ANY`

```c
/* log_value(string label, any value): value must be int or string */
static value_t *native_log_value(ti_handle_t handle, value_t **args, int argc)
{
    (void)argc;                                   /* count is guaranteed by the runtime */

    if (args[1]->type != VAL_INT && args[1]->type != VAL_STRING) {
        ti_raise_error(handle, TI_ERR_TYPE_MISMATCH,
                       "log_value: 'value' must be int or string, got %s",
                       val_type_to_str(args[1]->type));
        return NULL;
    }

    printf("%s = ", args[0]->string_val);
    if (args[1]->type == VAL_INT) printf("%d\n", args[1]->int_val);
    else                          printf("%s\n", args[1]->string_val);
    return val_new_void();
}
```

Script: `log_value("n", 5);` prints `n = 5`; `log_value("f", 1.5);` stops the script with `log_value: 'value' must be int or string, got float` at that line.

---

## 5. Native Runs a Script Function (Events and Callbacks)

A native reaches back into the script by **function name**. The script hands over the name by registering it (`register_event(on_msg)` / `register_callback(on_query)`), and the native stores the name and the runtime handle. Two ways to call, chosen by whether you need the answer:

| | Async event | Sync callback |
| :--- | :--- | :--- |
| Native API | `ti_post_event(handle, name, args, argc)` | `ti_call(handle, name, args, argc, &result, timeout_ms)` |
| Caller waits? | No (queued) | Yes (until the function returns) |
| Gets a return value? | No | Yes, in `*result` |
| Typical caller | MQTT/HTTP handler, timer task, driver task | Driver that needs a decision (auth check, config query) |
| ISR-safe? | No (allocates): from an ISR, notify a task and let the task post | No |

Full rules, timing diagrams and thread-safety are in [Asynchronous Events & Synchronous Callbacks](ASYNC_EVENTS_AND_CALLBACKS.md). The points that matter for interaction:

### 5.1 Data going into the script

Build arguments with the usual `val_new_*` constructors. Ownership differs:

* `ti_post_event`: the call **takes ownership** of every argument value, even when it fails. Do not free them afterward.
* `ti_call`: arguments are **borrowed**. You keep ownership and free them after the call returns.

### 5.2 Data coming back (`ti_call` only)

`*result` is **owned by the caller**: `val_free` it when done. It is `NULL` for a `void` function.

### 5.3 Errors inside the script function

A callback runs isolated, like a `try/except` around the call:

| Where the error happens | Effect |
| :--- | :--- |
| Inside an event handler | Logged as `[Event Error] ... at line N`; the script keeps running |
| Inside a `ti_call` callback | Returned to the native as the `ti_status_t` code (the message text is not returned); runtime status is reset to OK and the script keeps running |
| A `ti_stop`/destroy while waiting | `TI_ERR_INTERRUPTED`; this is never swallowed |

So a native using `ti_call` must **test the return code**:

```c
value_t *answer = NULL;
ti_status_t st = ti_call(handle, "on_auth", args, 1, &answer, 500);
if (st != TI_OK) {
    /* TI_ERR_TIMEOUT, TI_ERR_UNDEFINED, TI_ERR_STALE_HANDLE, TI_ERR_INTERRUPTED,
     * or the error kind the script function raised */
}
```

A native must also **not** hold its own mutex while it calls `ti_call` (see lock rules in the async guide).

---

## 6. Waiting Inside a Native

A native runs while holding the runtime's execution lock, so a plain `sleep()` freezes events and `ti_call` callers. Use:

```c
ti_wait(handle, ms);      /* releases the lock, runs queued events/callbacks, then keeps waiting for the rest of ms */
```

It returns early only if the script is stopped or the runtime destroyed. Check its return value if you must stop your own work on interruption.

---

## 7. Putting It Together: One Request, Both Directions

A driver that reads a sensor, asks the script whether to alert, and reports bad configuration:

```c
/* script: register_callback(should_alert);  alert_on(80); */
static value_t *native_alert_on(ti_handle_t handle, value_t **args, int argc)
{
    (void)argc;
    int threshold = args[0]->int_val;
    if (threshold < 0 || threshold > 100) {                    /* 4: caller mistake -> raise */
        ti_raise_error(handle, TI_ERR_VALUE_OUT_OF_RANGE,
                       "alert_on: threshold must be 0..100, got %d", threshold);
        return NULL;
    }
    s_threshold = threshold;
    s_handle = handle;                                         /* 5: remember how to reach back */
    return val_new_void();
}

/* Called from a driver context when a reading arrives */
void on_reading(int value)
{
    value_t *arg = val_new_int(value);   /* ti_call borrows it, so we free it below */
    value_t *answer = NULL;
    if (arg != NULL &&
        ti_call(s_handle, "should_alert", &arg, 1, &answer, 200) == TI_OK &&   /* 5.2: sync question */
        answer != NULL && answer->type == VAL_BOOL && answer->bool_val) {
        trigger_alarm();
    }
    val_free(answer);
    val_free(arg);
}
```

Examples to run: `examples/ti/async_event.ti`, `examples/ti/sync_callback.ti`; CLI natives that raise errors: `cli/builtin/builtin_bytes.c` (`modbus_read_request`), `cli/builtin/builtin_callback.c`.

---

## 8. Checklist for a New Native Function

1. Declare exact `param_t` types; use `VAL_ANY` for open types and `-1` only for open counts.
2. Validate **values** (ranges, lengths, protocol rules); raise with the right `kind`.
3. Every error path: free temporaries, `ti_raise_error`, `return NULL`.
4. Expected non-errors (miss, empty) return values, not errors.
5. Return the declared type, freshly allocated; never free what you return.
6. Keep it short; wait with `ti_wait`, never `sleep`.
7. Reach back into the script only with `ti_post_event` (no answer needed) or `ti_call` (answer needed), never while holding your own lock.
