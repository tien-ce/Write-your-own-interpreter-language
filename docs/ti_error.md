# Maintainer Guide: Structured Error Handling & Propagation System

> **Audience:** Developers working on runtime error reporting, evaluators, memory cleanup during error unwinding, and exception handling in `src/ti_runtime.c`, `src/ti_runtime_eval_*.c`, `src/debug.c`, `src/include/ti_type.h`, and `src/include/ti_runtime.h`.

---

## 1. Architectural Overview & Design Principles

The `TienInterpreter` utilizes a **structured error propagation model** with deterministic stack unwinding. No error ever terminates the host program: runtime errors are captured, recorded on the runtime instance, and propagated up the Abstract Syntax Tree (AST) call stack, and every error finally reaches the host as a `ti_status_t` return value (section 4).

### Core Principles:
1. **First-Error Latch**: Only the first runtime error that occurs is recorded into `rt->error`. Subsequent cascading failures or unhandled expressions during stack unwinding are ignored, preserving the root-cause diagnostics.
2. **Sentinel Failure Propagation**:
   - In Expression Evaluators: Returning `NULL` signifies that an error (or cancellation request) has occurred. Callers inspect `if (val == NULL)`, clean up local temporary values, and immediately return `NULL`.
   - In Statement Evaluators: A non-NULL sentinel (`TI_VAL_OK`) indicates successful execution. `NULL` strictly indicates an error.
3. **Decoupled Data Layer**:
   - Data structures (`context_t`, `list_t`, `dict_t`) remain pure containers that return status codes (`ti_status_t`) or lookup pointers. They do not hold runtime context or invoke `ti_raise`.
4. **Status at the Boundary, Never a Fatal Handler**:
   - There is no fatal callback (`ti_register_fatal` / `ti_fatal` were removed in v3). The host decides what an error means: an ESP32 task may log it and run the next script, a CLI may exit.
   - Build errors (lexer, parser) are logged and returned by the compiler as `TI_ERR_SYNTAX`.
   - Runtime errors are logged by `ti_runtime_execute` after the whole call stack has unwound and returned as their kind; `ti_execute` then destroys the runtime as on success.

---

## 2. Core Data Structures & Enumerations

### 2.1. Status & Error Codes (`ti_status_t`)
Defined in `src/include/ti_type.h`:

```c
typedef enum {
    TI_OK = 0,                  // Operation completed successfully
    TI_ERR_INVALID_ARG,         // NULL or malformed argument
    TI_ERR_NO_MEMORY,           // Dynamic memory allocation failed
    TI_ERR_STALE_HANDLE,        // Generation handle does not refer to a live runtime
    TI_ERR_QUEUE_FULL,          // Pending event queue reached its capacity
    TI_ERR_INTERRUPTED,         // Script execution was stopped/cancelled
    TI_ERR_TYPE_MISMATCH,       // Value type does not match expected parameter/operand type
    TI_ERR_INDEX_OUT_OF_RANGE,  // List/bytes index outside [0, count)
    TI_ERR_VALUE_OUT_OF_RANGE,  // Value does not fit the target (e.g. a byte outside 0..255)
    TI_ERR_INVALID_VALUE,       // Right type but unusable content (e.g. to_int("abc"))
    TI_ERR_KEY_NOT_FOUND,       // Dictionary key does not exist
    TI_ERR_LIMIT_EXCEEDED,      // Configured limit reached (call recursion depth, max items)
    TI_ERR_UNDEFINED,           // Variable, function or module name is not defined
    TI_ERR_DIV_ZERO,            // Division or modulo by zero
    TI_ERR_INTERNAL,            // Interpreter invariant violated (unhandled AST node/operator)
    TI_ERR_RUNTIME,             // Generic script runtime error
    TI_ERR_TIMEOUT,             // Timed out waiting for the runtime execution lock
    TI_ERR_READ_ONLY,           // Target can be read but not written (e.g. a module member)
    TI_ERR_SYNTAX,              // Source text does not compile (the build error is already logged)
} ti_status_t;
```

### 2.2. Error String Conversion (`ti_err_to_str`)
Declared in `src/include/debug.h` and implemented in `src/debug.c`:

```c
const char *ti_err_to_str(ti_status_t status);
```
Converts each `ti_status_t` enumeration into a short, human-readable ASCII string (e.g. `TI_ERR_UNDEFINED` $\rightarrow$ `"undefined name"`, `TI_ERR_DIV_ZERO` $\rightarrow$ `"division by zero"`).

### 2.3. Runtime Execution State & Error Record
Defined in `src/include/ti_runtime.h`:

```c
typedef enum {
    TI_RT_OK = 0,      // Normal script execution
    TI_RT_INTERRUPTED, // ti_stop() requested cancellation
    TI_RT_ERROR,       // Runtime error raised, evaluators unwind to top level
} ti_rt_status_t;

typedef struct {
    ti_status_t kind;                         // Error category (TI_ERR_*)
    int         line;                         // Source line where error occurred (0 if unknown)
    const char *path;                         // Path of the program whose code raised it (NULL if none)
    char        message[TI_ERROR_MESSAGE_SIZE]; // Formatted diagnostic message
} ti_error_t;
```

