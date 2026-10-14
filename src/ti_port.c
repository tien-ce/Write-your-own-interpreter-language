#include "include/ti_port.h"

#include <stdlib.h>

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#elif defined(_WIN32)
#include <windows.h>
#else
#include <errno.h>
#include <pthread.h>
#include <time.h>
#endif

/* -------------------- ESP32 / FreeRTOS -------------------- */
#if defined(ESP_PLATFORM)

struct ti_mutex  { SemaphoreHandle_t h; };
struct ti_signal { SemaphoreHandle_t h; };

/**
 * @brief Convert a millisecond timeout into scheduler ticks.
 * @param ms Timeout in ms or TI_PORT_WAIT_FOREVER.
 * @return Tick count suitable for xSemaphoreTake.
 */
static TickType_t port_ticks(uint32_t ms)
{
    if (ms == TI_PORT_WAIT_FOREVER) {
        return portMAX_DELAY;
    }
    return pdMS_TO_TICKS(ms);
}

ti_caller_t ti_current_caller(void)
{
    return (ti_caller_t)xTaskGetCurrentTaskHandle();
}

uint32_t ti_millis(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

ti_mutex_t *ti_mutex_create(void)
{
    ti_mutex_t *m = malloc(sizeof(*m));
    if (!m) {
        return NULL;
    }
    /* Standard mutex: non-recursive, with priority inheritance. */
    m->h = xSemaphoreCreateMutex();
    if (!m->h) {
        free(m);
        return NULL;
    }
    return m;
}

void ti_mutex_destroy(ti_mutex_t *m)
{
    if (!m) {
        return;
    }
    vSemaphoreDelete(m->h);
    free(m);
}

bool ti_mutex_lock(ti_mutex_t *m, uint32_t timeout_ms)
{
    return m && xSemaphoreTake(m->h, port_ticks(timeout_ms)) == pdTRUE;
}

void ti_mutex_unlock(ti_mutex_t *m)
{
    if (m) {
        xSemaphoreGive(m->h);
    }
}

ti_signal_t *ti_signal_create(void)
{
    ti_signal_t *s = malloc(sizeof(*s));
    if (!s) {
        return NULL;
    }
    s->h = xSemaphoreCreateBinary();
    if (!s->h) {
        free(s);
        return NULL;
    }
    return s;
}

void ti_signal_destroy(ti_signal_t *s)
{
    if (!s) {
        return;
    }
    vSemaphoreDelete(s->h);
    free(s);
}

void ti_signal_give(ti_signal_t *s)
{
    if (!s) {
        return;
    }
    /* Binary semaphore: a give on an already-set signal is a no-op. */
    if (xPortInIsrContext()) {
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(s->h, &woken);
        if (woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    } else {
        xSemaphoreGive(s->h);
    }
}

bool ti_signal_take(ti_signal_t *s, uint32_t timeout_ms)
{
    return s && xSemaphoreTake(s->h, port_ticks(timeout_ms)) == pdTRUE;
}

/* -------------------- Windows -------------------- */
#elif defined(_WIN32)

/* Both primitives are counting semaphores capped at 1, so the mutex is
 * non-recursive (consistent with the other ports) and the signal is binary. */
struct ti_mutex  { HANDLE h; };
struct ti_signal { HANDLE h; };

/**
 * @brief Convert a millisecond timeout into a Win32 wait value.
 * @param ms Timeout in ms or TI_PORT_WAIT_FOREVER.
 * @return Value for WaitForSingleObject.
 */
static DWORD port_wait(uint32_t ms)
{
    return ms == TI_PORT_WAIT_FOREVER ? INFINITE : (DWORD)ms;
}

ti_caller_t ti_current_caller(void)
{
    return (ti_caller_t)GetCurrentThreadId();
}

uint32_t ti_millis(void)
{
    return (uint32_t)GetTickCount64();
}

ti_mutex_t *ti_mutex_create(void)
{
    ti_mutex_t *m = malloc(sizeof(*m));
    if (!m) {
        return NULL;
    }
    m->h = CreateSemaphoreW(NULL, 1, 1, NULL);
    if (!m->h) {
        free(m);
        return NULL;
    }
    return m;
}

void ti_mutex_destroy(ti_mutex_t *m)
{
    if (!m) {
        return;
    }
    CloseHandle(m->h);
    free(m);
}

bool ti_mutex_lock(ti_mutex_t *m, uint32_t timeout_ms)
{
    return m && WaitForSingleObject(m->h, port_wait(timeout_ms)) == WAIT_OBJECT_0;
}

void ti_mutex_unlock(ti_mutex_t *m)
{
    if (m) {
        ReleaseSemaphore(m->h, 1, NULL);
    }
}

ti_signal_t *ti_signal_create(void)
{
    ti_signal_t *s = malloc(sizeof(*s));
    if (!s) {
        return NULL;
    }
    s->h = CreateSemaphoreW(NULL, 0, 1, NULL);
    if (!s->h) {
        free(s);
        return NULL;
    }
    return s;
}

void ti_signal_destroy(ti_signal_t *s)
{
    if (!s) {
        return;
    }
    CloseHandle(s->h);
    free(s);
}

void ti_signal_give(ti_signal_t *s)
{
    if (s) {
        /* Count is capped at 1, so a give on a set signal fails harmlessly. */
        ReleaseSemaphore(s->h, 1, NULL);
    }
}

bool ti_signal_take(ti_signal_t *s, uint32_t timeout_ms)
{
    return s && WaitForSingleObject(s->h, port_wait(timeout_ms)) == WAIT_OBJECT_0;
}

/* -------------------- POSIX -------------------- */
#else

struct ti_mutex  { pthread_mutex_t m; };
struct ti_signal {
    pthread_mutex_t m;
    pthread_cond_t  c;
    bool            set;
};

/**
 * @brief Build an absolute deadline `ms` from now on the given clock.
 * @param clk Clock id (CLOCK_REALTIME or CLOCK_MONOTONIC).
 * @param ms  Offset in milliseconds.
 * @param out Receives the absolute time.
 */
static void port_deadline(clockid_t clk, uint32_t ms, struct timespec *out)
{
    clock_gettime(clk, out);
    out->tv_sec  += ms / 1000;
    out->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (out->tv_nsec >= 1000000000L) {
        out->tv_sec  += 1;
        out->tv_nsec -= 1000000000L;
    }
}

ti_caller_t ti_current_caller(void)
{
    return (ti_caller_t)pthread_self();
}

uint32_t ti_millis(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

ti_mutex_t *ti_mutex_create(void)
{
    ti_mutex_t *m = malloc(sizeof(*m));
    if (!m) {
        return NULL;
    }
    /* Default (non-recursive) mutex. */
    if (pthread_mutex_init(&m->m, NULL) != 0) {
        free(m);
        return NULL;
    }
    return m;
}

void ti_mutex_destroy(ti_mutex_t *m)
{
    if (!m) {
        return;
    }
    pthread_mutex_destroy(&m->m);
    free(m);
}

bool ti_mutex_lock(ti_mutex_t *m, uint32_t timeout_ms)
{
    if (!m) {
        return false;
    }
    if (timeout_ms == TI_PORT_WAIT_FOREVER) {
        return pthread_mutex_lock(&m->m) == 0;
    }
    if (timeout_ms == 0) {
        return pthread_mutex_trylock(&m->m) == 0;
    }
    struct timespec ts;
    port_deadline(CLOCK_REALTIME, timeout_ms, &ts);
    return pthread_mutex_timedlock(&m->m, &ts) == 0;
}

void ti_mutex_unlock(ti_mutex_t *m)
{
    if (m) {
        pthread_mutex_unlock(&m->m);
    }
}

ti_signal_t *ti_signal_create(void)
{
    ti_signal_t *s = malloc(sizeof(*s));
    if (!s) {
        return NULL;
    }
    pthread_condattr_t attr;
    if (pthread_mutex_init(&s->m, NULL) != 0) {
        free(s);
        return NULL;
    }
    if (pthread_condattr_init(&attr) != 0) {
        pthread_mutex_destroy(&s->m);
        free(s);
        return NULL;
    }
    /* Monotonic clock so wall-clock jumps cannot stretch or shrink timeouts. */
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    int rc = pthread_cond_init(&s->c, &attr);
    pthread_condattr_destroy(&attr);
    if (rc != 0) {
        pthread_mutex_destroy(&s->m);
        free(s);
        return NULL;
    }
    s->set = false;
    return s;
}

void ti_signal_destroy(ti_signal_t *s)
{
    if (!s) {
        return;
    }
    pthread_cond_destroy(&s->c);
    pthread_mutex_destroy(&s->m);
    free(s);
}

void ti_signal_give(ti_signal_t *s)
{
    if (!s) {
        return;
    }
    pthread_mutex_lock(&s->m);
    s->set = true;
    pthread_cond_signal(&s->c);
    pthread_mutex_unlock(&s->m);
}

bool ti_signal_take(ti_signal_t *s, uint32_t timeout_ms)
{
    if (!s) {
        return false;
    }

    struct timespec ts;
    if (timeout_ms != TI_PORT_WAIT_FOREVER && timeout_ms != 0) {
        port_deadline(CLOCK_MONOTONIC, timeout_ms, &ts);
    }

    bool got = false;
    pthread_mutex_lock(&s->m);
    /* Loop to absorb spurious wakeups; exit on set, timeout or poll. */
    while (!s->set) {
        if (timeout_ms == 0) {
            break;
        }
        if (timeout_ms == TI_PORT_WAIT_FOREVER) {
            pthread_cond_wait(&s->c, &s->m);
        } else if (pthread_cond_timedwait(&s->c, &s->m, &ts) == ETIMEDOUT) {
            break;
        }
    }
    if (s->set) {
        s->set = false;
        got = true;
    }
    pthread_mutex_unlock(&s->m);
    return got;
}

#endif
