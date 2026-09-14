# Maintainer Guide: Lexer Module

> **Audience:** Developers and maintainers modifying `src/lexer.c` or extending tokenization in TienInterpreter.

---

## 1. Struct: `lexer_t` (`struct LEXER_STRUCT`)

Defined in `src/include/lexer.h`:

```c
typedef struct LEXER_STRUCT {
    char c;                 // Current character under cursor
    unsigned int i;         // Byte offset index in contents
    unsigned int line_num;  // 1-based line counter for error diagnostics
    char *contents;         // Pointer to source code buffer
    char *line;             // Pointer to the start of the current source line
} lexer_t;
```

### Purpose & Field Contribution
The `lexer_t` struct encapsulates the complete traversal state of the tokenizer. Instead of passing separate cursor indices and line counters across recursive calls, this struct acts as a stateful cursor traversing an immutable source buffer.

| Field | Type | Purpose & Contribution to Logic |
| :--- | :--- | :--- |
| `c` | `char` | **Active character cache:** Stores `contents[i]` so that functions inspect `lexer->c` directly rather than constantly dereferencing `lexer->contents[lexer->i]`. When at the end of input, `c` becomes `'\0'`. |
| `i` | `unsigned int` | **Byte index cursor:** Tracks the exact byte position within `contents`. Incremented by `lexer_advance()` and decremented by `lexer_go_back()`. |
| `line_num` | `unsigned int` | **Diagnostics line tracker:** 1-based counter incremented whenever `\n` is encountered in `lexer_skip_whitespace()`. Passed to `ti_log()` when reporting syntax errors. |
| `contents` | `char *` | **Input source buffer:** A borrowed pointer to the null-terminated source code string. The lexer never modifies or frees this buffer. |
| `line` | `char *` | **Current line origin pointer:** Points to the first character of the current line within `contents`. When `\n` is consumed, this pointer updates to `contents + i + 1`. This allows `ti_log_line(lexer->line)` to print the entire offending source line upon an error without rescanning backward. |

---

## 2. Core Functions & Technical Logic

### 2.1. State Navigation & Lookahead

#### `lexer_advance(lexer_t *lexer)`
- **Purpose:** Moves the cursor forward by exactly 1 character and synchronizes `lexer->c`.
- **Logic:**
  1. Computes `length = strlen(lexer->contents)`.
  2. If `lexer->c != '\0'` and `lexer->i < length`, increments `lexer->i`.
  3. Updates `lexer->c = lexer->contents[lexer->i]`.
- **Maintainer Note:** Because it checks `strlen(lexer->contents)` on every call, advancing across a large source buffer takes $O(N)$ per character if not optimized. If the buffer is large, caching `strlen` in `lexer_t` during `lexer_init` is a recommended optimization.

#### `lexer_go_back(lexer_t *lexer)`
- **Purpose:** Steps the cursor backward by 1 character.
- **Logic:**
  - Decrements `lexer->i` (if `i != 0`) and sets `lexer->c = lexer->contents[lexer->i]`.
- **Maintainer Note:** Used exclusively to implement 1-character lookahead rollback for two-character operators (`==`, `!=`, `<=`, `>=`, `&&`, `||`). When the second character does not match, `lexer_go_back` reverts the cursor so the first character can be emitted as a single-character token.

#### `lexer_advance_with_token(lexer_t *lexer, token_t *token)`
- **Purpose:** Helper to consume the current character and immediately return a token.
- **Logic:** Calls `lexer_advance(lexer)` and returns `token`. Used for single-character delimiters (`(`, `)`, `;`, `+`, `-`, `,`, `{`, `}`) and after recognizing the second character of compound operators.

#### `lexer_skip_whitespace(lexer_t *lexer)`
- **Purpose:** Consumes contiguous whitespace and updates line tracking.
- **Logic:**
  1. Loops while `lexer->c == ' ' || lexer->c == '\n'`.
  2. If `lexer->c == '\n'`:
     - Increments `lexer->line_num`.
     - Updates `lexer->line = lexer->contents + lexer->i + 1` to mark the start of the next line.
  3. Calls `lexer_advance(lexer)` on every space or newline.
- **Maintainer Note:** Currently only handles `' '` and `'\n'`. If source files contain tabs (`'\t'`) or carriage returns (`'\r'`), they will hit the `default` branch in `lexer_get_next_token` as unexpected characters unless added here.

---

### 2.2. Token Accumulation & Memory Mechanics

#### `lexer_get_current_char_as_string(lexer_t *lexer)`
- **Purpose:** Converts the single character `lexer->c` into a heap-allocated, null-terminated 2-byte string (`char *`).
- **Why this function exists:**
  In C, character literals are primitive `char` bytes, but string accumulation routines (`strcat`) require null-terminated `char *` pointers. This function wraps `lexer->c` into a temporary C-string so it can be appended to dynamically resizing token value buffers.
