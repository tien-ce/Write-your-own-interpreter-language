/*
 * Script modules: 'import <name>;' of a name no native module has goes through the host's module loader.
 * Covers calls into a module (its functions see its own globals and functions), reading its variables,
 * namespaces that keep the same names apart, a top level that runs once per runtime, the importer's path
 * handed to the loader (the main script's, then a module's for the imports inside it), the file path as
 * the module's identity (two names of one file are one module), path names (import ../lib/a;), circular
 * imports, handles kept in module variables, and every error message. The loader serves sources from an
 * in-memory table.
 *
 * Build and run (from the repository root); AddressSanitizer reports any leak at exit:
 *   gcc -Isrc -Ilib/chashmap/include -fsanitize=address -g -Wall -Wextra tests/test_script_module.c \
 *       $(find src lib/chashmap/src -name '*.c') -lcjson -pthread -lm -o /tmp/test_script_module
 *   /tmp/test_script_module
 */
#include "TienInterpreter.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); g_fails++; } } while (0)

static int g_fails;
static int g_destroyed;        /* Number of native objects destroyed */
static int g_result;           /* Value reported by t.mark (-1 = not reported) */
static int g_ticks;            /* Number of t.tick() calls (counts how often a top level ran) */
static char g_log[4096];       /* Everything the interpreter logged during one script */
static char g_importers[512];  /* "name<-importer_path;" for every loader call of one script */

/* -------------------- Module Sources -------------------- */

/* Files the loader can serve, by clean path ("<importer dir><name>.ti" without "." and "..") */
static const struct {
    const char *path;
    const char *source;
} s_files[] = {
    { "dir/lib.ti",
      "import t;\n"
      "int count = 1;\n"
      "int helper() { return 0; }\n"
      "int add(int a, int b) { return a + b + helper(); }\n"
      "int get_count() { return count; }\n"
      "void bump() { count += 1; }\n"
      "count = count + 1;\n" },
    { "dir/counter.ti", "import t; t.tick(); int runs = 1;\n" },
    { "dir/boxes.ti",   "import t; handle b = t.box(5);\n" },
    { "dir/outer.ti",   "import inner; int value = inner.value + 1;\n" },
    { "dir/inner.ti",   "int value = 41;\n" },
    { "dir/lib/a.ti",   "int v = 7;\n" },
    { "dir/sub/user.ti", "import ../lib/a; int v = a.v;\n" },
    { "dir/cyc_a.ti",   "import cyc_b; int a = 1;\n" },
    { "dir/cyc_b.ti",   "import cyc_a; int b = 2;\n" },
    { "dir/bad.ti",     "int x = ;\n" },
    { "dir/raises.ti",  "import t; t.fail();\n" },
};

/* -------------------- Host Callbacks -------------------- */

/* Collect log output so a case can check the error message */
static void on_log(const char *fmt, va_list args)
{
    size_t used = strlen(g_log);
    vsnprintf(g_log + used, sizeof(g_log) - used, fmt, args);
}

/**
 * @brief Copy a string into a ti_raw_malloc'd buffer (the library frees it with ti_raw_free).
 * @param text String to copy.
 * @return The copy, or NULL if out of memory.
 */
static char *raw_dup(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = ti_raw_malloc(length);
    if (copy != NULL) {
        memcpy(copy, text, length);
    }
    return copy;
}

/**
 * @brief Remove "." parts and "<dir>/.." pairs from a relative path, in place (realpath without a disk),
 * so every spelling of one file gives one path: "dir/sub/../lib/a.ti" -> "dir/lib/a.ti".
 * @param path Path to clean (at most 128 bytes, at most 16 parts).
 */
static void clean_path(char *path)
{
    char *parts[16];
    int count = 0;
    for (char *part = strtok(path, "/"); part != NULL && count < 16; part = strtok(NULL, "/")) {
        if (strcmp(part, ".") == 0) {
            continue;
        }
        if (strcmp(part, "..") == 0 && count > 0 && strcmp(parts[count - 1], "..") != 0) {
            count--;
            continue;
        }
        parts[count++] = part;
    }

    char clean[128] = "";
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            strcat(clean, "/");
        }
        strcat(clean, parts[i]);
    }
    strcpy(path, clean);
}

