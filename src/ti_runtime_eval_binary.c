#include "include/ti_runtime_visitor.h"
#include "include/tracked_memory.h"
#include "include/debug.h"
#include "TienInterpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------- Static Function Prototypes -------------------- */

static value_t *binary_add(ti_runtime_t *rt, value_t *left, value_t *right, int line);
static value_t *binary_sub(ti_runtime_t *rt, value_t *left, value_t *right, int line);
static value_t *binary_mul(ti_runtime_t *rt, value_t *left, value_t *right, int line);
static value_t *binary_div(ti_runtime_t *rt, value_t *left, value_t *right, int line);
static value_t *binary_equal(ti_runtime_t *rt, value_t *left, value_t *right, int line);
static value_t *binary_not_equal(ti_runtime_t *rt, value_t *left, value_t *right, int line);
static value_t *binary_greater(ti_runtime_t *rt, value_t *left, value_t *right, int line);
static value_t *binary_less(ti_runtime_t *rt, value_t *left, value_t *right, int line);
static value_t *binary_greater_equal(ti_runtime_t *rt, value_t *left, value_t *right, int line);
static value_t *binary_less_equal(ti_runtime_t *rt, value_t *left, value_t *right, int line);
static value_t *binary_logical_and(ti_runtime_t *rt, value_t *left, value_t *right, int line);
static value_t *binary_logical_or(ti_runtime_t *rt, value_t *left, value_t *right, int line);

/* -------------------- Static Operator Functions -------------------- */

/**
 * @brief Evaluate binary addition for integers, floats, or strings (concatenation).
 * @param rt Pointer to active runtime instance.
 * @param left Left operand value.
 * @param right Right operand value.
 * @param line Source line number for error reporting.
 * @return Newly allocated addition result value_t.
 */
static value_t *binary_add(ti_runtime_t *rt, value_t *left, value_t *right, int line)
{
    /* Validate operands are non-null; halt execution on runtime error */
    if (left == NULL || right == NULL) {
        ti_raise(rt, TI_ERR_INTERNAL, line, "Invalid operands in binary add");
        return NULL;
    }

    /* Initialize result value container matching the left operand type */
    value_t *value = val_init(left->type);
    switch (left->type) {
    case VAL_INT:
        /* Evaluate integer addition */
        value->int_val = left->int_val + right->int_val;
        break;
    case VAL_FLOAT:
        /* Evaluate floating-point addition */
        value->float_val = left->float_val + right->float_val;
        break;
    case VAL_STRING: {
        /* String concatenation: calculate combined length including null terminator,
         * allocate raw memory buffer via ti_raw_calloc, and concatenate operands */
        const char *s_left = left->string_val ? left->string_val : "";
        const char *s_right = right->string_val ? right->string_val : "";
        int length = strlen(s_left) + strlen(s_right) + 1;
        value->string_val = ti_raw_calloc(1, sizeof(char) * length);
        strcat(value->string_val, s_left);
        strcat(value->string_val, s_right);
        break;
    }
    default:
        /* Handle unsupported operand types and signal fatal error */
        ti_raise(rt, TI_ERR_TYPE_MISMATCH, line, "Unexpected operands %s, %s in binary add", val_type_to_str(left->type), val_type_to_str(right->type));
        val_free(value);
        return NULL;
    }
    return value;
}

/**
 * @brief Evaluate binary subtraction for integers or floats.
 * @param rt Pointer to active runtime instance.
 * @param left Left operand value.
 * @param right Right operand value.
 * @param line Source line number for error reporting.
 * @return Newly allocated subtraction result value_t.
 */
