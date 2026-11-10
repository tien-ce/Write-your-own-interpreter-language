/*
 * Modules: the C registry API (register, find module / function / constant) and the script side
 * (import, import ... as, module.f(args), module.CONST, read-only constants, scope of an import,
 * a module function that returns a handle, == / to_string on modules, every error message).
 * The test module's tables are all static const, parameter tables included.
 *
 * Build and run (from the repository root); AddressSanitizer reports any leak at exit:
 *   gcc -Isrc -Ilib/chashmap/include -fsanitize=address -g -Wall -Wextra tests/test_module.c \
 *       $(find src lib/chashmap/src -name '*.c') -lcjson -pthread -lm -o /tmp/test_module
 *   /tmp/test_module
 */
#include "TienInterpreter.h"
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); g_fails++; } } while (0)

static int g_fails;
static int g_destroyed;   /* Number of native objects destroyed */
static int g_result;      /* Value reported by mark (-1 = not reported) */
static char g_log[4096];  /* Everything the interpreter logged during one script */
static jmp_buf g_env;

/* -------------------- Host Callbacks -------------------- */

/* Collect log output so a case can check the error message */
static void on_log(const char *fmt, va_list args)
{
    size_t used = strlen(g_log);
    vsnprintf(g_log + used, sizeof(g_log) - used, fmt, args);
}

/* Fatal handler that never returns (build errors) */
static void on_fatal(void)
{
    longjmp(g_env, 1);
}

/* -------------------- Test Natives -------------------- */

static const ti_handle_type_t s_box_type;

/* Destructor of a box: counts calls so leaks and double destroys show up */
static void box_destroy(void *ptr)
{
    g_destroyed++;
    free(ptr);
}

/* tmod.add(int a, int b): a + b */
static value_t *tmod_add(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle; (void)argc;
    return val_new_int(argv[0]->int_val + argv[1]->int_val);
}

/* tmod.mark(bool ok): lets a script report a result (1 / 0) */
static value_t *tmod_mark(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle; (void)argc;
    g_result = argv[0]->bool_val ? 1 : 0;
    return val_new_void();
}

/* tmod.fail(): always raises, to test a native error through a module call */
static value_t *tmod_fail(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argv; (void)argc;
    ti_raise_error(handle, TI_ERR_INVALID_VALUE, "tmod.fail: always fails");
    return NULL;
}

/* tmod.box(int n): a "box" handle wrapping n */
static value_t *tmod_box(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle; (void)argc;
    int *n = malloc(sizeof(int));
    *n = argv[0]->int_val;
    value_t *result = val_new_handle(n, &s_box_type);
    if (result == NULL) {
        free(n);
    }
    return result;
}

/* b.get(): the wrapped int */
static value_t *box_get(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    int *n = val_handle_get(argv[0], &s_box_type);
    if (n == NULL) {
        ti_raise_error(handle, TI_ERR_INVALID_VALUE, "box.get: closed");
        return NULL;
    }
    return val_new_int(*n);
}

/* -------------------- Test Module Tables (all static const) -------------------- */

static const param_t s_p_add[]  = { PARAM("a", VAL_INT), PARAM("b", VAL_INT) };
static const param_t s_p_mark[] = { PARAM("ok", VAL_BOOL) };
static const param_t s_p_box[]  = { PARAM("n", VAL_INT) };
static const param_t s_p_self[] = { PARAM_HANDLE("self", &s_box_type) };

static const ti_builtin_def_t s_box_methods[] = {
    { "get", VAL_INT, s_p_self, 1, box_get },
};

static const ti_handle_type_t s_box_type = {
    .name         = "box",
    .destroy      = box_destroy,
    .methods      = s_box_methods,
    .method_count = 1,
};

static const ti_builtin_def_t s_tmod_funcs[] = {
    { "add",  VAL_INT,    s_p_add,  2, tmod_add },
    { "mark", VAL_VOID,   s_p_mark, 1, tmod_mark },
    { "fail", VAL_VOID,   NULL,     0, tmod_fail },
    { "box",  VAL_HANDLE, s_p_box,  1, tmod_box },
};

/* One constant of each scalar type; the string points to a literal (read-only memory) */
static const ti_module_const_t s_tmod_consts[] = {
    { "N",    { .type = VAL_INT,    .int_val = 7 } },
    { "HALF", { .type = VAL_FLOAT,  .float_val = 0.5f } },
    { "ON",   { .type = VAL_BOOL,   .bool_val = true } },
    { "NAME", { .type = VAL_STRING, .string_val = (char *)"tm" } },
};

static const ti_module_t s_tmod = {
    .name = "tmod", .funcs = s_tmod_funcs, .func_count = 4, .consts = s_tmod_consts, .const_count = 4,
};

/* A module with no functions and no constants: import works, every lookup misses */
static const ti_module_t s_empty_mod = { .name = "empty" };

