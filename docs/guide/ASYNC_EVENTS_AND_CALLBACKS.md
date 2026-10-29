# Developer Guide: Asynchronous Events & Synchronous Callbacks

> **Audience:** Developers connecting drivers that run on their own thread or task (MQTT, Web server, BLE, GPIO interrupts, timers) to TI scripts.

A driver can reach a script function in two ways:

| | **Asynchronous event** | **Synchronous callback** |
|---|---|---|
| API | `ti_post_event(handle, name, args, argc)` | `ti_call(handle, name, args, argc, &result, timeout_ms)` |
| Caller waits? | No. Returns as soon as the event is queued | Yes. Returns after the script function finished |
| Gets a return value? | No | Yes (`value_t *result`) |
| Gets the script's error? | No (logged by the runtime) | Yes (the error kind is the return status) |
| Runs on | The script's own context, at its next safe point | The **caller's** context, while the script is paused |
| Order | FIFO, one at a time | One caller at a time (first to take the lock) |
| Callable from an ISR | Not today (it allocates) | No |
| Queue limit | `TI_MAX_PENDING_EVENTS`, then `TI_ERR_QUEUE_FULL` | None (callers wait, up to `timeout_ms`) |
| Script registers with | `register_event(handler)` | `register_callback(handler)` |

Both run the script function **to completion**: a handler is never interrupted by another handler, and the main script never runs in the middle of a handler.

---

## 1. When to use which

**Use an asynchronous event when the driver only reports that something happened:**
* An MQTT message, a sensor reading, a button press, a periodic timer tick.
* The driver must not wait (it is in a receive loop, a high-priority task, or cannot afford a block).
* Many events may arrive in a burst. They are queued in order, and `TI_ERR_QUEUE_FULL` tells you when to drop.

**Use a synchronous callback when the driver needs the script's answer before it can continue:**
* An HTTP/Web request handler that must return a response body.
* An MQTT command that needs an acknowledgement or a result.
* A validation or lookup (`bool on_authorize(string user)`).
* The driver wants to know whether the script function failed.

**Rule of thumb:** if the driver would otherwise store the event and poll for a reply, use `ti_call`. If it does not care what happens next, use `ti_post_event`.

---

## 2. How it works: the execution lock

Each runtime has one **execution lock**. Whoever runs TI code holds it.

* `ti_execute` holds it for the whole script run.
* The script gives it up only at **safe points**: statement boundaries, loop iterations, and while inside `delay()` / `ti_wait()` / `ti_wait_flag()`. At a safe point it checks for waiting `ti_call` callers and for queued events.
* A callback (event or `ti_call`) holds it for the whole callback. Nothing else runs TI code meanwhile.

```text
script:      ---run--[safe point]--------------------run---
                          |  lock handed over  ^ lock returned
ti_call:                  +--- callback runs ---+   (caller's stack)
```

Consequences:
* No locks are needed in script code, and there are no data races between the script and its handlers (they never run at the same time).
* A `ti_call` can only be served at a safe point. A script stuck inside one very long native function delays it. That is what `timeout_ms` is for.
* `delay(ms)` is the best place for a script to be waiting. The lock is released for the whole sleep, so events and calls run at once and the delay then **continues for its remaining time** (it ends early only on `ti_stop`).

### 2.1 Generation handles (`ti_handle_t`)

Drivers never hold a runtime pointer, only a `ti_handle_t` (`[ generation:24 | slot:8 ]`). When a runtime is destroyed its slot generation increases, so a late `ti_post_event` / `ti_call` with the old handle fails safely with `TI_ERR_STALE_HANDLE` and never touches freed memory. `ti_destroy` also waits until every call in progress has returned.

---

## 3. Asynchronous events

### 3.1 Script side

```c
int received = 0;

/* Runs at the script's next safe point, in the order the events were posted */
void on_message(string text) {
    received++;
    print("event #" + to_string(received) + ": " + text);
}

register_event(on_message);

while (true) {
    delay(1000);     /* events run during the delay; it keeps its full length */
}
```

Full file: [`examples/ti/async_event.ti`](../../examples/ti/async_event.ti).

### 3.2 Native side: remember the handle, then post

The registration function stores the handle and the function name. Post from any thread:

```c
/* Ownership of every value in args passes to ti_post_event, success or not */
value_t *args[1] = { val_new_string(text) };
ti_status_t status = ti_post_event(handle, func_name, args, 1);

if (status == TI_ERR_STALE_HANDLE) {
    /* The script runtime is gone: forget this listener */
} else if (status == TI_ERR_QUEUE_FULL) {
    /* Too many pending events: drop or retry later */
}
```

The reference implementation is [`cli/builtin/builtin_event.c`](../../cli/builtin/builtin_event.c) (`register_event` and `cli_simulate_event`).

### 3.3 What happens to errors

The callback's error is **consumed by the runtime**: it is logged as `[Event Error] ... at line N`, the event is dropped and the main script and the other queued events keep running. A function that is not defined is also logged and dropped. Argument count and type are checked first (`TI_ERR_TYPE_MISMATCH`).

---

## 4. Synchronous callbacks

