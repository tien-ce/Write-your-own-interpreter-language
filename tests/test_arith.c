/*
 * Integer arithmetic: the remainder operator (%, %=).
 * A valid case checks its own result: a wrong result assigns a string to an int (TI_ERR_TYPE_MISMATCH).
 *
 * Build and run (from the repository root); AddressSanitizer reports any leak at exit:
 *   gcc -Isrc -Ilib/chashmap/include -fsanitize=address -g tests/test_arith.c \
 *       $(find src lib/chashmap/src -name '*.c') -lcjson -pthread -lm -o /tmp/test_arith
 *   /tmp/test_arith
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
        printf("FAIL: %-36s returned status %d, expected %d\n", name, (int)status, (int)want);
        return 1;
    }
    return 0;
}

int main(void)
{
    /* Scripts that run to the end */
    static const struct { const char *name; const char *script; } ok_cases[] = {
        { "remainder signs follow the left operand",
          "if (7 % 3 != 1 || -7 % 3 != -1 || 7 % -3 != 1 || -7 % -3 != -1 || 6 % 3 != 0) { int bad = \"x\"; }\n" },
        { "remainder precedence and associativity",
          "if (2 + 7 % 4 * 3 != 11 || 17 % 5 % 3 != 2 || (2 + 7) % 4 != 1) { int bad = \"x\"; }\n" },
        { "remainder by -1",
          "int min = -2147483647 - 1;\nif (min % -1 != 0 || 5 % -1 != 0 || min % 7 != -2) { int bad = \"x\"; }\n" },
        { "remainder in a loop",
          "int even = 0;\nfor (int i = 0; i < 10; i += 1) { if (i % 2 == 0) { even += 1; } }\n"
          "if (even != 5) { int bad = \"x\"; }\n" },
        { "remainder assignment",
          "int x = 17;\nx %= 5;\nlist int l = [10, 20];\nl[1] %= 6;\n"
          "if (x != 2 || l[1] != 2) { int bad = \"x\"; }\n" },
    };

    ti_register_log(on_log);

    int failed = 0;
    for (size_t i = 0; i < sizeof(ok_cases) / sizeof(ok_cases[0]); i++) {
        failed += run_case(ok_cases[i].name, ok_cases[i].script, TI_OK);
    }

    /* Errors */
    failed += run_case("remainder by zero",            "int a = 5 % 0;\n",                TI_ERR_DIV_ZERO);
    failed += run_case("remainder assignment by zero", "int a = 5;\na %= 0;\n",           TI_ERR_DIV_ZERO);
    failed += run_case("remainder of floats",          "float f = 5.0 % 2.0;\n",          TI_ERR_TYPE_MISMATCH);
    failed += run_case("remainder of strings",         "string s = \"a\" % \"b\";\n",     TI_ERR_TYPE_MISMATCH);
    failed += run_case("remainder of int and float",   "int a = 5 % 2.0;\n",              TI_ERR_TYPE_MISMATCH);
    failed += run_case("remainder without left side",  "int a = % 2;\n",                  TI_ERR_SYNTAX);

    printf("%s (%d failed)\n", failed ? "FAILED" : "ALL PASSED", failed);
    return failed ? 1 : 0;
}
