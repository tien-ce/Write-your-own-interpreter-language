#include "include/ti_runtime_context.h"
#include "include/ti_runtime_visitor.h"
#include "include/ti_type.h"
#include "include/ti_type_value_dict.h"
#include "include/tracked_memory.h"
#include "include/debug.h"
#include "TienInterpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------- Static Function Prototypes -------------------- */

static variable_t *eval_lookup_variable(ti_runtime_t *rt, context_t *ctx, const char *name, int line);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Find a variable that must exist and hold a value; raise a runtime error otherwise.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to context.
 * @param name Variable identifier name.
 * @param line Source line used in the error report.
 * @return The variable, or NULL after ti_raise() (TI_ERR_UNDEFINED / TI_ERR_INTERNAL).
 */
static variable_t *eval_lookup_variable(ti_runtime_t *rt, context_t *ctx, const char *name, int line)
{
    /* Search for variable by name across active context hierarchy */
    variable_t *variable = context_find_variable(ctx, name);
    if (variable == NULL) {
        ti_raise(rt, TI_ERR_UNDEFINED, line, "Undefined variable '%s'", name);
        return NULL;
    }

    /* A registered variable always holds a value; anything else is an interpreter bug */
    if (variable->value == NULL) {
        ti_raise(rt, TI_ERR_INTERNAL, line, "Variable '%s' has no value", name);
        return NULL;
    }
    return variable;
}

/* -------------------- Variable & Identifier Evaluators -------------------- */

/**
 * @brief Evaluate a variable definition node and register into context.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to context.
 * @param node Variable definition AST node.
 * @return TI_VAL_OK on success, NULL on failure.
 */
value_t *eval_variable_definition(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    value_t *result = NULL;
    value_t *value = NULL;
    ti_status_t status = TI_OK;

    /* Duplicate variable identifier name on runtime memory list */
    char *variable_name = tracked_strdup(&rt->alloc_list, node->value.variable_definition.variable_name);
    if (variable_name == NULL) {
        ti_raise(rt, TI_ERR_NO_MEMORY, node->line, "Cannot allocate variable name");
        return NULL;
    }

    /* Recursively evaluate initializer expression; NULL means it already failed or was cancelled */
    value = visitor_visit(rt, ctx, node->value.variable_definition.value);
    if (value == NULL) {
        goto out;
    }

    /* Enforce declared type matches evaluated value type */
    if (node->value.variable_definition.variable_type != value->type) {
        ti_raise(rt, TI_ERR_TYPE_MISMATCH, node->line,
                 "Type mismatch in definition of '%s'. Expected %s, but got %s",
                 variable_name,
                 val_type_to_str(node->value.variable_definition.variable_type),
                 val_type_to_str(value->type));
        goto out;
    }

    /* Lists must also agree on the element type (list int vs list string) */
    if (value->type == VAL_LIST && value->list_val->elem_type != node->value.variable_definition.element_type) {
        ti_raise(rt, TI_ERR_TYPE_MISMATCH, node->line,
                 "Type mismatch in definition of '%s'. Expected list %s, but got list %s",
                 variable_name,
                 val_type_to_str(node->value.variable_definition.element_type),
                 val_type_to_str(value->list_val->elem_type));
        goto out;
    }

    /* Register new variable binding into the current scope context */
    status = context_add_variable(&rt->alloc_list, ctx, variable_name, value);
    if (status != TI_OK) {
        ti_raise(rt, status, node->line, "Cannot define variable '%s': %s",
                 variable_name, status == TI_ERR_RUNTIME ? "already defined in this scope" : ti_err_to_str(status));
        goto out;
    }

    /* The context now owns the name and the value: do not free them below */
    variable_name = NULL;
    value = NULL;
    result = TI_VAL_OK;

out:
    /* Single cleanup point for whatever the context did not take */
    val_free(value);
    tracked_free(&rt->alloc_list, variable_name);
    return result;
}

/**
 * @brief Evaluate an assignment statement node.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to context.
 * @param node Assignment AST node.
 * @return TI_VAL_OK on success, NULL on failure.
 */
