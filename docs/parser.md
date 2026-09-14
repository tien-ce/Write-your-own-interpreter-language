# Maintainer Guide: Parser Module

> **Audience:** Developers maintaining or extending the recursive descent parser in `src/parser.c`.

---

## 1. Struct: `parser_t` (`struct PARSER_STRUCT`)

Defined in `src/include/parser.h`:

```c
typedef struct PARSER_STRUCT {
    lexer_t *lexer;         // Underlying lexer token generator
    token_t *current_token; // Active lookahead token (LL(1) window)
} parser_t;
```

### Purpose & Field Contribution
The parser uses an **LL(1)** recursive descent strategy with selective **LL(2)** lookahead.

| Field | Type | Purpose & Contribution to Logic |
| :--- | :--- | :--- |
| `lexer` | `lexer_t *` | **Input Token Producer:** Retains a reference to the active lexer. Called whenever the parser needs to consume and advance the token stream. |
| `current_token` | `token_t *` | **Lookahead Window:** Always stores the token currently being inspected. Functions query `parser->current_token->type` to decide grammar branches without consuming input prematurely. |

---

## 2. Fundamental Operations: `parser_eat` & `parser_peek`

### `parser_eat(parser_t *parser, int expected_type)`
- **Purpose:** Verifies that the current token matches the grammar's expected terminal symbol, then advances to the next token.
- **Internal Mechanics:**
  ```c
  if (parser->current_token->type != expected_type) {
      ti_log("[Parser Error] Expected %s, but got %s at line %d\n",
             token_to_str(expected_type),
             token_to_str(parser->current_token->type),
             parser->lexer->line_num);
      ti_log_line(parser->lexer->line);
      ti_fatal();
  }
  parser->current_token = lexer_get_next_token(parser->lexer);
  ```
- **Error Behavior:** Any syntax deviation halts the entire interpreter via `ti_fatal()`. It prints the exact offending line from `parser->lexer->line`.

### `parser_peek(parser_t *parser)` (Tricky / Hard Logic)
- **Purpose:** Inspects the *token after next* without consuming anything from the real lexer.
- **Why this function exists:**
  When parsing a type keyword like `int`, the parser cannot know whether it is encountering:
  1. A variable definition: `int x = 5;` (peek sees identifier, then `=`)
  2. A function definition: `int add(int a, int b) { ... }` (peek sees identifier, then `(`)
- **Implementation Mechanics:**
  ```c
  static token_t *parser_peek(parser_t *parser)
  {
      lexer_t *temp_lexer = lexer_copy(parser->lexer);
      (void)lexer_get_next_token(temp_lexer);
      token_t *next_token = lexer_get_next_token(temp_lexer);
      tracked_free((void *)temp_lexer);
      return next_token;
  }
  ```
- **Maintainer Caveat:** It clones the entire lexer state with `lexer_copy()`, scans ahead on the clone, and immediately destroys the clone. This prevents any side-effects on the master cursor `parser->lexer`.

---

## 3. Expression Grammar & Precedence Climbing

Expressions are parsed using a strict precedence hierarchy where lower precedence functions call higher precedence functions.

```
parser_parse_expr()        [Precedence 1: Logical OR (||)]
       │
       ▼
parser_parse_comparison()  [Precedence 2: Logical AND (&&), Relational (==, !=, <, <=, >, >=)]
       │
       ▼
parser_parse_additive()    [Precedence 3: Addition (+), Subtraction (-)]
       │
       ▼
parser_parse_term()        [Precedence 4: Multiplication (*), Division (/)]
       │
       ▼
parser_parse_primary()     [Precedence 5: Unary (!, -, +), Grouping (expr), Literals, Identifiers, Calls]
```

### Precedence Level Implementations

#### Level 1: `parser_parse_expr(parser_t *parser)`
- Evaluates logical OR (`||`).
- First calls `parser_parse_comparison()`.
- While `current_token->type == TOKEN_LOGIC_OR`, consumes operator and wraps left and right sub-trees into an `AST_BINARY_EXPR` with `OP_LOGICAL_OR`.

#### Level 2: `parser_parse_comparison(parser_t *parser)`
- Evaluates logical AND (`&&`) and comparisons (`==`, `!=`, `<`, `<=`, `>`, `>=`).
- Calls `parser_parse_additive()`.
- While the current token is a comparison or `&&`, builds left-associative `AST_BINARY_EXPR` nodes.

#### Level 3: `parser_parse_additive(parser_t *parser)`
- Evaluates `+` and `-`.
- Calls `parser_parse_term()`.
- Converts token type to `OP_ADD` or `OP_SUB` via `token_type_to_op()`.

#### Level 4: `parser_parse_term(parser_t *parser)`
- Evaluates `*` and `/`.
- Calls `parser_parse_primary()`.
- Converts token type to `OP_MUL` or `OP_DIV`.

#### Level 5: `parser_parse_primary(parser_t *parser)` (Leaves & Unary)
- **Parenthesized Expressions:** If `current_token == TOKEN_LPAREN`, eats `(`, calls root `parser_parse_expr()`, and eats `)`.
- **Unary Operators:** If `current_token` is `!`, `-`, or `+`, eats the operator, recursively calls `parser_parse_primary()`, and wraps into an `AST_UNARY_EXPR` (`OP_NOT`, `OP_NEG`, `OP_POS`).
- **Literals:** Directly constructs `AST_INT_LITERAL`, `AST_FLOAT_LITERAL`, `AST_STRING_LITERAL`, or `AST_BOOLEAN`.
- **Identifiers vs Calls:** If `TOKEN_ID`:
  - Saves the identifier name.
  - Eats `TOKEN_ID`.
  - If next token is `TOKEN_LPAREN`, delegates to `parser_parse_function_call(parser, func_name)`.
  - Otherwise, returns `AST_IDENTIFIER`.

---

## 4. Statement & Declaration Logic

### Disambiguating Function Definition vs Variable Definition
In `parser_parse_definition(parser_t *parser)`:
1. The parser sees a type keyword (`int`, `float`, `string`, `bool`, `void`).
2. It calls `parser_peek(parser)` to check the token following the identifier:
   - If the peeked token is `(`, it branches to `parser_parse_function_definition()`.
   - Otherwise, it branches to `parser_parse_variable_definition()`.

### `parser_parse_statements` & Compound Block Building
- Gathers a sequence of statements inside curly braces `{ ... }` or at the file top-level.
- Dynamically resizes an array of `ast_t *` pointers (`compound_value`) using `tracked_realloc`.
- Returns an `AST_COMPOUND` node storing the statement count and array of statement ASTs.
