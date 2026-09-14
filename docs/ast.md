# Maintainer Guide: Abstract Syntax Tree (AST) & Token Subsystem

> **Audience:** Developers modifying AST representation or memory lifecycle in `src/AST.c` and `src/include/AST.h`.

---

## 1. Struct: `ast_t` (`struct AST_STRUCT`)

Defined in `src/include/AST.h`:

```c
typedef struct AST_STRUCT {
    enum {
        AST_INT_LITERAL,
        AST_FLOAT_LITERAL,
        AST_STRING_LITERAL,
        AST_BOOLEAN,
        AST_IDENTIFIER,
        AST_BINARY_EXPR,
        AST_UNARY_EXPR,
        AST_FUNCTION_CALL,
        AST_ARRAY_ACCESS,
        AST_COMPOUND,
        AST_IF_STATEMENT,
        AST_WHILE_STATEMENT,
        AST_FOR_STATEMENT,
        AST_RETURN_STATEMENT,
        AST_VARIABLE_DEFINITION,
        AST_FUNCTION_DEFINITION,
        AST_ASSIGNMENT,
        AST_NOOP,
        AST_PROGRAM
    } type;

    union {
        int int_value;
        double float_value;
        bool bool_value;
        char *string_value;
        char *identifier;
        
        struct { int op; struct AST_STRUCT *left; struct AST_STRUCT *right; } binary_expr;
        struct { int op; struct AST_STRUCT *operand; } unary_expr;
        struct { char *id; struct AST_STRUCT *index_expr; } array_access;
        struct { type variable_type; char *variable_name; struct AST_STRUCT *value; } variable_definition;
        struct { char *func; struct AST_STRUCT **args; int num_arg; } function_call;
        struct { type func_type; char *func_name; int num_args; struct AST_STRUCT **args; struct AST_STRUCT *body; } function_definition;
        struct { struct AST_STRUCT **compound_value; int compound_size; } compound;
        struct { struct AST_STRUCT *condition; struct AST_STRUCT *body; } while_statement;
        struct { struct AST_STRUCT *condition; struct AST_STRUCT *body; struct AST_STRUCT *else_body; } if_statement;
        struct { struct AST_STRUCT *id; struct AST_STRUCT *value; } assignment;
    } value;
} ast_t;
```

### Purpose of the Tagged Union Architecture
Every AST node represents a syntactical construct. Rather than declaring dozens of separate structs and dealing with complex C casting, `ast_t` uses a **discriminated (tagged) union**:
- The `type` enum identifies which union variant is currently active.
- The `value` union overlays memory so that all AST nodes share a uniform memory size and pointer type (`ast_t *`), allowing homogeneous recursive tree walking.

### Union Variants & Their Purpose

| Variant | Struct Elements | Role in Interpreter |
| :--- | :--- | :--- |
| `int_value`, `float_value`, `bool_value` | Primitive values | Stores raw literal values directly inside the node without separate heap strings. |
| `string_value`, `identifier` | `char *` | Heap-allocated string representing literal text or variable/function reference name. |
| `binary_expr` | `op`, `*left`, `*right` | Tree branch for operations (`+`, `-`, `==`, `&&`, etc.) holding two sub-expressions. |
| `unary_expr` | `op`, `*operand` | Single sub-expression branch for prefix operators (`!`, `-`, `+`). |
| `variable_definition` | `variable_type`, `*variable_name`, `*value` | Declaration statement associating a variable name with an initializer expression. |
| `assignment` | `*id`, `*value` | Assignment statement storing the target identifier AST and new evaluated value expression. |
| `compound` | `**compound_value`, `compound_size` | Sequence block holding an array of statement ASTs (e.g. inside `{ ... }`). |
| `if_statement` | `*condition`, `*body`, `*else_body` | Branching statement. `else_body` is `NULL` if no else branch is present. |
| `while_statement` | `*condition`, `*body` | Loop statement holding a condition expression and a compound body. |
| `function_call` | `*func`, `**args`, `num_arg` | Function invocation holding the function identifier string and array of argument expressions. |
| `function_definition` | `func_type`, `*func_name`, `num_args`, `**args`, `*body` | Function signature, parameter list, and compound body. |

---

## 2. Memory Lifecycle & Recursive Destruction (`ast_free`)

Because an AST is an arbitrarily deep recursive hierarchy, deallocation must strictly follow tree traversal rules:

### `ast_free(ast_t *ast)` (Hard / Critical Logic)
- **Check Null:** Returns immediately if `!ast`.
- **Recursive Branch Deallocation:**
  - **`AST_COMPOUND`:** Iterates through `0` to `compound_size`, recursively calling `ast_free()` on every child statement, then frees the `compound_value` pointer array.
  - **`AST_FUNCTION_CALL`:** Frees string `func`, loops through `0` to `num_arg` to free each argument expression AST, then frees the `args` array.
  - **`AST_BINARY_EXPR`:** Calls `ast_free(binary_expr.left)` and `ast_free(binary_expr.right)`.
  - **`AST_UNARY_EXPR`:** Calls `ast_free(unary_expr.operand)`.
  - **`AST_VARIABLE_DEFINITION`:** Frees `variable_name` string, then calls `ast_free(value)`.
  - **`AST_ASSIGNMENT`:** Calls `ast_free(id)` and `ast_free(value)`.
  - **`AST_IF_STATEMENT`:** Calls `ast_free` on `condition`, `body`, and `else_body` (if non-NULL).
  - **`AST_WHILE_STATEMENT`:** Calls `ast_free` on `condition` and `body`.
  - **Leaves:** Frees string payloads (`string_value`, `identifier`).
- **Node Destruction:** Finally, frees the root node `ast` via `tracked_free(ast)`.

> **Maintainer Warning:** Whenever adding a new node type to `AST.h`, you **must** implement its corresponding cleanup branch in `src/AST.c:ast_free`. Failing to free child pointers in `ast_free` causes memory leaks across the entire AST sub-tree.
