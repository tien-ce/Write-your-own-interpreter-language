#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include "../src/TienInterpreter.h"

/* -------------------- Static Function Prototypes -------------------- */

static char *read_string_from_file(const char *path);

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

    char *contents = tracked_malloc(length + 1);
    fread(contents, 1, length, file);
    contents[length] = '\0';

    fclose(file);
    return contents;
}

void* do_something(void *arg)
{
  const char *contents = (char*) arg;
  ti_run_string(contents);
  return NULL;
}
/* -------------------- Main Entry Point -------------------- */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("Usage: %s <file.ti>\n", argv[0]);
        exit(1);
    }
    ti_init_builtin();
    char *contents = "while(true){print(\"ABC\");\ndelay(1000);}";
    //pthread_t thread_id;
    //pthread_create(&thread_id, NULL, do_something, (void*)contents);
    //sleep(3);
    printf("Main: Created thread successfully.\n");
    contents = read_string_from_file(argv[1]);
    ti_run_string(contents);
    tracked_free(contents);
    //pthread_join(thread_id,NULL);
    printf("Main: Thread finished execution");
    return 0;
}
