# TienInterpreter: Comprehensive Issues, Audit Report & Roadmap

> **Status Tracking:** Updated following full codebase architecture & memory audit.  
> **Legend:** `[ ]` Open / Pending | `[x]` Resolved & Verified

---

## 🔴 Priority 1: Critical (Must Fix Immediately)

Critical memory leaks during normal interpretation and core architectural violations.

- [x] **ISSUE-01: Token payload string memory leak in `parser_eat`** (`src/parser.c: parser_eat`)
  - **Description:** `parser_eat` frees the `token_t` struct via `tracked_free(old_token)`, but previously did not free `old_token->value`. For literals and keywords, the string payload was not transferred to the AST, leaking heap memory on every eaten token.
  - **Resolution:** Added safe deallocation of `old_token->value` via `tracked_free(old_token->value)` prior to releasing `old_token` in `src/parser.c:parser_eat`.

- [x] **ISSUE-02: Double token memory leak in `parser_peek`** (`src/parser.c: parser_peek`)
  - **Description:** `parser_peek` previously discarded the first lookahead token from `temp_lexer` without deallocating it, and callers did not free the returned peeked `token_t`.
  - **Resolution:** Properly deallocated the discarded lookahead token (both payload value and struct) inside `parser_peek`, and updated caller `parser_parse_definition` to free the peeked token's value and struct immediately after inspecting `next_type`.

- [x] **ISSUE-03: Header file placement & encapsulation — `src/TienInterpreter.h`**
  - **Description:** `TienInterpreter.h` is intentionally positioned at `src/TienInterpreter.h` to satisfy Arduino IDE specification (`#include <TienInterpreter.h>`). Internal subsystem headers reside in `src/include/`.
  - **Resolution:** Decoupled all internal pipeline headers (`token.h`, `AST.h`, `lexer.h`, `parser.h`, `visitor.h`, `tracked_memory.h`) from `src/TienInterpreter.h`. External consumers only receive `ti_type.h`, `value.h`, and `function.h` with forward declarations isolating internal AST and context structures.

---

## 🟠 Priority 2: High (Performance, Safety & Resource Management)

Issues that degrade performance exponentially on large files or cause memory corruption/leaks under low-memory conditions.

- [ ] **ISSUE-04: Unsafe `tracked_realloc` pointer reassignment** (`src/parser.c`, `src/context.c`)
  - **Description:** Pattern `ptr = tracked_realloc(ptr, new_size)` overwrites the original pointer with `NULL` on allocation failure, permanently leaking the original block.
  - **Remedy:** Use a temporary pointer idiom: `void *tmp = tracked_realloc(ptr, sz); if (!tmp) ti_fatal(); ptr = tmp;`.

- [x] **ISSUE-05: User function table memory management & lifecycle** (`src/visitor_eval_func.c`)
  - **Description:** Registered user-defined functions in `s_user_functions` (and their copied parameter metadata) previously lacked automated cleanup across script runs.
  - **Resolution:** Decoupled function tables from `context_t` and partitioned them into host builtins (`s_builtin_functions`) and thread-local script functions (`s_user_functions`). Implemented automatic co-occurrent teardown in `visitor_set_global_context(NULL)` invoking `user_functions_clear()`.

- [ ] **ISSUE-06: Anonymous enum inside `ast_t` breaks type safety** (`src/include/AST.h`)
  - **Description:** `struct AST_STRUCT` defines node types as an anonymous `enum { ... } type;`, forcing functions like `ast_init(int type)` to accept raw `int` instead of an explicit `ast_type_t`.
  - **Remedy:** Extract and name `typedef enum AST_TYPE_ENUM { ... } ast_type_t;`.