static value_t *binary_sub(ti_runtime_t *rt, value_t *left, value_t *right, int line)
{
    /* Validate operands are non-null; halt execution on runtime error */
    if (left == NULL || right == NULL) {
        ti_raise(rt, TI_ERR_INTERNAL, line, "Invalid operands in binary sub");
        return NULL;
    }

    /* Initialize result value container matching the left operand type */
    value_t *value = val_init(left->type);
    switch (left->type) {
    case VAL_INT:
        /* Evaluate integer subtraction */
        value->int_val = left->int_val - right->int_val;
        break;
    case VAL_FLOAT:
        /* Evaluate floating-point subtraction */
        value->float_val = left->float_val - right->float_val;
        break;
    default:
        /* Handle unsupported operand types and signal fatal error */
        ti_raise(rt, TI_ERR_TYPE_MISMATCH, line, "Unexpected operands %s, %s in binary sub", val_type_to_str(left->type), val_type_to_str(right->type));
        val_free(value);
        return NULL;
    }
    return value;
}

/**
 * @brief Evaluate binary multiplication for integers or floats.
 * @param rt Pointer to active runtime instance.
 * @param left Left operand value.
 * @param right Right operand value.
 * @param line Source line number for error reporting.
 * @return Newly allocated multiplication result value_t.
 */
static value_t *binary_mul(ti_runtime_t *rt, value_t *left, value_t *right, int line)
{
    /* Validate operands are non-null; halt execution on runtime error */
    if (left == NULL || right == NULL) {
        ti_raise(rt, TI_ERR_INTERNAL, line, "Invalid operands in binary mul");
        return NULL;
    }

    /* Initialize result value container matching the left operand type */
    value_t *value = val_init(left->type);
    switch (left->type) {
    case VAL_INT:
        /* Evaluate integer multiplication */
        value->int_val = left->int_val * right->int_val;
        break;
    case VAL_FLOAT:
        /* Evaluate floating-point multiplication */
        value->float_val = left->float_val * right->float_val;
        break;
    default:
        /* Handle unsupported operand types and signal fatal error */
        ti_raise(rt, TI_ERR_TYPE_MISMATCH, line, "Unexpected operands %s, %s in binary mul", val_type_to_str(left->type), val_type_to_str(right->type));
        val_free(value);
        return NULL;
    }
    return value;
}

/**
 * @brief Evaluate binary division for integers or floats (with division-by-zero check).
 * @param rt Pointer to active runtime instance.
 * @param left Left operand value.
 * @param right Right operand value.
 * @param line Source line number for error reporting.
 * @return Newly allocated division result value_t.
 */
static value_t *binary_div(ti_runtime_t *rt, value_t *left, value_t *right, int line)
{
    /* Validate operands are non-null; halt execution on runtime error */
    if (left == NULL || right == NULL) {
        ti_raise(rt, TI_ERR_INTERNAL, line, "Invalid operands in binary div");
        return NULL;
    }

    /* Initialize result value container matching the left operand type */
    value_t *value = val_init(left->type);
    switch (left->type) {
    case VAL_INT:
        /* Guard against integer division by zero */
        if (right->int_val == 0) {
            ti_raise(rt, TI_ERR_DIV_ZERO, line, "Division by zero error");
            val_free(value);
            return NULL;
        }
        /* Evaluate integer division */
        value->int_val = left->int_val / right->int_val;
        break;
    case VAL_FLOAT:
        /* Guard against floating-point division by zero */
        if (right->float_val == 0.0f) {
            ti_raise(rt, TI_ERR_DIV_ZERO, line, "Division by zero error");
            val_free(value);
            return NULL;
        }
        /* Evaluate floating-point division */
        value->float_val = left->float_val / right->float_val;
        break;
    default:
        /* Handle unsupported operand types and signal fatal error */
        ti_raise(rt, TI_ERR_TYPE_MISMATCH, line, "Unexpected operands %s, %s in binary div", val_type_to_str(left->type), val_type_to_str(right->type));
        val_free(value);
        return NULL;
    }
    return value;
}

/**
 * @brief Evaluate equality comparison (==) between two values.
 * @param rt Pointer to active runtime instance.
 * @param left Left operand value.
 * @param right Right operand value.
 * @param line Source line number for error reporting.
 * @return Newly allocated boolean value_t.
 */
