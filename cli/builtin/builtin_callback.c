#include "builtin.h"
#include <pthread.h>
#include <string.h>

/* -------------------- Static Types -------------------- */

/**
 * @brief Script function registered to answer synchronous calls from a native dispatcher.
 */
typedef struct {
    ti_handle_t handle; /* Runtime that registered the handler */
    value_t    *func;   /* TI handler function value (owned copy) */
} callback_record_t;

/* -------------------- Static Variables -------------------- */

/* Registered handler (NULL until a script calls register_callback) */
static callback_record_t *s_callback = NULL;

/* Guards s_callback: written on the script thread, read on the dispatcher thread.
 * It is never held while calling ti_call (see the lock-order rule in the guide). */
static pthread_mutex_t s_callback_mutex = PTHREAD_MUTEX_INITIALIZER;

/* -------------------- Static Function Prototypes -------------------- */

static value_t *builtin_register_callback(ti_handle_t handle, value_t **argv, int argc);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Built-in native register_callback function: bind synchronous calls to a TI handler.
 * @param handle Calling runtime handle, stored so ti_call can target it later.
 * @param argv Array of argument values (handler function value).
 * @param argc Number of arguments passed.
 * @return Void value_t, or NULL after raising an error.
 */
static value_t *builtin_register_callback(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;

    /* An empty function value ('func f;') has nothing to call */
    if (val_func_name(argv[0]) == NULL) {
        ti_raise_error(handle, TI_ERR_INVALID_ARG, "register_callback() expects a defined function");
        return NULL;
    }

    /* Keep an own copy of the function value (argv is freed after this call); copy it before taking
     * the lock to keep the critical section short */
    value_t *func_copy = val_copy(argv[0]);
    if (func_copy == NULL) {
        ti_raise_error(handle, TI_ERR_NO_MEMORY, "register_callback() out of memory");
        return NULL;
    }

    pthread_mutex_lock(&s_callback_mutex);

    /* Allocate the record on first registration */
    if (s_callback == NULL) {
        s_callback = (callback_record_t *)ti_raw_calloc(1, sizeof(callback_record_t));
    }

    /* Replace any previous registration (possibly from an already destroyed runtime) */
    if (s_callback != NULL) {
        val_free(s_callback->func);
        s_callback->func = func_copy;
        s_callback->handle = handle;
        func_copy = NULL;
    }

    pthread_mutex_unlock(&s_callback_mutex);

    /* Record allocation failed: the copy was not handed over */
    if (func_copy != NULL) {
        val_free(func_copy);
        ti_raise_error(handle, TI_ERR_NO_MEMORY, "register_callback() out of memory");
        return NULL;
    }
    return val_new_void();
}

/* -------------------- Public Functions -------------------- */

/* Register register_callback() */
void builtin_callback_register(void)
{
    static const param_t register_callback_params[] = { PARAM_FUNC("handler") };

    register_builtin_function("register_callback", VAL_VOID, register_callback_params, 1, builtin_register_callback);
}

/* Call the registered handler synchronously and wait for its answer */
ti_status_t cli_simulate_call(const char *text, char *answer, size_t answer_size)
{
    if (answer != NULL && answer_size > 0) {
        answer[0] = '\0';
    }

    /* Copy the target out of the record, then drop the mutex before ti_call:
     * the handler may run natives that take the same mutex. */
    ti_handle_t handle = TI_INVALID_HANDLE;
    value_t *func = NULL;

    pthread_mutex_lock(&s_callback_mutex);
    if (s_callback != NULL) {
        handle = s_callback->handle;
        func = val_copy(s_callback->func);
    }
    pthread_mutex_unlock(&s_callback_mutex);

    if (handle == TI_INVALID_HANDLE) {
        return TI_ERR_STALE_HANDLE;
    }
    if (func == NULL) {
        return TI_ERR_NO_MEMORY;
    }

    /* Arguments and the function value are borrowed by ti_call, so this side frees them afterwards */
    value_t *arg = val_new_string(text != NULL ? text : "");
    value_t *args[1] = { arg };
    value_t *result = NULL;

    /* Wait at most one second for the script to reach a safe point */
    ti_status_t status = ti_call(handle, func, args, 1, &result, 1000);
    val_free(arg);
    val_free(func);

    /* A string result is the handler's answer to the dispatcher */
    if (status == TI_OK && result != NULL && result->type == VAL_STRING && answer != NULL) {
        strncpy(answer, result->string_val, answer_size - 1);
        answer[answer_size - 1] = '\0';
    }
    val_free(result);

    /* Lazy deregistration: the runtime is gone, so drop the record */
    if (status == TI_ERR_STALE_HANDLE) {
        pthread_mutex_lock(&s_callback_mutex);
        if (s_callback != NULL && s_callback->handle == handle) {
            val_free(s_callback->func);
            ti_raw_free(s_callback);
            s_callback = NULL;
        }
        pthread_mutex_unlock(&s_callback_mutex);
    }
    return status;
}
