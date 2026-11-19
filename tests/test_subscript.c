/*
 * Subscripts on any expression: nested reads (s[0]["k"], d["rows"][1][0], f()[i]) and nested writes
 * (s[0]["k"] = v), plus the copy rules they rely on:
 *   - reading a list, dict or bytes (a variable, s[i], d["k"]) shares the container, so a write
 *     through it changes the original;
 *   - storing a list or dict into a container (s[i] = v, d["k"] = v, a literal) copies it, so later
 *     changes to v do not reach the stored copy;
 *   - scalars and strings are always copied.
 * A valid case checks its own result: a wrong result assigns a string to an int (TI_ERR_TYPE_MISMATCH).
 *
 * Build and run (from the repository root); AddressSanitizer reports any leak at exit:
 *   gcc -Isrc -Ilib/chashmap/include -fsanitize=address -g tests/test_subscript.c \
 *       $(find src lib/chashmap/src -name '*.c') -lcjson -pthread -lm -o /tmp/test_subscript
 *   /tmp/test_subscript
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

/* Run one script and report whether it ended with the expected status */
static int run_case(const char *name, const char *script, ti_status_t want)
{
    ti_status_t status = ti_run_string(script, NULL);
    if (status != want) {
        printf("FAIL: %-40s returned status %d, expected %d\n", name, (int)status, (int)want);
        return 1;
    }
    return 0;
}

int main(void)
{
    /* Scripts that run to the end */
    static const struct { const char *name; const char *script; } ok_cases[] = {
        /* Nested reads */
        { "read a dict inside a list",
          "list dict s = [{\"a\": 1, \"l\": [10, 20, 30]}, {\"a\": 2}];\n"
          "if (s[0][\"a\"] != 1 || s[1][\"a\"] != 2 || s[0][\"l\"][2] != 30) { int bad = \"x\"; }\n" },
        { "read three levels",
          "dict d = {\"rows\": [[1, 2], [3, 4]]};\nint i = 1;\n"
          "if (d[\"rows\"][1][0] != 3 || d[\"rows\"][i][i - 1] + d[\"rows\"][0][1] != 5) { int bad = \"x\"; }\n" },
        { "read bytes inside a dict",
          "dict h = {\"b\": x\"0A0B\"};\nif (h[\"b\"][1] != 11) { int bad = \"x\"; }\n" },
        { "read the result of a call",
          "list int pair() { return [7, 8]; }\nif (pair()[1] != 8) { int bad = \"x\"; }\n" },
        { "read with a variable key",
          "list dict s = [{\"a\": 1}];\nstring k = \"a\";\nif (s[0][k] != 1) { int bad = \"x\"; }\n" },

        /* Nested writes change the container held by the outer one */
        { "write a dict inside a list",
          "list dict s = [{\"a\": 1}, {\"a\": 2}];\ns[0][\"a\"] = 5;\ns[1][\"new\"] = 7;\n"
          "if (s[0][\"a\"] != 5 || s[1][\"new\"] != 7 || s[1][\"a\"] != 2) { int bad = \"x\"; }\n" },
        { "write three levels",
          "dict d = {\"rows\": [[1, 2], [3, 4]]};\nd[\"rows\"][1][0] = 30;\n"
          "if (to_string(d) != \"{\\\"rows\\\":[[1,2],[30,4]]}\") { int bad = \"x\"; }\n" },
        { "write bytes inside a dict",
          "dict h = {\"b\": x\"0A0B\"};\nh[\"b\"][1] = 255;\nif (h[\"b\"][1] != 255) { int bad = \"x\"; }\n" },
        { "compound write nested",
          "list dict s = [{\"n\": 1}];\ns[0][\"n\"] += 10;\ns[0][\"n\"] *= 2;\nif (s[0][\"n\"] != 22) { int bad = \"x\"; }\n" },
        { "one-level write is unchanged",
          "list int nums = [5, 6];\nnums[1] = 60;\ndict d = {};\nd[\"k\"] = 1;\n"
          "if (nums[1] != 60 || d[\"k\"] != 1) { int bad = \"x\"; }\n" },
        { "write into a call result is allowed",
          "list int pair() { return [7, 8]; }\npair()[0] = 1;\n" },

        /* Reading a container shares it */
        { "variable from an element shares it",
          "list dict s = [{\"a\": 1}];\ndict d = s[0];\nd[\"a\"] = 5;\nif (s[0][\"a\"] != 5) { int bad = \"x\"; }\n" },
        { "variable from a variable shares it",
          "list int a = [1, 2];\nlist int b = a;\nb[0] = 9;\nif (a[0] != 9) { int bad = \"x\"; }\n" },
        { "scalar element is copied",
          "list int l = [1];\nint x = l[0];\nx = 5;\nif (l[0] != 1) { int bad = \"x\"; }\n" },

        /* Storing a container copies it */
        { "stored dict is a copy",
          "dict b = {};\nlist dict s = [{}];\ns[0] = b;\nb[\"k\"] = 1;\nif (to_string(s) != \"[{}]\") { int bad = \"x\"; }\n" },
        { "nested stored dict is a copy",
          "dict b = {\"v\": 1};\nlist dict s = [{}];\ns[0][\"inner\"] = b;\nb[\"v\"] = 2;\n"
          "if (s[0][\"inner\"][\"v\"] != 1) { int bad = \"x\"; }\n" },
        { "literal element is a copy",
          "list int l = [1];\nlist list outer = [l];\nl[0] = 9;\nif (outer[0][0] != 1) { int bad = \"x\"; }\n" },
    };

    ti_register_log(on_log);

    int failed = 0;
    for (size_t i = 0; i < sizeof(ok_cases) / sizeof(ok_cases[0]); i++) {
        failed += run_case(ok_cases[i].name, ok_cases[i].script, TI_OK);
    }

    /* Errors */
    failed += run_case("subscript an int",          "list int l = [1];\nint x = l[0][0];\n",                   TI_ERR_TYPE_MISMATCH);
    failed += run_case("write into an int element", "list dict s = [{\"a\": 1}];\ns[0][\"a\"][\"b\"] = 2;\n", TI_ERR_TYPE_MISMATCH);
    failed += run_case("nested key not a string",   "list dict s = [{\"a\": 1}];\nint x = s[0][1];\n",          TI_ERR_TYPE_MISMATCH);
    failed += run_case("nested index out of range", "list dict s = [{\"a\": 1}];\ns[3][\"a\"] = 2;\n",          TI_ERR_INDEX_OUT_OF_RANGE);
    failed += run_case("nested missing key",        "list dict s = [{\"a\": 1}];\nint x = s[0][\"zz\"];\n",     TI_ERR_KEY_NOT_FOUND);
    failed += run_case("nested element type kept",  "list dict s = [{\"a\": 1}];\ns[0][\"a\"] = \"x\";\n",      TI_ERR_TYPE_MISMATCH);
    failed += run_case("undefined container",       "zz[0] = 1;\n",                                            TI_ERR_UNDEFINED);

    printf("%s (%d failed)\n", failed ? "FAILED" : "ALL PASSED", failed);
    return failed ? 1 : 0;
}
