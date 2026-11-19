# Maintainer Guide: Visitor & Evaluator Subsystem

> **Audience:** Developers maintaining or extending evaluation logic in `src/ti_runtime_visitor.c`, `src/ti_runtime_eval_*.c`, `src/include/ti_runtime_visitor.h` (unified visitor header), and `src/include/ti_type.h`.

---

### 1. Master Dispatcher Architecture (`src/ti_runtime_visitor.c`)

The evaluation engine is organized as a modular hierarchy coordinated by `visitor_visit()` in `src/ti_runtime_visitor.c`:

```
                 visitor_visit(rt, ctx, node)  [Master Dispatcher]
                              │
          ┌───────────────────┼───────────────────┬───────────────────┐
          ▼                   ▼                   ▼                   ▼
ti_runtime_eval_binary.c  ti_runtime_eval_control.c  ti_runtime_eval_func.c  ti_runtime_eval_variable.c
 (Math & Logic)        (If / While / Scopes)   (Builtins & Calls)   (Defs & Assigns)
                                                                      │
                                                                      ▼
                                                            ti_runtime_eval_literal.c
                                                            (Leaf Constant Wrappers)
```

`visitor_visit()` acts as the recursive mediator: every sub-evaluator calls `visitor_visit(rt, ctx, child_node)` to evaluate sub-trees without needing direct coupling between the sub-modules. All visitor evaluator prototypes and master dispatchers are consolidated directly inside `src/include/ti_runtime_visitor.h` as the unified visitor header.

---

## 2. Expression Evaluation (`src/ti_runtime_eval_binary.c`) — Hard Functions

### 2.1. `eval_binary_expr` (Operator Dispatch & Memory Lifecycle)
- **Operand Recursion:** Evaluates both `left` and `right` sub-trees:
  ```c
  value_t *left = visitor_visit(rt, ctx, node->value.binary_expr.left);
  value_t *right = visitor_visit(rt, ctx, node->value.binary_expr.right);
  ```
- **null comparison (before the type check):** when the operator is `OP_DEQ` / `OP_NEQ` and either operand is `VAL_NULL`, the result is computed right there: `both_null = (left->type == right->type)`, `==` gives `both_null`, `!=` gives `!both_null`, then it jumps to the cleanup. So `x == null` works for every type of `x`. It must run before the type check and must not reach `binary_equal`: there `5 == null` would compare `5` with the null's unused `int_val`. `binary_equal` / `binary_not_equal` therefore have no `VAL_NULL` case.
- **Type Checking:** Strictly enforces operand compatibility (`left->type == right->type`). Disallows implicit type coercions. Every other operator with `null` fails here (`int and null`) or, for `null` with `null`, in the `default` branch of each `binary_*` handler (`Unexpected operands null, null in binary add`); `binary_logical_*` and `binary_bitwise` reject it by their own type checks.
- **Critical Memory Cleanup:** Once the operation produces `result`, `left` and `right` are temporary intermediate values that **must be immediately destroyed**:
  ```c
  val_free_internal(&rt->alloc_list, left);
  val_free_internal(&rt->alloc_list, right);
  tracked_free(&rt->alloc_list, left);
  tracked_free(&rt->alloc_list, right);
  return result;
  ```
  Failing to free both operands here would cause exponential memory leakage on every mathematical expression evaluated.

---

### 2.2. Complex Operator Functions

#### `binary_add(ti_runtime_t *rt, value_t *left, value_t *right, int line)`
- **Polymorphism:** Supports integers, floats, and string concatenation.
- **Diagnostics:** Raises `TI_ERR_TYPE_MISMATCH` with `ti_raise(rt, ..., line, ...)` on invalid operands and returns `NULL`; the caller frees both operands.
- **String Concatenation Logic:**
  ```c
  int length = strlen(s_left) + strlen(s_right) + 1;
  value->string_val = tracked_calloc(1, sizeof(char) * length);
  strcat(value->string_val, s_left);
  strcat(value->string_val, s_right);
  ```
  Allocates exact memory for the concatenated string on the tracked heap.