### 4.1 Script side

```c
int served = 0;

/* Runs to completion while the script is paused; the returned value goes back to the caller */
string on_request(string payload) {
    served++;
    return "ok #" + to_string(served) + " for '" + payload + "'";
}

register_callback(on_request);

while (true) {
    delay(500);
}
```

Full file: [`examples/ti/sync_callback.ti`](../../examples/ti/sync_callback.ti). Handlers may take arguments and return any type; define them at **file scope**.

### 4.2 Native side: call and wait

```c
value_t *arg = val_new_string(text);   /* borrowed by ti_call: you free it */
value_t *args[1] = { arg };
value_t *result = NULL;

/* Wait at most one second for the script to reach a safe point */
ti_status_t status = ti_call(handle, func_name, args, 1, &result, 1000);

val_free(arg);
if (status == TI_OK && result != NULL && result->type == VAL_STRING) {
    use(result->string_val);            /* e.g. send it back as the HTTP body */
}
val_free(result);                       /* result is owned by the caller (NULL is fine) */
```

Reference implementation: [`cli/builtin/builtin_callback.c`](../../cli/builtin/builtin_callback.c) (`register_callback` and `cli_simulate_call`).

### 4.3 Return status of `ti_call`

| Status | Meaning |
|---|---|
| `TI_OK` | Handler returned; `*result` holds its value |
| `TI_ERR_TIMEOUT` | The lock was not free within `timeout_ms` (script busy in a long native). Nothing ran |
| `TI_ERR_STALE_HANDLE` | The runtime was destroyed. Drop the listener |
| `TI_ERR_INTERRUPTED` | The runtime was stopped with `ti_stop` |
| `TI_ERR_UNDEFINED` | No script function with that name |
| `TI_ERR_INVALID_ARG` / `TI_ERR_TYPE_MISMATCH` | Wrong argument count or types |
| any other `TI_ERR_*` | The handler raised this error while running (division by zero, bad index, ...) |

Use `timeout_ms = TI_PORT_WAIT_FOREVER` to wait without limit and `0` to try once.

### 4.4 What happens to errors

A callback error behaves like a caught exception: **`ti_call` consumes it**, returns its kind to your driver, logs `[Call Error] ... at line N`, and resets the runtime to a healthy state. **The main script keeps running** and later calls work normally. Only `ti_stop` is not consumed: the script still stops.

### 4.5 Calling from inside the script's own context

If a native that the script called itself calls `ti_call` on the same handle, the call runs inline (the lock is already held). It never deadlocks, but remember it then runs while the script is mid-expression.

---

## 5. Rules for drivers

1. **Never hold a module mutex while calling `ti_call`.** The handler may run a native that takes the same mutex. Copy the handle and name out under your mutex, release it, then call (see `cli_simulate_call`).
2. **Natives that wait must not hold the lock while blocked.** Use `ti_wait(handle, ms)` to sleep, or `ti_wait_flag(handle, &done, timeout_ms)` + `ti_give(handle)` to wait for another thread's I/O. Both release the lock and service events and calls. Do not use `sleep()`/`usleep()` or block on a lock another thread holds while it calls `ti_call`.
3. **Not from an ISR.** `ti_post_event` and `ti_call` allocate. From an ISR, notify a task and let the task post or call. (`ti_stop` is the exception and is safe from an ISR.)
4. **Dispatcher stack size.** A `ti_call` handler runs on the *caller's* stack. Size the dispatcher task for the interpreter's recursion (`max_call_depth`, 64 by default, deep call chains use a lot) and test the deepest handler.
5. **Priority (FreeRTOS).** The execution lock is a mutex with priority inheritance. A high-priority dispatcher that waits on a low-priority script lifts the script's priority until the next safe point. Keep handlers short.
6. **Values are not shared across threads.** `value_t` reference counts are not atomic. Create the argument values in the calling thread, free them there, and never keep a `value_t *` that belongs to the script. Copy data out (`strdup`, `val_copy`) instead.
7. **Define functions at file scope.** Defining functions inside a function body, loop or callback is a user error. The function table must not change while a handler is running.
8. **Callbacks run to completion.** Do not rely on one handler being interrupted by another. A long handler delays every other call and event.
9. **Do not call `ti_destroy` from inside a handler or while `ti_execute` is still running.** Stop first (`ti_stop`), wait for `ti_execute` to return, then destroy.

---

## 6. Try the examples

```bash
make
./ti.out examples/ti/async_event.ti      # type a line + Enter: delivered with ti_post_event
./ti.out examples/ti/sync_callback.ti    # type !hello + Enter: delivered with ti_call, answer printed
```

In the `cli`, a line starting with `!` is sent as a synchronous call (`cli_simulate_call`); any other line is an asynchronous event (`cli_simulate_event`). Typical output of `sync_callback.ti`:

```text
[main] call "hello" -> status 0, answer "ok #1 for 'hello'"
script alive, ticks=1 served=1
```

Thread-safety tests for both paths are in `tests/test_ti_call.c`, `tests/test_ti_wait.c` and `tests/test_ti_wait_flag.c` (build with `-fsanitize=thread`).
