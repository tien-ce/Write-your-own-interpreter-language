# Maintainer Guide: Context & Value Runtime System

> **Audience:** Developers modifying runtime execution scopes, variable storage, or dynamic value representation in `src/context.c`, `src/value.c`, and `src/include/visitor_internal.h`.

---

## 1. Structs & Memory Layout

### 1.1. `value_t` (`struct VALUE_STRUCT`)
```c
typedef enum {
    VAL_NULL,
    VAL_INT,
    VAL_FLOAT,
    VAL_STRING,
    VAL_BOOL,
} value_type_t;

typedef struct VALUE_STRUCT {
    value_type_t type;
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
- **Destructor (`val_free_internal`):**
  Only frees `string_val` when `type == VAL_STRING`. Never touches numeric fields.

---

### 1.2. `variable_t` (`struct VARIABLE_STRUCT`)
```c
typedef struct VARIABLE_STRUCT {
    const char *name; // Variable identifier name
    value_t *value;   // Pointer to active value object
} variable_t;
```
- **Purpose:** Represents an active variable binding in a specific scope.
- **Ownership:** Owns `value`. When the variable is updated (in assignment), the old `value` is deallocated via `val_free_internal` + `tracked_free` before binding the new `value_t`.

---

### 1.3. `context_t` (`struct InterpreterContext`)
```c
typedef struct InterpreterContext {
    struct InterpreterContext *parent; // Pointer to enclosing parent scope
    variable_t **variables;            // Array of variable pointers in this scope
    int variable_size;                 // Number of variables in this scope
} context_t, InterpreterContext;
```
- **Purpose:** Implements a lexical environment frame (call frame / block scope).
- **Scope Hierarchy:**
  - Root scope has `parent = NULL`.
  - Child scopes (inside `if`, `while`, or function bodies) point their `parent` pointer to the enclosing context.

---

## 2. Hard / Critical Logic in Context Operations

### 2.1. Lexical Scope Resolution (`context_find_variable`)
```c
variable_t *context_find_variable(context_t *ctx, const char *variable_name)
{
    context_t *current_ctx = ctx;
    while (current_ctx != NULL) {
        for (int i = 0; i < current_ctx->variable_size; i++) {
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
- Checks only the **current local scope** (`ctx->variables[0..variable_size]`):
  - If a variable with `name` already exists in *this* scope, logs an error (`"Redefinition of variable"`) and calls `ti_fatal()`.
  - Shadowing an outer variable from a parent scope is permitted.
- Dynamically resizes the `ctx->variables` pointer array using `tracked_realloc`.

---

### 2.4. Scope Destruction (`context_free_internal`)
- Iterates through `0` to `variable_size`:
  1. Frees variable value via `val_free_internal(var->value)` and `tracked_free(var->value)`.
  2. Frees `var` struct.
- Frees the `ctx->variables` pointer array.
- **Note:** Does **not** modify or free `ctx->parent`, as the parent context belongs to the enclosing caller.