/* Rejected or extra descriptors for the registry cases */
static const ti_module_t s_dup_tmod  = { .name = "tmod" };
static const ti_module_t s_no_name   = { .name = NULL };
static const ti_module_t s_empty_name = { .name = "" };
static ti_module_t s_fillers[TI_MAX_MODULES];
static char s_filler_names[TI_MAX_MODULES][16];

/* -------------------- C API Cases -------------------- */

/* Register rules, table full, and every find function */
static void test_c_api(void)
{
    /* Valid modules register once; invalid or duplicate names are refused */
    CHECK(ti_register_module(&s_tmod));
    CHECK(ti_register_module(&s_empty_mod));
    CHECK(!ti_register_module(NULL));
    CHECK(!ti_register_module(&s_no_name));
    CHECK(!ti_register_module(&s_empty_name));
    CHECK(!ti_register_module(&s_dup_tmod));
    CHECK(!ti_register_module(&s_tmod));

    /* Fill the registry: the slot after the last one is refused */
    int registered = 2;
    for (int i = 0; registered < TI_MAX_MODULES; i++, registered++) {
        snprintf(s_filler_names[i], sizeof(s_filler_names[i]), "filler%d", i);
        s_fillers[i].name = s_filler_names[i];
        CHECK(ti_register_module(&s_fillers[i]));
    }
    static const ti_module_t s_one_too_many = { .name = "one_too_many" };
    CHECK(!ti_register_module(&s_one_too_many));
    CHECK(ti_module_find("one_too_many") == NULL);

    /* Module lookup returns the registered address itself */
    CHECK(ti_module_find("tmod") == &s_tmod);
    CHECK(ti_module_find("empty") == &s_empty_mod);
    CHECK(ti_module_find("filler0") == &s_fillers[0]);
    CHECK(ti_module_find("missing") == NULL);
    CHECK(ti_module_find(NULL) == NULL);

    /* Function lookup returns the row inside the module's table */
    CHECK(ti_module_find_func(&s_tmod, "add") == &s_tmod_funcs[0]);
    CHECK(ti_module_find_func(&s_tmod, "box") == &s_tmod_funcs[3]);
    CHECK(ti_module_find_func(&s_tmod, "N") == NULL);
    CHECK(ti_module_find_func(&s_empty_mod, "add") == NULL);
    CHECK(ti_module_find_func(NULL, "add") == NULL);
    CHECK(ti_module_find_func(&s_tmod, NULL) == NULL);

    /* Constant lookup returns the row; its value is the static initializer */
    const ti_module_const_t *n = ti_module_find_const(&s_tmod, "N");
    CHECK(n == &s_tmod_consts[0] && n->value.type == VAL_INT && n->value.int_val == 7);
    CHECK(ti_module_find_const(&s_tmod, "add") == NULL);
    CHECK(ti_module_find_const(&s_empty_mod, "N") == NULL);
    CHECK(ti_module_find_const(NULL, "N") == NULL);
    CHECK(ti_module_find_const(&s_tmod, NULL) == NULL);

    /* A constant read is a copy: freeing it leaves the table (and the string literal) untouched */
    value_t *copy = val_copy(&ti_module_find_const(&s_tmod, "NAME")->value);
    CHECK(copy != NULL && copy->string_val != s_tmod_consts[3].value.string_val);
    CHECK(strcmp(copy->string_val, "tm") == 0);
    val_free(copy);
    CHECK(strcmp(s_tmod_consts[3].value.string_val, "tm") == 0);
}

/* -------------------- Script Cases -------------------- */

/**
 * @brief Run one script and compare the reported value, the destroy count and the log.
 * @param name Case name printed on failure.
 * @param src Script source.
 * @param want_result Expected g_result (-1 = nothing reported).
 * @param want_destroyed Expected number of destroyed objects.
 * @param want_log Text the log must contain, or NULL for an empty log.
 */
static void run(const char *name, const char *src, int want_result, int want_destroyed, const char *want_log)
{
    g_destroyed = 0;
    g_result = -1;
    g_log[0] = '\0';
    if (setjmp(g_env) == 0) {
        ti_run_string(src);
    }

    bool log_ok = (want_log != NULL) ? strstr(g_log, want_log) != NULL : g_log[0] == '\0';
    if (g_result != want_result || g_destroyed != want_destroyed || !log_ok) {
        printf("FAIL %-24s result=%d destroyed=%d log=%s\n", name, g_result, g_destroyed, g_log);
        g_fails++;
    }
}

