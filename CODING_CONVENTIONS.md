# TienInterpreter C Coding Conventions & Style Guide

This document defines the official coding standards, formatting rules, encapsulation principles, and documentation practices for the TienInterpreter project. All code in the repository must conform to these conventions.

---

## 1. File Structure & Organization

### 1.1 Header Files (`.h`)
- Every header file must include standard `#ifndef` include guards:
  ```c
  #ifndef MODULE_NAME_H
  #define MODULE_NAME_H

  /* ... */

  #endif /* !MODULE_NAME_H */
  ```
- **Encapsulation Rule:** Headers must **only** expose the public API required by other translation units. Internal helper functions, private structures, and implementation details must **never** be declared in header files.
- Header files must contain full Doxygen docstrings (`@brief`, `@param`, `@return`) for all exported types and global functions.

### 1.2 Source Files (`.c`)
- Organization order within `.c` files:
  1. `#include` directives (local includes first, system includes second).
  2. `#define` constants and macros.
  3. File-static variable declarations (prefixed with `s_`).
  4. Static function prototypes (forward declarations).
  5. Static function definitions (with full Doxygen `@brief` docstrings).
  6. Public function definitions (with concise 1-line description comments, deferring full documentation to the `.h` file).

---

## 2. Formatting & Syntax

### 2.1 Indentation & Line Width
- Indent using **4 spaces** per level. Never use tab characters (`\t`).
- Target a maximum line length of 100 characters.

### 2.2 Braces (`{` and `}`)
- **Function Definitions:** The opening brace `{` is placed on its own line in column 0:
  ```c
  ast_t *ast_init(int type)
  {
      ...
  }
  ```
- **Control Blocks (`if`, `while`, `for`, `switch`):** The opening brace `{` is placed on the **same line** (1TBS / Linux Kernel style):
  ```c
  if (condition) {
      handle_true();
  } else {
      handle_false();
  }
  ```
- **Single-Statement Bodies:** Always wrap statement bodies with braces `{ ... }`, even if the body is a single line:
  ```c
  /* Correct */
  if (!ast) {
      return;
  }

  /* Incorrect */
  if (!ast)
      return;
  ```

### 2.3 Parentheses `(` and Spacing
- **Control Keywords:** Always place one space between the keyword and opening parenthesis:
  `if (cond)`, `while (cond)`, `for (init; cond; step)`, `switch (expr)`.
  *Never:* `if(cond)`.
- **Function Calls & Definitions:** No space between function name and opening parenthesis:
  `ast_init(type)` — *Never:* `ast_init (type)`.
- **Commas:** Always put one space after a comma, never before:
  `tracked_calloc(1, sizeof(token_t))` — *Never:* `(1 ,sizeof(...))` or `(1,sizeof(...))`.
- **Operators:**
  - Binary operators: Spaces around binary operators (`a + b`, `x == y`, `ptr != NULL`).
  - Unary operators: No space between operator and operand (`!condition`, `-value`, `i++`).
  - Pointer asterisk: Space before `*`, attached to identifier name (`char *str`, `ast_t *node`).

---

## 3. Naming Conventions

### 3.1 Function Naming: `module_action`
All public and module-scoped functions must strictly follow the **`module_action`** pattern (`<module>_<action>`) rather than `<action>_<module>`:
- **Module Initializers & Destructors:**
  - AST module: `ast_init`, `ast_free` (not `init_ast`, `free_ast`)
  - Lexer module: `lexer_init`, `lexer_advance` (not `init_lexer`)
  - Parser module: `parser_init`, `parser_parse` (not `init_parser`)
  - Token module: `token_init` (not `init_token`)
  - Visitor / Context: `context_init`, `context_free` (not `init_interpreter_context`, `free_context`)
  - Value system: `val_init`, `val_new_int`, `val_new_float`, `val_new_string`, `val_new_bool`, `val_new_null` (not `init_val`)
  - Public API: `ti_init_builtin`, `ti_run_string`, `ti_log`, `ti_fatal` (prefixed with `ti_`)
- **Rationale:**
  - Logical alphabetical grouping in IDE autocompletion, symbols search, and documentation.
  - Consistent with the C standard library (`pthread_create`, `str_len`, etc.) and the interpreter's existing naming (`lexer_advance`, `parser_parse`, `visitor_visit`).
- **Clean Internal Architecture:**
  - Do not keep legacy macro aliases for internal functions. Refactor all call sites directly to maintain clean, modern headers without deprecated compatibility layers.

### 3.2 Identifiers
- **Functions:** Lowercase `snake_case` in `module_action` format (e.g. `lexer_init`, `lexer_get_next_token`).
- **Variables & Struct Members:** Lowercase `snake_case` (e.g. `variable_name`, `num_args`, `line_num`). Avoid ambiguous single-letter names like `l` (which resembles `1`).
- **Constants & Enums:** Uppercase `UPPER_SNAKE_CASE` (e.g. `TOKEN_KW_INT`, `VAL_FLOAT`).
- **Types:** Lowercase `snake_case_t` (e.g. `ast_t`, `token_t`, `context_t`). Do not use PascalCase aliases.

