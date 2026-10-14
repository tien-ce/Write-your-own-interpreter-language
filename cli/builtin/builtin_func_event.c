#include "builtin.h"
#include <pthread.h>

/* -------------------- Static Types -------------------- */

/**
 * @brief Script callback registered for simulated input events.
 */
typedef struct {
    ti_handle_t handle;    /* Runtime that registered the callback */
    char       *func_name; /* TI callback function name (owned) */
} event_listener_t;

/* -------------------- Static Variables -------------------- */

/* Registered input listener (NULL until a script calls register_event) */
static event_listener_t *s_listener = NULL;

/* Guards s_listener: registered on the script thread, read on the input thread */
static pthread_mutex_t s_listener_mutex = PTHREAD_MUTEX_INITIALIZER;

/* -------------------- Static Function Prototypes -------------------- */

static value_t *builtin_register_event(ti_handle_t handle, value_t **argv, int argc);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Built-in native register_event function: bind simulated input events to a TI callback.
 * @param handle Calling runtime handle, stored so events can be posted to it later.
 * @param argv Array of argument values (callback function name).
 * @param argc Number of arguments passed.
 * @return Null value_t.
 */
static value_t *builtin_register_event(ti_handle_t handle, value_t **argv, int argc)
{
    (void)argc;

    const char *func_name = argv[0]->func_name;
    if (func_name == NULL || func_name[0] == '\0') {
        ti_log("[ERROR]: register_event() expects a non-empty function name\n");
        return val_new_null();
    }

    /* Copy the name before taking the lock to keep the critical section short */
    char *name_copy = ti_raw_strdup(func_name);
    if (name_copy == NULL) {
        ti_log("[ERROR]: register_event() out of memory\n");
        return val_new_null();
    }

    pthread_mutex_lock(&s_listener_mutex);

    /* Allocate the listener on first registration */
    if (s_listener == NULL) {
        s_listener = (event_listener_t *)ti_raw_calloc(1, sizeof(event_listener_t));
    }

    /* Replace any previous registration (possibly from an already destroyed runtime) */
    if (s_listener != NULL) {
        ti_raw_free(s_listener->func_name);
        s_listener->func_name = name_copy;
        s_listener->handle = handle;
        name_copy = NULL;
    }

    pthread_mutex_unlock(&s_listener_mutex);

    /* Listener allocation failed: the copy was not handed over */
    if (name_copy != NULL) {
        ti_raw_free(name_copy);
        ti_log("[ERROR]: register_event() out of memory\n");
    }
    return val_new_null();
}

/* -------------------- Public Functions -------------------- */

/* Register register_event() */
void builtin_event_register(void)
{
    static param_t register_event_params[] = { { VAL_FUNC, "func_name", VAL_NULL } };

    register_builtin_function("register_event", VAL_VOID, register_event_params, 1, builtin_register_event);
}

/* Post a simulated input event carrying text to the registered TI callback */
ti_status_t cli_simulate_event(const char *text)
{
    ti_status_t status = TI_ERR_STALE_HANDLE;

    pthread_mutex_lock(&s_listener_mutex);
    if (s_listener != NULL) {
        /* Ownership of the argument value passes to ti_post_event in every case */
        value_t *args[1] = { val_new_string(text != NULL ? text : "") };
        status = ti_post_event(s_listener->handle, s_listener->func_name, args, 1);

        /* Lazy deregistration: the runtime is gone, so drop the listener */
        if (status == TI_ERR_STALE_HANDLE) {
            ti_raw_free(s_listener->func_name);
            ti_raw_free(s_listener);
            s_listener = NULL;
        }
    }
    pthread_mutex_unlock(&s_listener_mutex);

    return status;
}