#### `int` overflow (`binary_add`, `binary_sub`, `binary_mul`, `binary_div`, `OP_NEG` in `eval_unary_expr`)
- An `int` result outside 32 bits raises `TI_ERR_VALUE_OUT_OF_RANGE` (`Integer overflow in '+': %d + %d`); it never wraps (signed overflow is undefined behavior in C) and never reaches a trapping CPU instruction.
- `+`, `-`, `*` use `__builtin_add_overflow` / `__builtin_sub_overflow` / `__builtin_mul_overflow` (GCC/Clang, also ESP-IDF), which compute into `value->int_val` and report the overflow.
- `/`: the only overflowing quotient is `INT_MIN / -1`, checked explicitly after the zero check. Without it x86's `idiv` raises SIGFPE and kills the host process.
- Unary `-`: only `-INT_MIN`. `%` cannot overflow (`binary_mod` returns 0 for `% -1`).
- Compound assignment and `++`/`--` are binary nodes after parsing, so they are covered. Shifts and hex literals wrap by design; `float` is not checked.
- Decimal literals are range-checked by the parser instead (see [parser.md](parser.md), level 9), so no out-of-range `int` enters through a literal.

#### `binary_div(ti_runtime_t *rt, value_t *left, value_t *right, int line)` (Division by Zero Prevention)
- **Zero Divisor Check:**
  - Integer: `if (right->int_val == 0)` &rarr; raises `TI_ERR_DIV_ZERO` (`Division by zero error`) and returns `NULL`.
  - Float: `if (right->float_val == 0.0f)` &rarr; raises `TI_ERR_DIV_ZERO` (`Division by zero error`) and returns `NULL`.
  Guarantees that script errors never trigger an unhandled CPU `SIGFPE` exception.

#### `binary_mod(ti_runtime_t *rt, value_t *left, value_t *right, int line)` (`%`, `OP_MOD`)
- `int` only (the caller already checked both operands share a type): anything else raises `TI_ERR_TYPE_MISMATCH` (`Operator '%' only supports int operands, ...`).
- `right == 0` raises `TI_ERR_DIV_ZERO` (`Modulo by zero error`).
- `right == -1` returns `0` without dividing: C's `INT_MIN % -1` overflows inside the CPU's divide instruction (`idiv` on x86 traps: SIGFPE kills the host), and every other `x % -1` is `0` anyway.
- Otherwise C's `%`: the sign follows the left operand (`-7 % 3` is `-1`).

#### `binary_logical_and` & `binary_logical_or`
- Strictly checks `left->type == VAL_BOOL && right->type == VAL_BOOL`.
- Returns a newly initialized boolean `value_t` holding `(left->bool_val && right->bool_val)`.

#### `binary_bitwise` (`&`, `|`, `^`, `<<`, `>>`)
- Both operands must be `VAL_INT`; any other type raises `TI_ERR_TYPE_MISMATCH` ("Operator '&' only supports int operands, ...").
- `<<` / `>>` require a shift count in `0..31`, otherwise `TI_ERR_VALUE_OUT_OF_RANGE`. `<<` shifts as `uint32_t` so bits past the sign bit wrap; `>>` is arithmetic (`~(~l >> r)` for negative values), both without undefined behavior.
- The result is a plain `int`. There is no per-operator byte check: a value is range-checked only when stored into a narrower slot (e.g. `val_bytes_set` rejects anything outside `0..255`), so scripts mask explicitly with `& 0xFF`.

#### `eval_unary_expr`
- Dispatches `OP_POS` (+), `OP_NEG` (-), `OP_NOT` (!), and `OP_BNOT` (~).
- Validates that unary plus/minus only operate on numbers, unary not (`!`) only on `VAL_BOOL`, and complement (`~`) only on `VAL_INT`. There is no separate null check: `null` is rejected by each operator's own check (`Unary '!' only supports bool, got type null`), on purpose, so `!x` is never a null test.
- Frees the single `operand` intermediate value before returning `result`.

---

## 3. Control Flow & Scope Execution (`src/ti_runtime_eval_control.c`)

### 3.1. `eval_boolean_condition`
- Evaluates the condition AST node: `visitor_visit(rt, ctx, cond_node)`.
- Verifies that `value->type == VAL_BOOL`.
- Copies the primitive boolean `bool res = value->bool_val`, immediately frees `value` via `val_free(&rt->alloc_list, value)`, and returns `res`.