static value_t *binary_equal(ti_runtime_t *rt, value_t *left, value_t *right, int line)
{
    /* Validate operands are non-null; halt execution on runtime error */
    if (left == NULL || right == NULL) {
        ti_raise(rt, TI_ERR_INTERNAL, line, "Invalid operands in binary equal");
        return NULL;
    }

    /* Initialize boolean result value */
    value_t *value = val_init(VAL_BOOL);
    switch (left->type) {
    case VAL_INT:
        /* Compare integer values */
        value->bool_val = (left->int_val == right->int_val);
        break;
    case VAL_FLOAT:
        /* Compare floating-point values */
        value->bool_val = (left->float_val == right->float_val);
        break;
    case VAL_STRING: {
        /* Compare string contents lexicographically using strcmp */
        const char *s_left = left->string_val ? left->string_val : "";
        const char *s_right = right->string_val ? right->string_val : "";
        value->bool_val = (strcmp(s_left, s_right) == 0);
        break;
    }
    case VAL_BOOL:
        /* Compare boolean states */
        value->bool_val = (left->bool_val == right->bool_val);
        break;
    case VAL_NULL:
        /* Both operands are NULL; evaluate to true */
        value->bool_val = true;
        break;
    default:
        /* Handle unsupported operand types and signal fatal error */
        ti_raise(rt, TI_ERR_TYPE_MISMATCH, line, "Unexpected operands %s, %s in binary equal", val_type_to_str(left->type), val_type_to_str(right->type));
        val_free(value);
        return NULL;
    }
    return value;
}

/**
 * @brief Evaluate equality comparison (==) between two values.
 * @param rt Pointer to active runtime instance.
 * @param left Left operand value.
 * @param right Right operand value.
 * @param line Source line number for error reporting.
 * @return Newly allocated boolean value_t.
 */
static value_t *binary_not_equal(ti_runtime_t *rt, value_t *left, value_t *right, int line)
{
    /* Validate operands are non-null; halt execution on runtime error */
    if (left == NULL || right == NULL) {
        ti_raise(rt, TI_ERR_INTERNAL, line, "Invalid operands in binary equal");
        return NULL;
    }

    /* Initialize boolean result value */
    value_t *value = val_init(VAL_BOOL);
    switch (left->type) {
    case VAL_INT:
        /* Compare integer values */
        value->bool_val = (left->int_val != right->int_val);
        break;
    case VAL_FLOAT:
        /* Compare floating-point values */
        value->bool_val = (left->float_val != right->float_val);
        break;
    case VAL_STRING: {
        /* Compare string contents lexicographically using strcmp */
        const char *s_left = left->string_val ? left->string_val : "";
        const char *s_right = right->string_val ? right->string_val : "";
        value->bool_val = (strcmp(s_left, s_right) != 0);
        break;
    }
    case VAL_BOOL:
        /* Compare boolean states */
        value->bool_val = (left->bool_val != right->bool_val);
        break;
    case VAL_NULL:
        /* Both operands are NULL; evaluate to false */
        value->bool_val = false;
        break;
    default:
        /* Handle unsupported operand types and signal fatal error */
        ti_raise(rt, TI_ERR_TYPE_MISMATCH, line, "Unexpected operands %s, %s in binary not equal", val_type_to_str(left->type), val_type_to_str(right->type));
        val_free(value);
        return NULL;
    }
    return value;
}

/**
 * @brief Evaluate greater-than comparison (>) between two values.
 * @param rt Pointer to active runtime instance.
 * @param left Left operand value.
 * @param right Right operand value.
 * @param line Source line number for error reporting.
 * @return Newly allocated boolean value_t.
 */
static value_t *binary_greater(ti_runtime_t *rt, value_t *left, value_t *right, int line)
{
    /* Validate operands are non-null; halt execution on runtime error */
    if (left == NULL || right == NULL) {
        ti_raise(rt, TI_ERR_INTERNAL, line, "Invalid operands in binary greater");
        return NULL;
    }

    /* Initialize boolean result value */
    value_t *value = val_init(VAL_BOOL);
    switch (left->type) {
    case VAL_INT:
        /* Compare integer values */
        value->bool_val = (left->int_val > right->int_val);
        break;
    case VAL_FLOAT:
        /* Compare floating-point values */
        value->bool_val = (left->float_val > right->float_val);
        break;
    case VAL_STRING: {
        /* Compare string contents lexicographically */
        const char *s_left = left->string_val ? left->string_val : "";
        const char *s_right = right->string_val ? right->string_val : "";
        value->bool_val = (strcmp(s_left, s_right) > 0);
        break;
    }
    default:
        /* Handle unsupported operand types and signal fatal error */
        ti_raise(rt, TI_ERR_TYPE_MISMATCH, line, "Unexpected operands %s, %s in binary greater", val_type_to_str(left->type), val_type_to_str(right->type));
        val_free(value);
        return NULL;
    }
    return value;
}

