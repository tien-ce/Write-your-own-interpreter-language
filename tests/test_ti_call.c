/*
 * Multi-thread test for ti_call: a "driver" thread calls script callbacks while the script thread
 * runs a busy loop. The script hands its functions to the test through the keep() native, the way a
 * driver's register function would. Build with -fsanitize=thread (or address) and run without arguments.
 */
#include "TienInterpreter.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *SCRIPT =
    "int counter = 0;\n"
    "int add(int a, int b) { return a + b; }\n"
    "void bump() { counter += 1; }\n"
    "int get_counter() { return counter; }\n"
    "void bad() { undefined_fn(); }\n"
    "keep(add);\n"
    "keep(bump);\n"
    "keep(get_counter);\n"
    "keep(bad);\n"
    "int spin = 0;\n"
    "while (1 == 1) { spin++; }\n";

/* Order in which the script calls keep() */
enum { F_ADD, F_BUMP, F_GET_COUNTER, F_BAD, F_COUNT };

static ti_handle_t g_handle;
static ti_program_t *g_prog;
static ti_status_t g_exec_status; /* What ti_execute returned (read after joining the script thread) */
static int g_failures;
static value_t *g_funcs[F_COUNT]; /* Own copies of the function values the script handed out */
static int g_kept;                /* Number of entries filled (atomic access) */

#define CHECK(cond, msg) \
    do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); g_failures++; } } while (0)

/* Native used by the script: keeps a copy of a function value, like a driver's register function */
static value_t *native_keep(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle;
    (void)argc;
    int index = __atomic_load_n(&g_kept, __ATOMIC_ACQUIRE);
    if (index < F_COUNT) {
        g_funcs[index] = val_copy(argv[0]);
        __atomic_store_n(&g_kept, index + 1, __ATOMIC_RELEASE);
    }
    return val_new_void();
}

static void *script_thread(void *arg)
{
    (void)arg;
    g_exec_status = ti_execute(g_handle, g_prog);
    return NULL;
}

static void log_cb(const char *fmt, va_list args) { (void)fmt; (void)args; }

/* Wait until the script handed out all its functions */
static void wait_ready(void)
{
    for (int i = 0; i < 2000 && __atomic_load_n(&g_kept, __ATOMIC_ACQUIRE) < F_COUNT; i++) {
        struct timespec ts = {0, 1000000};
        nanosleep(&ts, NULL);
    }
}

int main(void)
{
    ti_register_log(log_cb);
    static const param_t keep_params[] = { PARAM_FUNC("func") };
    register_builtin_function("keep", VAL_VOID, keep_params, 1, native_keep);

    g_prog = ti_compile(SCRIPT, NULL);
    g_handle = ti_create();
    CHECK(g_prog != NULL && g_handle != TI_INVALID_HANDLE, "setup");

    /* A missing function value or a value of another type is rejected before the handle is used */
    value_t *not_func = val_new_int(1);
    CHECK(ti_call(g_handle, NULL, NULL, 0, NULL, 100) == TI_ERR_INVALID_ARG, "NULL function value");
    CHECK(ti_call(g_handle, not_func, NULL, 0, NULL, 100) == TI_ERR_INVALID_ARG, "not a function value");
    val_free(not_func);

    pthread_t th;
    pthread_create(&th, NULL, script_thread, NULL);
    wait_ready();
    CHECK(__atomic_load_n(&g_kept, __ATOMIC_ACQUIRE) == F_COUNT, "script handed out its functions");

    /* Results, argument errors and callback errors while the script keeps spinning */
    const int N = 3000;
    int bumps = 0;
    for (int i = 0; i < N; i++) {
        value_t *args[2] = { val_new_int(i), val_new_int(2) };
        value_t *res = NULL;
        ti_status_t st = ti_call(g_handle, g_funcs[F_ADD], args, 2, &res, TI_PORT_WAIT_FOREVER);
        CHECK(st == TI_OK && res != NULL && res->type == VAL_INT && res->int_val == i + 2, "add result");
        val_free(res);

        st = ti_call(g_handle, g_funcs[F_BUMP], NULL, 0, NULL, TI_PORT_WAIT_FOREVER);
        CHECK(st == TI_OK, "bump");
        bumps++;

        if (i % 50 == 0) {
            st = ti_call(g_handle, g_funcs[F_BAD], NULL, 0, NULL, TI_PORT_WAIT_FOREVER);
            CHECK(st == TI_ERR_UNDEFINED, "bad returns its error kind");
            st = ti_call(g_handle, g_funcs[F_ADD], args, 1, NULL, TI_PORT_WAIT_FOREVER);
            CHECK(st == TI_ERR_INVALID_ARG, "wrong argument count is an error, not fatal");
        }
        val_free(args[0]);
        val_free(args[1]);
    }

    /* The callback errors did not break the script or the runtime */
    value_t *res = NULL;
    CHECK(ti_call(g_handle, g_funcs[F_GET_COUNTER], NULL, 0, &res, TI_PORT_WAIT_FOREVER) == TI_OK, "get_counter");
    CHECK(res != NULL && res->int_val == bumps, "counter equals number of bumps");
    val_free(res);

    /* A zero timeout never blocks: either it got in or it reports a timeout */
    for (int i = 0; i < 2000; i++) {
        ti_status_t st = ti_call(g_handle, g_funcs[F_BUMP], NULL, 0, NULL, 0);
        CHECK(st == TI_OK || st == TI_ERR_TIMEOUT, "zero timeout result");
        if (st == TI_OK) {
            bumps++;
        }
    }

    /* Stop: the script exits, ti_execute destroys the runtime, later calls see a stale handle */
    CHECK(ti_stop(g_handle) == TI_OK, "stop");
    pthread_join(th, NULL);
    CHECK(g_exec_status == TI_ERR_INTERRUPTED, "execute reports the stop");
    CHECK(ti_call(g_handle, g_funcs[F_BUMP], NULL, 0, NULL, 100) == TI_ERR_STALE_HANDLE, "call after the run");
    CHECK(ti_destroy(g_handle) == TI_ERR_STALE_HANDLE, "execute already destroyed the runtime");

    /* A function of the destroyed runtime sent to another runtime: rejected without touching the freed
     * function (ASan reports any read); a queued event of it is freed with the runtime */
    ti_handle_t other = ti_create();
    CHECK(other != TI_INVALID_HANDLE, "second runtime");
    CHECK(ti_call(other, g_funcs[F_BUMP], NULL, 0, NULL, 100) == TI_ERR_UNDEFINED, "foreign function call");
    CHECK(ti_post_event(other, g_funcs[F_BUMP], NULL, 0) == TI_OK, "foreign function event is queued");
    CHECK(ti_destroy(other) == TI_OK, "destroy second runtime");

    /* Freeing a function value never touches its function, so it is safe after the runtime is gone */
    for (int i = 0; i < F_COUNT; i++) {
        val_free(g_funcs[i]);
    }
    ti_program_free(g_prog);

    printf(g_failures == 0 ? "ALL PASSED\n" : "FAILURES: %d\n", g_failures);
    return g_failures != 0;
}