- **Internal Mechanics:**
  ```c
  char *str = tracked_calloc(2, sizeof(char));
  str[0] = lexer->c;
  str[1] = '\0';
  return str;
  ```
- **Crucial Maintenance Rule:** Every call allocates heap memory through the tracked memory subsystem (`tracked_calloc`). Any calling function **must** call `tracked_free(s)` immediately after appending `s` to avoid ballooning the tracked memory table during tokenization.

---

### 2.3. Literal & Identifier Collectors

#### `lexer_collect_string(lexer_t *lexer)`
- **Triggered when:** `lexer->c == '"'`.
- **Purpose:** Scans double-quoted string literals, decodes escape sequences, and creates a `TOKEN_STRING` token.
- **Step-by-Step Logic:**
  1. Calls `lexer_advance(lexer)` to consume the opening quote `"`.
  2. Allocates a tracked 1-byte buffer `value` initialized to `""`.
  3. Loops while `lexer->c != '"' && lexer->c != '\0'`:
     - **Escape character handling:** If `lexer->c == '\\'`:
       - Calls `lexer_advance(lexer)`.
       - Translates escaped character:
         - `'n'` &rarr; converts `lexer->c` to `\n` (newline, ASCII 10).
         - `'t'` &rarr; converts `lexer->c` to `\t` (horizontal tab, ASCII 9).
         - `'r'` &rarr; converts `lexer->c` to `\r` (carriage return, ASCII 13).
     - Converts `lexer->c` into temporary string `s` using `lexer_get_current_char_as_string`.
     - Reallocates `value` with `tracked_realloc(value, strlen(value) + strlen(s) + 1)`.
     - Appends `s` with `strcat(value, s)` and frees `s` with `tracked_free(s)`.
     - Calls `lexer_advance(lexer)`.
  4. **Error check:** If the loop terminated because `lexer->c == '\0'`, the closing quote was missing. Logs `"Missing close quote"` and halts execution via `ti_fatal()`.
  5. If closed properly, calls `lexer_advance(lexer)` to skip the closing quote and returns `token_init(TOKEN_STRING, value)`.
- **Maintainer Note:** The resulting heap string `value` is transferred directly to `token_t->value`, which becomes owned by the token.

---

#### `lexer_collect_number(lexer_t *lexer)`
- **Triggered when:** `isdigit(lexer->c)`.
- **Purpose:** Scans numeric literals, distinguishes integer from floating-point values, and validates syntax suffixes.
- **Step-by-Step Logic:**
  1. **Integer segment:** While `isdigit(lexer->c)`, appends digits to `value` using `tracked_realloc` + `strcat`.
  2. **Float check (`.`):** If `lexer->c == '.'`:
     - Appends `.` to `value`.
     - Calls `lexer_advance(lexer)`.
     - While `isdigit(lexer->c)`, appends fractional digits.
     - **Invalid suffix validation:** Checks `if (isalpha(lexer->c) || lexer->c == '_')`. If true (e.g. `3.14a` or `1.0_var`), logs an error (`"Invalid suffix '%c' on float constant"`) and calls `ti_fatal()`.
     - Returns `token_init(TOKEN_FLOAT, value)`.
  3. **Integer validation:** If no `.` was encountered:
     - Checks `if (isalpha(lexer->c) || lexer->c == '_')`. If true (e.g. `123abc`), logs an error (`"Invalid suffix '%c' on integer constant"`) and calls `ti_fatal()`.
     - Returns `token_init(TOKEN_INT, value)`.
- **Maintainer Note:** Numbers are kept as raw text in `token->value` (e.g. `"42"`, `"3.14"`). Numeric conversion (`atoi` / `atof`) is intentionally deferred to AST/Visitor evaluation.

---

