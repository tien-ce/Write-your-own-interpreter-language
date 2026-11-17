/*
 * Leak check for the one-step runners: ti_run_string and ti_run_string_take must return the right
 * status and leave nothing allocated, whether the script succeeds, fails to compile or fails at run time.
 * Each case runs 5 times through each runner; under AddressSanitizer LeakSanitizer must report nothing.
 *
 * Build and run (from the repository root):
 *   gcc -Isrc -Ilib/chashmap/include -fsanitize=address -g tests/test_fatal_leak.c \
 *       $(find src lib/chashmap/src -name '*.c') -lcjson -pthread -lm -o /tmp/fatal_leak
 *   /tmp/fatal_leak
 */
#include "TienInterpreter.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Silent log sink */
static void on_log(const char *fmt, va_list a)
{
    (void)fmt;
    (void)a;
}

/**
 * @brief Copy a script into a ti_raw_malloc'd buffer, as ti_run_string_take requires.
 * @param src Script text.
 * @return The copy (the caller hands it to ti_run_string_take, which frees it with ti_raw_free).
 */
static char *dup_source(const char *src)
{
    char *buf = ti_raw_malloc(strlen(src) + 1);
    if (buf == NULL) {
        exit(2);
    }
    strcpy(buf, src);
    return buf;
}

int main(void)
{
    static const struct { const char *name; const char *script; ti_status_t want; } cases[] = {
        { "success",                "int a = 1;\nlist int l = [1, 2];\ndict d = {\"x\": 1};\n",  TI_OK },
        { "syntax error",           "int a = 1;\nlist int l = [1, 2];\nint b = ;\n",                 TI_ERR_SYNTAX },
        { "runtime error",          "int a = 1;\nundefined_fn();\n",                                 TI_ERR_UNDEFINED },
        { "runtime error with vars","int a = 1;\nstring s = \"hello\" ;\nint b = s + 1;\n",          TI_ERR_TYPE_MISMATCH },
        { "error inside function",  "int f(int x) { list int l = [x]; return y; }\nint r = f(1);\n", TI_ERR_UNDEFINED },
    };

    ti_register_log(on_log);

    int failed = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        for (int round = 0; round < 5; round++) {
            /* Borrowed source: the caller keeps it */
            ti_status_t status = ti_run_string(cases[i].script);
            if (status != cases[i].want) {
                printf("FAIL: %-24s ti_run_string returned status %d\n", cases[i].name, (int)status);
                failed++;
            }

            /* Owned source: freed by the runner on every path */
            status = ti_run_string_take(dup_source(cases[i].script), "case.ti");
            if (status != cases[i].want) {
                printf("FAIL: %-24s ti_run_string_take returned status %d\n", cases[i].name, (int)status);
                failed++;
            }
        }
    }

    /* NULL source is rejected without touching anything */
    if (ti_run_string(NULL) != TI_ERR_INVALID_ARG || ti_run_string_take(NULL, NULL) != TI_ERR_INVALID_ARG) {
        printf("FAIL: NULL source\n");
        failed++;
    }

    printf("%s (%d failed)\n", failed ? "FAILED" : "ALL PASSED", failed);
    return failed ? 1 : 0;
}
