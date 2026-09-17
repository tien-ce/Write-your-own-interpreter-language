#include "include/visitor_internal.h"
#include "include/tracked_memory.h"
#include "TienInterpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------- Static Function Prototypes -------------------- */

static value_t *binary_add(value_t *left, value_t *right);
static value_t *binary_sub(value_t *left, value_t *right);
static value_t *binary_mul(value_t *left, value_t *right);
static value_t *binary_div(value_t *left, value_t *right);
static value_t *binary_equal(value_t *left, value_t *right);
static value_t *binary_greater(value_t *left, value_t *right);
static value_t *binary_less(value_t *left, value_t *right);
static value_t *binary_greater_equal(value_t *left, value_t *right);
static value_t *binary_less_equal(value_t *left, value_t *right);
static value_t *binary_logical_and(value_t *left, value_t *right);
static value_t *binary_logical_or(value_t *left, value_t *right);

/* -------------------- Static Operator Functions -------------------- */

/**
 * @brief Evaluate binary addition for integers, floats, or strings (concatenation).
 * @param left Left operand value.
 * @param right Right operand value.
 * @return Newly allocated addition result value_t.
 */
static value_t *binary_add(value_t *left, value_t *right)
{
    if (left == NULL || right == NULL) {
        ti_log("[ERROR]: Invalid operands in binary add\n");
        ti_fatal();
        return NULL;
    }

    value_t *value = val_init(left->type);
    switch (left->type) {
    case VAL_INT:
        value->int_val = left->int_val + right->int_val;
        break;
    case VAL_FLOAT:
        value->float_val = left->float_val + right->float_val;
        break;
    case VAL_STRING: {
        const char *s_left = left->string_val ? left->string_val : "";
        const char *s_right = right->string_val ? right->string_val : "";
        int length = strlen(s_left) + strlen(s_right) + 1;
        value->string_val = tracked_calloc(1, sizeof(char) * length);
        strcat(value->string_val, s_left);
        strcat(value->string_val, s_right);
        break;
    }
    default:
        ti_log("[ERROR]: Unexpected operands %d, %d in binary add\n", left->type, right->type);
        ti_fatal();
        break;
    }
    return value;
}

/**
 * @brief Evaluate binary subtraction for integers or floats.
 * @param left Left operand value.
 * @param right Right operand value.
 * @return Newly allocated subtraction result value_t.
 */
static value_t *binary_sub(value_t *left, value_t *right)
{
    if (left == NULL || right == NULL) {
        ti_log("[ERROR]: Invalid operands in binary sub\n");
        ti_fatal();
        return NULL;
    }

    value_t *value = val_init(left->type);
    switch (left->type) {
    case VAL_INT:
        value->int_val = left->int_val - right->int_val;
        break;
    case VAL_FLOAT:
        value->float_val = left->float_val - right->float_val;
        break;
    default:
        ti_log("[ERROR]: Unexpected operands %d, %d in binary sub\n", left->type, right->type);
        ti_fatal();
        break;
    }
    return value;
}

/**
 * @brief Evaluate binary multiplication for integers or floats.
 * @param left Left operand value.
 * @param right Right operand value.
 * @return Newly allocated multiplication result value_t.
 */
static value_t *binary_mul(value_t *left, value_t *right)
{
    if (left == NULL || right == NULL) {
        ti_log("[ERROR]: Invalid operands in binary mul\n");
        ti_fatal();
        return NULL;
    }

    value_t *value = val_init(left->type);
    switch (left->type) {
    case VAL_INT:
        value->int_val = left->int_val * right->int_val;
        break;
    case VAL_FLOAT:
        value->float_val = left->float_val * right->float_val;
        break;
    default:
        ti_log("[ERROR]: Unexpected operands %d, %d in binary mul\n", left->type, right->type);
        ti_fatal();
        break;
    }
    return value;
}

/**
 * @brief Evaluate binary division for integers or floats (with division-by-zero check).
 * @param left Left operand value.
 * @param right Right operand value.
 * @return Newly allocated division result value_t.
 */