#### `lexer_collect_id(lexer_t *lexer)`
- **Triggered when:** `isalpha(lexer->c)`.
- **Purpose:** Scans identifier names and disambiguates keywords and boolean literals from user variable/function names.
- **Step-by-Step Logic:**
  1. Loops while `isalnum(lexer->c) || lexer->c == '_'`.
  2. Appends characters to `value`.
  3. **Keyword resolution:** Compares `value` against known keywords:
     - Types: `"int"` (`TOKEN_KW_INT`), `"float"` (`TOKEN_KW_FLOAT`), `"string"` (`TOKEN_KW_STRING`), `"bool"` (`TOKEN_KW_BOOL`), `"void"` (`TOKEN_KW_VOID`).
     - Control flow: `"if"` (`TOKEN_KW_IF`), `"else"` (`TOKEN_KW_ELSE`), `"while"` (`TOKEN_KW_WHILE`), `"return"` (`TOKEN_KW_RETURN`).
     - **Why Keywords Free `value` and Pass `NULL`:**
       ```c
       // src/lexer.c line 147:
       if (strcmp(value, "int") == 0) { 
           tracked_free(value); 
           return token_init(TOKEN_KW_INT, NULL); 
       }
       ```
       Every token has a `type` and a string `value`:
       ```c
       typedef struct TOKEN_STRUCT {
           token_type_t type;  // e.g. TOKEN_KW_INT
           char *value;        // NULL for keywords!
       } token_t;
       ```
       - For a keyword like `"int"`, the enum `TOKEN_KW_INT` already gives 100% of the information needed. Storing the string `"int"` in `token->value` would be completely redundant and waste heap allocations.
       - Therefore, as soon as `strcmp` detects that `value` is `"int"`, the temporary string `value` is immediately freed via `tracked_free(value)`, and `token_init` is called with `NULL`.
  4. **Why Identifiers Retain `value`:**
     - For a user variable like `my_counter`, the token type is just generic `TOKEN_ID`.
     - The type `TOKEN_ID` does **not** tell the parser what the variable is named! The parser and visitor need the exact identifier name to look up variables in `context_t`.
     - Therefore, `value` **cannot** be freed. It is handed over to `token_init(TOKEN_ID, value)` and stored inside `token->value`.
  5. **Why Boolean Literals Retain `value`:**
     - Both `"true"` and `"false"` share the exact same enum: `TOKEN_BOOL`.
     - Because `TOKEN_BOOL` alone does not specify whether the value is true (1) or false (0), the token must keep `value` (`"true"` or `"false"`) so that the parser and visitor can check `strcmp(token->value, "true") == 0`.
       ```c
       if (strcmp(value, "true") == 0) {
           return token_init(TOKEN_BOOL, value); // Keeps "true" to distinguish from "false"
       }
       ```

---

### 2.4. Master Dispatcher & Lookahead Matching

#### `lexer_get_next_token(lexer_t *lexer)`
- **Purpose:** Public token iterator. Called repeatedly by the parser until `TOKEN_EOF`.
- **Execution Flow:**
  ```
  While lexer->c != '\0':
    1. If whitespace/newline: skip via lexer_skip_whitespace()
    2. If '"': return lexer_collect_string()
    3. If digit: return lexer_collect_number()
    4. If alpha: return lexer_collect_id()
    5. Switch on lexer->c:
         Single-char delimiters -> lexer_advance_with_token()
         Prefix operators (=, !, <, >, &, |) -> Peek ahead:
           - Advance 1 char
           - If matches 2nd char (==, !=, <=, >=, &&, ||): return compound token
           - Else: lexer_go_back() and return single-char token
         Default -> Print offending character, print line via ti_log_line, call ti_fatal()
  End of input -> return token_init(TOKEN_EOF, NULL)
  ```

#### Two-Character Lookahead Logic Details:
For operators that can be either single-character or compound:
- **`=`:** Advances. If next is `=`, returns `TOKEN_DEQUALS` (`==`). Else calls `lexer_go_back` and returns `TOKEN_EQUALS` (`=`).
- **`!`:** Advances. If next is `=`, returns `TOKEN_NOT_EQUALS` (`!=`). Else calls `lexer_go_back` and returns `TOKEN_NOT` (`!`).
- **`<`:** Advances. If next is `=`, returns `TOKEN_LTE` (`<=`). Else calls `lexer_go_back` and returns `TOKEN_LT` (`<`).
- **`>`:** Advances. If next is `=`, returns `TOKEN_GTE` (`>=`). Else calls `lexer_go_back` and returns `TOKEN_GT` (`>`).
- **`&`:** Advances. If next is `&`, returns `TOKEN_LOGIC_AND` (`&&`). Else calls `lexer_go_back` and returns `TOKEN_AND` (`&`).
- **`|`:** Advances. If next is `|`, returns `TOKEN_LOGIC_OR` (`||`). Else calls `lexer_go_back` and returns `TOKEN_OR` (`|`).

---

### 2.5. Lifecycle & Backtracking Functions

#### `lexer_init(char *str)`
- **Purpose:** Allocates and configures a new `lexer_t`.
- **Mechanics:** Calls `tracked_calloc(1, sizeof(struct LEXER_STRUCT))`, points `contents` and `line` to `str`, sets `line_num = 1`, `i = 0`, and `c = str[0]`.

#### `lexer_copy(lexer_t *lexer)`
- **Purpose:** Clones the lexer state for speculative parsing (backtracking in parser).
- **Why this function exists:**
  The parser sometimes needs to look ahead multiple tokens (e.g. to determine if an identifier is a function call, variable assignment, or declaration) without permanently advancing the main token stream. The parser calls `lexer_copy`, tests the lookahead on the copy, and discards or commits the state.
- **Maintainer Note:** Both the original and copied lexers share the same `contents` pointer (shallow copy). The copied struct must be freed via `tracked_free()`.