/**
 * @brief Evaluate less-than comparison (<) between two values.
 * @param rt Pointer to active runtime instance.
 * @param left Left operand value.
 * @param right Right operand value.
 * @param line Source line number for error reporting.
 * @return Newly allocated boolean value_t.
 */
static value_t *binary_less(ti_runtime_t *rt, value_t *left, value_t *right, int line)
{
    /* Validate operands are non-null; halt execution on runtime error */
    if (left == NULL || right == NULL) {
        ti_raise(rt, TI_ERR_INTERNAL, line, "Invalid operands in binary less");
        return NULL;
    }

    /* Initialize boolean result value */
    value_t *value = val_init(VAL_BOOL);
    switch (left->type) {
    case VAL_INT:
        /* Compare integer values */
        value->bool_val = (left->int_val < right->int_val);
        break;
    case VAL_FLOAT:
        /* Compare floating-point values */
        value->bool_val = (left->float_val < right->float_val);
        break;
    case VAL_STRING: {
        /* Compare string contents lexicographically */
        const char *s_left = left->string_val ? left->string_val : "";
        const char *s_right = right->string_val ? right->string_val : "";
        value->bool_val = (strcmp(s_left, s_right) < 0);
        break;
    }
    default:
        /* Handle unsupported operand types and signal fatal error */
        ti_raise(rt, TI_ERR_TYPE_MISMATCH, line, "Unexpected operands %s, %s in binary less", val_type_to_str(left->type), val_type_to_str(right->type));
        val_free(value);
        return NULL;
    }
    return value;
}

/**
 * @brief Evaluate greater-than-or-equal comparison (>=) between two values.
 * @param rt Pointer to active runtime instance.
 * @param left Left operand value.
 * @param right Right operand value.
 * @param line Source line number for error reporting.
 * @return Newly allocated boolean value_t.
 */
static value_t *binary_greater_equal(ti_runtime_t *rt, value_t *left, value_t *right, int line)
{
    /* Validate operands are non-null; halt execution on runtime error */
    if (left == NULL || right == NULL) {
        ti_raise(rt, TI_ERR_INTERNAL, line, "Invalid operands in binary greater equal");
        return NULL;
    }

    /* Initialize boolean result value */
    value_t *value = val_init(VAL_BOOL);
    switch (left->type) {
    case VAL_INT:
        /* Compare integer values */
        value->bool_val = (left->int_val >= right->int_val);
        break;
    case VAL_FLOAT:
        /* Compare floating-point values */
        value->bool_val = (left->float_val >= right->float_val);
        break;
    case VAL_STRING: {
        /* Compare string contents lexicographically */
        const char *s_left = left->string_val ? left->string_val : "";
        const char *s_right = right->string_val ? right->string_val : "";
        value->bool_val = (strcmp(s_left, s_right) >= 0);
        break;
    }
    default:
        /* Handle unsupported operand types and signal fatal error */
        ti_raise(rt, TI_ERR_TYPE_MISMATCH, line, "Unexpected operands %s, %s in binary greater equal", val_type_to_str(left->type), val_type_to_str(right->type));
        val_free(value);
        return NULL;
    }
    return value;
}

/**
 * @brief Evaluate less-than-or-equal comparison (<=) between two values.
 * @param rt Pointer to active runtime instance.
 * @param left Left operand value.
 * @param right Right operand value.
 * @param line Source line number for error reporting.
 * @return Newly allocated boolean value_t.
 */
