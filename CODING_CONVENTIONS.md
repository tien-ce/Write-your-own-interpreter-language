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
