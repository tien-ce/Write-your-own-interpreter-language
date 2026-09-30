#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdbool.h>
#include "../src/TienInterpreter.h"

/* -------------------- Constants & State -------------------- */

/* How often the event loop re-checks whether the script finished */
#define POLL_INTERVAL_MS 50

static atomic_bool s_script_done = false;

/* -------------------- External Function Prototypes -------------------- */

/* Defined in built_in_functions.c: post a simulated input event to the registered callback */
ti_status_t cli_simulate_event(const char *text);

/* -------------------- Static Function Prototypes -------------------- */

static char *read_string_from_file(const char *path);
static void *script_thread(void *arg);
static void wait_for_events(void);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Read entire file into a null-terminated dynamically allocated string.
 * @param path Path to source file.
 * @return Tracked buffer containing file contents.
 */
static char *read_string_from_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        printf("Could not open file: %s\n", path);
        exit(1);
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *contents = malloc(length + 1);
    fread(contents, 1, length, file);
    contents[length] = '\0';

    fclose(file);
    return contents;
}

/**
 * @brief Script worker: run the Ti script, then flag completion so the main thread can exit.
 * @param arg Null-terminated script source.
 * @return Always NULL.
 */
static void *script_thread(void *arg)
{
    ti_run_string((const char *)arg);
    atomic_store(&s_script_done, true);
    return NULL;
}

/**
 * @brief Wait for stdin lines and forward each one to the script as a simulated event.
 * Polls the raw descriptor (not stdio) so the fatal callback's exit() cannot deadlock on the
 * stdin lock, and wakes periodically to notice script completion.
 */
static void wait_for_events(void)
{
    char line[256];
    size_t length = 0;
    struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };

    /* Keep serving stdin after the script ends: late events must hit the stale-handle path */
    for (;;) {
        /* Timeout keeps the loop responsive to script completion */
        if (poll(&pfd, 1, POLL_INTERVAL_MS) <= 0) {
            continue;
        }

        char ch;
        if (read(STDIN_FILENO, &ch, 1) != 1) {
            return; /* stdin closed: no more events can arrive */
        }

        /* A newline completes the event text (over-long lines are truncated, not split) */
        if (ch == '\n') {
            line[length] = '\0';
            ti_status_t status = cli_simulate_event(line);
            printf("[main] event \"%s\" -> status %d%s\n", line, (int)status,
                   atomic_load(&s_script_done) ? " (script finished)" : "");
            length = 0;
        } else if (ch != '\r' && length < sizeof(line) - 1) {
            line[length++] = ch;
        }
    }
}

/* -------------------- Main Entry Point -------------------- */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: %s <file.ti>\n", argv[0]);
        exit(1);
    }
    ti_init_builtin();
    char *contents = read_string_from_file(argv[1]);

    /* Child thread runs the script; the main thread services events */
    pthread_t script_id;
    if (pthread_create(&script_id, NULL, script_thread, contents) != 0) {
        printf("Could not start script thread\n");
        free(contents);
        return 1;
    }
    /* Runs until stdin closes, even after the script has finished */
    wait_for_events();
    pthread_join(script_id, NULL);

    free(contents);
    return 0;
}
