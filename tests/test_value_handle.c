/*
 * VAL_HANDLE: the C API (create, type check, share, close, destroy exactly once) and the script side
 * (declaration, type-checked native parameters, to_string, == / !=, containers, JSON rejection).
 *
 * Build and run (from the repository root); AddressSanitizer reports any leak at exit:
 *   gcc -Isrc -Ilib/chashmap/include -fsanitize=address -g tests/test_value_handle.c \
 *       $(find src lib/chashmap/src -name '*.c') -lcjson -pthread -lm -o /tmp/value_handle
 *   /tmp/value_handle
 */
#include "TienInterpreter.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); g_fails++; } } while (0)

static int g_fails;
static int g_destroyed;   /* Number of native objects destroyed */
static int g_result;      /* Value reported by cfg_read or mark (-1 = not reported) */
static char g_log[4096];  /* Everything the interpreter logged during one script */

/* -------------------- Host Callbacks -------------------- */

/* Collect log output so a case can check the error message */
static void on_log(const char *fmt, va_list args)
{
    size_t used = strlen(g_log);
    vsnprintf(g_log + used, sizeof(g_log) - used, fmt, args);
}

/* -------------------- Test Natives -------------------- */

/* Destructor of every test object: counts calls so double destroys and leaks show up */
static void cfg_destroy(void *ptr)
{
    g_destroyed++;
    free(ptr);
}

/* -------------------- Test Handle Types -------------------- */

/* Forward declarations: the parameter and method tables point to s_test_type, which points back */
static const ti_handle_type_t s_test_type;
static value_t *cfg_read(ti_handle_t handle, value_t **argv, int argc);
static value_t *cfg_close(ti_handle_t handle, value_t **argv, int argc);
static value_t *cfg_add(ti_handle_t handle, value_t **argv, int argc);
static value_t *cfg_me(ti_handle_t handle, value_t **argv, int argc);

/* Parameter tables shared by the free functions and the methods (params[0] is self) */
static const param_t s_p_cfg[]     = { PARAM_HANDLE("cfg", &s_test_type) };
static const param_t s_p_cfg_int[] = { PARAM_HANDLE("cfg", &s_test_type), PARAM("n", VAL_INT) };

/* Methods of test_cfg: read and close reuse the free functions' natives */
static const ti_builtin_def_t s_test_methods[] = {
    { "read",  VAL_INT,    s_p_cfg,     1, cfg_read },
    { "close", VAL_VOID,   s_p_cfg,     1, cfg_close },
    { "add",   VAL_INT,    s_p_cfg_int, 2, cfg_add },
    { "me",    VAL_HANDLE, s_p_cfg,     1, cfg_me },
};

/* Two types that own heap objects, so the type check and destroy counting can be tested;
 * only test_cfg has methods (uart_cfg tests a type whose method table is NULL) */
static const ti_handle_type_t s_test_type = { "test_cfg", cfg_destroy, s_test_methods, 4 };
static const ti_handle_type_t s_uart_type = { "uart_cfg", cfg_destroy, NULL, 0 };

/* Same name as s_test_type but another descriptor: identity is the address, not the name */
static const ti_handle_type_t s_fake_test_type = { "test_cfg", cfg_destroy, NULL, 0 };

/* No destructor: the native keeps ownership (static objects) */
static const ti_handle_type_t s_static_type = { "static", NULL, NULL, 0 };

/* Invalid descriptors: rejected by val_new_handle */
static const ti_handle_type_t s_empty_name_type = { "", NULL, NULL, 0 };
static const ti_handle_type_t s_null_name_type  = { NULL, NULL, NULL, 0 };

/* cfg_open(): a "test_cfg" handle wrapping an int 42 */
static value_t *cfg_open(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle; (void)argv; (void)argc;
    int *cfg = malloc(sizeof(int));
    *cfg = 42;
    return val_new_handle(cfg, &s_test_type);
}

/* uart_open(): a handle of another type, to test the type check */
static value_t *uart_open(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle; (void)argv; (void)argc;
    return val_new_handle(malloc(4), &s_uart_type);
}

/* cfg_read(handle cfg): report the wrapped int, or raise if the handle is closed */
static value_t *cfg_read(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    int *cfg = val_handle_get(argv[0], &s_test_type);
    if (cfg == NULL) {
        ti_raise_error(handle, TI_ERR_INVALID_VALUE, "cfg_read: closed");
        return NULL;
    }
    g_result = *cfg;
    return val_new_int(*cfg);
}

