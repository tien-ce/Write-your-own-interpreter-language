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

#### `binary_add(value_t *left, value_t *right)`
- **Polymorphism:** Supports integers, floats, and string concatenation.
- **String Concatenation Logic:**
  ```c
  int length = strlen(s_left) + strlen(s_right) + 1;
  value->string_val = tracked_calloc(1, sizeof(char) * length);
  strcat(value->string_val, s_left);
  strcat(value->string_val, s_right);
  ```
  Allocates exact memory for the concatenated string on the tracked heap.

#### `binary_div(value_t *left, value_t *right)` (Division by Zero Prevention)
- **Zero Divisor Check:**
  - Integer: `if (right->int_val == 0)` &rarr; logs fatal error `ti_fatal()`.
  - Float: `if (right->float_val == 0.0f)` &rarr; logs fatal error `ti_fatal()`.
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

### 3.2. `visitor_execute_body` (Hard / Scope Boundary Logic)
- **Purpose:** Executes a compound block within an isolated child scope.
- **Lifecycle:**
  1. Creates local scope: `context_t *local_ctx = context_init()`.
  2. Chains to parent: `local_ctx->parent = parent_ctx`.
  3. Executes block statements: `visitor_visit(local_ctx, body_node)`.
  4. Tears down local scope:
     ```c
     context_free_internal(local_ctx);
     tracked_free(local_ctx);
     ```
  All variables defined inside the loop or if branch are automatically freed when the block ends.

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

## 5. Functions & Built-in Registry (`src/visitor_eval_func.c`)

### Native C Builtin Registry
- `s_builtins`: Dynamic table of `builtin_func_t` (`name`, `fn` pointer).
- `register_builtin_function`: Uses standard `realloc` to append functions (e.g. `print`, `println`) to the table.

### `eval_function_call`
1. Evaluates all argument expressions sequentially in the caller's context.
2. Accumulates argument values into `value_t **argv`.
3. Looks up function name in `s_builtins`.
4. Invokes the native callback `ret = s_builtins[i].fn(argv, argc)`.
5. Cleans up: Iterates and frees every `argv[i]` intermediate value, then frees `argv`.

---

## 6. Literal Evaluators (`src/visitor_eval_literal.c`)

### Unified Implementation Architecture
Literal evaluators (`eval_int_literal`, `eval_float_literal`, `eval_string_literal`, `eval_boolean_literal`) are simple leaf AST node unwrappers:
- **Mechanism:** Each function directly delegates to constructor helpers (`val_new_int`, `val_new_float`, `val_new_string`, `val_new_bool`) defined in `src/value.c` to produce boxed `value_t *` instances without duplicating allocation and string duplication logic.
- **Reason for Separation:** Keeps primitive boxing completely separate from AST dispatching, making memory lifetimes transparent.
