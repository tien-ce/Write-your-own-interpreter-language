# Technical Implementation Plan: Asynchronous Event Bridge & Native Callback System

## 1. Executive Summary & Objective

This document defines the technical requirements and architectural contracts for integrating an asynchronous event callback bridge into `TienInterpreter`. 

The objective is to allow C Native code to post events to an active interpreter runtime without multithreading race conditions, and to allow TI scripts to register functions that react to these events.

---

## 2. Architectural Requirements & System Contracts

### 2.1 Native Function Signature Contract
- **Runtime Injection**: All native C built-in functions must accept the active runtime instance (`ti_runtime_t *rt`) as their first parameter, followed by the argument array and argument count.
- **Backward Compatibility**: Existing built-in functions in `cli/built_in_functions.c` that do not require runtime access must mark the parameter as unused (`(void)rt;`).
- **Caller Propagation**: The core function runner (`run_function`) must pass its active `rt` reference when dispatching built-in function calls.

### 2.2 Pending Task Management in Runtime
- **Task Storage Requirement**: The runtime instance (`ti_runtime_t`) must encapsulate a dedicated data structure to store pending event tasks queued from C native code.
- **Task Record Properties**: Each pending task entry must store:
  - The target function identifier.
  - The array of argument value pointers.
  - The argument count.
- **Lifecycle Management**:
  - Initialized to an empty state when a runtime instance is created (`ti_runtime_init`).
  - Purged completely during runtime teardown (`ti_runtime_destroy`), ensuring any unhandled tasks and their argument payloads are deallocated without memory leaks.

### 2.3 Dispatching & Execution Semantics
- **Public API Contract**:
  - An event posting function (`ti_runtime_post_event`) allowing C native code to enqueue a target function name with its argument values into a specified runtime.
  - An event dispatching function (`ti_runtime_dispatch_pending_events`) responsible for draining and executing pending tasks.
- **Execution via `run_function`**:
  - The dispatcher must look up the target function in the user function table.
  - Execution must be routed through `run_function` (rather than invoking low-level internal runners directly) so that argument count and type compatibility are strictly validated against the declared parameter signature of the TI function.
- **Context & Flow State Isolation**:
  - Each callback invocation must run in an isolated execution context whose parent is set to the runtime's global context (`func_ctx->parent = rt->global_context`).
  - The callback must be able to read and modify global variables, but must not access or mutate local variables of any interrupted outer scope.
  - Any control flow signals escaping the callback (`FLOW_RETURN`, `FLOW_BREAK`, `FLOW_CONTINUE`) must be trapped, reset to `FLOW_NORMAL`, and their return values cleanly freed to avoid corrupting the outer script execution.
- **Memory Cleanup**:
  - All argument value objects (`value_t*`) transferred into a task must be freed after execution.
  - The task record itself must be deallocated upon completion.

### 2.4 Safe-Point Verification (Visitor & Sliced Delay)
- **Universal Safe-Point in `visitor_visit`**:
  - Before evaluating any AST node, `visitor_visit` must check if pending tasks exist in the runtime and trigger event dispatching.
  - If the runtime is flagged as interrupted during dispatch, evaluation must halt immediately.
- **Sliced Delay in `built_in_delay`**:
  - The delay implementation must not sleep for the full duration in a single blocking call.
  - It must slice the requested duration into small intervals (e.g. 10ms steps), invoking `ti_runtime_dispatch_pending_events` at each step until the total requested duration has elapsed.

### 2.5 CLI Simulation & Dynamic Listener
- **Dynamic Listener Storage**:
  - In `cli/built_in_functions.c`, manage a dynamic listener pointer initialized to `NULL` to store the registered runtime reference and callback function name.
- **Registration Built-in**:
  - Expose a built-in function to TI scripts to register a callback function.
  - When invoked from script, allocate/update the listener pointer with the current runtime and target function name.