/* cfg_close(handle cfg): close now; raise if already closed */
static value_t *cfg_close(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    ti_status_t status = val_handle_close(argv[0], &s_test_type);
    if (status != TI_OK) {
        ti_raise_error(handle, status, "cfg_close: already closed");
        return NULL;
    }
    return val_new_void();
}

/* cfg.add(int n): wrapped int + n, or raise if the handle is closed */
static value_t *cfg_add(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    int *cfg = val_handle_get(argv[0], &s_test_type);
    if (cfg == NULL) {
        ti_raise_error(handle, TI_ERR_INVALID_VALUE, "cfg_add: closed");
        return NULL;
    }
    return val_new_int(*cfg + argv[1]->int_val);
}

/* cfg.me(): return self (another copy of the same handle), so calls can be chained */
static value_t *cfg_me(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle; (void)argc;
    return val_copy(argv[0]);
}

/* mark(bool ok): lets a script report a comparison result (1 / 0) */
static value_t *mark(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle; (void)argc;
    g_result = argv[0]->bool_val ? 1 : 0;
    return val_new_void();
}

/* -------------------- C API Cases -------------------- */

/* Create, type check, share through copies and containers, close, destroy exactly once */
static void test_c_api(void)
{
    /* Create and read back with the right type only */
    int *obj = malloc(sizeof(int));
    value_t *h = val_new_handle(obj, &s_test_type);
    CHECK(h != NULL && h->type == VAL_HANDLE);
    CHECK(val_handle_get(h, &s_test_type) == obj);
    CHECK(val_handle_get(h, &s_uart_type) == NULL);
    CHECK(val_handle_get(h, NULL) == NULL);
    CHECK(val_handle_type(h) == &s_test_type && strcmp(val_handle_type(h)->name, "test_cfg") == 0);
    CHECK(val_handle_is_open(h));

    /* Type identity is the descriptor address: an equal name in another descriptor never matches */
    CHECK(val_handle_get(h, &s_fake_test_type) == NULL);
    CHECK(val_handle_close(h, &s_fake_test_type) == TI_ERR_TYPE_MISMATCH && val_handle_is_open(h));

    /* Copies (shallow and deep) share the same handle */
    g_destroyed = 0;
    value_t *copy = val_copy(h);
    value_t *deep = val_deep_copy(h, TI_MAX_CONTAINER_DEPTH);
    CHECK(copy->handle_val == h->handle_val && deep->handle_val == h->handle_val);
    CHECK(h->handle_val->refcount == 3);
    val_free(deep);
    val_free(copy);
    CHECK(g_destroyed == 0);
    val_free(h);
    CHECK(g_destroyed == 1);

    /* Close through one copy: every copy reads as closed, destroy still runs once */
    g_destroyed = 0;
    h = val_new_handle(malloc(4), &s_test_type);
    copy = val_copy(h);
    CHECK(val_handle_close(copy, &s_uart_type) == TI_ERR_TYPE_MISMATCH);
    CHECK(val_handle_close(copy, NULL) == TI_ERR_TYPE_MISMATCH);
    CHECK(val_handle_close(copy, &s_test_type) == TI_OK && g_destroyed == 1);
    CHECK(val_handle_get(h, &s_test_type) == NULL && !val_handle_is_open(h));
    CHECK(val_handle_type(h) == &s_test_type); /* a closed handle keeps its type for messages */
    CHECK(val_handle_close(h, &s_test_type) == TI_ERR_INVALID_VALUE);
    val_free(h);
    val_free(copy);
    CHECK(g_destroyed == 1);

    /* Invalid arguments are rejected without taking ownership */
    static int static_obj;
    CHECK(val_new_handle(NULL, &s_test_type) == NULL);
    CHECK(val_new_handle(&static_obj, NULL) == NULL);
    CHECK(val_new_handle(&static_obj, &s_empty_name_type) == NULL);
    CHECK(val_new_handle(&static_obj, &s_null_name_type) == NULL);

    /* A type without destructor: the native keeps ownership, neither release nor close frees it */
    g_destroyed = 0;
    h = val_new_handle(&static_obj, &s_static_type);
    CHECK(val_handle_get(h, &s_static_type) == &static_obj);
    val_free(h);
    h = val_new_handle(&static_obj, &s_static_type);
    CHECK(val_handle_close(h, &s_static_type) == TI_OK && !val_handle_is_open(h));
    val_free(h);
    CHECK(g_destroyed == 0);

    /* Other types are never mistaken for a handle */
    value_t *num = val_new_int(5);
    CHECK(val_handle_get(num, &s_test_type) == NULL && val_handle_type(num) == NULL && !val_handle_is_open(num));
    val_free(num);

    /* Inside containers: shared, destroyed with the last container */
    g_destroyed = 0;
    h = val_new_handle(malloc(4), &s_test_type);
    value_t *dict = val_new_dict();
    CHECK(val_dict_set(dict->dict_val, "cfg", val_copy(h)) == TI_OK);
    value_t *list = val_new_list(VAL_DICT);
    CHECK(val_list_push(list->list_val, val_copy(dict)) == TI_OK);
    val_free(h);
    val_free(dict);
    CHECK(g_destroyed == 0);
    val_free(list);
    CHECK(g_destroyed == 1);

    /* Method lookup: by name in the type's table; no table, no type or no name finds nothing */
    const ti_builtin_def_t *row = handle_type_find_method(&s_test_type, "add");
    CHECK(row != NULL && row->function == cfg_add && row->param_count == 2);
    CHECK(handle_type_find_method(&s_test_type, "missing") == NULL);
    CHECK(handle_type_find_method(&s_uart_type, "read") == NULL);
    CHECK(handle_type_find_method(NULL, "read") == NULL);
    CHECK(handle_type_find_method(&s_test_type, NULL) == NULL);
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
    (void)ti_run_string(src, NULL);

    bool log_ok = (want_log != NULL) ? strstr(g_log, want_log) != NULL : g_log[0] == '\0';
    if (g_result != want_result || g_destroyed != want_destroyed || !log_ok) {
        printf("FAIL %-22s result=%d destroyed=%d log=%s\n", name, g_result, g_destroyed, g_log);
        g_fails++;
    }
}

