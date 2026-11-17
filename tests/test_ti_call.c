/*
 * Multi-thread test for ti_call: a "driver" thread calls script callbacks while the script thread
 * runs a busy loop. Build with -fsanitize=thread (or address) and run without arguments.
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
    "int spin = 0;\n"
    "while (1 == 1) { spin++; }\n";

static ti_handle_t g_handle;
static ti_program_t *g_prog;
static ti_status_t g_exec_status; /* What ti_execute returned (read after joining the script thread) */
static int g_failures;

#define CHECK(cond, msg) \
    do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); g_failures++; } } while (0)

static void *script_thread(void *arg)
{
    (void)arg;
    g_exec_status = ti_execute(g_handle, g_prog);
    return NULL;
}

static void log_cb(const char *fmt, va_list args) { (void)fmt; (void)args; }

/* Wait until the script defined its functions (ti_call reports undefined until then) */
static void wait_ready(void)
{
    for (int i = 0; i < 2000; i++) {
        if (ti_call(g_handle, "get_counter", NULL, 0, NULL, 1000) == TI_OK) {
            return;
        }
        struct timespec ts = {0, 1000000};
        nanosleep(&ts, NULL);
    }
}

int main(void)
{
    ti_register_log(log_cb);

    g_prog = ti_compile(SCRIPT);
    g_handle = ti_create();
    CHECK(g_prog != NULL && g_handle != TI_INVALID_HANDLE, "setup");

    /* Before the script runs: the lock is free, an undefined callback reports it */
    CHECK(ti_call(g_handle, "add", NULL, 0, NULL, 100) == TI_ERR_UNDEFINED, "undefined before run");
    CHECK(ti_call(TI_INVALID_HANDLE, "add", NULL, 0, NULL, 100) == TI_ERR_STALE_HANDLE, "stale handle");
    CHECK(ti_call(g_handle, "", NULL, 0, NULL, 100) == TI_ERR_INVALID_ARG, "empty name");

    pthread_t th;
    pthread_create(&th, NULL, script_thread, NULL);
    wait_ready();

    /* Results, argument errors and callback errors while the script keeps spinning */
    const int N = 3000;
    int bumps = 0;
    for (int i = 0; i < N; i++) {
        value_t *args[2] = { val_new_int(i), val_new_int(2) };
        value_t *res = NULL;
        ti_status_t st = ti_call(g_handle, "add", args, 2, &res, TI_PORT_WAIT_FOREVER);
        CHECK(st == TI_OK && res != NULL && res->type == VAL_INT && res->int_val == i + 2, "add result");
        val_free(res);

        st = ti_call(g_handle, "bump", NULL, 0, NULL, TI_PORT_WAIT_FOREVER);
        CHECK(st == TI_OK, "bump");
        bumps++;

        if (i % 50 == 0) {
            st = ti_call(g_handle, "bad", NULL, 0, NULL, TI_PORT_WAIT_FOREVER);
            CHECK(st == TI_ERR_UNDEFINED, "bad returns its error kind");
            st = ti_call(g_handle, "add", args, 1, NULL, TI_PORT_WAIT_FOREVER);
            CHECK(st == TI_ERR_INVALID_ARG, "wrong argument count is an error, not fatal");
        }
        val_free(args[0]);
        val_free(args[1]);
    }

    /* The callback errors did not break the script or the runtime */
    value_t *res = NULL;
    CHECK(ti_call(g_handle, "get_counter", NULL, 0, &res, TI_PORT_WAIT_FOREVER) == TI_OK, "get_counter");
    CHECK(res != NULL && res->int_val == bumps, "counter equals number of bumps");
    val_free(res);

    /* A zero timeout never blocks: either it got in or it reports a timeout */
    for (int i = 0; i < 2000; i++) {
        ti_status_t st = ti_call(g_handle, "bump", NULL, 0, NULL, 0);
        CHECK(st == TI_OK || st == TI_ERR_TIMEOUT, "zero timeout result");
        if (st == TI_OK) {
            bumps++;
        }
    }

    /* Stop: the script exits, ti_execute destroys the runtime, later calls see a stale handle */
    CHECK(ti_stop(g_handle) == TI_OK, "stop");
    pthread_join(th, NULL);
    CHECK(g_exec_status == TI_ERR_INTERRUPTED, "execute reports the stop");
    CHECK(ti_call(g_handle, "bump", NULL, 0, NULL, 100) == TI_ERR_STALE_HANDLE, "call after the run");
    CHECK(ti_destroy(g_handle) == TI_ERR_STALE_HANDLE, "execute already destroyed the runtime");
    ti_program_free(g_prog);

    printf(g_failures == 0 ? "ALL PASSED\n" : "FAILURES: %d\n", g_failures);
    return g_failures != 0;
}