/* Loader like the CLI's: "<directory of importer_path><name>.ti", cleaned, served from s_files */
static ti_status_t test_loader(const char *importer_path, const char *name, char **text_out, char **path_out)
{
    /* Record who asked for what, so a case can check the path handed to the loader */
    size_t used = strlen(g_importers);
    snprintf(g_importers + used, sizeof(g_importers) - used, "%s<-%s;", name,
             importer_path != NULL ? importer_path : "(null)");

    /* A load failure that is not "no such module" */
    if (strcmp(name, "io") == 0) {
        return TI_ERR_TIMEOUT;
    }

    /* A loader that forgets the path: the library must refuse the module */
    if (strcmp(name, "nopath") == 0) {
        *text_out = raw_dup("int x = 1;\n");
        *path_out = NULL;
        return TI_OK;
    }

    /* Another name for counter.ti: the same file, so the same module */
    if (strcmp(name, "same_as_counter") == 0) {
        name = "counter";
    }

    /* Resolve the name from the importer's directory, then clean it to one spelling per file */
    char path[128];
    const char *slash = (importer_path != NULL) ? strrchr(importer_path, '/') : NULL;
    int dir_len = (slash != NULL) ? (int)(slash - importer_path) + 1 : 0;
    snprintf(path, sizeof(path), "%.*s%s.ti", dir_len, dir_len > 0 ? importer_path : "", name);
    clean_path(path);

    for (size_t i = 0; i < sizeof(s_files) / sizeof(s_files[0]); i++) {
        if (strcmp(s_files[i].path, path) == 0) {
            char *text = raw_dup(s_files[i].source);
            char *path_copy = raw_dup(path);
            if (text == NULL || path_copy == NULL) {
                ti_raw_free(text);
                ti_raw_free(path_copy);
                return TI_ERR_NO_MEMORY;
            }
            *text_out = text;
            *path_out = path_copy;
            return TI_OK;
        }
    }
    return TI_ERR_UNDEFINED;
}

/* -------------------- Test Natives (module t) -------------------- */

static const ti_handle_type_t s_box_type;

/* Destructor of a box: counts calls so leaks and double destroys show up */
static void box_destroy(void *ptr)
{
    g_destroyed++;
    free(ptr);
}

/* t.mark(bool ok): lets a script report a result (1 / 0) */
static value_t *t_mark(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle; (void)argc;
    g_result = argv[0]->bool_val ? 1 : 0;
    return val_new_void();
}

/* t.tick(): counts calls */
static value_t *t_tick(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle; (void)argv; (void)argc;
    g_ticks++;
    return val_new_void();
}

/* t.fail(): always raises */
static value_t *t_fail(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argv; (void)argc;
    ti_raise_error(handle, TI_ERR_INVALID_VALUE, "t.fail: always fails");
    return NULL;
}

/* t.box(int n): a "box" handle wrapping n */
static value_t *t_box(ti_handle_t handle, value_t **argv, int argc)
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

static const param_t s_p_mark[] = { PARAM("ok", VAL_BOOL) };
static const param_t s_p_box[]  = { PARAM("n", VAL_INT) };

static const ti_handle_type_t s_box_type = { .name = "box", .destroy = box_destroy };

static const ti_builtin_def_t s_t_funcs[] = {
    { "mark", VAL_VOID,   s_p_mark, 1, t_mark },
    { "tick", VAL_VOID,   NULL,     0, t_tick },
    { "fail", VAL_VOID,   NULL,     0, t_fail },
    { "box",  VAL_HANDLE, s_p_box,  1, t_box },
};

static const ti_module_t s_t = { .name = "t", .funcs = s_t_funcs, .func_count = 4 };

/* -------------------- Script Cases -------------------- */

/**
 * @brief Run one script with ti_run_string_take and compare the reported value, the destroy count and the log.
 * @param name Case name printed on failure.
 * @param path Path of the main script (NULL: none).
 * @param src Script source.
 * @param want_result Expected g_result (-1 = nothing reported).
 * @param want_destroyed Expected number of destroyed objects.
 * @param want_log Text the log must contain, or NULL for an empty log.
 */
static void run(const char *name, const char *path, const char *src, int want_result, int want_destroyed,
                const char *want_log)
{
    g_destroyed = 0;
    g_result = -1;
    g_ticks = 0;
    g_log[0] = '\0';
    g_importers[0] = '\0';
    (void)ti_run_string_take(raw_dup(src), path);

    bool log_ok = (want_log != NULL) ? strstr(g_log, want_log) != NULL : g_log[0] == '\0';
    if (g_result != want_result || g_destroyed != want_destroyed || !log_ok) {
        printf("FAIL %-24s result=%d destroyed=%d log=%s\n", name, g_result, g_destroyed, g_log);
        g_fails++;
    }
}