static value_t *binary_div(value_t *left, value_t *right)
{
    if (left == NULL || right == NULL) {
        ti_log("[ERROR]: Invalid operands in binary div\n");
        ti_fatal();
        return NULL;
    }

    value_t *value = val_init(left->type);
    switch (left->type) {
    case VAL_INT:
        if (right->int_val == 0) {
            ti_log("[ERROR]: Division by zero error\n");
            ti_fatal();
            break;
        }
        value->int_val = left->int_val / right->int_val;
        break;
    case VAL_FLOAT:
        if (right->float_val == 0.0f) {
            ti_log("[ERROR]: Division by zero error\n");
            ti_fatal();
            break;
        }
        value->float_val = left->float_val / right->float_val;
        break;
    default:
        ti_log("[ERROR]: Unexpected operands %d, %d in binary div\n", left->type, right->type);
        ti_fatal();
        break;
    }
    return value;
}

/**
 * @brief Evaluate equality comparison (==) between two values.
 * @param left Left operand value.
 * @param right Right operand value.
 * @return Newly allocated boolean value_t.
 */
static value_t *binary_equal(value_t *left, value_t *right)
{
    if (left == NULL || right == NULL) {
        ti_log("[ERROR]: Invalid operands in binary equal\n");
        ti_fatal();
        return NULL;
    }

    value_t *value = val_init(VAL_BOOL);
    switch (left->type) {
    case VAL_INT:
        value->bool_val = (left->int_val == right->int_val);
        break;
    case VAL_FLOAT:
        value->bool_val = (left->float_val == right->float_val);
        break;
    case VAL_STRING: {
        const char *s_left = left->string_val ? left->string_val : "";
        const char *s_right = right->string_val ? right->string_val : "";
        value->bool_val = (strcmp(s_left, s_right) == 0);
        break;
    }
    case VAL_BOOL:
        value->bool_val = (left->bool_val == right->bool_val);
        break;
    case VAL_NULL:
        value->bool_val = true;
        break;
    default:
        ti_log("[ERROR]: Unexpected operands %d, %d in binary equal\n", left->type, right->type);
        ti_fatal();
        break;
    }
    return value;
}

/**
 * @brief Evaluate greater-than comparison (>) between two values.
 * @param left Left operand value.
 * @param right Right operand value.
 * @return Newly allocated boolean value_t.
 */
static value_t *binary_greater(value_t *left, value_t *right)
{
    if (left == NULL || right == NULL) {
        ti_log("[ERROR]: Invalid operands in binary greater\n");
        ti_fatal();
        return NULL;
    }

    value_t *value = val_init(VAL_BOOL);
    switch (left->type) {
    case VAL_INT:
        value->bool_val = (left->int_val > right->int_val);
        break;
    case VAL_FLOAT:
        value->bool_val = (left->float_val > right->float_val);
        break;
    case VAL_STRING: {
        const char *s_left = left->string_val ? left->string_val : "";
        const char *s_right = right->string_val ? right->string_val : "";
        value->bool_val = (strcmp(s_left, s_right) > 0);
        break;
    }
    default:
        ti_log("[ERROR]: Unexpected operands %d, %d in binary greater\n", left->type, right->type);
        ti_fatal();
        break;
    }
    return value;
}

/**
 * @brief Evaluate less-than comparison (<) between two values.
 * @param left Left operand value.
 * @param right Right operand value.
 * @return Newly allocated boolean value_t.
 */
