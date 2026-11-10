#include "builtin.h"
#include <pthread.h>
#include <unistd.h>

/* -------------------- Constants -------------------- */

#define AUTONICS_IO_DELAY_S   5     /* Simulated device response time */
#define AUTONICS_TIMEOUT_MS   10000 /* How long tk_get_pv waits for the worker */

/* -------------------- Static Types -------------------- */

/**
 * @brief One in-flight PV request shared with the worker thread.
 * Static storage: the worker may still write here after the native gave up (timeout / stop).
 */
typedef struct {
    ti_handle_t handle;    /* Runtime to wake with ti_give */
    bool        done;      /* Completion flag: set by the worker (release), read by ti_wait_flag (acquire) */
    bool        busy;      /* A worker is still running: no new request until it finishes */
    float       pv;        /* Result written by the worker before it sets done */
} autonics_request_t;

/* -------------------- Static Variables -------------------- */

static int s_autonics_slave_addr = 1;
static autonics_request_t s_pv_req;

/* -------------------- Static Function Prototypes -------------------- */

static value_t *builtin_autonics_tk_set_slave_address(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_autonics_tk_get_pv(ti_handle_t handle, value_t **argv, int argc);
static value_t *builtin_autonics_tk_get_sv(ti_handle_t handle, value_t **argv, int argc);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Built-in native autonics_tk_set_slave_address function.
 * @param handle Calling runtime handle (unused).
 * @param argv Array of argument values.
 * @param argc Number of arguments passed.
 * @return Null value_t.
 */
static value_t *builtin_autonics_tk_set_slave_address(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle;
    if (argc >= 1 && argv[0]->type == VAL_INT) {
        s_autonics_slave_addr = argv[0]->int_val;
    }
    return val_new_null();
}

/**
 * @brief Worker thread simulating the device: answers after AUTONICS_IO_DELAY_S seconds.
 * @param arg Unused.
 * @return NULL.
 */
static void *autonics_pv_worker(void *arg)
{
    (void)arg;
    sleep(AUTONICS_IO_DELAY_S);

    /* 1. Write the result, 2. publish the flag (release), 3. wake the waiting native */
    s_pv_req.pv = 28.5f;
    __atomic_store_n(&s_pv_req.done, true, __ATOMIC_RELEASE);
    ti_give(s_pv_req.handle);

    /* The request storage may be reused from now on */
    __atomic_store_n(&s_pv_req.busy, false, __ATOMIC_RELEASE);
    return NULL;
}

/**
 * @brief Built-in native autonics_tk_get_pv function: start a worker and wait for its answer.
 * Events and ti_call callbacks keep running while it waits.
 * @param handle Calling runtime handle, woken by the worker through ti_give.
 * @param argv Array of argument values (unused).
 * @param argc Number of arguments passed.
 * @return Present temperature float value_t, or NULL with an error raised.
 */
static value_t *builtin_autonics_tk_get_pv(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argv;
    (void)argc;

    /* A worker from an earlier timed-out request may still write the shared storage */
    if (__atomic_load_n(&s_pv_req.busy, __ATOMIC_ACQUIRE)) {
        ti_raise_error(handle, TI_ERR_RUNTIME, "autonics_tk_get_pv: previous request still in progress");
        return NULL;
    }

    /* Clear the flag before the worker can possibly set it */
    s_pv_req.handle = handle;
    __atomic_store_n(&s_pv_req.done, false, __ATOMIC_RELAXED);
    __atomic_store_n(&s_pv_req.busy, true, __ATOMIC_RELAXED);

    /* Detached: nobody joins it, it ends on its own after answering */
    pthread_t worker;
    if (pthread_create(&worker, NULL, autonics_pv_worker, NULL) != 0) {
        __atomic_store_n(&s_pv_req.busy, false, __ATOMIC_RELAXED);
        ti_raise_error(handle, TI_ERR_NO_MEMORY, "autonics_tk_get_pv: cannot start worker");
        return NULL;
    }
    pthread_detach(worker);

    /* Block this native (not the runtime): events and callbacks run until the worker answers */
    ti_status_t status = ti_wait_flag(handle, &s_pv_req.done, AUTONICS_TIMEOUT_MS);
    if (status == TI_ERR_INTERRUPTED) {
        return NULL; /* Script is stopping; the worker finishes on its own */
    }
    if (status != TI_OK) {
        ti_raise_error(handle, TI_ERR_TIMEOUT, "autonics_tk_get_pv: no answer in %d ms", AUTONICS_TIMEOUT_MS);
        return NULL;
    }
    return val_new_float(s_pv_req.pv);
}

/**
 * @brief Built-in native autonics_tk_get_sv function.
 * @param handle Calling runtime handle (unused).
 * @param argv Array of argument values.
 * @param argc Number of arguments passed.
 * @return Setpoint temperature float value_t.
 */
static value_t *builtin_autonics_tk_get_sv(ti_handle_t handle, value_t **argv, int argc)
{
    (void)handle;
    (void)argv;
    (void)argc;
    return val_new_float(85.0f);
}

/* -------------------- Public Functions -------------------- */

/* Register the autonics_tk_* functions */
void builtin_autonics_register(void)
{
    static param_t autonics_addr_params[] = { { VAL_INT, "address", VAL_NULL, NULL } };

    register_builtin_function("autonics_tk_set_slave_address", VAL_VOID, autonics_addr_params, 1, builtin_autonics_tk_set_slave_address);
    register_builtin_function("autonics_tk_get_pv", VAL_FLOAT, NULL, 0, builtin_autonics_tk_get_pv);
    register_builtin_function("autonics_tk_get_sv", VAL_FLOAT, NULL, 0, builtin_autonics_tk_get_sv);
}
