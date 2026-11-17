#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdbool.h>
#include "../src/TienInterpreter.h"
#include "builtin/builtin.h"
#include "module/module.h"

/* -------------------- Constants & State -------------------- */

/* How often the event loop re-checks whether the script finished */
#define POLL_INTERVAL_MS 50

/* Extension appended to a module name to find its file ('import lib;' -> lib.ti) */
#define MODULE_FILE_EXT ".ti"

static atomic_bool s_script_done = false;

/**
 * @brief What the script thread runs: the source buffer (handed to ti_run_string_take) and its path.
 */
typedef struct {
    char       *source; /* Allocated with ti_raw_malloc; ti_run_string_take frees it */
    const char *path;   /* Path given on the command line (argv[1], lives for the whole program) */
} script_job_t;

/* -------------------- Static Function Prototypes -------------------- */

static char *read_string_from_file(const char *path);
static ti_status_t cli_module_loader(const char *importer_path, const char *name, char **text_out, char **path_out);
static void *script_thread(void *arg);
static void wait_for_events(void);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Read an entire file into a null-terminated buffer allocated with ti_raw_malloc.
 * @param path Path to source file.
 * @return The buffer (free it with ti_raw_free, or hand it to the library), or NULL with errno set
 *         (ENOENT if the file does not exist, ENOMEM if out of memory).
 */
static char *read_string_from_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL; /* errno set by fopen */
    }

    /* Step 1: find the length of the file */
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    /* Step 2: the library frees this buffer with ti_raw_free, so it comes from ti_raw_malloc */
    char *contents = ti_raw_malloc((size_t)length + 1);
    if (!contents) {
        fclose(file);
        errno = ENOMEM;
        return NULL;
    }

    /* Step 3: read everything; a short read means the file could not be read */
    if (fread(contents, 1, (size_t)length, file) != (size_t)length) {
        ti_raw_free(contents);
        fclose(file);
        errno = EIO;
        return NULL;
    }
    contents[length] = '\0';

    fclose(file);
    return contents;
}

/**
 * @brief Module loader of the CLI: 'import lib;' loads lib.ti and 'import lib.count;' loads lib/count.ti,
 * from the directory of the main script (every module name is relative to it).
 * @param main_path Path of the main script (NULL: the current directory).
 * @param name Module name written after 'import'.
 * @param text_out Receives the module source (ti_raw_malloc), only on TI_OK.
 * @param path_out Receives the module path (ti_raw_malloc), only on TI_OK.
 * @return TI_OK, TI_ERR_UNDEFINED if the file does not exist, TI_ERR_NO_MEMORY, or TI_ERR_RUNTIME
 *         if the file exists but cannot be read.
 */
static ti_status_t cli_module_loader(const char *main_path, const char *name, char **text_out, char **path_out)
{
    /* Step 1: the directory part of the main script's path, '/' included ("" when there is none) */
    size_t dir_len = 0;
    if (main_path != NULL) {
        const char *slash = strrchr(main_path, '/');
        if (slash != NULL) {
            dir_len = (size_t)(slash - main_path) + 1;
        }
    }

    /* Step 2: build "<dir><name>.ti" in a ti_raw_malloc buffer (the library frees it with ti_raw_free);
     * each '.' of the module name is a directory level ("lib.count" -> "lib/count.ti") */
    size_t name_len = strlen(name);
    size_t ext_len = strlen(MODULE_FILE_EXT);
    char *path = ti_raw_malloc(dir_len + name_len + ext_len + 1);
    if (path == NULL) {
        return TI_ERR_NO_MEMORY;
    }
    if (dir_len > 0) {
        memcpy(path, main_path, dir_len); /* main_path may be NULL only when dir_len is 0 */
    }
    for (size_t i = 0; i < name_len; i++) {
        path[dir_len + i] = (name[i] == '.') ? '/' : name[i];
    }
    memcpy(path + dir_len + name_len, MODULE_FILE_EXT, ext_len + 1);

    /* Step 3: read the source; on failure nothing is handed over, so the path is freed here */
    char *text = read_string_from_file(path);
    if (text == NULL) {
        int error = errno;
        ti_raw_free(path);
        if (error == ENOENT) {
            return TI_ERR_UNDEFINED; /* reported by the library as 'Unknown module' */
        }
        return (error == ENOMEM) ? TI_ERR_NO_MEMORY : TI_ERR_RUNTIME;
    }

    /* Step 4: success: the library owns both buffers from here on */
    *text_out = text;
    *path_out = path;
    return TI_OK;
}

/**
 * @brief Script worker: run the Ti script, then flag completion so the main thread can exit.
 * @param arg The script_job_t (its source buffer is consumed by ti_run_string_take).
 * @return Always NULL.
 */
static void *script_thread(void *arg)
{
    script_job_t *job = (script_job_t *)arg;
    ti_run_string_take(job->source, job->path);
    job->source = NULL; /* freed by ti_run_string_take */
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
            if (line[0] == '!') {
                /* "!text": synchronous call, wait for the handler's answer (ti_call) */
                char answer[128];
                ti_status_t status = cli_simulate_call(line + 1, answer, sizeof(answer));
                printf("[main] call \"%s\" -> status %d, answer \"%s\"\n", line + 1, (int)status, answer);
            } else {
                /* "text": asynchronous event, queued and delivered at the script's next safe point */
                ti_status_t status = cli_simulate_event(line);
                printf("[main] event \"%s\" -> status %d%s\n", line, (int)status,
                       atomic_load(&s_script_done) ? " (script finished)" : "");
            }
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
    ti_init_modules();

    /* 'import <name>;' of a name no native module has loads <name>.ti next to the importing file */
    ti_register_module_loader(cli_module_loader);

    /* The path goes with the source: imports of the main script are resolved from its directory */
    script_job_t job = { .source = read_string_from_file(argv[1]), .path = argv[1] };
    if (job.source == NULL) {
        printf("Could not open file: %s\n", argv[1]);
        return 1;
    }

    /* Child thread runs the script; the main thread services events */
    pthread_t script_id;
    if (pthread_create(&script_id, NULL, script_thread, &job) != 0) {
        printf("Could not start script thread\n");
        ti_raw_free(job.source);
        return 1;
    }
    /* Runs until stdin closes, even after the script has finished */
    wait_for_events();
    pthread_join(script_id, NULL);
    return 0;
}