- [ ] **ISSUE-07: $O(N^2)$ `strlen` overhead in `lexer_advance`** (`src/ti_build_lexer.c: lexer_advance`)
  - **Description:** Every character advance calls `strlen(lexer->contents)`, scanning the entire source code buffer repeatedly. For embedded MCUs (ESP32), this causes quadratic $O(N^2)$ execution latency on larger scripts and risks triggering hardware watchdog timer (WDT) resets.
  - **Remedy:** Store `source_len` once inside `lexer_t` upon initialization, or simply check `lexer->c != '\0'`.

- [ ] **ISSUE-08: $O(N^2)$ byte-by-byte heap reallocation & fragmentation in `lexer_collect_*`** (`src/ti_build_lexer.c`)
  - **Description:** Collecting numbers, identifiers, and strings allocates a 2-byte buffer per character, reallocates the accumulator string byte-by-byte via `tracked_realloc`, and calls `strcat` in a loop. For a 50-character token, this triggers ~150 heap interactions, causing severe memory fragmentation on embedded SRAM.
  - **Remedy:** Use zero-copy string slicing: record start offset and length directly into `lexer->contents`, and perform exactly one allocation per token upon completion.

- [ ] **ISSUE-21: Inefficient and fragile lookahead via `parser_peek`** (`src/ti_build_parser.c: parser_peek`)
  - **Description:** To check if an identifier is a variable definition or function definition, `parser_peek` clones the entire `lexer_t` struct by value and calls `lexer_get_next_token()` twice on the heap. This causes redundant CPU overhead, temporary heap allocations, and fragile memory tracking.
  - **Remedy:** Refactor `parser_t` into a standard LL(1) sliding window maintaining `current_token` and `peek_token` fields directly in the struct, eliminating dynamic lexer cloning.

---

## 🟡 Priority 3: Medium (Missing Language Features & Logic Incompleteness)

Syntax and runtime operations that are defined or planned but currently missing handlers.

- [ ] **ISSUE-09: Missing `OP_NEQ` (`!=`) and `OP_MOD` (`%`) in Visitor** (`src/visitor_eval_binary.c`)
  - **Description:** `!=` is parsed into `OP_NEQ`, but `eval_binary_expr` lacks a `case OP_NEQ:` handler (falls into fatal error). Modulo `%` is also unhandled.
  - **Remedy:** Add `binary_not_equal` and `binary_mod` handlers in `src/visitor_eval_binary.c`.

- [ ] **ISSUE-10: Missing `AST_ARRAY_ACCESS` execution in Visitor** (`src/visitor.c`)
  - **Description:** The parser parses `arr[index]` into `AST_ARRAY_ACCESS`, but `visitor_visit()` has no dispatch case for it.
  - **Remedy:** Add `case AST_ARRAY_ACCESS:` and implement array indexing evaluator.

- [x] **ISSUE-11: Implement user-defined function execution (`run_ti_function`)** (`src/visitor_eval_func.c`)
  - **Description:** `run_ti_function` was previously an unlinked stub returning `NULL`.
  - **Resolution:** Fully implemented `run_ti_function` with stack frame creation (`func_ctx`), static scoping link to global context (`s_global_context`), argument cloning and binding, body evaluation via `visitor_visit`, loop signal interception (`FLOW_BREAK`/`FLOW_CONTINUE`), return value extraction, and strict return type validation.

- [ ] **ISSUE-12: Lexer missing tokens: `*`, `/`, `[`, `]`, `%`, `~`, `\t`, `\r`** (`src/lexer.c`)
  - **Description:** Single-character tokens for multiplication, division, array indexing, and modulo are not emitted. Tab characters (`\t`) and Windows CRLF (`\r`) trigger unexpected character fatal errors.
  - **Remedy:** Update `lexer_skip_whitespace` with `isspace()` and add cases for `*`, `/`, `[`, `]`, `%` in `lexer_get_next_token`.

- [ ] **ISSUE-13: Replace raw `int` with canonical enums across function signatures**
  - **Description:** `ast_init(int type)`, `val_init(int type)`, and `token_type_to_op` use `int` instead of `ast_type_t`, `val_type_t`, and `token_type_t`.
  - **Remedy:** Refactor prototypes to use strong enum types.

