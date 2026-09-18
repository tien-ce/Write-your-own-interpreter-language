#include "include/visitor_internal.h"
#include "include/tracked_memory.h"
#include "TienInterpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------- Static Function Prototypes -------------------- */

static value_t *binary_add(value_t *left, value_t *right, int line);
static value_t *binary_sub(value_t *left, value_t *right, int line);
static value_t *binary_mul(value_t *left, value_t *right, int line);
static value_t *binary_div(value_t *left, value_t *right, int line);
static value_t *binary_equal(value_t *left, value_t *right, int line);
static value_t *binary_greater(value_t *left, value_t *right, int line);
static value_t *binary_less(value_t *left, value_t *right, int line);
static value_t *binary_greater_equal(value_t *left, value_t *right, int line);
static value_t *binary_less_equal(value_t *left, value_t *right, int line);
static value_t *binary_logical_and(value_t *left, value_t *right, int line);
static value_t *binary_logical_or(value_t *left, value_t *right, int line);

/* -------------------- Static Operator Functions -------------------- */

/**
 * @brief Evaluate binary addition for integers, floats, or strings (concatenation).
 * @param left Left operand value.
 * @param right Right operand value.
 * @return Newly allocated addition result value_t.
 */
static value_t *binary_add(value_t *left, value_t *right, int line)
{
    if (left == NULL || right == NULL) {
        ti_log("[Runtime Error] Invalid operands in binary add at line %d\n", line);
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
        ti_log("[Runtime Error] Unexpected operands %d, %d in binary add at line %d\n", left->type, right->type, line);
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
static value_t *binary_sub(value_t *left, value_t *right, int line)
{
    if (left == NULL || right == NULL) {
        ti_log("[Runtime Error] Invalid operands in binary sub at line %d\n", line);
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
        ti_log("[Runtime Error] Unexpected operands %d, %d in binary sub at line %d\n", left->type, right->type, line);
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
static value_t *binary_mul(value_t *left, value_t *right, int line)
{
    if (left == NULL || right == NULL) {
        ti_log("[Runtime Error] Invalid operands in binary mul at line %d\n", line);
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
        ti_log("[Runtime Error] Unexpected operands %d, %d in binary mul at line %d\n", left->type, right->type, line);
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
static value_t *binary_div(value_t *left, value_t *right, int line)
{
    if (left == NULL || right == NULL) {
        ti_log("[Runtime Error] Invalid operands in binary div at line %d\n", line);
        ti_fatal();
        return NULL;
    }

    value_t *value = val_init(left->type);
    switch (left->type) {
    case VAL_INT:
        if (right->int_val == 0) {
            ti_log("[Runtime Error] Division by zero error at line %d\n", line);
            ti_fatal();
            break;
        }
        value->int_val = left->int_val / right->int_val;
        break;
    case VAL_FLOAT:
        if (right->float_val == 0.0f) {
            ti_log("[Runtime Error] Division by zero error at line %d\n", line);
            ti_fatal();
            break;
        }
        value->float_val = left->float_val / right->float_val;
        break;
    default:
        ti_log("[Runtime Error] Unexpected operands %d, %d in binary div at line %d\n", left->type, right->type, line);
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
static value_t *binary_equal(value_t *left, value_t *right, int line)
{
    if (left == NULL || right == NULL) {
        ti_log("[Runtime Error] Invalid operands in binary equal at line %d\n", line);
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
        ti_log("[Runtime Error] Unexpected operands %d, %d in binary equal at line %d\n", left->type, right->type, line);
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
static value_t *binary_greater(value_t *left, value_t *right, int line)
{
    if (left == NULL || right == NULL) {
        ti_log("[Runtime Error] Invalid operands in binary greater at line %d\n", line);
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
        ti_log("[Runtime Error] Unexpected operands %d, %d in binary greater at line %d\n", left->type, right->type, line);
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
static value_t *binary_less(value_t *left, value_t *right, int line)
{
    if (left == NULL || right == NULL) {
        ti_log("[Runtime Error] Invalid operands in binary less at line %d\n", line);
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
        ti_log("[Runtime Error] Unexpected operands %d, %d in binary less at line %d\n", left->type, right->type, line);
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
static value_t *binary_greater_equal(value_t *left, value_t *right, int line)
{
    if (left == NULL || right == NULL) {
        ti_log("[Runtime Error] Invalid operands in binary greater equal at line %d\n", line);
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
        ti_log("[Runtime Error] Unexpected operands %d, %d in binary greater equal at line %d\n", left->type, right->type, line);
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
static value_t *binary_less_equal(value_t *left, value_t *right, int line)
{
    if (left == NULL || right == NULL) {
        ti_log("[Runtime Error] Invalid operands in binary less equal at line %d\n", line);
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
        ti_log("[Runtime Error] Unexpected operands %d, %d in binary less equal at line %d\n", left->type, right->type, line);
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
static value_t *binary_logical_and(value_t *left, value_t *right, int line)
{
    if (left == NULL || right == NULL) {
        ti_log("[Runtime Error] Invalid operands in binary logical and at line %d\n", line);
        ti_fatal();
        return NULL;
    }
    if (left->type != VAL_BOOL || right->type != VAL_BOOL) {
        ti_log("[Runtime Error] Logical AND expects bool operands, got %d and %d at line %d\n", left->type, right->type, line);
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
static value_t *binary_logical_or(value_t *left, value_t *right, int line)
{
    if (left == NULL || right == NULL) {
        ti_log("[Runtime Error] Invalid operands in binary logical or at line %d\n", line);
        ti_fatal();
        return NULL;
    }
    if (left->type != VAL_BOOL || right->type != VAL_BOOL) {
        ti_log("[Runtime Error] Logical OR expects bool operands, got %d and %d at line %d\n", left->type, right->type, line);
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
        ti_log("[Runtime Error] Binary expression operand evaluated to NULL at line %d\n", node->line);
        ti_fatal();
    }

    if (left->type != right->type) {
        ti_log("[Runtime Error] Type mismatch in binary expression: %d and %d at line %d\n", left->type, right->type, node->line);
        ti_fatal();
    }

    switch (node->value.binary_expr.op) {
    case OP_ADD:
        result = binary_add(left, right, node->line);
        break;
    case OP_SUB:
        result = binary_sub(left, right, node->line);
        break;
    case OP_MUL:
        result = binary_mul(left, right, node->line);
        break;
    case OP_DIV:
        result = binary_div(left, right, node->line);
        break;
    case OP_DEQ:
        result = binary_equal(left, right, node->line);
        break;
    case OP_GT:
        result = binary_greater(left, right, node->line);
        break;
    case OP_LT:
        result = binary_less(left, right, node->line);
        break;
    case OP_GTE:
        result = binary_greater_equal(left, right, node->line);
        break;
    case OP_LTE:
        result = binary_less_equal(left, right, node->line);
        break;
    case OP_LOGICAL_AND:
        result = binary_logical_and(left, right, node->line);
        break;
    case OP_LOGICAL_OR:
        result = binary_logical_or(left, right, node->line);
        break;
    default:
        ti_log("[Runtime Error] Unknown operator: %d at line %d\n", node->value.binary_expr.op, node->line);
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
        ti_log("[Runtime Error] Unary expression operand evaluated to NULL at line %d\n", node->line);
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
            ti_log("[Runtime Error] Unary '+' only supports int and float, got type %d at line %d\n", operand->type, node->line);
            ti_fatal();
        }
        break;

    case OP_NEG:
        if (operand->type == VAL_INT) {
            result = val_new_int(-operand->int_val);
        } else if (operand->type == VAL_FLOAT) {
            result = val_new_float(-operand->float_val);
        } else {
            ti_log("[Runtime Error] Unary '-' only supports int and float, got type %d at line %d\n", operand->type, node->line);
            ti_fatal();
        }
        break;

    case OP_NOT:
        if (operand->type == VAL_BOOL) {
            result = val_new_bool(!operand->bool_val);
        } else {
            ti_log("[Runtime Error] Unary '!' only supports bool, got type %d at line %d\n", operand->type, node->line);
            ti_fatal();
        }
        break;

    default:
        ti_log("[Runtime Error] Unknown unary operator: %d at line %d\n", node->value.unary_expr.op, node->line);
        ti_fatal();
        break;
    }

    val_free_internal(operand);
    tracked_free(operand);
    return result;
}
