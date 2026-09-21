#ifndef TI_RUNTIME_CONTEXT_H
#define TI_RUNTIME_CONTEXT_H

#include "ti_type_value.h"
#include "tracked_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- Flow State Enum -------------------- */

/**
 * @brief Control flow interruption state flags.
 * Tracks non-sequential jumps (return, break, continue) across nested compound blocks.
 */
typedef enum {
    FLOW_NORMAL,     // Sequential execution within compound block
    FLOW_RETURN,     // Return statement triggered: unwinds scopes up to function boundary
    FLOW_BREAK,      // Break statement triggered: breaks out of innermost loop
    FLOW_CONTINUE,   // Continue statement triggered: jumps to next iteration of loop
} flow_state_t;

/* -------------------- Variable & Context Types -------------------- */

typedef struct VARIABLE_STRUCT {
    const char *name;
    value_t *value;
} variable_t;

typedef struct CONTEXT_STRUCT {
    struct CONTEXT_STRUCT *parent; // Enclosing parent scope (or NULL for root)
    /* Variable symbol table */
    variable_t **variables;        // Symbol table of local variables
    int variable_count;            // Number of registered local variables

    /* Control flow state */
    flow_state_t flow_state;       // Active flow interruption flag
    value_t *return_value;         // Evaluated return payload (owned by this context until consumed or bubbled)
    alloc_hdr_t **alloc_list;      // Dedicated allocation tracking list for this runtime
} context_t;

/* -------------------- Context & Variable Operations -------------------- */

/**
 * @brief Allocate a new interpreter context scope.
 * @param list Pointer to head of allocation list (or NULL).
 * @return Pointer to newly allocated context_t.
 */
context_t *context_init(alloc_hdr_t **list);

/**
 * @brief Free an interpreter context and its scoped variables.
 * @param list Pointer to head of allocation list (or NULL).
 * @param ctx Pointer to context scope to free.
 */
void context_free(alloc_hdr_t **list, context_t *ctx);

/**
 * @brief Free all variables and internal structures inside a context scope.
 * @param list Pointer to head of allocation list (or NULL).
 * @param ctx Pointer to context scope.
 */
void context_free_internal(alloc_hdr_t **list, context_t *ctx);

/**
 * @brief Allocate a new variable_t with the given variable name.
 * @param list Pointer to head of allocation list (or NULL).
 * @param variable_name Name string for the variable.
 * @return Newly allocated variable_t.
 */
variable_t *variable_init(alloc_hdr_t **list, const char *variable_name);

/**
 * @brief Free a variable structure, its name string, and its value payload.
 * @param list Pointer to head of allocation list (or NULL).
 * @param var Pointer to variable_t.
 */
void variable_free(alloc_hdr_t **list, variable_t *var);

/**
 * @brief Find a variable by name walking up from the current context to root parent.
 * @param ctx Starting context scope.
 * @param variable_name Identifier name to look up.
 * @return Pointer to variable_t if found, NULL otherwise.
 */
variable_t *context_find_variable(context_t *ctx, const char *variable_name);

/**
 * @brief Create a deep copy of a variable's value_t.
 * @param list Pointer to head of allocation list (or NULL).
 * @param variable Source variable pointer.
 * @return Newly allocated copied value_t.
 */
value_t *context_copy_value(alloc_hdr_t **list, variable_t *variable);

/**
 * @brief Add a newly defined variable to the given context scope.
 * @param list Pointer to head of allocation list (or NULL).
 * @param ctx Pointer to target context scope.
 * @param name Variable identifier name.
 * @param value Evaluated value pointer.
 */
void context_add_variable(alloc_hdr_t **list, context_t *ctx, const char *name, value_t *value);

#ifdef __cplusplus
}
#endif

#endif /* !TI_RUNTIME_CONTEXT_H */