value_t *eval_assignment(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    value_t *result = NULL;
    value_t *val = NULL;        /* right-hand side, owned until a container or variable takes it */
    value_t *key_val = NULL;    /* temporary dict key or list index */
    variable_t *variable = NULL;
    ti_status_t status = TI_OK;
    ast_t *target_node = node->value.assignment.target;

    /* Evaluate right-hand side; NULL means it already failed or was cancelled */
    val = visitor_visit(rt, ctx, node->value.assignment.value);
    if (val == NULL) {
        goto out;
    }

    switch ((int)target_node->type) {
    case AST_IDENTIFIER: /* variable = rhs */
        variable = eval_lookup_variable(rt, ctx, target_node->value.identifier, node->line);
        if (variable == NULL) {
            goto out;
        }

        /* Verify type compatibility with existing variable */
        if (variable->value->type != val->type) {
            ti_raise(rt, TI_ERR_TYPE_MISMATCH, node->line,
                     "Type mismatch in assignment to '%s'. Expected %s, but got %s",
                     target_node->value.identifier,
                     val_type_to_str(variable->value->type),
                     val_type_to_str(val->type));
            goto out;
        }

        /* Free previous value, then the variable takes ownership of val */
        val_free(variable->value);
        variable->value = val;
        val = NULL;
        break;

    case AST_ARRAY_ACCESS: { /* arr[0] = rhs or arr["key"] = rhs */
        const char *container_name = target_node->value.array_access.id;
        variable = eval_lookup_variable(rt, ctx, container_name, node->line);
        if (variable == NULL) {
            goto out;
        }

        /* Only dict and list support subscript assignment */
        val_type_t container_type = variable->value->type;
        if (container_type != VAL_DICT && container_type != VAL_LIST) {
            ti_raise(rt, TI_ERR_TYPE_MISMATCH, node->line,
                     "Type '%s' does not support subscript assignment", val_type_to_str(container_type));
            goto out;
        }

        /* Evaluate the key or index expression */
        key_val = visitor_visit(rt, ctx, target_node->value.array_access.index_expr);
        if (key_val == NULL) {
            goto out;
        }

        if (container_type == VAL_DICT) {
            if (key_val->type != VAL_STRING) {
                ti_raise(rt, TI_ERR_TYPE_MISMATCH, node->line,
                         "Dictionary keys must be strings for variable '%s'", container_name);
                goto out;
            }
            /* val_dict_set takes ownership of val even on failure */
            status = val_dict_set(variable->value->dict_val, key_val->string_val, val);
            val = NULL;
            if (status != TI_OK) {
                ti_raise(rt, status, node->line, "Cannot assign dictionary key '%s' of '%s': %s",
                         key_val->string_val, container_name, ti_err_to_str(status));
                goto out;
            }
        } else {
            if (key_val->type != VAL_INT) {
                ti_raise(rt, TI_ERR_TYPE_MISMATCH, node->line,
                         "List index must be an integer for variable '%s'", container_name);
                goto out;
            }
            /* val_list_set takes ownership of val even on failure */
            status = val_list_set(variable->value->list_val, key_val->int_val, val);
            val = NULL;
            if (status != TI_OK) {
                ti_raise(rt, status, node->line, "Cannot assign list index %d of '%s': %s",
                         key_val->int_val, container_name, ti_err_to_str(status));
                goto out;
            }
        }
        break;
    }

    default:
        ti_raise(rt, TI_ERR_INTERNAL, node->line, "Invalid assignment target type");
        goto out;
    }

    result = TI_VAL_OK;

out:
    /* Single cleanup point: temporaries and an rhs that nobody took */
    val_free(key_val);
    val_free(val);
    return result;
}

/**
 * @brief Look up and evaluate an identifier node.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to context.
 * @param node Identifier AST node.
 * @return Evaluated value_t pointer (deep copy), or NULL after raising a runtime error.
 */
value_t *eval_identifier(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    variable_t *variable = eval_lookup_variable(rt, ctx, node->value.identifier, node->line);
    if (variable == NULL) {
        return NULL;
    }

    /* Return an independent deep copy of the variable's value */
    value_t *value = context_copy_value(ctx ? ctx->alloc_list : NULL, variable);
    if (value == NULL) {
        ti_raise(rt, TI_ERR_NO_MEMORY, node->line, "Cannot copy value of '%s'", node->value.identifier);
    }
    return value;
}

/**
 * @brief Evaluate an array/dictionary access expression (R-Value).
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to context.
 * @param node Array access AST node.
 * @return Evaluated value_t pointer (deep copy), or NULL after raising a runtime error.
 */
value_t *eval_array_access(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    const char *container_name = node->value.array_access.id;
    value_t *result_val = NULL;
    value_t *key_val = NULL;
    ti_status_t status = TI_OK;

    /* Look up the container variable */
    variable_t *container_var = eval_lookup_variable(rt, ctx, container_name, node->line);
    if (container_var == NULL) {
        goto out;
    }

    /* Evaluate the index/key expression; NULL means it already failed or was cancelled */
    key_val = visitor_visit(rt, ctx, node->value.array_access.index_expr);
    if (key_val == NULL) {
        goto out;
    }

    /* Dispatch based on container type; result_val stays NULL unless a getter succeeds */
    switch ((int)container_var->value->type) {
    case VAL_DICT:
        if (key_val->type != VAL_STRING) {
            ti_raise(rt, TI_ERR_TYPE_MISMATCH, node->line,
                     "Dictionary keys must be strings for variable '%s'", container_name);
            goto out;
        }
        status = val_dict_get(container_var->value->dict_val, key_val->string_val, &result_val);
        if (status != TI_OK) {
            ti_raise(rt, status, node->line, "Cannot read key '%s' of dictionary '%s': %s",
                     key_val->string_val, container_name, ti_err_to_str(status));
        }
        break;

    case VAL_LIST:
        if (key_val->type != VAL_INT) {
            ti_raise(rt, TI_ERR_TYPE_MISMATCH, node->line,
                     "List index must be an integer for variable '%s'", container_name);
            goto out;
        }
        status = val_list_get(container_var->value->list_val, key_val->int_val, &result_val);
        if (status != TI_OK) {
            ti_raise(rt, status, node->line, "Cannot read list index %d of '%s' (size %d): %s",
                     key_val->int_val, container_name,
                     val_list_count(container_var->value->list_val), ti_err_to_str(status));
        }
        break;

    default:
        ti_raise(rt, TI_ERR_TYPE_MISMATCH, node->line,
                 "Type '%s' does not support subscript access",
                 val_type_to_str(container_var->value->type));
        break;
    }

out:
    /* Single cleanup point for the temporary key */
    val_free(key_val);
    return result_val;
}