### 3.2. `visitor_execute_body` (Scope Boundary & Propagation Logic)
- **Purpose:** Executes a compound block within an isolated child scope and bubbles up control flow signals.
- **Lifecycle:**
  1. Creates local scope: `context_t *local_ctx = context_init(&rt->alloc_list)`.
  2. Chains to parent: `local_ctx->parent = parent_ctx`.
  3. Executes block statements: `visitor_visit(rt, local_ctx, body_node)`.
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
     context_free_internal(&rt->alloc_list, local_ctx);
     tracked_free(&rt->alloc_list, local_ctx);
     ```
  All variables defined inside the child block are freed, while control flags and return values safely bubble up to the enclosing parent frame.

### 3.3. Loop Interruption & `eval_while_statement`
- Loop condition evaluation checks `eval_boolean_condition(rt, ctx, node->value.while_statement.condition)`.
- **Cancellation / Interruption Check:**
  ```c
  if (rt != NULL && rt->is_interrupted) {
      break;
  }
  ```
  Guarantees that asynchronous cancellation requests via `ti_runtime_interrupt(rt)` break out of active loops promptly.

### 3.4. `eval_compound_statement` (Early-Exit Statement Sequencer)
- Iterates over `statements[0..statement_count-1]`.
- Evaluates each statement via `visitor_visit(rt, ctx, statement)`.
- **Early Termination:** Checks `ctx->flow_state != FLOW_NORMAL` after each statement. If a jump (`return`, `break`, `continue`) occurred, immediately breaks out of the loop, skipping subsequent statements in the block.

### 3.5. Jump Evaluators (`eval_return_statement`, `eval_break_statement`, `eval_continue_statement`)
- **`eval_return_statement`:** Evaluates the expression (or creates `val_new_void(&rt->alloc_list)`), sets `ctx->flow_state = FLOW_RETURN`, and stores payload in `ctx->return_value`.
- **`eval_break_statement`:** Sets `ctx->flow_state = FLOW_BREAK`.
- **`eval_continue_statement`:** Sets `ctx->flow_state = FLOW_CONTINUE`.

---

## 4. Variable Evaluation (`src/ti_runtime_eval_variable.c`)

- **`eval_variable_definition`:** Creates the value in one of three ways: `size != NULL` (`list int l[n];`) → `eval_sized_list` (evaluates `n`, checks `int` and `0..TI_MAX_LIST_ITEMS`, rejects a `list` element type, `val_list_reserve(n)` once, then pushes `n` values from `eval_default_value` with `val_list_push_raw`); `value == NULL` (`int x;`) → `eval_default_value(rt, node, type, element_type)`; otherwise evaluates the initializer expression via `visitor_visit(rt, ctx, node->value.variable_definition.value)`. Then it verifies type compatibility with `val_can_replace(declared_type, value->type)` (so `null` fits any declared type; halting with an error via `val_type_to_str` if mismatched), duplicates the variable name string onto `&rt->alloc_list`, and adds to current scope via `context_add_variable(&rt->alloc_list, ctx, name, value)`.
- **`eval_assignment`:**
  1. Looks up variable via `context_find_variable(ctx, id)`.
  2. Evaluates new value: `visitor_visit(rt, ctx, node->value.assignment.value)`.
  3. **Type Safety:** `val_can_replace(variable->value->type, val->type)`: the same type or `null` is accepted; a variable holding `null` accepts nothing (`Variable 'x' is null and cannot be assigned`), any other difference is `Type mismatch in assignment`. Subscript targets get the same rule from `val_list_set` (against the element held now) and `val_dict_set` (existing key).
  4. Deallocates previous value (`val_free_internal(&rt->alloc_list, variable->value)`) and assigns the new `value_t *`.
- **`eval_array_access`** (`object[index]`, read): evaluates `object` with `visitor_visit` first, then the index. The container value it gets is a `val_copy`: a list, dict or bytes is the same object (reference count + 1), so the read sees the real container and the reference keeps it alive while the index expression runs script code. Reads through `val_dict_get` / `val_list_get` (also `val_copy`, so `s[0]` returned to an outer subscript is the element itself) or `val_bytes_get`; frees the container reference at `out`. Error messages name the variable for `l[i]` and say `value` for a nested level.
- **`eval_assignment`, `AST_ARRAY_ACCESS` target** (`object[index] = rhs`): same order (rhs, then `object`, then index) and the same shared reference, then `val_dict_set` / `val_list_set` / `val_bytes_set` on it, so `s[0]["k"] = v` writes into the dict held by `s`. No `variable_t` is used: only `x = rhs` (`AST_IDENTIFIER`) and definitions touch the variable itself. The rhs is deep-copied in by the set functions when it is a list or dict.
- **`eval_null_literal`** (`ti_runtime_eval_literal.c`, `AST_NULL`): returns a new heap `val_new_null()` (never the static `TI_VAL_OK`, which also has type `VAL_NULL`), or raises `TI_ERR_NO_MEMORY`. `eval_list_literal` raises `TI_ERR_TYPE_MISMATCH` (`List element 0 is null: cannot infer the list element type`) when it must infer the element type from a `null` first element, and the usual element mismatch for a later `null`.
- **`eval_identifier`:** Locates variable and returns an isolated copy via `context_copy_value(ctx ? ctx->alloc_list : NULL, variable)`.

---

## 5. Functions & Function Registry (`src/ti_runtime_eval_func.c`, `src/include/ti_type_func.h`)

### 5.1. Host Registry & Namespace Function Storage
Function management in `src/ti_runtime_eval_func.c` maintains strict separation between host native C bindings and user script functions:

- **Built-in Registry (`s_builtin_functions`):** Hash map storing registered host C functions (`native_fn_t`), populated during startup via `register_builtin_function()`.
- **Namespaces (`ti_namespace_t`, `src/include/ti_runtime_namespace.h`):** Every AST that runs has its own namespace: `globals` (its top-level variables), `functions` (its `FUNC_TI` functions, an array on `rt->alloc_list` resized by `tracked_realloc`) and `prog` (the program it runs). The main script uses `rt->main_ns`; each script module has its own namespace, created on its first `import`.
- **Running namespace (`rt->ns`):** The namespace whose code is being visited. Plain function names (definitions and calls) resolve in `rt->ns`; plain variable names resolve through the `ctx` chain, whose root is that namespace's `globals`. Only `ti_namespace_run` (a top level) and `run_ti_function` (a call) switch `rt->ns`, and both restore it on every path.
- **Teardown:** `ti_runtime_destroy` frees each script module's namespace (`ti_namespace_free`: variables, globals scope, each function's `params`, the function array) and its program, then the main globals, then everything left on `rt->alloc_list` with `tracked_free_all`.

### 5.2. Parameter & Function Representation
```c
typedef struct PARAM_STRUCT {
    val_type_t type;          /* Expected parameter type (VAL_INT, VAL_STRING, etc.); VAL_ANY (natives only) accepts any type */
    const char *name;         /* Never owned: a string literal in a native table, or borrowed from the AST_PARAM node */
    val_type_t element_type;  /* Expected element type when type is VAL_LIST (VAL_NULL accepts any) */
    const ti_handle_type_t *handle_type; /* Expected handle type when type is VAL_HANDLE (NULL accepts any) */
} param_t;

