/*
 * Every lexer/parser error must (1) make ti_run_string return TI_ERR_SYNTAX and (2) leak nothing.
 * A few valid-syntax scripts that fail at run time check the other side: they are not syntax errors.
 *
 * Build and run (from the repository root); AddressSanitizer reports any leak at exit:
 *   gcc -Isrc -Ilib/chashmap/include -fsanitize=address -g tests/test_parser_errors.c \
 *       $(find src lib/chashmap/src -name '*.c') -lcjson -pthread -lm -o /tmp/parser_errors
 *   /tmp/parser_errors
 */
#include "TienInterpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
        printf("FAIL: %-28s returned status %d, expected %d\n", name, (int)status, (int)want);
        return 1;
    }
    return 0;
}

/* Build "prefix" + repeat * unit + "suffix" for the size-limit cases */
static char *make_repeat(const char *prefix, const char *unit, int repeat, const char *suffix)
{
    size_t len = strlen(prefix) + strlen(unit) * (size_t)repeat + strlen(suffix) + 1;
    char *buf = malloc(len);
    if (!buf) {
        exit(2);
    }
    strcpy(buf, prefix);
    for (int i = 0; i < repeat; i++) {
        strcat(buf, unit);
    }
    strcat(buf, suffix);
    return buf;
}

int main(void)
{
    static const struct { const char *name; const char *script; } cases[] = {
        /* Lexer */
        { "missing close quote",       "string s = \"abc;\n" },
        { "bytes bad character",       "bytes b = x\"GG\";\n" },
        { "bytes missing close quote", "bytes b = x\"41 42;\n" },
        { "bytes odd digit count",     "bytes b = x\"414\";\n" },
        { "hex too wide",              "int a = 0x1FFFFFFFF;\n" },
        { "hex bad suffix",            "int a = 0x1Z;\n" },
        { "float bad suffix",          "float f = 1.5abc;\n" },
        { "int bad suffix",            "int a = 12abc;\n" },
        { "unexpected character",      "int a = 1 @ 2;\n" },
        /* Parser */
        { "missing identifier",        "int = 5;\n" },
        { "missing expression",        "int a = ;\n" },
        { "void parameter",            "void f(void x) { }\n" },
        { "dict key not string",       "dict d = {1: 2};\n" },
        { "dict missing colon",        "dict d = {\"a\" 1};\n" },
        { "dict missing comma",        "dict d = {\"a\": 1 \"b\": 2};\n" },
        { "list missing comma",        "list int l = [1 2];\n" },
        { "unclosed block",            "int a = 1;\nif (a == 1) {\n" },
        { "unclosed call",             "print(1;\n" },
        { "for init invalid",          "for (print(1); 1 < 2; ) { }\n" },
        { "for step invalid",          "for (int i = 0; i < 2; print(i)) { }\n" },
        { "stray closing paren",       "int a = 1;\n)\n" },
        { "method missing name",       "handle d;\nd.;\n" },
        { "method name is number",     "handle d;\nd.5;\n" },
        { "method unclosed call",      "handle d;\nd.read(1;\n" },
        { "method on chain missing",   "handle d;\nd.read().;\n" },
        { "sized non-list",            "int x[3];\n" },
        { "sized list with init",      "list int l[3] = [1, 2, 3];\n" },
        { "sized list unclosed",       "list int l[3;\n" },
    };

    /* Valid syntax that fails at run time: a nested list literal and attribute reads (obj.name) */
    static const struct { const char *name; const char *script; } runtime_cases[] = {
        { "nested list",               "list int l = [[1]];\n" },
        { "attribute on empty handle", "handle d;\nd.baud;\n" },
        { "attribute in expression",   "handle d;\nint b = d.baud + 1;\n" },
        { "sized list size not int",   "list int l[\"3\"];\n" },
        { "sized list of lists",       "list list l[2];\n" },
    };

    /* Sized list declarations that run: a wrong content assigns a string to an int (TI_ERR_TYPE_MISMATCH) */
    static const struct { const char *name; const char *script; } sized_ok_cases[] = {
        { "sized list of dicts",       "int n = 3;\nlist dict s[n];\n"
                                       "if (to_string(s) != \"[{},{},{}]\") { int bad = \"x\"; }\n" },
        { "sized list defaults",       "list int i[2];\nlist string t[1];\nlist bool b[1];\n"
                                       "if (to_string(i) != \"[0,0]\" || to_string(t) != \"[\\\"\\\"]\" || to_string(b) != \"[false]\") { int bad = \"x\"; }\n" },
        { "sized list empty",          "list float f[0];\nif (to_string(f) != \"[]\") { int bad = \"x\"; }\n" },
        { "sized list in loop",        "int c = 0;\nwhile (c < 3) {\n  list dict w[2];\n  dict d = w[0];\n"
                                       "  if (to_string(w) != \"[{},{}]\") { int bad = \"x\"; }\n  d[\"c\"] = c;\n  c = c + 1;\n}\n" },
        { "sized list in for init",    "for (list int l[4]; false; ) { }\n" },
    };

    ti_register_log(on_log);

    int failed = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        failed += run_case(cases[i].name, cases[i].script, TI_ERR_SYNTAX);
    }
    for (size_t i = 0; i < sizeof(runtime_cases) / sizeof(runtime_cases[0]); i++) {
        failed += run_case(runtime_cases[i].name, runtime_cases[i].script, TI_ERR_TYPE_MISMATCH);
    }
    for (size_t i = 0; i < sizeof(sized_ok_cases) / sizeof(sized_ok_cases[0]); i++) {
        failed += run_case(sized_ok_cases[i].name, sized_ok_cases[i].script, TI_OK);
    }
    failed += run_case("sized list negative", "list int l[-1];\n", TI_ERR_VALUE_OUT_OF_RANGE);
    failed += run_case("sized list too large", "list int l[1025];\n", TI_ERR_VALUE_OUT_OF_RANGE);
    failed += run_case("sized list at limit", "list int l[1024];\n", TI_OK);

    /* Size limits are only reachable with a long literal */
    char *long_bytes = make_repeat("bytes b = x\"", "41", 5000, "\";\n");
    char *long_list = make_repeat("list int l = [", "1,", 5000, "1];\n");
    failed += run_case("bytes literal too long", long_bytes, TI_ERR_SYNTAX);
    failed += run_case("list literal too long", long_list, TI_ERR_SYNTAX);
    free(long_bytes);
    free(long_list);

    printf("%s (%d failed)\n", failed ? "FAILED" : "ALL PASSED", failed);
    return failed ? 1 : 0;
}
