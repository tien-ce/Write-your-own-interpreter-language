/*
 * null: the literal, == / != with any type, definition and assignment rules (val_can_replace), null in
 * list elements and dict entries (and copies of them), natives that return null, and the errors.
 * A valid case checks its own result: a wrong result assigns a string to an int (TI_ERR_TYPE_MISMATCH).
 *
 * Build and run (from the repository root); AddressSanitizer reports any leak at exit:
 *   gcc -Isrc -Ilib/chashmap/include -fsanitize=address -g tests/test_null.c \
 *       $(find src lib/chashmap/src -name '*.c') -lcjson -pthread -lm -o /tmp/test_null
 *   /tmp/test_null
 */
#include "TienInterpreter.h"
#include <stdio.h>
#include <stdarg.h>

/* Silent log sink */
static void on_log(const char *fmt, va_list a)
{
    (void)fmt;
    (void)a;
}

/* Native declared int: returns its argument */
static value_t *native_read_ok(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle;
    (void)argc;
    return val_new_int(argv[0]->int_val);
}

/* Native declared int that failed: returns null instead of an int */
static value_t *native_read_fail(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle;
    (void)argv;
    (void)argc;
    return val_new_null();
}

/* Native declared void: returns void, never null */
static value_t *native_do_nothing(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle;
    (void)argv;
    (void)argc;
    return val_new_void();
}

/* Run one script and report whether it ended with the expected status */
static int run_case(const char *name, const char *script, ti_status_t want)
{
    ti_status_t status = ti_run_string(script, NULL);
    if (status != want) {
        printf("FAIL: %-36s returned status %d, expected %d\n", name, (int)status, (int)want);
        return 1;
    }
    return 0;
}

int main(void)
{
    /* Scripts that run to the end */
    static const struct { const char *name; const char *script; } ok_cases[] = {
        { "literal compares",
          "if (!(null == null) || null != null) { int bad = \"x\"; }\n"
          "if (to_string(null) != \"null\") { int bad = \"x\"; }\n" },
        { "compare with every type",
          "int i = 5;\nstring s = \"a\";\ndict d = {};\nlist int l = [1];\nhandle h;\nbool b = false;\n"
          "if (i == null || null == s || d == null || l == null || h == null || b == null) { int bad = \"x\"; }\n"
          "if (!(i != null) || !(null != s)) { int bad = \"x\"; }\n" },
        { "define as null",
          "int a = null;\nstring s = null;\ndict d = null;\nlist int l = null;\n"
          "if (a != null || s != null || d != null || l != null) { int bad = \"x\"; }\n" },
        { "assign null over a value",
          "int b;\nb = null;\nstring s = \"hello\";\ns = null;\ndict d = {\"k\": \"v\"};\nd = null;\n"
          "if (b != null || s != null || d != null) { int bad = \"x\"; }\n" },
        { "native returns null",
          "int t = read_fail();\nif (t != null) { int bad = \"x\"; }\n"
          "if (read_fail() != null) { int bad = \"x\"; }\n" },
        { "native returns a value",
          "int v = read_ok(3);\nif (v == null || v != 3) { int bad = \"x\"; }\n" },
        { "list element set to null",
          "list int l = [1, 2, 3];\nl[0] = null;\nl[2] = 30;\n"
          "if (to_string(l) != \"[null,2,30]\" || l[0] != null || l[1] != 2) { int bad = \"x\"; }\n" },
        { "dict entry set to null",
          "dict d = {\"x\": 1};\nd[\"x\"] = null;\nd[\"y\"] = null;\n"
          "if (d[\"x\"] != null || d[\"y\"] != null) { int bad = \"x\"; }\n"
          "dict one = {\"k\": 1};\none[\"k\"] = null;\nif (to_string(one) != \"{\\\"k\\\":null}\") { int bad = \"x\"; }\n" },
        { "copy a list holding null",
          "list int l = [1, 2];\nl[0] = null;\nlist list n = [l];\ndict h = {\"l\": l};\n"
          "if (to_string(n) != \"[[null,2]]\" || to_string(h) != \"{\\\"l\\\":[null,2]}\") { int bad = \"x\"; }\n" },
    };

    /* Scripts that must stop with TI_ERR_TYPE_MISMATCH */
    static const struct { const char *name; const char *script; } mismatch_cases[] = {
        { "assign to a null variable",       "int a = null;\na = 5;\n" },
        { "assign null to a null variable",  "int a = null;\na = null;\n" },
        { "assign another type",             "int a = 5;\na = \"x\";\n" },
        { "set a null list element",         "list int l = [1];\nl[0] = null;\nl[0] = 2;\n" },
        { "set a null dict entry",           "dict d = {\"x\": 1};\nd[\"x\"] = null;\nd[\"x\"] = 2;\n" },
        { "literal starts with null",        "to_string([null, 1]);\n" },
        { "literal holds null",              "list int l = [1, null];\n" },
        { "compound assign on null",         "int a = null;\na += 1;\n" },
        { "add null",                        "int a = 5 + null;\n" },
        { "order nulls",                     "bool b = null < null;\n" },
        { "logic with null",                 "bool b = null && true;\n" },
        { "not null",                        "bool b = !null;\n" },
        { "negate null",                     "int a = -null;\n" },
        { "subscript a null variable",       "list int l = null;\nl[0] = 1;\n" },
        { "void native result",              "int x = do_nothing();\n" },
    };

    static const param_t read_params[] = { PARAM_INT("x") };
    ti_register_log(on_log);
    register_builtin_function("read_ok", VAL_INT, read_params, 1, native_read_ok);
    register_builtin_function("read_fail", VAL_INT, NULL, 0, native_read_fail);
    register_builtin_function("do_nothing", VAL_VOID, NULL, 0, native_do_nothing);

    int failed = 0;
    for (size_t i = 0; i < sizeof(ok_cases) / sizeof(ok_cases[0]); i++) {
        failed += run_case(ok_cases[i].name, ok_cases[i].script, TI_OK);
    }
    for (size_t i = 0; i < sizeof(mismatch_cases) / sizeof(mismatch_cases[0]); i++) {
        failed += run_case(mismatch_cases[i].name, mismatch_cases[i].script, TI_ERR_TYPE_MISMATCH);
    }

    /* null is a keyword, not a name */
    failed += run_case("null as a variable name", "int null = 1;\n", TI_ERR_SYNTAX);

    printf("%s (%d failed)\n", failed ? "FAILED" : "ALL PASSED", failed);
    return failed ? 1 : 0;
}
