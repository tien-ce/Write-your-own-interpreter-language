# Maintainer Guide: Context & Value Runtime System

> **Audience:** Developers modifying runtime execution scopes, variable storage, or dynamic value representation in `src/context.c`, `src/value.c`, `src/include/value.h`, and `src/include/context.h`.

---

## 1. Structs & Memory Layout

### 1.1. `value_t` (`struct VALUE_STRUCT`)
```c
/* Defined in src/include/ti_type.h */
typedef enum {
    VAL_NULL,
    VAL_INT,
    VAL_FLOAT,
    VAL_STRING,
    VAL_BOOL,
    VAL_VOID,
} val_type_t;

typedef struct VALUE_STRUCT {
    val_type_t type;
    union {
        int int_val;
        float float_val;
        char *string_val;
        bool bool_val;
    };
} value_t;
```
- **Purpose:** Represents dynamically typed runtime values during AST execution.
- **Tagged Union Design:**
  - `type`: Discriminating tag identifying which union field is valid.
  - `string_val`: The only field requiring heap management (`tracked_strdup` / `tracked_free`).
- **Destructor (`val_free_internal` & `val_free`):**
  - `val_free_internal`: Frees internal dynamic payload (`string_val` when `type == VAL_STRING`). Does not deallocate the `value_t` container.
  - `val_free`: Complete destructor. Checks for `NULL`, invokes `val_free_internal`, and releases the `value_t` container via `tracked_free`.

---

### 1.2. `variable_t` (`struct VARIABLE_STRUCT`)
```c
typedef struct VARIABLE_STRUCT {
    const char *name; // Variable identifier name
    value_t *value;   // Pointer to active value object
} variable_t;
```
- **Purpose:** Represents an active variable binding in a specific scope.
- **Ownership & Destructor (`variable_free`):** Owns `value` and heap-allocated `name`. `variable_free` deallocates `var->value` via `val_free`, releases `(void *)var->name` via `tracked_free`, and deallocates `var` itself.

---

### 1.3. `flow_state_t` & `context_t` (`struct CONTEXT_STRUCT`)
```c
typedef enum {
    FLOW_NORMAL,     // Sequential execution within compound block
    FLOW_RETURN,     // Return statement triggered: unwinds scopes up to function boundary
    FLOW_BREAK,      // Break statement triggered: breaks out of innermost loop
    FLOW_CONTINUE,   // Continue statement triggered: jumps to next iteration of loop
} flow_state_t;

typedef struct CONTEXT_STRUCT {
    struct CONTEXT_STRUCT *parent; // Pointer to enclosing parent scope (NULL for root)
    variable_t **variables;        // Array of variable pointers in this scope
    int variable_count;            // Number of variables in this scope
    flow_state_t flow_state;       // Active flow interruption flag
    value_t *return_value;         // Evaluated return payload (owned by this context)
} context_t;
```
- **Purpose:** Implements a lexical environment frame (call frame / block scope) augmented with control flow signal propagation.
- **Decoupled Function Storage:** Functions are decoupled from `context_t`. Built-in functions and user-defined script functions reside in dedicated symbol tables within `src/visitor_eval_func.c`. User function lifecycle is coordinated with the global execution context through co-occurrent teardown (`visitor_set_global_context(NULL)` triggering `user_functions_clear()`).
- **Scope Hierarchy:**
  - Root scope has `parent = NULL`.
  - Child scopes (inside `if`, `while`, or function bodies) point their `parent` pointer to the enclosing context.
- **Control Flow Interruption:**
  - When non-sequential control flow occurs (`return`, `break`, `continue`), `flow_state` is updated from `FLOW_NORMAL` to the corresponding flag.
  - If returning a value, `return_value` holds the evaluated `value_t *`.


---

## 2. Hard / Critical Logic in Context Operations