/* Declaration, type check, sharing, to_string, == / != and JSON from scripts */
static void test_scripts(void)
{
    static const param_t p_bool[] = { PARAM("ok", VAL_BOOL) };

    register_builtin_function("cfg_open", VAL_HANDLE, NULL, 0, cfg_open);
    register_builtin_function("uart_open", VAL_HANDLE, NULL, 0, uart_open);
    register_builtin_function("cfg_read", VAL_INT, s_p_cfg, 1, cfg_read);
    register_builtin_function("cfg_close", VAL_VOID, s_p_cfg, 1, cfg_close);
    register_builtin_function("mark", VAL_VOID, p_bool, 1, mark);

    /* Lifetime and sharing */
    run("declare and read", "handle c = cfg_open(); int v = cfg_read(c);", 42, 1, NULL);
    run("pass to function", "int f(handle h) { return cfg_read(h); }\nhandle c = cfg_open(); int v = f(c);", 42, 1, NULL);
    run("copy shares close", "handle c = cfg_open(); handle d = c; cfg_close(d); int v = cfg_read(c);", -1, 1, "cfg_read: closed");
    run("close twice", "handle c = cfg_open(); cfg_close(c); cfg_close(c);", -1, 1, "already closed");
    run("in dict", "handle c = cfg_open(); dict m = {\"c\": c}; handle e = m[\"c\"]; int v = cfg_read(e);", 42, 1, NULL);
    run("for init", "for (handle h = cfg_open(); cfg_read(h) < 0; ) { }", 42, 1, NULL);
    run("block scope", "if (true) { handle h = cfg_open(); int v = cfg_read(h); }", 42, 1, NULL);

    /* Value type and handle type checks */
    run("int from handle", "int x = cfg_open();", -1, 1, "Expected int, but got handle");
    run("handle from int", "handle h = 5;", -1, 0, "Expected handle, but got int");
    run("wrong handle type", "handle u = uart_open(); cfg_read(u);", -1, 1, "expected handle test_cfg, but got handle uart_cfg");
    run("empty handle param", "handle e; cfg_read(e);", -1, 0, "expected handle test_cfg, but got an empty handle");
    run("closed keeps type", "handle c = cfg_open(); cfg_close(c); cfg_read(c);", -1, 1, "cfg_read: closed");
    run("script fn any type","int f(handle h) { return 7; }\nhandle u = uart_open(); int v = f(u); handle c = cfg_open(); cfg_read(c);", 42, 2, NULL);

    /* to_string, == / != and JSON */
    run("to_string open", "handle c = cfg_open(); mark(to_string(c) == \"<handle test_cfg>\");", 1, 1, NULL);
    run("to_string closed", "handle c = cfg_open(); cfg_close(c); mark(to_string(c) == \"<handle test_cfg closed>\");", 1, 1, NULL);
    run("to_string empty", "handle e; mark(to_string(e) == \"<handle none>\");", 1, 0, NULL);
    run("== same object", "handle c = cfg_open(); handle d = c; handle o = cfg_open(); mark(c == d && c != o);", 1, 2, NULL);
    run("== both empty", "handle a; handle b; mark(a == b);", 1, 0, NULL);
    run("!= empty vs open", "handle a; handle c = cfg_open(); mark(a != c && !(a == c));", 1, 1, NULL);
    run("== after dict copy", "handle c = cfg_open(); dict m = {\"c\": c}; handle e = m[\"c\"]; mark(e == c);", 1, 1, NULL);
    run("json rejects handle", "handle c = cfg_open(); dict m = {\"c\": c}; string s = to_string(m);", -1, 1, "to_string");

    /* Method calls: obj.name(args) runs the type's native with obj as argument 0 (self) */
    run("method call", "handle c = cfg_open(); int v = c.read();", 42, 1, NULL);
    run("method statement", "handle c = cfg_open(); c.read();", 42, 1, NULL);
    run("method == function", "handle c = cfg_open(); mark(c.read() == cfg_read(c));", 1, 1, NULL);
    run("method with arg", "handle c = cfg_open(); mark(c.add(8) == 50);", 1, 1, NULL);
    run("method in script fn", "int f(handle h) { return h.read(); }\nhandle c = cfg_open(); int v = f(c);", 42, 1, NULL);
    run("method unary minus", "handle c = cfg_open(); mark(-c.read() == -42);", 1, 1, NULL);
    run("method in expression", "handle c = cfg_open(); mark(c.add(1) + c.add(2) * 2 == 131);", 1, 1, NULL);

    /* Receivers other than a variable, and chaining */
    run("temporary receiver", "int v = cfg_open().read();", 42, 1, NULL);
    run("dict element receiver", "handle c = cfg_open(); dict m = {\"c\": c}; int v = m[\"c\"].read();", 42, 1, NULL);
    run("chained methods", "handle c = cfg_open(); int v = c.me().me().read();", 42, 1, NULL);
    run("chained temporary", "int v = cfg_open().me().add(0);", -1, 1, NULL);
    run("chained same object", "handle c = cfg_open(); mark(c.me() == c);", 1, 1, NULL);

    /* Closing through a method closes every copy */
    run("method close", "handle c = cfg_open(); handle d = c; d.close(); cfg_read(c);", -1, 1, "cfg_read: closed");
    run("method on closed", "handle c = cfg_open(); c.close(); c.add(1);", -1, 1, "cfg_add: closed");
    run("method close twice", "handle c = cfg_open(); c.close(); c.close();", -1, 1, "already closed");

    /* Errors: run_function checks count and types including self */
    run("method arg count", "handle c = cfg_open(); c.add();", -1, 1, "Function 'add' expects 2 argument(s), but received 1");
    run("method arg type", "handle c = cfg_open(); c.add(\"x\");", -1, 1, "parameter 2 ('n') expected type int, but got string");
    run("unknown method", "handle c = cfg_open(); c.nope();", -1, 1, "Handle test_cfg has no method 'nope'");
    run("type without methods", "handle u = uart_open(); u.read();", -1, 1, "Handle uart_cfg has no method 'read'");
    run("method on int", "int x = 5; x.read();", -1, 0, "Cannot call method 'read' on int");
    run("method on result int", "handle c = cfg_open(); c.read().read();", 42, 1, "Cannot call method 'read' on int");
    run("method on empty handle", "handle e; e.read();", -1, 0, "Cannot call method 'read' on an empty handle");
    run("method bad arg first", "handle c = cfg_open(); c.add(nope);", -1, 1, "nope");
}

int main(void)
{
    ti_register_log(on_log);

    test_c_api();
    test_scripts();

    printf(g_fails ? "FAILED (%d)\n" : "ALL PASSED\n", g_fails);
    return g_fails != 0;
}
