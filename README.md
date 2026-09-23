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
│ 1. LEXICAL ANALYSIS (`ti_build_lexer.c`, `ti_build_token.c`) │
│    Scans characters into a sequential token stream (`token_t`)│
└─────────────────────────────┬───────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. SYNTACTIC ANALYSIS (`ti_build_parser.c`, `ti_type_ast.c`) │
│    Parses tokens into an Abstract Syntax Tree (`ast_t`)      │
└─────────────────────────────┬───────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. EVALUATION / VISITOR ENGINE                              │
│    Recursively executes AST nodes in a lexical environment   │
│                                                             │
│    ┌──────────────────────────────────────────────────────┐ │
│    │ Master Dispatcher: `src/ti_runtime_visitor.c`        │ │
│    └──────────┬───────────────────────────────────────────┘ │
│            ├──► Expressions:  `src/ti_runtime_eval_binary.c`  │ │
│            ├──► Control Flow: `src/ti_runtime_eval_control.c` │ │
│            ├──► Variables:    `src/ti_runtime_eval_variable.c`│ │
│            ├──► Functions:    `src/ti_runtime_eval_func.c`    │ │
│            └──► Literals:     `src/ti_runtime_eval_literal.c` │ │
└─────────────────────────────┬───────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│ RUNTIME ENVIRONMENT & MEMORY                                │
│ • Scopes: `src/ti_runtime_context.c`                        │
│ • Values: `src/ti_type_value.c`                             │
│ • Allocation Tracking: `src/tracked_memory.c`               │
│ • Diagnostics: `src/TienInterpreter.c`, `src/debug.c`        │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. Module Directory & Responsibility Matrix

The codebase consists of **8 major modules** organized cleanly across `src/` and `src/include/`:

| Module | Source Files | Headers | Primary Role | Maintainer Doc |
| :--- | :--- | :--- | :--- | :--- |
| **1. Lexer** | `src/ti_build_lexer.c` | `src/include/ti_build_lexer.h`<br>`src/include/ti_build_token.h` | Character-by-character scanner, escape decoder, keyword resolution | [`docs/lexer.md`](docs/lexer.md) |
| **2. Parser** | `src/ti_build_parser.c` | `src/include/ti_build_parser.h` | Recursive descent precedence climbing parser | [`docs/parser.md`](docs/parser.md) |
| **3. Program Build** | `src/ti_build_program.c` | `src/include/ti_build_program.h` | Build-time program compilation container, lifecycle, and memory isolation | [`docs/build_program.md`](docs/build_program.md) |
| **4. AST & Token** | `src/ti_type_ast.c`<br>`src/ti_build_token.c` | `src/include/ti_type_ast.h`<br>`src/include/ti_build_token.h`<br>`src/include/ti_type.h` | Tagged union AST definitions, token enums, canonical types, tree deallocation | [`docs/ast.md`](docs/ast.md) |
| **5. Visitor Engine** | `src/ti_runtime_visitor.c`<br>`src/ti_runtime_eval_*.c` | `src/include/ti_runtime_visitor.h` | AST node dispatcher and domain-specific expression/statement evaluators | [`docs/visitor.md`](docs/visitor.md) |
| **6. Context & Value** | `src/ti_runtime_context.c`<br>`src/ti_type_value.c` | `src/include/ti_runtime_context.h`<br>`src/include/ti_type_value.h`<br>`src/include/ti_type_func.h` | Lexical scope chains, symbol tables, dynamic value wrappers | [`docs/context_value.md`](docs/context_value.md) |
| **7. Tracked Memory** | `src/tracked_memory.c` | `src/include/tracked_memory.h` | Allocation tracking table, leak detection, safe pointer inspection | [`docs/tracked_memory.md`](docs/tracked_memory.md) |
| **8. CLI & Diagnostics**| `cli/main.c`<br>`cli/built_in_functions.c`<br>`src/TienInterpreter.c`<br>`src/debug.c` | `src/TienInterpreter.h`<br>`src/include/debug.h` | Entrypoint, host built-in functions, diagnostic loggers (`ti_log`, `ti_fatal`), driver | Code comments |

---

## 3. Communication & Internal Contracts

- **Unified Visitor Header Contract:** All visitor sub-modules (`ti_runtime_eval_binary.c`, `ti_runtime_eval_control.c`, etc.) and caller subsystems communicate through `src/include/ti_runtime_visitor.h` as the unified visitor header.
- **Mutual Recursion Contract:** Because expressions require evaluation of arbitrary child nodes (e.g. function call arguments, short-circuit operands), evaluators call `visitor_visit(rt, ctx, ...)` directly. `src/include/ti_runtime_visitor.h` provides the shared function prototype mediator across all evaluator translation units.
- **Tracked Heap Allocation:** All dynamic memory throughout the pipeline uses `tracked_calloc`, `tracked_realloc`, `tracked_strdup`, and `tracked_free` to guarantee zero memory leaks upon script termination.

---

## 4. Dependencies & External Libraries

The project relies on external libraries located in the `lib/` directory:

- **chashmap**: A C library for dictionary and symbol table implementations. Located at `lib/chashmap` (symlinked). Used extensively for variable scope resolution and environment contexts.