/* Calls, variables, namespaces and the top level of a script module */
static void test_namespaces(void)
{
    const char *main_path = "dir/main.ti";

    /* A module function runs in its module: it calls its own helper and reads its own globals */
    run("call script fn", main_path, "import t; import lib; t.mark(lib.add(2, 3) == 5);", 1, 0, NULL);
    run("read variable", main_path, "import t; import lib; t.mark(lib.count == 2);", 1, 0, NULL);
    run("fn reads own global", main_path, "import t; import lib; t.mark(lib.get_count() == 2);", 1, 0, NULL);
    run("fn writes own global", main_path,
        "import t; import lib; lib.bump(); t.mark(lib.count == 3 && lib.get_count() == 3);", 1, 0, NULL);

    /* The same names in main and in the module never clash */
    run("same fn name", main_path,
        "import t; int helper() { return 100; }\nimport lib; t.mark(lib.add(1, 1) == 2 && helper() == 100);", 1, 0, NULL);
    run("same var name", main_path,
        "import t; int count = 50; import lib; t.mark(count == 50 && lib.count == 2);", 1, 0, NULL);

    /* Reading a variable gives a copy */
    run("copy not alias", main_path,
        "import t; import lib; int x = lib.count; x = 9; t.mark(lib.count == 2);", 1, 0, NULL);

    /* One instance per runtime: the top level runs once, every import gets the same module */
    run("top level once", main_path,
        "import t; import counter; void f() { import counter as c; t.mark(c == counter); }\n"
        "f(); import counter as again; t.mark(again == counter);", 1, 0, NULL);
    CHECK(g_ticks == 1);
    run("alias equal", main_path, "import t; import lib; import lib as l; t.mark(l == lib);", 1, 0, NULL);

    /* A module variable that holds a handle is destroyed with the runtime */
    run("handle in module", main_path, "import boxes;", -1, 1, NULL);

    /* Circular import: the second import finds the module already listed and does not run it again */
    run("circular", main_path, "import t; import cyc_a; t.mark(cyc_a.a == 1);", 1, 0, NULL);
}

/* The path handed to the loader, and the path as the module's identity */
static void test_module_path(void)
{
    /* The loader gets the path of the file that runs the import: main's, then the module's */
    run("nested import", "dir/main.ti", "import t; import outer; t.mark(outer.value == 42);", 1, 0, NULL);
    CHECK(strstr(g_importers, "outer<-dir/main.ti;") != NULL);
    CHECK(strstr(g_importers, "inner<-dir/outer.ti;") != NULL);

    /* A module in a subdirectory reaches the same file with "../": it gets main's instance */
    run("shared from module", "dir/main.ti",
        "import t; import lib/a; import sub/user; t.mark(user.v == 7 && user.a == a);", 1, 0, NULL);
    CHECK(strstr(g_importers, "../lib/a<-dir/sub/user.ti;") != NULL);

    /* Two names the loader resolves to one file are one module: one top level, one instance */
    run("same file two names", "dir/main.ti",
        "import t; import counter; import same_as_counter as s; t.mark(s == counter);", 1, 0, NULL);
    CHECK(g_ticks == 1);
    run("same file name first", "dir/main.ti",
        "import t; import same_as_counter as s; import counter; t.mark(s == counter);", 1, 0, NULL);
    CHECK(g_ticks == 1);

    /* Without a path the loader gets NULL (resolving is the host's business: here nothing is found) */
    run("no path", NULL, "import lib;", -1, 0, "Unknown module 'lib'");
    CHECK(strcmp(g_importers, "lib<-(null);") == 0);

    /* A path name reaches the loader as written; without 'as' the variable is its last part */
    run("path name", "dir/main.ti", "import t; import lib/a; t.mark(a.v == 7);", 1, 0, NULL);
    CHECK(strcmp(g_importers, "lib/a<-dir/main.ti;") == 0);
    run("path name as", "dir/main.ti", "import t; import lib/a as m; t.mark(m.v == 7);", 1, 0, NULL);
    run("dot slash name", "dir/main.ti", "import t; import ./lib/a; t.mark(a.v == 7);", 1, 0, NULL);
    CHECK(strcmp(g_importers, "./lib/a<-dir/main.ti;") == 0);
    run("two spellings", "dir/main.ti", "import t; import lib/a; import ./lib/a as m; t.mark(m == a);", 1, 0, NULL);
    run("parent from main", "dir/sub/main.ti", "import t; import ../../dir/lib/a; t.mark(a.v == 7);", 1, 0, NULL);
    CHECK(strcmp(g_importers, "../../dir/lib/a<-dir/sub/main.ti;") == 0);

    /* A native module never reaches the loader */
    run("native skips loader", "dir/main.ti", "import t; t.mark(true);", 1, 0, NULL);
    CHECK(g_importers[0] == '\0');
}