static value_t *binary_less_equal(ti_runtime_t *rt, value_t *left, value_t *right, int line)
{
    /* Validate operands are non-null; halt execution on runtime error */
    if (left == NULL || right == NULL) {
        ti_raise(rt, TI_ERR_INTERNAL, line, "Invalid operands in binary less equal");
        return NULL;
    }

    /* Initialize boolean result value */
    value_t *value = val_init(VAL_BOOL);
    switch (left->type) {
    case VAL_INT:
        /* Compare integer values */
        value->bool_val = (left->int_val <= right->int_val);
        break;
    case VAL_FLOAT:
        /* Compare floating-point values */
        value->bool_val = (left->float_val <= right->float_val);
        break;
    case VAL_STRING: {
        /* Compare string contents lexicographically */
        const char *s_left = left->string_val ? left->string_val : "";
        const char *s_right = right->string_val ? right->string_val : "";
        value->bool_val = (strcmp(s_left, s_right) <= 0);
        break;
    }
    default:
        /* Handle unsupported operand types and signal fatal error */
        ti_raise(rt, TI_ERR_TYPE_MISMATCH, line, "Unexpected operands %s, %s in binary less equal", val_type_to_str(left->type), val_type_to_str(right->type));
        val_free(value);
        return NULL;
    }
    return value;
}

/**
 * @brief Evaluate binary logical AND (&&) between two boolean values.
 * @param rt Pointer to active runtime instance.
 * @param left Left operand value.
 * @param right Right operand value.
 * @param line Source line number for error reporting.
 * @return Newly allocated boolean value_t.
 */
static value_t *binary_logical_and(ti_runtime_t *rt, value_t *left, value_t *right, int line)
{
    /* Validate operands are non-null */
    if (left == NULL || right == NULL) {
        ti_raise(rt, TI_ERR_INTERNAL, line, "Invalid operands in binary logical and");
        return NULL;
    }
    /* Enforce boolean operand types for logical operations */
    if (left->type != VAL_BOOL || right->type != VAL_BOOL) {
        ti_raise(rt, TI_ERR_TYPE_MISMATCH, line, "Logical AND expects bool operands, got %s and %s", val_type_to_str(left->type), val_type_to_str(right->type));
        return NULL;
    }

    /* Initialize boolean result and evaluate logical AND */
    value_t *value = val_init(VAL_BOOL);
    value->bool_val = (left->bool_val && right->bool_val);
    return value;
}

/**
 * @brief Evaluate binary logical OR (||) between two boolean values.
 * @param rt Pointer to active runtime instance.
 * @param left Left operand value.
 * @param right Right operand value.
 * @param line Source line number for error reporting.
 * @return Newly allocated boolean value_t.
 */
static value_t *binary_logical_or(ti_runtime_t *rt, value_t *left, value_t *right, int line)
{
    /* Validate operands are non-null */
    if (left == NULL || right == NULL) {
        ti_raise(rt, TI_ERR_INTERNAL, line, "Invalid operands in binary logical or");
        return NULL;
    }
    /* Enforce boolean operand types for logical operations */
    if (left->type != VAL_BOOL || right->type != VAL_BOOL) {
        ti_raise(rt, TI_ERR_TYPE_MISMATCH, line, "Logical OR expects bool operands, got %s and %s", val_type_to_str(left->type), val_type_to_str(right->type));
        return NULL;
    }

    /* Initialize boolean result and evaluate logical OR */
    value_t *value = val_init(VAL_BOOL);
    value->bool_val = (left->bool_val || right->bool_val);
    return value;
}

/* -------------------- Public Expression Evaluators -------------------- */

