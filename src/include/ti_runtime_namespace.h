#ifndef TI_RUNTIME_NAMESPACE_H
#define TI_RUNTIME_NAMESPACE_H

#include "ti_type.h"
#include <stdbool.h>

/* Forward declarations: a namespace only holds pointers, so it needs no full definitions */
struct CONTEXT_STRUCT;
typedef struct CONTEXT_STRUCT context_t;

struct VARIABLE_STRUCT;
typedef struct VARIABLE_STRUCT variable_t;

struct FUNCTION_STRUCT;
typedef struct FUNCTION_STRUCT function_t;

struct TI_RUNTIME_STRUCT;
typedef struct TI_RUNTIME_STRUCT ti_runtime_t;

struct AST_STRUCT;
typedef struct AST_STRUCT ast_t;

struct TI_PROGRAM_STRUCT;
typedef struct TI_PROGRAM_STRUCT ti_program_t;

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- Namespace Types -------------------- */

/**
 * @brief The top-level variables and functions defined by one AST (the main script, later a script module).
 * A plain name (a variable at top level, a call f(), a definition) is resolved in the namespace whose
 * code is running, so two ASTs can define the same name without clashing.
 */
typedef struct TI_NAMESPACE_STRUCT {
    context_t  *globals;        /* Top-level variable scope (parent of every call frame of its functions) */
    function_t **functions;     /* FUNC_TI functions defined by this AST: an array of pointers, each entry its own
                                   block (both on the runtime alloc list), so an entry never moves when it grows */
    int         function_count; /* Number of entries in functions */
    ti_program_t *prog;         /* Program whose AST this namespace runs (its path: prog->path). Borrowed for
                                   main_ns (the host owns it); owned by a namespace from ti_namespace_load */
} ti_namespace_t;

/* -------------------- Namespace Functions -------------------- */

/**
 * @brief Initialize a namespace in storage the caller provides: create its globals scope, no functions yet.
 * The struct itself is never allocated here (main_ns lives inside the runtime).
 * @param rt Pointer to active runtime instance (globals are allocated on rt->alloc_list).
 * @param ns Namespace storage to fill (e.g. &rt->main_ns); must not be NULL.
 * @return TI_OK, or TI_ERR_NO_MEMORY if the globals scope cannot be created (ns->globals stays NULL).
 */
ti_status_t ti_namespace_init(ti_runtime_t *rt, ti_namespace_t *ns);

/**
 * @brief Free everything a namespace owns: its variables (values freed, handles destroyed), the globals scope,
 * each function's parameter array and the function table. AST parts (names, bodies) are borrowed and stay.
 * Must not be called while a function of this namespace is running. Not for main_ns (ti_runtime_destroy owns it).
 * @param rt Pointer to active runtime instance.
 * @param ns Namespace to empty (fields reset to NULL/0; the struct itself is not freed); NULL does nothing.
 */
void ti_namespace_free(ti_runtime_t *rt, ti_namespace_t *ns);

/**
 * @brief Run the top-level statements of an AST inside a namespace: its variables go to ns->globals and
 * its function definitions to ns->functions. rt->ns is switched to ns for the run and restored on every path.
 * A 'break' or 'continue' that escapes to the top level raises an error; a top-level 'return' just ends the run.
 * The caller holds the execution lock and reports a raised error itself.
 * @param rt Pointer to active runtime instance.
 * @param ns Namespace to run in (globals already created, e.g. &rt->main_ns).
 * @param root Root AST node of the program to run.
 * @return true if the AST ran to the end; false if an error was raised or the runtime was interrupted.
 */
bool ti_namespace_run(ti_runtime_t *rt, ti_namespace_t *ns, ast_t *root);

/**
 * @brief Find a FUNC_TI function defined at the top level of a namespace.
 * The pointer is one entry of ns->functions: it stays valid until the namespace is freed.
 * @param ns Namespace to search.
 * @param name Function name.
 * @return The function, or NULL if the namespace has no function with that name.
 */
function_t *ti_namespace_find_function(ti_namespace_t *ns, const char *name);

/**
 * @brief Find a variable defined at the top level of a namespace (in ns->globals).
 * @param ns Namespace to search.
 * @param name Variable name.
 * @return The variable (owned by the namespace: copy its value before handing it to a script),
 *         or NULL if the namespace has no variable with that name.
 */
variable_t *ti_namespace_find_variable(ti_namespace_t *ns, const char *name);

/**
 * @brief Ask the host's module loader for the source and the path of a script module.
 * The loader gets the path of the file that runs the import; the path it returns names the module's file,
 * so the caller can find a module already loaded from it before compiling anything.
 * @param rt Pointer to active runtime instance.
 * @param name Module name written after 'import' ("../lib/calibration").
 * @param line Line of the import statement, for error reports.
 * @param text_out Receives the source text (ti_raw_malloc; the caller frees it or hands it to ti_namespace_load).
 * @param path_out Receives the path (ti_raw_malloc; the caller frees it or hands it to ti_namespace_load).
 * @return true with both out parameters set, or false after raising an error (nothing is left allocated).
 */
bool ti_namespace_read_source(ti_runtime_t *rt, const char *name, int line, char **text_out, char **path_out);

/**
 * @brief Compile a script module's source and give it a new empty namespace.
 * Its top level has not run yet (the caller runs it with ti_namespace_run).
 * @param rt Pointer to active runtime instance (the namespace is allocated on its alloc list).
 * @param name Module name, for error reports.
 * @param line Line of the import statement, for error reports.
 * @param text Source from ti_namespace_read_source; always freed here.
 * @param path Path from ti_namespace_read_source; always freed here (the program keeps its own copy).
 * @return The new namespace (ns->prog owns the compiled AST and the path), or NULL after raising an error
 *         (nothing is left allocated).
 */
ti_namespace_t *ti_namespace_load(ti_runtime_t *rt, const char *name, int line, char *text, char *path);

#ifdef __cplusplus
}
#endif

#endif /* !TI_RUNTIME_NAMESPACE_H */
