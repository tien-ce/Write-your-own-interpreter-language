# Maintainer Guide: Structured Error Handling & Propagation System

> **Audience:** Developers working on runtime error reporting, evaluators, memory cleanup during error unwinding, and exception handling in `src/ti_runtime.c`, `src/ti_runtime_eval_*.c`, `src/debug.c`, `src/include/ti_type.h`, and `src/include/ti_runtime.h`.

---

## 1. Architectural Overview & Design Principles

The `TienInterpreter` utilizes a **structured error propagation model** with deterministic stack unwinding. Instead of terminating execution immediately on error via `ti_fatal()` / `exit()` within expression evaluators, runtime errors are captured, recorded on the runtime instance, and propagated up the Abstract Syntax Tree (AST) call stack.

### Core Principles:
1. **First-Error Latch**: Only the first runtime error that occurs is recorded into `rt->error`. Subsequent cascading failures or unhandled expressions during stack unwinding are ignored, preserving the root-cause diagnostics.
2. **Sentinel Failure Propagation**:
   - In Expression Evaluators: Returning `NULL` signifies that an error (or cancellation request) has occurred. Callers inspect `if (val == NULL)`, clean up local temporary values, and immediately return `NULL`.
   - In Statement Evaluators: A non-NULL sentinel (`TI_VAL_OK`) indicates successful execution. `NULL` strictly indicates an error.
3. **Decoupled Data Layer**:
   - Data structures (`context_t`, `list_t`, `dict_t`) remain pure containers that return status codes (`ti_status_t`) or lookup pointers. They do not hold runtime context or invoke `ti_raise`.
4. **Isolated Fatal Boundary**:
   - `ti_fatal()` is strictly reserved for:
     - Lexer & Parser (frontend AST generation before runtime initialization).
     - Script boundary (`ti_execute()`): invoked only after the entire call stack has safely unwound and reclaimed 100% of temporary allocations.

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
    TI_ERR_INDEX_OUT_OF_RANGE,  // List index outside [0, count)
    TI_ERR_KEY_NOT_FOUND,       // Dictionary key does not exist
    TI_ERR_LIMIT_EXCEEDED,      // Configured limit reached (call recursion depth, max items)
    TI_ERR_UNDEFINED,           // Variable or function name is not defined in active scope
    TI_ERR_DIV_ZERO,            // Division or modulo by zero
    TI_ERR_INTERNAL,            // Interpreter invariant violated (unhandled AST node/operator)
    TI_ERR_RUNTIME,             // Generic script runtime error
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
    char        message[TI_ERROR_MESSAGE_SIZE]; // Formatted diagnostic message
} ti_error_t;
```

---

## 3. Error Raising & Unwinding Protocol

### 3.1. Raising an Error (`ti_raise`)
```c
void ti_raise(ti_runtime_t *rt, ti_status_t kind, int line, const char *fmt, ...);
```
- Sets `rt->status = TI_RT_ERROR;`.
- If no previous error was latched, copies `kind`, `line`, and formats `fmt` into `rt->error.message`.
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

---

## 4. Migration Plan & Status

| Step | Scope | Description | Status |
| :--- | :--- | :--- | :--- |
| **Step 1** | **Enum & Utilities** | Add `TI_ERR_UNDEFINED`, `TI_ERR_DIV_ZERO`, `TI_ERR_INTERNAL` to `ti_status_t` and implement `ti_err_to_str`. | **Completed** |
| **Step 2** | `eval_variable.c` | Add `rt` to `eval_identifier`, implement `eval_lookup_variable(rt, ctx, name, line)` helper. | **Completed** |
| **Step 3** | `eval_func.c` | Migrate `run_function`, `run_ti_function`, `eval_function_call` from `ti_log`+`ti_fatal` to `ti_raise`. | **Completed** |
| **Step 4** | `eval_control.c`, `eval_literal.c`, `ti_runtime_visitor.c` | Migrate type assertions and default visitor branches to `ti_raise`. | Pending |
| **Step 5** | `eval_binary.c` | Pass `rt` down to static binary operators (`binary_add`, etc.) and call `ti_raise` on mismatch/zero division. | Pending |
| **Step 6** | `ti_runtime_context.c` | Refactor pure context lookups to return status codes. | Pending |
| **Step 7** | Audit | Ensure `ti_fatal()` exists only at script boundary (`ti_execute()`) and frontend lexer/parser. | Pending |
| **Step 8** | Verification | Run test suite with AddressSanitizer (ASan) to verify zero leaks on all error paths. | Pending |
