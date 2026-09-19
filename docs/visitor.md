# Maintainer Guide: Visitor & Evaluator Subsystem

> **Audience:** Developers maintaining or extending evaluation logic in `src/visitor.c`, `src/visitor_eval_*.c`, `src/include/visitor_internal.h`, and `src/include/ti_type.h`.

---

## 1. Master Dispatcher Architecture (`src/visitor.c`)

The evaluation engine is organized as a modular hierarchy coordinated by `visitor_visit()` in `src/visitor.c`:

```
                 visitor_visit(ctx, node)  [Master Dispatcher]
                             │
         ┌───────────────────┼───────────────────┬───────────────────┐
         ▼                   ▼                   ▼                   ▼
visitor_eval_binary.c  visitor_eval_control.c  visitor_eval_func.c  visitor_eval_variable.c
 (Math & Logic)        (If / While / Scopes)   (Builtins & Calls)   (Defs & Assigns)
                                                                     │
                                                                     ▼
                                                           visitor_eval_literal.c
                                                           (Leaf Constant Wrappers)
```

`visitor_visit()` acts as the recursive mediator: every sub-evaluator calls `visitor_visit(ctx, child_node)` to evaluate sub-trees without needing direct coupling between the sub-modules.

---

## 2. Expression Evaluation (`src/visitor_eval_binary.c`) — Hard Functions

### 2.1. `eval_binary_expr` (Operator Dispatch & Memory Lifecycle)
- **Operand Recursion:** Evaluates both `left` and `right` sub-trees:
  ```c
  value_t *left = visitor_visit(ctx, node->value.binary_expr.left);
  value_t *right = visitor_visit(ctx, node->value.binary_expr.right);
  ```
- **Type Checking:** Strictly enforces operand compatibility (`left->type == right->type`). Disallows implicit type coercions.
- **Critical Memory Cleanup:** Once the operation produces `result`, `left` and `right` are temporary intermediate values that **must be immediately destroyed**:
  ```c
  val_free_internal(left);
  val_free_internal(right);
  tracked_free(left);
  tracked_free(right);
  return result;
  ```
  Failing to free both operands here would cause exponential memory leakage on every mathematical expression evaluated.

---

### 2.2. Complex Operator Functions

#### `binary_add(value_t *left, value_t *right, int line)`
- **Polymorphism:** Supports integers, floats, and string concatenation.
- **Diagnostics:** Emits `[Runtime Error] ... at line %d` before calling `ti_fatal()` on invalid operands.
- **String Concatenation Logic:**
  ```c
  int length = strlen(s_left) + strlen(s_right) + 1;
  value->string_val = tracked_calloc(1, sizeof(char) * length);
  strcat(value->string_val, s_left);
  strcat(value->string_val, s_right);
  ```
  Allocates exact memory for the concatenated string on the tracked heap.

#### `binary_div(value_t *left, value_t *right, int line)` (Division by Zero Prevention)
- **Zero Divisor Check:**
  - Integer: `if (right->int_val == 0)` &rarr; logs `[Runtime Error] Division by zero error at line %d` and halts via `ti_fatal()`.
  - Float: `if (right->float_val == 0.0f)` &rarr; logs `[Runtime Error] Division by zero error at line %d` and halts via `ti_fatal()`.
  Guarantees that script errors never trigger an unhandled CPU `SIGFPE` exception.

#### `binary_logical_and` & `binary_logical_or`
- Strictly checks `left->type == VAL_BOOL && right->type == VAL_BOOL`.
- Returns a newly initialized boolean `value_t` holding `(left->bool_val && right->bool_val)`.

#### `eval_unary_expr`
- Dispatches `OP_POS` (+), `OP_NEG` (-), and `OP_NOT` (!).
- Validates that unary plus/minus only operate on numbers, and unary not (`!`) only operates on `VAL_BOOL`.
- Frees the single `operand` intermediate value before returning `result`.

---

## 3. Control Flow & Scope Execution (`src/visitor_eval_control.c`)

### 3.1. `eval_boolean_condition`
- Evaluates the condition AST node.
- Verifies that `value->type == VAL_BOOL`.
- Copies the primitive boolean `bool res = value->bool_val`, immediately frees `value`, and returns `res`.