static value_t *binary_less(value_t *left, value_t *right)
{
    if (left == NULL || right == NULL) {
        ti_log("[ERROR]: Invalid operands in binary less\n");
        ti_fatal();
        return NULL;
    }

    value_t *value = val_init(VAL_BOOL);
    switch (left->type) {
    case VAL_INT:
        value->bool_val = (left->int_val < right->int_val);
        break;
    case VAL_FLOAT:
        value->bool_val = (left->float_val < right->float_val);
        break;
    case VAL_STRING: {
        const char *s_left = left->string_val ? left->string_val : "";
        const char *s_right = right->string_val ? right->string_val : "";
        value->bool_val = (strcmp(s_left, s_right) < 0);
        break;
    }
    default:
        ti_log("[ERROR]: Unexpected operands %d, %d in binary less\n", left->type, right->type);
        ti_fatal();
        break;
    }
    return value;
}

/**
 * @brief Evaluate greater-than-or-equal comparison (>=) between two values.
 * @param left Left operand value.
 * @param right Right operand value.
 * @return Newly allocated boolean value_t.
 */
static value_t *binary_greater_equal(value_t *left, value_t *right)
{
    if (left == NULL || right == NULL) {
        ti_log("[ERROR]: Invalid operands in binary greater equal\n");
        ti_fatal();
        return NULL;
    }

    value_t *value = val_init(VAL_BOOL);
    switch (left->type) {
    case VAL_INT:
        value->bool_val = (left->int_val >= right->int_val);
        break;
    case VAL_FLOAT:
        value->bool_val = (left->float_val >= right->float_val);
        break;
    case VAL_STRING: {
        const char *s_left = left->string_val ? left->string_val : "";
        const char *s_right = right->string_val ? right->string_val : "";
        value->bool_val = (strcmp(s_left, s_right) >= 0);
        break;
    }
    default:
        ti_log("[ERROR]: Unexpected operands %d, %d in binary greater equal\n", left->type, right->type);
        ti_fatal();
        break;
    }
    return value;
}

/**
 * @brief Evaluate less-than-or-equal comparison (<=) between two values.
 * @param left Left operand value.
 * @param right Right operand value.
 * @return Newly allocated boolean value_t.
 */
static value_t *binary_less_equal(value_t *left, value_t *right)
{
    if (left == NULL || right == NULL) {
        ti_log("[ERROR]: Invalid operands in binary less equal\n");
        ti_fatal();
        return NULL;
    }

    value_t *value = val_init(VAL_BOOL);
    switch (left->type) {
    case VAL_INT:
        value->bool_val = (left->int_val <= right->int_val);
        break;
    case VAL_FLOAT:
        value->bool_val = (left->float_val <= right->float_val);
        break;
    case VAL_STRING: {
        const char *s_left = left->string_val ? left->string_val : "";
        const char *s_right = right->string_val ? right->string_val : "";
        value->bool_val = (strcmp(s_left, s_right) <= 0);
        break;
    }
    default:
        ti_log("[ERROR]: Unexpected operands %d, %d in binary less equal\n", left->type, right->type);
        ti_fatal();
        break;
    }
    return value;
}

/**
 * @brief Evaluate binary logical AND (&&) between two boolean values.
 * @param left Left operand value.
 * @param right Right operand value.
 * @return Newly allocated boolean value_t.
 */
static value_t *binary_logical_and(value_t *left, value_t *right)
{
    if (left == NULL || right == NULL) {
        ti_log("[ERROR]: Invalid operands in binary logical and\n");
        ti_fatal();
        return NULL;
    }
    if (left->type != VAL_BOOL || right->type != VAL_BOOL) {
        ti_log("[ERROR]: Logical AND expects bool operands, got %d and %d\n", left->type, right->type);
        ti_fatal();
        return NULL;
    }

    value_t *value = val_init(VAL_BOOL);
    value->bool_val = (left->bool_val && right->bool_val);
    return value;
}

/**
 * @brief Evaluate binary logical OR (||) between two boolean values.
 * @param left Left operand value.
 * @param right Right operand value.
 * @return Newly allocated boolean value_t.
 */