- [ ] **ISSUE-14: Defensive `return` statements after `ti_fatal()`**
  - **Description:** Some sites assume `ti_fatal()` never returns without an explicit defensive return statement immediately following the fatal call.
  - **Remedy:** Ensure all failure branches return immediately after `ti_fatal()`.

- [ ] **ISSUE-15: Complete `for` loop implementation end-to-end**
  - **Description:** `for` loop token, grammar parsing, and visitor execution remain stubs.
  - **Remedy:** Implement `TOKEN_KW_FOR`, `parser_parse_for_statement`, and `eval_for_statement`.

- [ ] **ISSUE-16: Update stale documentation in `docs/`**
  - **Description:** Minor documentation drift in `docs/parser.md` regarding operator precedence ladder and control flow flags.

---

## 🔵 Priority 4: Low (Code Style, Conventions & Cleanup)

- [ ] **ISSUE-17: Normalize `AST_BOOLEAN` naming** (`src/include/AST.h`)
  - **Description:** Named `AST_BOOLEAN` instead of `AST_BOOL_LITERAL` or `AST_BOOLEAN_LITERAL` to match `AST_INT_LITERAL` and `AST_FLOAT_LITERAL`.

- [ ] **ISSUE-18: Standardize heap cleanup in CLI** (`cli/built_in_functions.c`)
  - **Description:** Line 306 calls raw `free(printed)` instead of tracked deallocation.

- [ ] **ISSUE-19: Unused static AST renderer (`ast_draw`)** (`src/draw.c`)
  - **Description:** `ast_draw` is implemented but uncalled; consider exposing a `--dump-ast` CLI flag.

- [ ] **ISSUE-20: Unchecked signed integer overflow in division** (`src/visitor_eval_binary.c`)
  - **Description:** `INT_MIN / -1` triggers undefined behavior on x86/ARM platforms. Add guard check.

---

## ✅ Resolved Issues History

- [x] **Tracked memory header offset computation** (`tracked_free` cast fixed to `(alloc_hdr_t*)ptr - 1`).
- [x] **Implement `tracked_realloc` with neighbor node pointer fixups**.
- [x] **Implement `tracked_strdup` with leak tracking**.
- [x] **Float literal parsing bug** (fractional digits consumed properly).
- [x] **Unterminated string literal hang** (added explicit EOF check and syntax fatal error).
- [x] **Empty compound block `{}` handling** (supports zero-statement blocks without NULL crash).
- [x] **Function call semicolon consumption** (semicolon moved to statement parser).
- [x] **Parser `default` case silent NULL bug** (all unexpected statements now trigger fatal syntax error).
- [x] **Float equality check (`VAL_FLOAT`)** (comparison properly stored in `bool_val`).
- [x] **Memory leak on variable reassignment** (old value safely deallocated before assignment).
- [x] **Keywords `"break"` and `"continue"` tokenized and parsed** into `AST_BREAK_STATEMENT` and `AST_CONTINUE_STATEMENT`.
- [x] **Keyword `"return"` parsed and evaluated** into `AST_RETURN_STATEMENT` with optional expression payload.
- [x] **Control flow interruption flags (`flow_state_t`) and bubble-up logic** in `context_t` and compound blocks.
- [x] **Debug string conversions for all token types in `src/debug.c`**.
- [x] **ISSUE-01: Token payload string memory leak in `parser_eat`** (Freed `old_token->value` before `tracked_free(old_token)`).
- [x] **ISSUE-02: Double token memory leak in `parser_peek`** (Freed discarded lookahead token in `parser_peek` and peeked token in `parser_parse_definition`).
- [x] **ISSUE-11: User-defined function execution `run_ti_function`** (Implemented function call frames, static scoping to global context, argument binding, body execution, and return value validation).
