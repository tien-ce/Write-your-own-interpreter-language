# Technical Implementation Plan: Asynchronous Event Bridge & Native Callback System

## 1. Executive Summary & Goals

This plan specifies the implementation of a native event callback system for `TienInterpreter`.

### Key Objectives:
1. **Pass Runtime to Built-ins**: Update `native_fn_t` signature to accept `ti_runtime_t *rt` so native C functions directly receive the calling runtime instance. Update all built-ins in `cli/built_in_functions.c` with `(void)rt;` where unused.
2. **Pending Task Queue in Runtime**: `ti_runtime_t` maintains a list of pending event tasks posted by C native code.
3. **Universal Safe-Point in `visitor_visit` & Sliced Delay**:
   - Check and drain pending tasks directly inside `visitor_visit()` at every AST node evaluation (universal safe-point across all statements and blocks).
   - In `built_in_delay(rt, argv, argc)`, divide sleep time into 10ms slices, calling event dispatch at each step until the total duration is accumulated.
4. **Dispatcher Uses `run_function`**: The runtime dispatcher resolves `function_t*` and executes it via `run_function()` (not `run_ti_function` directly), allowing `run_function` to automatically validate argument count and types against the TI function signature.
5. **Pointer-Based Callback Listener in CLI**:
   - Define a listener structure and a static pointer initialized to `NULL` (`callback_listener_t *s_listener = NULL;`).
   - Provide a built-in registration function that allocates/populates this listener pointer when called from TI scripts.
   - Provide a simulation trigger function that receives text input and invokes `ti_runtime_post_event()` using the registered listener pointer.

---

## 2. Architecture & Component Design

### 2.1 Native Function Signature Update
In `src/include/ti_type_func.h`:
```c
typedef value_t *(*native_fn_t)(ti_runtime_t *rt, value_t **args, int argc);
```
In `src/ti_runtime_eval_func.c`, `run_function()` already receives `rt`. It directly passes `rt` to `func->native_fn(rt, argv, argc);`.

### 2.2 Pending Event Queue in Runtime
In `src/include/ti_runtime.h`:
```c
typedef struct EVENT_TASK_STRUCT {
    char *func_name;
    struct VALUE_STRUCT **argv;
    int argc;
    struct EVENT_TASK_STRUCT *next;
} event_task_t;
```
Inside `struct TI_RUNTIME_STRUCT`:
```c
event_task_t *pending_tasks_head;
event_task_t *pending_tasks_tail;
```

### 2.3 Universal Safe-Point in `visitor_visit`
In `src/ti_runtime_visitor.c`, inside `visitor_visit(rt, ctx, node)`:
```c
/* Drain pending asynchronous events before evaluating any AST node */
if (rt != NULL && rt->pending_tasks_head != NULL) {
    ti_runtime_dispatch_pending_events(rt);
    if (rt->is_interrupted) {
        return NULL;
    }
}
```

### 2.4 Dispatching via `run_function`
When draining pending tasks in `ti_runtime_dispatch_pending_events(rt)`:
1. Dequeue `task` from `rt->pending_tasks_head`.
2. Look up `function_t *func = user_find_function(rt, task->func_name);`.
3. If found:
   - Create an isolated `func_ctx` with `func_ctx->parent = rt->global_context;`.
   - Call `value_t *ret = run_function(rt, func_ctx, func, task->argv, task->argc, NULL);`.
   - `run_function` automatically validates argument counts and types against `func->params`.
   - Trap and reset `func_ctx->flow_state = FLOW_NORMAL;`.
   - If `ret != NULL`, free it via `val_free(ret)`.
   - Free `func_ctx`.
4. Free `task->argv` elements via `val_free()`, free `task->argv`, free `task->func_name`, free `task`.

### 2.5 10ms Sliced Delay
In `cli/built_in_functions.c`, inside `built_in_delay(rt, argv, argc)`:
```c
int total_ms = argv[0]->int_val;
int elapsed = 0;
while (elapsed < total_ms) {
    int slice = (total_ms - elapsed > 10) ? 10 : (total_ms - elapsed);
    usleep(slice * 1000);
    ti_runtime_dispatch_pending_events(rt);
    elapsed += slice;
}
```

### 2.6 Dynamic Callback Listener & Simulation
In `cli/built_in_functions.c`:
1. Listener type definition and static pointer:
   ```c
   typedef struct {
       ti_runtime_t *rt;
       char *func_name;
   } callback_listener_t;

   static callback_listener_t *s_callback_listener = NULL;
   ```
2. Built-in `register_callback(rt, argv, argc)`:
   - If `s_callback_listener == NULL`, allocates `s_callback_listener = malloc(sizeof(callback_listener_t));`.
   - Otherwise, frees old `s_callback_listener->func_name` if already set.
   - Assigns `s_callback_listener->rt = rt;`.
   - Assigns `s_callback_listener->func_name = strdup(argv[0]->string_val);`.
3. Simulation function `simulate_user_input(const char *text)`:
   - If `s_callback_listener != NULL && s_callback_listener->rt != NULL`:
     - Allocates `value_t *str_val = val_new_string(text);`.
     - Calls `ti_runtime_post_event(s_callback_listener->rt, s_callback_listener->func_name, &str_val, 1);`.

---

## 3. Affected Files Matrix