typedef struct FUNCTION_STRUCT {
    const char *name;          /* Function identifier in Ti scripts */
    func_type_t type;          /* FUNC_BUILTIN or FUNC_TI */
    val_type_t  return_type;   /* Declared return type (VAL_INT, VAL_VOID, etc.) */
    val_type_t  return_element_type; /* Element type when return_type is VAL_LIST */
    const param_t *params;     /* Read-only through this pointer */
    int         param_count;   /* Number of declared parameters (-1 for variadic builtins) */
    ti_namespace_t *ns;        /* FUNC_TI: namespace it was defined in (its home); NULL for builtins */
    union {
        native_fn_t native_fn; /* Builtin native C callback */
        ast_t      *def;       /* Interpreter TI AST function definition */
    };
} function_t;
```

**Who owns what behind `params`:**

| Function kind | `params` points to | `params[i].name` | Freed by |
| :--- | :--- | :--- | :--- |
| `FUNC_BUILTIN` (registered, method, module function) | the native's `static const param_t` table (flash on ESP32) | string literal | never |
| `FUNC_TI` | array built by `eval_function_definition` on `rt->alloc_list` | borrowed from the `AST_PARAM` node | `ti_namespace_free` (module) or `tracked_free_all` (main) at runtime destroy |

`params` is `const param_t *` because a builtin's table may sit in read-only memory. Only `function_free` casts `const` away, and only for `FUNC_TI`, whose array is writable heap memory.

A `FUNC_TI` borrows `name`, `def` and the parameter names from the AST. This is safe because every program outlives the functions it defines: `ti_execute` destroys the runtime before the host (or `ti_run_string*`) frees the main program, and a script module's program is freed by `ti_runtime_destroy` only after its namespace.

- `register_builtin_function`: Registers native C callbacks (`FUNC_BUILTIN`) with full signature metadata into the builtin hashmap:
  ```c
  bool register_builtin_function(const char *name, val_type_t return_type, const param_t *params, int param_count, native_fn_t function);
  ```
  Sets `params = NULL` and `param_count = -1` for fully variadic C functions (such as `print`: neither count nor types checked), or explicit `params` and `param_count` for typed native functions (e.g. `delay`, `relay_set_state`). A parameter typed `VAL_ANY` keeps the count enforced but accepts any argument type (e.g. `to_string(any)`). Registration fails if `param_count > 0` and `params` is `NULL`. The table is borrowed, never copied.

### 5.3. Lookup Helpers
- `builtin_find_function(const char *name)`: Looks up the builtin hash map.
- `user_find_function(ti_runtime_t *rt, const char *name)`: Scans `rt->ns->functions`, the running namespace only (main and each module have their own function names).
- `ti_namespace_find_function(ns, name)` / `ti_namespace_find_variable(ns, name)`: Look up a given namespace; used for `module.f()` and `module.x` on a script module.

### 5.4. `eval_function_definition`
- Takes `ti_runtime_t *rt`, `context_t *ctx`, and `AST_FUNCTION_DEFINITION` node.
- **Redefinition Check:** If `name` is a builtin or already defined in the running namespace, raises `TI_ERR_RUNTIME` (`Redefinition of function '%s'`).
- **Allocation:** Expands `rt->ns->functions` using `tracked_realloc(&rt->alloc_list, ...)` (raises `TI_ERR_NO_MEMORY` on failure). The array may move, so a `function_t *` into it is valid only until the next definition in that namespace.
- **Parameter Table:** If `param_count > 0`, allocates the parameter array via `tracked_calloc(&rt->alloc_list, ...)` (raises `TI_ERR_NO_MEMORY` on failure), copies each type and borrows each name from its `AST_PARAM` node.
- **Registration:** Populates `name`, `type = FUNC_TI`, `return_type`, `return_element_type`, `params`, `param_count`, `def = node` and `ns = rt->ns` (its home), then increments `rt->ns->function_count`. Returns `TI_VAL_OK`.

### 5.5. `eval_function_call`
1. Evaluates all argument expressions sequentially in the caller's context via `visitor_visit(rt, ctx, args[i])`, into `value_t **argv` allocated via `tracked_calloc(&rt->alloc_list, ...)`. A `NULL` argument means it already raised: stop.
2. **Symbol Resolution Hierarchy:**
   - First the running namespace's script functions via `user_find_function(rt, func_name)`.
   - Then native functions via `builtin_find_function(func_name)`.
   - Then a variable holding a function value (`func f = g;`), resolved by its stored name.
   - If nothing matches, raises `TI_ERR_UNDEFINED` (`Call to undefined function '%s'`).
3. Delegates call execution to `run_function(rt, ctx, func, argv, argc, node)`.
4. Single cleanup point: frees every evaluated argument with `val_free` and `argv` with `tracked_free(&rt->alloc_list, argv)`.
5. Returns the resulting `value_t *` (or `NULL` after an error).

`eval_method_call` (`obj.f(args)`) resolves `f` from the receiver instead: a handle type's methods (receiver passed as self), a native module's table, or a script module's namespace (`ti_namespace_find_function`). Every kind ends in the same single `run_function` call.

### 5.6. `run_function` (Gatekeeper: Type & Count Validation)
- Signature:
  ```c
  value_t *run_function(ti_runtime_t *rt, context_t *ctx, function_t *func, value_t **argv, int argc, ast_t *node);
  ```
- **Execution Flow:**
  1. **Count Check:** If `func->param_count >= 0` and `argc != func->param_count`, raises `TI_ERR_INVALID_ARG` (`Function '%s' expects %d argument(s), but received %d`).
  2. **Type Check:** Loops through `0` to `func->param_count - 1`. A parameter whose `type` is `VAL_ANY` is skipped (any argument type is accepted); otherwise asserts `argv[i]->type == func->params[i].type`, the list element type and the handle type. Raises `TI_ERR_TYPE_MISMATCH` with parameter name, expected type, and actual type at `node->line` if mismatched. Only native signatures can contain `VAL_ANY` (scripts cannot declare it), so this skip never affects script functions.
  3. **Dispatch:**
     - `FUNC_BUILTIN`: Calls `func->native_fn(rt->handle, argv, argc)`. A native that raised without a line gets the call's line.
     - `FUNC_TI`: Calls `run_ti_function(rt, ctx, func, argv, argc, node)`.

### 5.7. `run_ti_function` (User-Defined Function Call Execution)
- Signature:
  ```c
  value_t *run_ti_function(ti_runtime_t *rt, context_t *ctx, function_t *func, value_t **argv, int argc, ast_t *node);
  ```
- **Step-by-Step Execution Mechanics:**
  1. **Unwind Check:** Returns `NULL` at once if the runtime is cancelled or already in error.
  2. **Recursion Depth Limit Check:** If `rt->call_depth >= rt->max_call_depth`, raises `TI_ERR_LIMIT_EXCEEDED` (`Maximum recursion depth exceeded (%d)`).
  3. **Call Depth Tracking:** Increments `rt->call_depth` upon entry; `function_frame_release` restores it on every exit path.
  4. **Stack Frame Creation:** Allocates an isolated execution scope `context_t *func_ctx = context_init(&rt->alloc_list)`.
  5. **Lexical Binding:** Binds `func_ctx->parent = func->ns->globals`, the globals of the namespace the function was **defined** in, never the caller's frame. A module function therefore sees its module's variables even when main calls it.
  6. **Argument-to-Parameter Binding:** Iterates from `0` to `argc - 1`, creating deep clones of arguments via `val_copy(argv[i])` and registering them under each parameter's identifier via `context_add_variable(&rt->alloc_list, func_ctx, ...)`.
  7. **Namespace Switch & Body Execution:** Saves `rt->ns`, sets `rt->ns = func->ns` (so calls and definitions in the body resolve in its home namespace), and invokes `visitor_visit(rt, func_ctx, body)` on `func->def->value.function_definition.body`.
  8. **Control Flow Signal Validation:** If an unhandled `FLOW_BREAK` or `FLOW_CONTINUE` remains at the function boundary, raises `TI_ERR_RUNTIME` (`'break'/'continue' statement not within a loop inside function '%s'`).
  9. **Return Value Extraction & Consumption:**
     - If `func_ctx->flow_state == FLOW_RETURN`: extracts `func_ctx->return_value`, transfers ownership by setting `func_ctx->return_value = NULL`, and clears `func_ctx->flow_state = FLOW_NORMAL`.
     - If execution reached the end of the body without an explicit `return`:
       - If `func->return_type != VAL_VOID`: raises `TI_ERR_RUNTIME` (`Non-void function '%s' reached end of body without returning a value`).
       - If `func->return_type == VAL_VOID`: generates a default void return payload via `val_new_void()`.
  10. **Return Type Enforcement:** Verifies `ret_val->type == func->return_type` (and the list element type). If mismatched, raises `TI_ERR_TYPE_MISMATCH`.
  11. **Single Cleanup Point:** Restores the caller's `rt->ns`, frees an unreturned value, and releases the frame via `function_frame_release(rt, func_ctx)`. Returns the result, or `NULL` after an error.

---

## 6. Literal Evaluators (`src/ti_runtime_eval_literal.c`)

### Unified Implementation Architecture
Literal evaluators (`eval_int_literal`, `eval_float_literal`, `eval_string_literal`, `eval_boolean_literal`) are simple leaf AST node unwrappers:
- **Mechanism:** Each function directly delegates to constructor helpers (`val_new_int`, `val_new_float`, `val_new_string`, `val_new_bool`) defined in `src/ti_type_value.c` to produce boxed `value_t *` instances without duplicating allocation and string duplication logic.
- **Reason for Separation:** Keeps primitive boxing completely separate from AST dispatching, making memory lifetimes transparent.