/* The other ways in pass the main script's path the same way: ti_run_string, and compile/create/execute */
static void test_other_runners(void)
{
    const char *src = "import t; import lib/a; t.mark(a.v == 7);";

    /* ti_run_string copies the path like ti_run_string_take */
    g_result = -1;
    g_log[0] = '\0';
    g_importers[0] = '\0';
    CHECK(ti_run_string(src, "dir/main.ti") == TI_OK);
    CHECK(g_result == 1);
    CHECK(strcmp(g_importers, "lib/a<-dir/main.ti;") == 0);

    /* Two-phase: the path goes into the program at compile time; execute hands it to the loader */
    g_result = -1;
    g_importers[0] = '\0';
    ti_program_t *prog = ti_compile(src, "dir/main.ti");
    CHECK(prog != NULL);
    ti_handle_t handle = ti_create();
    CHECK(handle != TI_INVALID_HANDLE);
    CHECK(ti_execute(handle, prog) == TI_OK);
    ti_program_free(prog);
    CHECK(g_result == 1);
    CHECK(strcmp(g_importers, "lib/a<-dir/main.ti;") == 0);
    CHECK(g_log[0] == '\0');
}

/* Every error message of a script module */
static void test_errors(void)
{
    const char *main_path = "dir/main.ti";

    /* Loading */
    run("unknown script", main_path, "import nope;", -1, 0, "Unknown module 'nope'");
    run("load failure", main_path, "import io;", -1, 0, "Cannot load module 'io'");
    run("loader gives no path", main_path, "import nopath;", -1, 0, "Cannot load module 'nopath': the loader returned no path");
    run("compile failure", main_path, "import bad;", -1, 0, "Cannot import module 'bad': syntax error");
    CHECK(strstr(g_log, "in expression at dir/bad.ti:1\n") != NULL); /* the parser names the module's file */
    CHECK(strstr(g_log, "[Syntax Error]") == NULL);                 /* and is the only build error line */
    run("module raises", main_path, "import raises;", -1, 0, "t.fail: always fails");

    /* Module names: ('./' | '../')* ID ('/' ID)*; any other token is a syntax error */
    run("string name", main_path, "import \"lib/a\" as a;", -1, 0, "Expected token ID, but received STRING");
    run("trailing slash", main_path, "import lib/;", -1, 0, "Expected token ID, but received ';'");
    run("dot in name", main_path, "import lib.a;", -1, 0, "Expected token ';', but received '.'");
    run("three dots", main_path, "import .../a;", -1, 0, "Expected token '/', but received '.'");
    run("dots without slash", main_path, "import ..a;", -1, 0, "Expected token '/', but received ID");
    run("parent in the middle", main_path, "import lib/../a;", -1, 0, "Expected token ID, but received '.'");
    run("number part", main_path, "import lib/2;", -1, 0, "Expected token ID");

    /* Members */
    run("no function", main_path, "import lib; lib.nope();", -1, 0, "Module lib has no function 'nope'");
    run("var is not fn", main_path, "import lib; lib.count();", -1, 0, "Module lib has no function 'count'");
    run("no variable", main_path, "import lib; int x = lib.nope;", -1, 0, "Module lib has no variable 'nope'");
    run("fn is not var", main_path, "import lib; int x = lib.add;", -1, 0, "Module lib has no variable 'add'");
    run("arg count", main_path, "import lib; lib.add(1);", -1, 0, "Function 'add' expects 2 argument(s), but received 1");
    run("write module var", main_path, "import lib; lib.count = 5;", -1, 0, "Cannot set 'count' of module lib: read-only");
    run("to_string", main_path, "import lib; to_string(lib);", -1, 0, "to_string cannot convert module");
}

int main(void)
{
    ti_register_log(on_log);
    CHECK(ti_register_module(&s_t));
    ti_register_module_loader(test_loader);

    test_namespaces();
    test_module_path();
    test_other_runners();
    test_errors();

    printf(g_fails ? "FAILED (%d)\n" : "ALL PASSED\n", g_fails);
    return g_fails != 0;
}
