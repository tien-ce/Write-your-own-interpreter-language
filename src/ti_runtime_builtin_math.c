#include "include/ti_runtime_builtin.h"
#include "include/debug.h"
#include "TienInterpreter.h"
#include <limits.h>
#include <math.h>

/* -------------------- Constants -------------------- */

/* Value of pi in single precision */
#define TI_MATH_PI 3.14159265358979323846f

/* int range expressed as floats: INT_MIN is exact, 2^31 is the first value that no longer fits */
#define TI_MATH_INT_MIN_F (-2147483648.0f)
#define TI_MATH_INT_END_F (2147483648.0f)

/* -------------------- Static Types -------------------- */

/* One-argument float function (sqrtf, sinf, ...) */
typedef float (*unary_fn_t)(float);

/* Two-argument float function (powf, atan2f) */
typedef float (*binary_fn_t)(float, float);

/* -------------------- Static Function Prototypes -------------------- */

static bool number_arg(ti_handle_t handle, const char *name, int index, const value_t *arg, float *out);
static value_t *finite_result(ti_handle_t handle, const char *name, float result);
static value_t *apply_unary(ti_handle_t handle, const char *name, value_t **argv, unary_fn_t fn);
static value_t *apply_binary(ti_handle_t handle, const char *name, value_t **argv, binary_fn_t fn);
static value_t *round_to_int(ti_handle_t handle, const char *name, value_t **argv, unary_fn_t fn);
static bool same_number_type(ti_handle_t handle, const char *name, value_t **argv);
static float round_half_away(float x);
static value_t *builtin_pow(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_sqrt(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_exp(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_log(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_log10(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_sin(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_cos(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_tan(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_atan2(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_pi(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_floor(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_ceil(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_round(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_abs(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_min(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_max(ti_handle_t handle, value_t **argv, int argc);

/* -------------------- Static Variables -------------------- */

/* Numbers may be int or float, so the type is open (VAL_ANY) while the argument count stays checked */
static param_t s_one_number[] = { { VAL_ANY, "x", VAL_NULL } };
static param_t s_two_numbers[] = { { VAL_ANY, "x", VAL_NULL }, { VAL_ANY, "y", VAL_NULL } };

/* abs, min and max return the type of their arguments, so their declared return type is VAL_ANY */
static const ti_builtin_def_t s_defs[] = {
    { "pow",   VAL_FLOAT, s_two_numbers, 2,    builtin_pow   },
    { "sqrt",  VAL_FLOAT, s_one_number,  1,    builtin_sqrt  },
    { "exp",   VAL_FLOAT, s_one_number,  1,    builtin_exp   },
    { "log",   VAL_FLOAT, s_one_number,  1,    builtin_log   },
    { "log10", VAL_FLOAT, s_one_number,  1,    builtin_log10 },
    { "sin",   VAL_FLOAT, s_one_number,  1,    builtin_sin   },
    { "cos",   VAL_FLOAT, s_one_number,  1,    builtin_cos   },
    { "tan",   VAL_FLOAT, s_one_number,  1,    builtin_tan   },
    { "atan2", VAL_FLOAT, s_two_numbers, 2,    builtin_atan2 },
    { "pi",    VAL_FLOAT, NULL,          0,    builtin_pi    },
    { "floor", VAL_INT,   s_one_number,  1,    builtin_floor },
    { "ceil",  VAL_INT,   s_one_number,  1,    builtin_ceil  },
    { "round", VAL_INT,   s_one_number,  1,    builtin_round },
    { "abs",   VAL_ANY,   s_one_number,  1,    builtin_abs   },
    { "min",   VAL_ANY,   s_two_numbers, 2,    builtin_min   },
    { "max",   VAL_ANY,   s_two_numbers, 2,    builtin_max   },
};

/* -------------------- Static Functions -------------------- */

/**
 * @brief Read an argument that must be an int or a float as a float.
 * @param handle Calling runtime handle, used to report errors.
 * @param name Function name for the message.
 * @param index Zero-based argument position.
 * @param arg Argument value.
 * @param out Receives the numeric value.
 * @return true if the argument is a number, false after raising TI_ERR_TYPE_MISMATCH.
 */
static bool number_arg(ti_handle_t handle, const char *name, int index, const value_t *arg, float *out)
{
    if (arg->type == VAL_INT) {
        *out = (float)arg->int_val;
        return true;
    }
    if (arg->type == VAL_FLOAT) {
        *out = arg->float_val;
        return true;
    }

    ti_raise_error(handle, TI_ERR_TYPE_MISMATCH, "%s: argument %d must be int or float, got %s",
                   name, index + 1, val_type_to_str(arg->type));
    return false;
}

/**
 * @brief Wrap a float result, refusing NaN and infinity so scripts never see them.
 * @param handle Calling runtime handle, used to report errors.
 * @param name Function name for the message.
 * @param result Computed result.
 * @return New float value, or NULL after raising an error.
 */
static value_t *finite_result(ti_handle_t handle, const char *name, float result)
{
    /* sqrt(-1), log(0), overflowing exp/pow and 0^-1 all end up here */
    if (!isfinite(result)) {
        ti_raise_error(handle, TI_ERR_VALUE_OUT_OF_RANGE, "%s: the result is not a finite number", name);
        return NULL;
    }

    value_t *value = val_new_float(result);
    if (value == NULL) {
        ti_raise_error(handle, TI_ERR_NO_MEMORY, "%s: cannot allocate the result", name);
    }
    return value;
}

/**
 * @brief Evaluate a one-argument float function on a number.
 * @param handle Calling runtime handle.
 * @param name Function name for messages.
 * @param argv Arguments (one number).
 * @param fn Function to apply.
 * @return Float value, or NULL after raising an error.
 */
static value_t *apply_unary(ti_handle_t handle, const char *name, value_t **argv, unary_fn_t fn)
{
    float x = 0.0f;
    if (!number_arg(handle, name, 0, argv[0], &x)) {
        return NULL;
    }
    return finite_result(handle, name, fn(x));
}

/**
 * @brief Evaluate a two-argument float function on two numbers.
 * @param handle Calling runtime handle.
 * @param name Function name for messages.
 * @param argv Arguments (two numbers).
 * @param fn Function to apply.
 * @return Float value, or NULL after raising an error.
 */
static value_t *apply_binary(ti_handle_t handle, const char *name, value_t **argv, binary_fn_t fn)
{
    float x = 0.0f;
    float y = 0.0f;
    if (!number_arg(handle, name, 0, argv[0], &x) || !number_arg(handle, name, 1, argv[1], &y)) {
        return NULL;
    }
    return finite_result(handle, name, fn(x, y));
}

/**
 * @brief Round a number to an int with the given rounding function.
 * @param handle Calling runtime handle.
 * @param name Function name for messages.
 * @param argv Arguments (one number).
 * @param fn Rounding function (floorf, ceilf, round_half_away).
 * @return Int value, or NULL after raising an error.
 */
static value_t *round_to_int(ti_handle_t handle, const char *name, value_t **argv, unary_fn_t fn)
{
    /* An int is already whole: return it as is instead of passing through float and losing digits */
    if (argv[0]->type == VAL_INT) {
        return val_new_int(argv[0]->int_val);
    }

    float x = 0.0f;
    if (!number_arg(handle, name, 0, argv[0], &x)) {
        return NULL;
    }

    float rounded = fn(x);
    if (!isfinite(rounded) || rounded < TI_MATH_INT_MIN_F || rounded >= TI_MATH_INT_END_F) {
        ti_raise_error(handle, TI_ERR_VALUE_OUT_OF_RANGE, "%s: the result does not fit an int", name);
        return NULL;
    }

    value_t *value = val_new_int((int)rounded);
    if (value == NULL) {
        ti_raise_error(handle, TI_ERR_NO_MEMORY, "%s: cannot allocate the result", name);
    }
    return value;
}

/**
 * @brief Check that both arguments are numbers of the same type (no silent int/float mixing).
 * @param handle Calling runtime handle.
 * @param name Function name for messages.
 * @param argv Arguments (two values).
 * @return true if both are int or both are float, false after raising TI_ERR_TYPE_MISMATCH.
 */
static bool same_number_type(ti_handle_t handle, const char *name, value_t **argv)
{
    float unused = 0.0f;
    if (!number_arg(handle, name, 0, argv[0], &unused) || !number_arg(handle, name, 1, argv[1], &unused)) {
        return false;
    }

    if (argv[0]->type != argv[1]->type) {
        ti_raise_error(handle, TI_ERR_TYPE_MISMATCH, "%s: arguments must have the same type, got %s and %s (use to_float or to_int)",
                       name, val_type_to_str(argv[0]->type), val_type_to_str(argv[1]->type));
        return false;
    }
    return true;
}

/**
 * @brief Round to the nearest whole number, halves away from zero (2.5 -> 3, -2.5 -> -3).
 * @param x Input.
 * @return Rounded value.
 */
static float round_half_away(float x)
{
    return roundf(x);
}

/* pow(x, y): x to the power y */
static value_t *builtin_pow(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    return apply_binary(handle, "pow", argv, powf);
}

/* sqrt(x): square root, an error for x < 0 */
static value_t *builtin_sqrt(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    return apply_unary(handle, "sqrt", argv, sqrtf);
}

/* exp(x): e to the power x */
static value_t *builtin_exp(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    return apply_unary(handle, "exp", argv, expf);
}

/* log(x): natural logarithm, an error for x <= 0 */
static value_t *builtin_log(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    return apply_unary(handle, "log", argv, logf);
}

/* log10(x): base-10 logarithm, an error for x <= 0 */
static value_t *builtin_log10(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    return apply_unary(handle, "log10", argv, log10f);
}

/* sin(x): sine, x in radians */
static value_t *builtin_sin(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    return apply_unary(handle, "sin", argv, sinf);
}

/* cos(x): cosine, x in radians */
static value_t *builtin_cos(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    return apply_unary(handle, "cos", argv, cosf);
}

/* tan(x): tangent, x in radians */
static value_t *builtin_tan(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    return apply_unary(handle, "tan", argv, tanf);
}

/* atan2(y, x): angle of the point (x, y) in radians; the first argument is y like in C */
static value_t *builtin_atan2(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    return apply_binary(handle, "atan2", argv, atan2f);
}

/* pi(): the constant pi */
static value_t *builtin_pi(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argv;
    (void)argc;
    return finite_result(handle, "pi", TI_MATH_PI);
}

/* floor(x): largest whole number <= x, as an int */
static value_t *builtin_floor(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    return round_to_int(handle, "floor", argv, floorf);
}

/* ceil(x): smallest whole number >= x, as an int */
static value_t *builtin_ceil(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    return round_to_int(handle, "ceil", argv, ceilf);
}

/* round(x): nearest whole number, halves away from zero, as an int */
static value_t *builtin_round(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    return round_to_int(handle, "round", argv, round_half_away);
}

/* abs(x): absolute value, same type as the argument */
static value_t *builtin_abs(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;

    float unused = 0.0f;
    if (!number_arg(handle, "abs", 0, argv[0], &unused)) {
        return NULL;
    }

    if (argv[0]->type == VAL_FLOAT) {
        return val_new_float(fabsf(argv[0]->float_val));
    }

    /* -INT_MIN does not fit an int */
    if (argv[0]->int_val == INT_MIN) {
        ti_raise_error(handle, TI_ERR_VALUE_OUT_OF_RANGE, "abs: the absolute value of %d does not fit an int", INT_MIN);
        return NULL;
    }
    return val_new_int(argv[0]->int_val < 0 ? -argv[0]->int_val : argv[0]->int_val);
}

/* min(x, y): smaller number; both arguments must have the same type */
static value_t *builtin_min(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    if (!same_number_type(handle, "min", argv)) {
        return NULL;
    }

    if (argv[0]->type == VAL_FLOAT) {
        return val_new_float(argv[1]->float_val < argv[0]->float_val ? argv[1]->float_val : argv[0]->float_val);
    }
    return val_new_int(argv[1]->int_val < argv[0]->int_val ? argv[1]->int_val : argv[0]->int_val);
}

/* max(x, y): larger number; both arguments must have the same type */
static value_t *builtin_max(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    if (!same_number_type(handle, "max", argv)) {
        return NULL;
    }

    if (argv[0]->type == VAL_FLOAT) {
        return val_new_float(argv[1]->float_val > argv[0]->float_val ? argv[1]->float_val : argv[0]->float_val);
    }
    return val_new_int(argv[1]->int_val > argv[0]->int_val ? argv[1]->int_val : argv[0]->int_val);
}

/* -------------------- Public Functions -------------------- */

/* Register the math functions */
void ti_runtime_builtin_math_register(void)
{
    ti_register_builtin_table(s_defs, (int)(sizeof(s_defs) / sizeof(s_defs[0])));
}
