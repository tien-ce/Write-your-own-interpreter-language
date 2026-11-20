/*
 * Test for ti_wait_flag / ti_give: a native blocks until an I/O thread reports completion, while
 * events and ti_call callbacks keep running; timeout, stop and stale handles are reported.
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
static value_t *g_bump;     /* Own copy of the script's bump function value (handed out by keep) */
static value_t *g_on_event; /* Own copy of the script's on_event function value */

/* Shared with the I/O thread: static storage, so it outlives any wait */
static bool g_done;
static int g_io_result;
static long g_io_delay_ms;

/* Recorded by the native */
static volatile ti_status_t g_wait_status;
static volatile uint32_t g_wait_elapsed;
static volatile int g_seen_result;
static volatile int g_reported = -1; /* Value the script hands out through report() before it ends */

#define CHECK(cond, msg) \
    do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); g_failures++; } } while (0)

static void sleep_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* I/O thread: write the result, publish the flag (release), then wake the waiter */
static void *io_thread(void *arg)
{
    (void)arg;
    sleep_ms(g_io_delay_ms);
    g_io_result = 1234;
    __atomic_store_n(&g_done, true, __ATOMIC_RELEASE);
    ti_give(g_handle);
    return NULL;
}

/* Native used by the script: wait_io(timeout_ms) blocks on g_done */
static value_t *native_wait_io(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;
    int timeout = argv[0]->int_val;
    uint32_t start = ti_millis();
    g_wait_status = ti_wait_flag(handle, &g_done, timeout < 0 ? TI_PORT_WAIT_FOREVER : (uint32_t)timeout);
    g_wait_elapsed = ti_millis() - start;
    if (g_wait_status == TI_OK) {
        g_seen_result = g_io_result;
    }
    return val_new_void();
}

/* Native used by the script: hands a value to the test (the runtime is gone once ti_execute returns) */
static value_t *native_report(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle;
    (void)argc;
    g_reported = argv[0]->int_val;
    return val_new_void();
}

/* Native used by the script: keeps a copy of a function value, like a driver's register function */
static value_t *native_keep(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle;
    (void)argc;
    /* The first call hands out bump, the second on_event */
    value_t **slot = (__atomic_load_n(&g_bump, __ATOMIC_ACQUIRE) == NULL) ? &g_bump : &g_on_event;
    __atomic_store_n(slot, val_copy(argv[0]), __ATOMIC_RELEASE);
    return val_new_void();
}

static void *script_thread(void *arg)
{
    (void)arg;
    ti_execute(g_handle, g_prog);
    return NULL;
}

static void log_cb(const char *fmt, va_list args) { (void)fmt; (void)args; }

static void start_script(const char *source)
{
    g_done = false;
    g_io_result = 0;
    g_seen_result = 0;
    g_wait_status = TI_ERR_RUNTIME;
    g_prog = ti_compile(source, NULL);
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
    static const param_t params[] = { { VAL_INT, "timeout_ms", VAL_NULL, NULL } };
    static const param_t report_params[] = { PARAM_INT("value") };
    register_builtin_function("wait_io", VAL_VOID, params, 1, native_wait_io);
    static const param_t keep_params[] = { PARAM_FUNC("func") };
    register_builtin_function("report", VAL_VOID, report_params, 1, native_report);
    register_builtin_function("keep", VAL_VOID, keep_params, 1, native_keep);

    pthread_t th, io;

    /* A: completion wakes the wait early; calls and events run meanwhile; the result is visible */
    start_script("int counter = 0;\n"
                 "void bump() { counter += 1; }\n"
                 "void on_event() { counter += 100; }\n"
                 "keep(bump);\n"
                 "keep(on_event);\n"
                 "wait_io(5000);\n"
                 "report(counter);\n");
    g_io_delay_ms = 400;
    pthread_create(&th, NULL, script_thread, NULL);
    pthread_create(&io, NULL, io_thread, NULL);
    sleep_ms(50);
    for (int i = 0; i < 10; i++) {
        CHECK(ti_call(g_handle, __atomic_load_n(&g_bump, __ATOMIC_ACQUIRE), NULL, 0, NULL, 1000) == TI_OK, "bump during wait");
    }
    CHECK(ti_post_event(g_handle, __atomic_load_n(&g_on_event, __ATOMIC_ACQUIRE), NULL, 0) == TI_OK, "post event");
    pthread_join(io, NULL);
    pthread_join(th, NULL);
    CHECK(g_wait_status == TI_OK, "wait returns TI_OK");
    CHECK(g_wait_elapsed >= 390 && g_wait_elapsed < 1000, "woken at completion, not at timeout");
    CHECK(g_seen_result == 1234, "result written before the flag is visible");
    CHECK(g_reported == 10 + 100, "calls and events ran during the wait");
    finish_script();

    /* B: no completion -> TI_ERR_TIMEOUT after the timeout */
    start_script("wait_io(300);\n");
    pthread_create(&th, NULL, script_thread, NULL);
    pthread_join(th, NULL);
    CHECK(g_wait_status == TI_ERR_TIMEOUT, "timeout status");
    CHECK(g_wait_elapsed >= 290 && g_wait_elapsed < 800, "timeout duration");
    finish_script();

    /* C: flag already set -> returns at once */
    start_script("wait_io(5000);\n");
    g_done = true;
    pthread_create(&th, NULL, script_thread, NULL);
    pthread_join(th, NULL);
    CHECK(g_wait_status == TI_OK && g_wait_elapsed < 100, "flag already set");
    finish_script();

    /* D: ti_stop ends a wait with no limit */
    start_script("wait_io(-1);\n");
    pthread_create(&th, NULL, script_thread, NULL);
    sleep_ms(100);
    CHECK(ti_stop(g_handle) == TI_OK, "stop");
    pthread_join(th, NULL);
    CHECK(g_wait_status == TI_ERR_INTERRUPTED, "stop interrupts the wait");
    finish_script();

    /* E: completion also wakes a wait with no limit */
    start_script("wait_io(-1);\n");
    g_io_delay_ms = 200;
    pthread_create(&th, NULL, script_thread, NULL);
    pthread_create(&io, NULL, io_thread, NULL);
    pthread_join(io, NULL);
    pthread_join(th, NULL);
    CHECK(g_wait_status == TI_OK && g_wait_elapsed < 1000, "forever wait woken by ti_give");
    finish_script();

    /* F: ti_give on a destroyed runtime is rejected safely */
    CHECK(ti_give(g_handle) == TI_ERR_STALE_HANDLE, "stale handle");

    /* Freeing a function value never touches its function, so it is safe after the runtime is gone */
    val_free(g_bump);
    val_free(g_on_event);

    printf(g_failures == 0 ? "ALL PASSED\n" : "FAILURES: %d\n", g_failures);
    return g_failures != 0;
}