/* Import, calls, constants, scope, handles and every error message */
static void test_scripts(void)
{
    /* Import and alias */
    run("import and call", "import tmod; tmod.mark(tmod.add(2, 3) == 5);", 1, 0, NULL);
    run("alias", "import tmod as t; t.mark(t.add(1, 1) == 2);", 1, 0, NULL);
    run("alias == module", "import tmod; import tmod as t; t.mark(t == tmod);", 1, 0, NULL);
    run("!= other module", "import tmod; import empty; tmod.mark(tmod != empty);", 1, 0, NULL);
    run("to_string", "import tmod; tmod.mark(to_string(tmod) == \"<module tmod>\");", 1, 0, NULL);
    run("module in variable", "import tmod; int v = tmod.add(tmod.N, 1); tmod.mark(v == 8);", 1, 0, NULL);
    run("empty module import", "import empty; import tmod; tmod.mark(true);", 1, 0, NULL);

    /* Constants of every type are plain values once read */
    run("const int", "import tmod; tmod.mark(tmod.N == 7);", 1, 0, NULL);
    run("const float", "import tmod; tmod.mark(tmod.HALF * 2.0 == 1.0);", 1, 0, NULL);
    run("const bool", "import tmod; tmod.mark(tmod.ON);", 1, 0, NULL);
    run("const string", "import tmod; tmod.mark(tmod.NAME + \"!\" == \"tm!\");", 1, 0, NULL);
    run("const copy edited", "import tmod; string s = tmod.NAME; s = s + \"x\"; tmod.mark(tmod.NAME == \"tm\" && s == \"tmx\");", 1, 0, NULL);
    run("const in variable", "import tmod; int n = tmod.N; n += 1; tmod.mark(n == 8 && tmod.N == 7);", 1, 0, NULL);

    /* Scope: a top-level import is visible in functions; an inner import stays local */
    run("global import in fn", "import tmod; int f() { return tmod.add(1, 2); }\ntmod.mark(f() == 3);", 1, 0, NULL);
    run("import inside fn", "bool f() { import tmod as t; return t.N == 7; }\nimport tmod; tmod.mark(f());", 1, 0, NULL);
    run("fn import is local", "void f() { import tmod as t; }\nf(); t.N;", -1, 0, "Undefined variable 't'");
    run("block import is local", "if (true) { import tmod as t; } t.N;", -1, 0, "Undefined variable 't'");
    run("import fn twice", "int f() { import tmod as t; return t.N; }\nimport tmod; tmod.mark(f() + f() == 14);", 1, 0, NULL);

    /* A module function can create a handle; its methods work as usual */
    run("handle from module", "import tmod; handle b = tmod.box(9); tmod.mark(b.get() == 9);", 1, 1, NULL);
    run("temporary handle", "import tmod; tmod.mark(tmod.box(4).get() == 4);", 1, 1, NULL);
    run("handle in fn arg", "import tmod; int f(handle h) { return h.get(); }\ntmod.mark(f(tmod.box(3)) == 3);", 1, 1, NULL);

    /* Errors: import */
    run("unknown module", "import nope;", -1, 0, "Unknown module 'nope'");
    run("import twice", "import tmod; import tmod;", -1, 0, "Cannot import 'tmod': already defined in this scope");
    run("alias clashes var", "int t = 1; import tmod as t;", -1, 0, "Cannot import 't': already defined in this scope");
    run("missing name", "import ;", -1, 0, "Expected module name after 'import'");
    run("missing alias", "import tmod as ;", -1, 0, "Expected name after 'as'");

    /* Errors: functions and constants */
    run("no function", "import tmod; tmod.nope();", -1, 0, "Module tmod has no function 'nope'");
    run("const is not fn", "import tmod; tmod.N();", -1, 0, "Module tmod has no function 'N'");
    run("no constant", "import tmod; int x = tmod.NOPE;", -1, 0, "Module tmod has no constant 'NOPE'");
    run("fn is not const", "import tmod; int x = tmod.add;", -1, 0, "Module tmod has no constant 'add'");
    run("empty module fn", "import empty; empty.add(1, 2);", -1, 0, "Module empty has no function 'add'");
    run("arg count", "import tmod; tmod.add(1);", -1, 0, "Function 'add' expects 2 argument(s), but received 1");
    run("arg type", "import tmod; tmod.add(\"x\", 1);", -1, 0, "parameter 1 ('a') expected type int, but got string");
    run("native raises", "import tmod; tmod.fail();", -1, 0, "tmod.fail: always fails");
    run("const read-only", "import tmod; tmod.N = 5;", -1, 0, "Cannot set 'N' of module tmod: read-only");
    run("const += read-only", "import tmod; tmod.N += 1;", -1, 0, "Cannot set 'N' of module tmod: read-only");
    run("module into int", "import tmod; int x = tmod;", -1, 0, "Expected int, but got module");
    run("module not imported", "tmod.add(1, 2);", -1, 0, "Undefined variable 'tmod'");
}

/* The "missing semicolon" case must log something: checked separately since run() takes NULL as empty */
static void test_build_error_logged(void)
{
    g_log[0] = '\0';
    if (setjmp(g_env) == 0) {
        ti_run_string("import tmod");
    }
    CHECK(g_log[0] != '\0');
}

int main(void)
{
    ti_register_log(on_log);
    ti_register_fatal(on_fatal);

    test_c_api();
    test_scripts();
    test_build_error_logged();

    printf(g_fails ? "FAILED (%d)\n" : "ALL PASSED\n", g_fails);
    return g_fails != 0;
}