### 3.3 Scope Prefixes
- **File-static variables:** Prefix with `s_` (e.g. `static int s_builtin_count;`, `static alloc_hdr_t *s_alloc_head;`).
- **Global variables:** Prefix with `g_` (avoid globals when context passing is possible).

---

## 4. Documentation & Comments

### 4.1 Global Functions (Declared in `.h`)
- In `.h` files: Provide full Doxygen comments:
  ```c
  /**
   * @brief Allocate and initialize an AST node with the given type.
   * @param type Node type enum value.
   * @return Pointer to newly allocated ast_t.
   */
  ast_t *ast_init(int type);
  ```
- In `.c` files: Provide a concise single-line description:
  ```c
  /* Initialize a new AST node with the given type */
  ast_t *ast_init(int type)
  {
      ...
  }
  ```

### 4.2 Static Functions (Private to `.c`)
- Provide full Doxygen comments directly above the static function definition:
  ```c
  /**
   * @brief Advance the lexer by one character.
   * @param lexer Pointer to active lexer instance.
   */
  static void lexer_advance(lexer_t *lexer)
  {
      ...
  }
  ```

---

## 5. Interpreter Variable & Field Naming Framework (Quy tắc tư duy đặt tên)

To prevent cognitive overload (quá tải nhận thức) and eliminate ambiguity (sự mơ hồ, không rõ ràng) as the codebase grows larger, all variables and struct fields across the interpreter must comply with (tuân thủ) the following 5 mental rules (quy tắc tư duy).

### 5.1 Rule 1: Distinguish AST Nodes (`_node`) from Runtime Values (`_val`)
A common pitfall (bẫy lỗi phổ biến) in tree-walking interpreters written in C is naming both AST pointers and evaluated runtime values with generic (chung chung) names like `res`, `val`, or `x`.
- **AST Nodes (`ast_t *`):** Must always use the suffix (hậu tố) `_node` (or `node` if standalone).
  - Examples: `cond_node`, `body_node`, `left_node`, `right_node`, `expr_node`, `stmt_node`.
- **Runtime Values (`value_t *`):** Must always use the suffix `_val` (or `value` / `ret_val`).
  - Examples: `left_val`, `right_val`, `cond_val`, `ret_val`, `arg_val`.

```c
/* Clear distinction (phân biệt rõ ràng): */
ast_t   *left_node = node->value.binary_expr.left;   /* Static syntax node (node cú pháp tĩnh) */
value_t *left_val  = visitor_visit(ctx, left_node); /* Evaluated runtime result (giá trị runtime) */
```

### 5.2 Rule 2: Item Count (`_count`) vs Byte Size (`_size` / `_bytes`)
Do not mix item counts with memory allocations:
- **Quantity of items (số lượng phần tử):** Always use `_count`.
  - `statement_count` (never `compound_size`).
  - `variable_count` (never `variable_size`).
  - `param_count` (number of parameters in function definition).
  - `arg_count` (number of arguments in function call).
- **Memory buffer size (dung lượng bộ nhớ theo byte):** Reserve `_size` or `_bytes` exclusively (riêng biệt) for memory buffers and byte allocations.
  - `buffer_size`, `sizeof(function_t)`.

### 5.3 Rule 3: Symmetry between Plural Arrays and `_count` (Tính đối xứng giữa mảng và bộ đếm)
Every array of pointers (`**`) must have a corresponding counterpart (thành phần đối ứng) count sharing the identical prefix (tiền tố giống nhau):

| Entity (Thực thể) | Array Pointer (`**`) | Count Variable (`int`) | Single Item |
| :--- | :--- | :--- | :--- |
| Block statements | `statements` | `statement_count` | `stmt_node` |
| Function parameters | `params` | `param_count` | `param_node` |
| Call arguments | `args` | `arg_count` | `arg_val` / `arg_node` |
| Scoped variables | `variables` | `variable_count` | `var` / `variable` |
| Loaded functions | `s_functions` | `s_function_count` | `func` |

### 5.4 Rule 4: Distinguish Identifier Names (`_name`) from String Content (`_str` / `_text`)
Avoid ambiguous (mơ hồ) names like `id`:
- **Identifier Name (Tên định danh):** Must contain `name`.
  - `var_name`, `func_name`, `param_name`.
- **String Text Payload (Nội dung chuỗi):**
  - `string_val` (raw string inside `value_t`), `src_text`, `line_text`.

### 5.5 Rule 5: Distinguish Syntax Terminology from Runtime Semantics (Từ vựng cú pháp vs Thực thi)
- **Function Definition (`AST_FUNCTION_DEFINITION`):**
  - Use `return_type` for declared return data type (`val_type_t`). Never use `func_type` (avoids collision (xung đột) with `func_type_t`).
  - Use `params` & `param_count` (declared parameters in signature).
  - Use `body` (compound block node).
- **Function Call (`AST_FUNCTION_CALL`):**
  - Use `func_name` for target function name.
  - Use `args` & `arg_count` (passed arguments).
- **Assignment (`AST_ASSIGNMENT`):**
  - Left-hand side: `target` (ast_t pointer, accommodates (đáp ứng) both simple variables and array indexing). Never name it `id`.
  - Right-hand side: `value` (ast_t pointer).
- **Return Statement (`AST_RETURN_STATEMENT`):**
  - `value` (ast_t expression node to return, NULL for void return).