`path` is borrowed from the program of the running namespace (`rt->ns->prog->path`): the main script's program lives until `ti_execute` returns, a script module's until the runtime is destroyed, so it is valid wherever the error is reported. `ti_runtime_report_error` prints:

| Record | Log line |
| :--- | :--- |
| path and line | `[Runtime Error] <message> at <path>:<line>` |
| path only | `[Runtime Error] <message> in <path>` |
| line only (no path, e.g. `ti_run_string`) | `[Runtime Error] <message> at line <line>` |
| neither | `[Runtime Error] <message>` |

Build errors name their file the same way: the lexer borrows the program's path (`lexer->path`), and every lexer and parser message ends with `at <path>:<line>` (`at line <line>` without a path), through `TI_BUILD_AT_FMT` / `TI_BUILD_AT_ARGS` in `src/include/ti_build_lexer.h`.

---

## 3. Error Raising & Unwinding Protocol

### 3.1. Raising an Error (`ti_raise`)
```c
void ti_raise(ti_runtime_t *rt, ti_status_t kind, int line, const char *fmt, ...);
```
- Sets `rt->status = TI_RT_ERROR;`.
- If no previous error was latched, copies `kind`, `line`, the running namespace's program path (`path`), and formats `fmt` into `rt->error.message`. A native that raises (`ti_raise_error`) runs in its caller's namespace, so the path is the file of the code that called it.
- **CRITICAL REQUIREMENT:** `ti_raise` does NOT terminate execution or jump via `longjmp`. The caller MUST immediately release local resources and return `NULL`.

```c
/* Standard Evaluator Error Pattern */
if (divisor == 0) {
    ti_raise(rt, TI_ERR_DIV_ZERO, line, "Division by zero");
    val_free(left_val);
    return NULL; // REQUIRED: Do not execute further code
}
```

### 3.2. Error Propagation Contract
Every evaluator receiving a `NULL` from child evaluation MUST clean up its own tracked temporaries and return `NULL`:

```c
value_t *eval_binary_expr(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    value_t *left = visitor_visit(rt, ctx, node->value.binary_expr.left);
    if (left == NULL) {
        return NULL; /* Left branch raised error; propagate immediately */
    }

    value_t *right = visitor_visit(rt, ctx, node->value.binary_expr.right);
    if (right == NULL) {
        val_free(left); /* Prevent leak of left operand */
        return NULL;
    }

    /* Perform operation */
    value_t *result = binary_execute_op(rt, left, right, node->line);
    val_free(left);
    val_free(right);
    return result;
}
```

### 3.3. Raising from Native Functions (`ti_raise_error`)
Natives never see `ti_runtime_t *`, so they use the handle-based wrapper declared in `src/TienInterpreter.h`:

```c
void ti_raise_error(ti_handle_t handle, ti_status_t kind, const char *fmt, ...);
```
- Resolves the handle, formats the message into a `TI_ERROR_MESSAGE_SIZE` buffer, then calls `ti_raise(rt, kind, 0, "%s", message)`. A stale handle is ignored.
- Line `0` means "unknown": `run_function` fills in the line of the call node when a native returns `NULL` with `rt->error.line == 0`.
- Same contract as `ti_raise`: the native MUST release its temporaries and `return NULL` right after. First-error latch applies.
- Developer-facing rules (error kind choice, raise vs. return a value): see `docs/guide/NATIVE_SCRIPT_INTERACTION.md`, section 4.

---

## 4. Where an Error Ends

| Error | Logged by | Returned to the host by |
| :--- | :--- | :--- |
| Build error (lexer, parser) | The lexer or parser (`[Lexer Error]` / `[Parser Error] ... at <path>:<line>`, then the offending line). `lexer_abort` jumps back to `ti_program_compile`, which frees the whole program. | `ti_program_compile` (`TI_ERR_SYNTAX`); `ti_compile` returns `NULL`; `ti_run_string*` return `TI_ERR_SYNTAX`. |
| Runtime error in the main script or a module | `ti_runtime_report_error`, called once by `ti_runtime_execute` after unwinding (`[Runtime Error] ...`). | `ti_execute` (the raised kind), after destroying the runtime; `ti_run_string*` pass it on. |
| Error in an event handler | The dispatcher (`[Event Error] ...`); the error is consumed. | Nobody: the script keeps running. |
| Error in a `ti_call` callback | `ti_call` (`[Call Error] ...`); the error is consumed. | `ti_call` (the raised kind); the script keeps running. |
| Cancellation (`ti_stop`) | Nothing. | `ti_execute` (`TI_ERR_INTERRUPTED`). |

A script module that fails to compile is a runtime error of the `import` that loaded it (`Cannot import module 'x': syntax error`), logged after the parser's own message that names the module's file.

Every error path is checked for leaks: the test runners in `tests/` are built with AddressSanitizer (see `tests/test_fatal_leak.c`, `tests/test_parser_errors.c`, `tests/test_script_module.c`).
