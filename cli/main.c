#include <stdio.h>
#include <stdlib.h>
#include "../src/TienInterpreter.h"

/* -------------------- Static Functions -------------------- */

/**
 * @brief Read entire file into a null-terminated dynamically allocated string.
 * @param path Path to source file.
 * @return Tracked buffer containing file contents.
 */
static char *read_string_from_file(const char *path)
{
  FILE *file = fopen(path, "rb");
  if (!file)
  {
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

/* -------------------- Main Entry Point -------------------- */

int main(int argc, char *argv[])
{
  if (argc < 2)
  {
    printf("Usage: %s <file.ti>\n", argv[0]);
    exit(1);
  }

  char *contents = read_string_from_file(argv[1]);
  init_builtin();
  ti_run_string(contents);
  tracked_free(contents);
  return 0;
}