/* Evaluate a binary expression node (+, -, *, /, ==, <, etc.) */
value_t *eval_binary_expr(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    value_t *result = NULL;
    value_t *left = NULL;
    value_t *right = NULL;

    /* Evaluate the left operand; NULL means it already failed, so the right one must not run */
    left = visitor_visit(rt, ctx, node->value.binary_expr.left);
    if (left == NULL) {
        goto out;
    }

    right = visitor_visit(rt, ctx, node->value.binary_expr.right);
    if (right == NULL) {
        goto out;
    }

    /* Enforce type symmetry between left and right operands */
    if (left->type != right->type) {
        ti_raise(rt, TI_ERR_TYPE_MISMATCH, node->line, "Type mismatch in binary expression: %s and %s", val_type_to_str(left->type), val_type_to_str(right->type));
        goto out;
    }

    /* Dispatch operator evaluation to the corresponding handler */
    switch (node->value.binary_expr.op) {
    case OP_ADD:
        result = binary_add(rt, left, right, node->line);
        break;
    case OP_SUB:
        result = binary_sub(rt, left, right, node->line);
        break;
    case OP_MUL:
        result = binary_mul(rt, left, right, node->line);
        break;
    case OP_DIV:
        result = binary_div(rt, left, right, node->line);
        break;
    case OP_DEQ:
        result = binary_equal(rt, left, right, node->line);
        break;
    case OP_NEQ:
        result = binary_not_equal(rt, left, right, node->line);
        break;
    case OP_GT:
        result = binary_greater(rt, left, right, node->line);
        break;
    case OP_LT:
        result = binary_less(rt, left, right, node->line);
        break;
    case OP_GTE:
        result = binary_greater_equal(rt, left, right, node->line);
        break;
    case OP_LTE:
        result = binary_less_equal(rt, left, right, node->line);
        break;
    case OP_LOGICAL_AND:
        result = binary_logical_and(rt, left, right, node->line);
        break;
    case OP_LOGICAL_OR:
        result = binary_logical_or(rt, left, right, node->line);
        break;
    default:
        ti_raise(rt, TI_ERR_INTERNAL, node->line, "Unknown operator: %d", node->value.binary_expr.op);
        break;
    }

out:
    /* Single cleanup point: operands are temporaries, val_free(NULL) is a no-op */
    val_free(right);
    val_free(left);
    return result;
}

/* Evaluate a unary expression node (!, -, +) */
value_t *eval_unary_expr(ti_runtime_t *rt, context_t *ctx, ast_t *node)
{
    value_t *result = NULL;
    value_t *operand = NULL;

    /* Recursively evaluate the operand expression */
    operand = visitor_visit(rt, ctx, node->value.unary_expr.operand);
    if (operand == NULL) {
        goto out;
    }

    if (operand->type == VAL_NULL) {
        ti_raise(rt, TI_ERR_TYPE_MISMATCH, node->line, "Unary expression operand evaluated to NULL");
        goto out;
    }

    /* Dispatch unary operator evaluation */
    switch (node->value.unary_expr.op) {
    case OP_POS:
        /* Unary plus: preserve integer or float value */
        if (operand->type == VAL_INT) {
            result = val_new_int(+operand->int_val);
        } else if (operand->type == VAL_FLOAT) {
            result = val_new_float(+operand->float_val);
        } else {
            ti_raise(rt, TI_ERR_TYPE_MISMATCH, node->line, "Unary '+' only supports int and float, got type %s", val_type_to_str(operand->type));
        }
        break;

    case OP_NEG:
        /* Unary minus: negate integer or float value */
        if (operand->type == VAL_INT) {
            result = val_new_int(-operand->int_val);
        } else if (operand->type == VAL_FLOAT) {
            result = val_new_float(-operand->float_val);
        } else {
            ti_raise(rt, TI_ERR_TYPE_MISMATCH, node->line, "Unary '-' only supports int and float, got type %s", val_type_to_str(operand->type));
        }
        break;

    case OP_NOT:
        /* Logical negation: invert boolean state */
        if (operand->type == VAL_BOOL) {
            result = val_new_bool(!operand->bool_val);
        } else {
            ti_raise(rt, TI_ERR_TYPE_MISMATCH, node->line, "Unary '!' only supports bool, got type %s", val_type_to_str(operand->type));
        }
        break;

    default:
        ti_raise(rt, TI_ERR_INTERNAL, node->line, "Unknown unary operator: %d", node->value.unary_expr.op);
        break;
    }

out:
    /* Single cleanup point for the operand temporary */
    val_free(operand);
    return result;
}