| File Path | Description of Changes |
| :--- | :--- |
| `src/include/ti_type_func.h` | Update `native_fn_t` prototype to accept `ti_runtime_t *rt`. |
| `src/include/ti_runtime.h` | Define `event_task_t` struct, add pending task queue pointers to `ti_runtime_t`. |
| `src/ti_runtime.c` | Initialize pending queue in `ti_runtime_init`, purge/free pending tasks in `ti_runtime_destroy`. |
| `src/include/TienInterpreter.h` | Expose `ti_runtime_post_event()` and `ti_runtime_dispatch_pending_events()`. |
| `src/ti_runtime_eval_func.c` | Pass `rt` to `func->native_fn(rt, argv, argc)` in `run_function()`. Implement `ti_runtime_post_event()` and `ti_runtime_dispatch_pending_events()`. |
| `src/ti_runtime_visitor.c` | In `visitor_visit()`, call `ti_runtime_dispatch_pending_events(rt)` at the top before dispatching AST nodes. |
| `cli/built_in_functions.c` | Update all built-ins with `(void)rt;`. Implement `built_in_delay` with 10ms slicing. Implement dynamic listener pointer, registration built-in, and `simulate_user_input()`. |

---

## 4. Step-by-Step Implementation Sequence

### Phase 1: Update Function Signatures & Headers
1. In `src/include/ti_type_func.h`:
   - Change `typedef value_t *(*native_fn_t)(ti_runtime_t *rt, value_t **args, int argc);`
2. In `src/include/ti_runtime.h`:
   - Define `event_task_t`.
   - Add `event_task_t *pending_tasks_head;` and `event_task_t *pending_tasks_tail;` to `struct TI_RUNTIME_STRUCT`.
3. In `src/include/TienInterpreter.h`:
   - Declare:
     ```c
     bool ti_runtime_post_event(ti_runtime_t *rt, const char *func_name, value_t **argv, int argc);
     void ti_runtime_dispatch_pending_events(ti_runtime_t *rt);
     ```

### Phase 2: Runtime Lifecycle Management
1. In `src/ti_runtime.c`:
   - In `ti_runtime_init()`: Set `rt->pending_tasks_head = NULL; rt->pending_tasks_tail = NULL;`.
   - In `ti_runtime_destroy()`: Drain `pending_tasks_head`, free all `argv` elements with `val_free()`, free `argv`, free `func_name`, and free the task node.

### Phase 3: Core Dispatcher & Caller Implementation
1. In `src/ti_runtime_eval_func.c`:
   - In `run_function()`: Update invocation to `func->native_fn(rt, argv, argc);`.
   - Implement `ti_runtime_post_event(rt, func_name, argv, argc)`:
     - Allocates `event_task_t`.
     - Stores `argv` array and duplicates `func_name`.
     - Appends to tail of `rt->pending_tasks_head/tail`.
   - Implement `ti_runtime_dispatch_pending_events(rt)`:
     - Loops while `rt->pending_tasks_head != NULL`.
     - Dequeues task node.
     - Resolves `function_t *func = user_find_function(rt, task->func_name);`.
     - If handler exists:
       - Creates isolated `context_t *func_ctx = context_init(&rt->alloc_list); func_ctx->parent = rt->global_context;`.
       - Calls `value_t *ret = run_function(rt, func_ctx, func, task->argv, task->argc, NULL);`.
       - Resets `func_ctx->flow_state = FLOW_NORMAL;`.
       - If `ret != NULL`, calls `val_free(ret)`.
       - Frees `func_ctx`.
     - Calls `val_free()` on `task->argv[i]`, frees `task->argv`, `task->func_name`, `task`.

### Phase 4: Universal Safe-Point & CLI Built-ins
1. In `src/ti_runtime_visitor.c`:
   - Inside `visitor_visit()`: At the beginning, check `if (rt != NULL && rt->pending_tasks_head != NULL)` and invoke `ti_runtime_dispatch_pending_events(rt)`.
2. In `cli/built_in_functions.c`:
   - Update all existing built-in functions: add `(void)rt;` where `rt` is unused.
   - Update `built_in_delay()`: Implement 10ms sliced loop calling `ti_runtime_dispatch_pending_events(rt)`.
   - Define `callback_listener_t` and `static callback_listener_t *s_callback_listener = NULL;`.
   - Implement `built_in_register_callback(rt, argv, argc)`:
     - Allocates `s_callback_listener` if NULL.
     - Updates `s_callback_listener->rt = rt;` and `s_callback_listener->func_name = strdup(argv[0]->string_val);`.
   - Implement `simulate_user_input(const char *text)`.

---

## 5. Verification Protocol

1. **Compilation**: Run `make clean && make`. Verify zero compilation warnings and errors.
2. **Execution Test**:
   - Write a test script registering a callback via `register_callback("on_input")` and running a loop with `delay(3000)`.
   - Trigger `simulate_user_input("hello")` during the delay.
   - Verify immediate callback execution without waiting for the full 3000ms delay.
3. **Type Safety Test**:
   - Define a callback expecting `int`, post a `string` via simulation, verify that `run_function` catches the mismatch and logs an error cleanly without crashing.
4. **Memory Verification**: Run Valgrind to ensure 0 definitely lost bytes on completion.