### 2.1. Lexical Scope Resolution (`context_find_variable`)
```c
variable_t *context_find_variable(context_t *ctx, const char *variable_name)
{
    context_t *current_ctx = ctx;
    while (current_ctx != NULL) {
        for (int i = 0; i < current_ctx->variable_count; i++) {
            if (strcmp(current_ctx->variables[i]->name, variable_name) == 0) {
                return current_ctx->variables[i];
            }
        }
        current_ctx = current_ctx->parent;
    }
    return NULL;
}
```
- **Mechanism:** Walks upwards along the `parent` linked chain from the current local scope to the root global scope.
- **Shadowing:** Inner variables match first in the loop, naturally shadowing outer variables with the same name. Returns `NULL` if not found in any enclosing scope.

---

### 2.2. Value Isolation via Deep Copying (`context_copy_value`)
```c
value_t *context_copy_value(variable_t *variable)
```
- **Why this function is critical:**
  When an AST node evaluates an identifier (e.g. evaluating `x` in `x + 1`), it **must not** return the raw pointer to `variable->value`. If it did:
  1. The binary evaluator would deallocate `left` after addition, destroying the variable's value inside the symbol table!
  2. Mutating operations would cause unexpected side effects across references.
- **Deep Copy Rule:** For strings, calls `tracked_strdup(variable->value->string_val)` to ensure a completely isolated copy on the heap.

---

### 2.3. Redefinition Collision Detection (`context_add_variable`)
- Checks only the **current local scope** (`ctx->variables[0..variable_count]`):
  - If a variable with `name` already exists in *this* scope, logs an error (`"Redefinition of variable"`) and calls `ti_fatal()`.
  - Shadowing an outer variable from a parent scope is permitted.
- Dynamically resizes the `ctx->variables` pointer array using `tracked_realloc`.

---

### 2.4. Modular Destructors Architecture

To ensure strict memory lifecycle control and prevent heap leaks across nested scopes, the runtime implements isolated, single-responsibility destructors:

| Destructor | Target | Responsibility & Memory Invariants |
| :--- | :--- | :--- |
| `val_free(value_t *value)` | `value_t *` | Calls `val_free_internal()` to release heap payloads (e.g. `string_val` via `tracked_free`), then deallocates the `value_t` container itself. Tolerates `NULL`. |
| `variable_free(variable_t *var)` | `variable_t *` | Deallocates variable payload via `val_free(var->value)`, deallocates heap-allocated identifier string `(void *)var->name` via `tracked_free`, and releases the `variable_t` struct. Tolerates `NULL`. |
| `params_free(param_t *params, int param_count)` | `param_t *` | Traverses parameter metadata array from `0` to `param_count - 1`, frees heap-allocated parameter identifier strings `params[i].name` via `tracked_free`, and releases the `params` contiguous array buffer via `tracked_free`. Tolerates `NULL`. |
| `function_free(function_t *func)` | `function_t *` | For user-defined functions (`FUNC_TI`), cleans up owned parameter metadata via `params_free(func->params, func->param_count)`, then releases the `function_t` struct via `tracked_free`. Tolerates `NULL`. |
| `context_free_internal(context_t *ctx)` | `context_t *` | Cleans up local scope bindings: iterates over `ctx->variables`, calling `variable_free` on each entry, and frees `ctx->variables` table. Frees unconsumed `ctx->return_value` via `val_free`. Does not free `ctx` or `ctx->parent`. |
| `context_free(context_t *ctx)` | `context_t *` | Invokes `context_free_internal(ctx)` to release all scoped bindings, then releases the `context_t` allocation itself via `tracked_free(ctx)`. |

### 2.5. Scope Destruction & Teardown Protocol (`context_free_internal`)
- **Variable Clean-up:**
  - Iterates through `0` to `ctx->variable_count - 1` invoking `variable_free(ctx->variables[i])`.
  - Frees the `ctx->variables` table via `tracked_free`, resets pointer to `NULL`, and resets count to `0`.
- **Return Value Safety:** If `ctx->return_value != NULL` (e.g. unconsumed return payload due to an error, loop break, or premature termination), safely deallocates `ctx->return_value` via `val_free(ctx->return_value)` and clears pointer to `NULL`.
- **Note:** Does **not** modify or free `ctx->parent`, as the parent context belongs to the enclosing caller.
- **Decoupled Function Teardown:** `context_t` no longer holds or frees function tables. Function records are managed in `src/visitor_eval_func.c` and freed when `visitor_set_global_context(NULL)` triggers `user_functions_clear()`.