static value_t *binary_logical_or(value_t *left, value_t *right)
{
    if (left == NULL || right == NULL) {
        ti_log("[ERROR]: Invalid operands in binary logical or\n");
        ti_fatal();
        return NULL;
    }
    if (left->type != VAL_BOOL || right->type != VAL_BOOL) {
        ti_log("[ERROR]: Logical OR expects bool operands, got %d and %d\n", left->type, right->type);
        ti_fatal();
        return NULL;
    }

    value_t *value = val_init(VAL_BOOL);
    value->bool_val = (left->bool_val || right->bool_val);
    return value;
}

/* -------------------- Public Expression Evaluators -------------------- */

/* Evaluate a binary expression node (+, -, *, /, ==, <, etc.) */
value_t *eval_binary_expr(context_t *ctx, ast_t *node)
{
    value_t *left = visitor_visit(ctx, node->value.binary_expr.left);
    value_t *right = visitor_visit(ctx, node->value.binary_expr.right);
    value_t *result = NULL;

    if (left == NULL || right == NULL) {
        ti_log("[ERROR]: Binary expression operand evaluated to NULL\n");
        ti_fatal();
    }

    if (left->type != right->type) {
        ti_log("[ERROR]: Type mismatch in binary expression: %d and %d\n", left->type, right->type);
        ti_fatal();
    }

    switch (node->value.binary_expr.op) {
    case OP_ADD:
        result = binary_add(left, right);
        break;
    case OP_SUB:
        result = binary_sub(left, right);
        break;
    case OP_MUL:
        result = binary_mul(left, right);
        break;
    case OP_DIV:
        result = binary_div(left, right);
        break;
    case OP_DEQ:
        result = binary_equal(left, right);
        break;
    case OP_GT:
        result = binary_greater(left, right);
        break;
    case OP_LT:
        result = binary_less(left, right);
        break;
    case OP_GTE:
        result = binary_greater_equal(left, right);
        break;
    case OP_LTE:
        result = binary_less_equal(left, right);
        break;
    case OP_LOGICAL_AND:
        result = binary_logical_and(left, right);
        break;
    case OP_LOGICAL_OR:
        result = binary_logical_or(left, right);
        break;
    default:
        ti_log("[ERROR]: Unknown operator: %d\n", node->value.binary_expr.op);
        ti_fatal();
        break;
    }

    val_free_internal(left);
    val_free_internal(right);
    tracked_free(left);
    tracked_free(right);
    return result;
}

/* Evaluate a unary expression node (!, -, +) */
value_t *eval_unary_expr(context_t *ctx, ast_t *node)
{
    value_t *operand = visitor_visit(ctx, node->value.unary_expr.operand); 
    if (operand == NULL || operand->type == VAL_NULL) {
        ti_log("[ERROR]: Unary expression operand evaluated to NULL\n");
        ti_fatal();
        return NULL;
    }

    value_t *result = NULL;
    switch (node->value.unary_expr.op) {
    case OP_POS:
        if (operand->type == VAL_INT) {
            result = val_new_int(+operand->int_val);
        } else if (operand->type == VAL_FLOAT) {
            result = val_new_float(+operand->float_val);
        } else {
            ti_log("[ERROR]: Unary '+' only supports int and float, got type %d\n", operand->type);
            ti_fatal();
        }
        break;

    case OP_NEG:
        if (operand->type == VAL_INT) {
            result = val_new_int(-operand->int_val);
        } else if (operand->type == VAL_FLOAT) {
            result = val_new_float(-operand->float_val);
        } else {
            ti_log("[ERROR]: Unary '-' only supports int and float, got type %d\n", operand->type);
            ti_fatal();
        }
        break;

    case OP_NOT:
        if (operand->type == VAL_BOOL) {
            result = val_new_bool(!operand->bool_val);
        } else {
            ti_log("[ERROR]: Unary '!' only supports bool, got type %d\n", operand->type);
            ti_fatal();
        }
        break;

    default:
        ti_log("[ERROR]: Unknown unary operator: %d\n", node->value.unary_expr.op);
        ti_fatal();
        break;
    }

    val_free_internal(operand);
    tracked_free(operand);
    return result;
}
