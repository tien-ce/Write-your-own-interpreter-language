# TienInterpreter: Architecture & Module Summary

> **Audience:** Maintainers and contributors.

---

## 1. Project Overview & Execution Pipeline

**TienInterpreter** is a tree-walking interpreter for a C-like scripting language (`.ti`). The interpreter executes source code through a three-stage pipeline backed by a scoped context environment and tracked memory subsystem:

```
                  ┌────────────────────────┐
                  │ Source Code (.ti file) │
                  └───────────┬────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│ 1. LEXICAL ANALYSIS (`src/lexer.c`, `src/token.c`)           │
│    Scans characters into a sequential token stream (`token_t`)│
└─────────────────────────────┬───────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. SYNTACTIC ANALYSIS (`src/parser.c`, `src/AST.c`)          │
│    Parses tokens into an Abstract Syntax Tree (`ast_t`)      │
└─────────────────────────────┬───────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. EVALUATION / VISITOR ENGINE                              │
│    Recursively executes AST nodes in a lexical environment   │
│                                                             │
│    ┌──────────────────────────────────────────────────────┐ │
│    │ Master Dispatcher: `src/visitor.c`                   │ │
│    └──────────┬───────────────────────────────────────────┘ │
│               ├──► Expressions:  `src/visitor_eval_binary.c`│ │
│               ├──► Control Flow: `src/visitor_eval_control.c│ │
│               ├──► Variables:    `src/visitor_eval_variable.c │
│               ├──► Functions:    `src/visitor_eval_func.c`  │ │
│               └──► Literals:     `src/visitor_eval_literal.c│ │
└─────────────────────────────┬───────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│ RUNTIME ENVIRONMENT & MEMORY                                │
│ • Scopes & Variables: `src/context.c`, `src/value.c`         │
│ • Allocation Tracking: `src/tracked_memory.c`               │
│ • Diagnostics: `src/TienInterpreter.c`, `src/debug.c`        │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. Module Directory & Responsibility Matrix

The codebase consists of **7 major modules** organized cleanly across `src/` and `src/include/`:

| Module | Source Files | Headers | Primary Role | Maintainer Doc |
| :--- | :--- | :--- | :--- | :--- |
| **1. Lexer** | `src/lexer.c` | `src/include/lexer.h`<br>`src/include/token.h` | Character-by-character scanner, escape decoder, keyword resolution | [`docs/lexer.md`](docs/lexer.md) |
| **2. Parser** | `src/parser.c` | `src/include/parser.h` | Recursive descent precedence climbing parser | [`docs/parser.md`](docs/parser.md) |
| **3. AST & Token** | `src/AST.c`<br>`src/token.c` | `src/include/AST.h`<br>`src/include/token.h` | Tagged union AST definitions, token enums, tree deallocation | [`docs/ast.md`](docs/ast.md) |
| **4. Visitor Engine** | `src/visitor.c`<br>`src/visitor_eval_*.c` | `src/include/visitor.h`<br>`src/include/visitor_internal.h` | AST node dispatcher and domain-specific expression/statement evaluators | [`docs/visitor.md`](docs/visitor.md) |
| **5. Context & Value** | `src/context.c`<br>`src/value.c` | `src/include/visitor_internal.h` | Lexical scope chains, symbol tables, dynamic value wrappers | [`docs/context_value.md`](docs/context_value.md) |
| **6. Tracked Memory** | `src/tracked_memory.c` | `src/include/tracked_memory.h` | Allocation tracking table, leak detection, safe pointer inspection | [`docs/tracked_memory.md`](docs/tracked_memory.md) |
| **7. CLI & Diagnostics**| `cli/main.c`<br>`src/TienInterpreter.c` | `src/TienInterpreter.h`<br>`src/include/debug.h` | File reader, diagnostic loggers (`ti_log`, `ti_fatal`), driver | Code comments |

---

## 3. Communication & Internal Contracts

- **Façade Pattern for Evaluators:** All visitor sub-modules (`visitor_eval_binary.c`, `visitor_eval_control.c`, etc.) communicate through `src/include/visitor_internal.h`. Outside modules (such as `src/TienInterpreter.c`) only include the public façade `src/include/visitor.h`.
- **Mutual Recursion Contract:** Because expressions require evaluation of arbitrary child nodes (e.g. function call arguments, short-circuit operands), evaluators call `visitor_visit(ctx, ...)` directly. `visitor_internal.h` provides the shared function prototype mediator.
- **Tracked Heap Allocation:** All dynamic memory throughout the pipeline uses `tracked_calloc`, `tracked_realloc`, `tracked_strdup`, and `tracked_free` to guarantee zero memory leaks upon script termination.