### 3.2. `visitor_execute_body` (Hard / Scope Boundary & Propagation Logic)
- **Purpose:** Executes a compound block within an isolated child scope and bubbles up control flow signals.
- **Lifecycle:**
  1. Creates local scope: `context_t *local_ctx = context_init()`.
  2. Chains to parent: `local_ctx->parent = parent_ctx`.
  3. Executes block statements: `visitor_visit(local_ctx, body_node)`.
  4. **Control Flow Propagation:**
     ```c
     if (local_ctx->flow_state != FLOW_NORMAL) {
         parent_ctx->flow_state = local_ctx->flow_state;
         parent_ctx->return_value = local_ctx->return_value;
         local_ctx->return_value = NULL; /* Transfer ownership */
     }
     ```
  5. Tears down local scope:
     ```c
     context_free_internal(local_ctx);
     tracked_free(local_ctx);
     ```
  All variables defined inside the child block are freed, while control flags and return values safely bubble up to the enclosing parent frame.

### 3.3. `eval_compound_statement` (Early-Exit Statement Sequencer)
- Iterates over `statements[0..statement_count-1]`.
- Evaluates each statement via `visitor_visit(ctx, statement)`.
- **Early Termination:** Checks `ctx->flow_state != FLOW_NORMAL` after each statement. If a jump (`return`, `break`, `continue`) occurred, immediately breaks out of the loop, skipping subsequent statements in the block.

### 3.4. Jump Evaluators (`eval_return_statement`, `eval_break_statement`, `eval_continue_statement`)
- **`eval_return_statement`:** Evaluates the expression (or creates `val_new_void()`), sets `ctx->flow_state = FLOW_RETURN`, and stores payload in `ctx->return_value`.
- **`eval_break_statement`:** Sets `ctx->flow_state = FLOW_BREAK`.
- **`eval_continue_statement`:** Sets `ctx->flow_state = FLOW_CONTINUE`.

---

## 4. Variable Evaluation (`src/visitor_eval_variable.c`)

- **`eval_variable_definition`:** Evaluates initializer expression, verifies type compatibility between declared variable type and value type (halting with an error via `val_type_to_str` if mismatched), duplicates the variable name string, and adds to current scope via `context_add_variable(ctx, name, value)`.
- **`eval_assignment`:**
  1. Looks up variable via `context_find_variable(ctx, id)`.
  2. Evaluates new value.
  3. **Type Safety:** Verifies `variable->value->type == val->type`. If type differs, halts with type mismatch error.
  4. Deallocates previous value (`val_free_internal(variable->value)`) and assigns the new `value_t *`.
- **`eval_identifier`:** Locates variable and returns an isolated copy via `context_copy_value(variable)`.

---

## 5. Functions & Function Registry (`src/visitor_eval_func.c`, `src/include/function.h`)

### 5.1. Dual Table Architecture & Co-Occurrent Teardown
Function management in `src/visitor_eval_func.c` maintains strict separation between native C bindings and user script functions:

- **Built-in Registry (`s_builtin_functions`):** Static array storing registered host C functions (`native_fn_t`), populated via `register_builtin_function()`.
- **User Functions Table (`s_user_functions`):** `_Thread_local` dynamic array storing script-defined functions (`FUNC_TI`), isolated per execution thread.
- **Global Context Binding (`s_global_context`):** `_Thread_local` pointer tracking the active root execution scope via `visitor_set_global_context()` and `visitor_get_global_context()`.
- **Co-Occurrent Teardown (`user_functions_clear`):**
  When execution terminates, `src/TienInterpreter.c` unregisters the root environment via `visitor_set_global_context(NULL)`. This immediately triggers `user_functions_clear()`, which:
  1. Iterates through all registered user functions in `s_user_functions`.
  2. Invokes `params_free()` on `s_user_functions[i].params` to free heap-allocated parameter names and arrays.
  3. Releases the dynamic array via `tracked_free(s_user_functions)`.
  4. Resets `s_user_functions = NULL` and `s_user_function_count = 0`.

### 5.2. Parameter & Function Representation
```c
typedef struct PARAM_STRUCT {
    val_type_t type;          /* Expected parameter type (VAL_INT, VAL_STRING, etc.) */
    char *name;               /* Parameter identifier name (e.g. "a", "count") */
} param_t;

typedef struct FUNCTION_STRUCT {
    const char *name;          /* Function identifier in Ti scripts */
    func_type_t type;          /* FUNC_BUILTIN or FUNC_TI */
    val_type_t  return_type;   /* Declared return type (VAL_INT, VAL_VOID, etc.) */
    param_t    *params;        /* Array of parameter metadata */
    int         param_count;   /* Number of declared parameters (-1 for variadic builtins) */
    union {
        native_fn_t native_fn; /* Builtin native C callback */
        ast_t      *def;       /* Interpreter TI AST function definition */
    };
} function_t;
```

