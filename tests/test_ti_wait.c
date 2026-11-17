/*
 * Test for ti_wait (the engine of delay): events and ti_call callbacks run during the sleep, the
 * sleep still lasts its full time, and ti_stop ends it at once.
 * Build with -fsanitize=thread (or address) and run without arguments.
 */
#include "TienInterpreter.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static ti_handle_t g_handle;
static ti_program_t *g_prog;
static int g_failures;
static volatile uint32_t g_sleep_elapsed;
static volatile int g_reported = -1; /* Value the script hands out through report() before it ends */

#define CHECK(cond, msg) \
    do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); g_failures++; } } while (0)

/* Native used by the script: sleeps through ti_wait and records how long it really took */
static value_t *native_sleepy(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    uint32_t start = ti_millis();
    ti_wait(handle, (uint32_t)argv[0]->int_val);
    g_sleep_elapsed = ti_millis() - start;
    return val_new_null();
}

/* Native used by the script: hands a value to the test (the runtime is gone once ti_execute returns) */
static value_t *native_report(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle;
    (void)argc;
    g_reported = argv[0]->int_val;
    return val_new_null();
}

static void *script_thread(void *arg)
{
    (void)arg;
    ti_execute(g_handle, g_prog);
    return NULL;
}

static void log_cb(const char *fmt, va_list args) { (void)fmt; (void)args; }

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void start_script(const char *source)
{
    g_prog = ti_compile(source);
    g_handle = ti_create();
    CHECK(g_prog != NULL && g_handle != TI_INVALID_HANDLE, "setup");
}

static void finish_script(void)
{
    /* ti_execute already destroyed the runtime: only the program is left */
    CHECK(ti_destroy(g_handle) == TI_ERR_STALE_HANDLE, "execute destroyed the runtime");
    ti_program_free(g_prog);
}

int main(void)
{
    ti_register_log(log_cb);
    static const param_t params[] = { { VAL_INT, "ms", VAL_NULL, NULL } };
    static const param_t report_params[] = { PARAM_INT("value") };
    register_builtin_function("sleepy", VAL_VOID, params, 1, native_sleepy);
    register_builtin_function("report", VAL_VOID, report_params, 1, native_report);

    /* A: calls and events run during the sleep, which still lasts its full time */
    start_script("int counter = 0;\n"
                 "void bump() { counter += 1; }\n"
                 "void on_event() { counter += 100; }\n"
                 "sleepy(600);\n"
                 "report(counter);\n");
    pthread_t th;
    pthread_create(&th, NULL, script_thread, NULL);
    sleep_ms(100);

    uint32_t worst = 0;
    for (int i = 0; i < 20; i++) {
        uint32_t t0 = ti_millis();
        CHECK(ti_call(g_handle, "bump", NULL, 0, NULL, 1000) == TI_OK, "bump during delay");
        uint32_t took = ti_millis() - t0;
        if (took > worst) {
            worst = took;
        }
    }
    CHECK(worst < 100, "ti_call during delay is served immediately");

    for (int i = 0; i < 5; i++) {
        CHECK(ti_post_event(g_handle, "on_event", NULL, 0) == TI_OK, "post event");
        sleep_ms(5);
    }

    pthread_join(th, NULL);
    CHECK(g_sleep_elapsed >= 590 && g_sleep_elapsed < 900, "delay lasts its full time");

    CHECK(g_reported == 20 + 5 * 100, "all calls and events ran");
    finish_script();

    /* B: ti_stop ends a long sleep immediately */
    start_script("sleepy(5000);\n");
    pthread_create(&th, NULL, script_thread, NULL);
    sleep_ms(100);
    CHECK(ti_stop(g_handle) == TI_OK, "stop");
    pthread_join(th, NULL);
    CHECK(g_sleep_elapsed < 1000, "stop wakes the delay at once");
    finish_script();

    printf(g_failures == 0 ? "ALL PASSED\n" : "FAILURES: %d\n", g_failures);
    return g_failures != 0;
}