- **Simulation Interface**:
  - Provide a C simulation trigger function (e.g., accepting text input) that constructs string value arguments and posts an event task to the registered runtime using `ti_runtime_post_event`.

---

## 3. Module Responsibilities Matrix

| File Path | Functional Responsibility |
| :--- | :--- |
| `src/include/ti_type_func.h` | Update `native_fn_t` definition to include `ti_runtime_t *rt` as the first argument. |
| `src/include/ti_runtime.h` | Define the task container struct and add task queue/collection tracking to `struct TI_RUNTIME_STRUCT`. |
| `src/ti_runtime.c` | Handle task queue allocation/initialization in `ti_runtime_init` and full cleanup/deallocation in `ti_runtime_destroy`. |
| `src/include/TienInterpreter.h` | Expose public C APIs for posting events and dispatching pending events. |
| `src/ti_runtime_eval_func.c` | Forward `rt` to built-in callbacks in `run_function`. Implement event posting and the safe-point event dispatcher. |
| `src/ti_runtime_visitor.c` | Invoke the event dispatcher at the top of `visitor_visit` before AST node evaluation. |
| `cli/built_in_functions.c` | Update existing built-in signatures with `(void)rt;`. Implement sliced 10ms delay. Implement dynamic listener pointer, registration built-in, and simulation trigger. |

---

## 4. Step-by-Step Execution Sequence

### Phase 1: Signatures & Contract Definitions
1. Update `native_fn_t` in `src/include/ti_type_func.h` to accept `ti_runtime_t *rt`.
2. Define the task structure and integrate task storage into `ti_runtime_t` in `src/include/ti_runtime.h`.
3. Declare `ti_runtime_post_event` and `ti_runtime_dispatch_pending_events` in `src/include/TienInterpreter.h`.

### Phase 2: Runtime Lifecycle & Memory Safety
1. Initialize task storage in `ti_runtime_init` (`src/ti_runtime.c`).
2. Implement robust deallocation in `ti_runtime_destroy` (`src/ti_runtime.c`), ensuring all queued tasks, task arguments (`val_free`), and task structures are cleanly freed.

### Phase 3: Event Dispatcher & Caller Integration
1. Update `run_function` in `src/ti_runtime_eval_func.c` to pass `rt` into native built-in calls.
2. Implement `ti_runtime_post_event` in `src/ti_runtime_eval_func.c` to queue event requests.
3. Implement `ti_runtime_dispatch_pending_events` in `src/ti_runtime_eval_func.c`:
   - Resolve target function from user functions.
   - Execute in an isolated context linked to `global_context`.
   - Use `run_function` to enforce parameter count and type checking.
   - Trap and reset control flow flags (`flow_state`).
   - Clean up return values and argument memory.

### Phase 4: Safe-Point Integration & CLI Simulation
1. Add the safe-point check at the entry of `visitor_visit` in `src/ti_runtime_visitor.c`.
2. Update all built-ins in `cli/built_in_functions.c` to match the new signature (mark `rt` as unused where applicable).
3. Refactor `built_in_delay` to use 10ms sliced intervals with dispatch calls.
4. Implement the dynamic listener pointer (`NULL` by default), the callback registration built-in, and the simulation trigger function in `cli/built_in_functions.c`.

---

## 5. Verification Protocol

1. **Compilation**: Run `make clean && make` to ensure zero compilation errors and warnings.
2. **Execution Test**:
   - Write a script that registers a callback and enters a loop with a multi-second delay.
   - Trigger the simulation function during the delay.
   - Verify that the callback executes promptly during the delay without waiting for the full sleep time to complete.
3. **Type Safety Test**:
   - Verify that passing an argument with an invalid type from C to a typed script callback triggers proper type error logging via `run_function` and unwinds safely without crashing.
4. **Memory Verification**:
   - Verify with Valgrind that all queued arguments, task structs, and return values are freed with 0 memory leaks.