- `register_builtin_function`: Registers native C callbacks (`FUNC_BUILTIN`) with full signature metadata into `s_builtin_functions`:
  ```c
  bool register_builtin_function(const char *name, val_type_t return_type, param_t *params, int param_count, native_fn_t function);
  ```
  Sets `params = NULL` and `param_count = -1` for variadic C functions (such as `print`), or explicit `params` and `param_count` for typed native functions (e.g. `delay`, `relay_set_state`).

### 5.3. Lookup Helpers
- `builtin_find_function(const char *name)`: Scans `s_builtin_functions` by identifier.
- `user_find_function(const char *name)`: Scans `s_user_functions` by identifier.

### 5.4. `eval_function_definition`
- Takes the parsed `AST_FUNCTION_DEFINITION` node.
- **Redefinition Check:** Verifies that `name` does not collide with either built-in functions (`builtin_find_function`) or previously defined user functions (`user_find_function`). Emits fatal error if a collision occurs.
- **Allocation:** Expands `s_user_functions` using `tracked_realloc()`.
- **Parameter Cloning:** If `param_count > 0`, allocates parameter metadata array via `tracked_calloc()` and clones each parameter identifier via `tracked_strdup()`.
- **Registration:** Populates `name`, `type = FUNC_TI`, `return_type`, `params`, `param_count`, and `def = node`, then increments `s_user_function_count`. Always returns `NULL`.

### 5.5. `eval_function_call`
1. Evaluates all argument expressions sequentially in the caller's context (`visitor_visit(ctx, args[i])`).
2. Collects evaluated argument values into `value_t **argv` allocated via `tracked_calloc()`.
3. **Symbol Resolution Hierarchy:**
   - First queries user-defined script functions via `user_find_function(func_name)`.
   - If not found, falls back to native functions via `builtin_find_function(func_name)`.
   - If neither matches, logs `[Runtime Error] Call to undefined function '%s' at line %d`, halts via `ti_fatal()`, and frees `argv`.
4. Delegates call execution to `run_function(ctx, func, argv, argc, node)`.
5. Cleans up evaluated arguments via `val_free(argv[i])` and deallocates `argv` via `tracked_free()`.
6. Returns the resulting `value_t *`.

### 5.6. `run_function` (Gatekeeper: Type & Count Validation)
- Signature:
  ```c
  value_t *run_function(context_t *ctx, function_t *func, value_t **argv, int argc, ast_t *node);
  ```
- **Execution Flow:**
  1. **Count Check:** If `func->param_count >= 0`, verifies `argc == func->param_count`. Emits fatal error if mismatched.
  2. **Type Check:** Loops through `0` to `func->param_count - 1`, asserting `argv[i]->type == func->params[i].type`. Emits fatal error with parameter name, expected type, and actual type at `node->line` if mismatched.
  3. **Dispatch:**
     - `FUNC_BUILTIN`: Calls `func->native_fn(argv, argc)`.
     - `FUNC_TI`: Calls `run_ti_function(ctx, func, argv, argc, node)`.

### 5.7. `run_ti_function`
- Signature:
  ```c
  value_t *run_ti_function(context_t *ctx, function_t *func, value_t **argv, int argc, ast_t *node);
  ```
- Handles execution of user-defined Ti function definitions: creates a new stack frame context chained to `visitor_get_global_context()`, binds arguments, evaluates the function body AST, inspects flow state (`FLOW_RETURN`, `FLOW_BREAK`, `FLOW_CONTINUE`), validates return type, and safely unwinds the local frame.

---

## 6. Literal Evaluators (`src/visitor_eval_literal.c`)

### Unified Implementation Architecture
Literal evaluators (`eval_int_literal`, `eval_float_literal`, `eval_string_literal`, `eval_boolean_literal`) are simple leaf AST node unwrappers:
- **Mechanism:** Each function directly delegates to constructor helpers (`val_new_int`, `val_new_float`, `val_new_string`, `val_new_bool`) defined in `src/value.c` to produce boxed `value_t *` instances without duplicating allocation and string duplication logic.
- **Reason for Separation:** Keeps primitive boxing completely separate from AST dispatching, making memory lifetimes transparent.
