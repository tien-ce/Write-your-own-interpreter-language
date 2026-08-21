#include <stdio.h>
#include <stdlib.h>
#include "../src/TienInterpreter.h"

extern void ast_draw(ast_t *node);

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

int main(int argc, char* argv[])
{
  if (argc < 2)
  {
    printf("Usage: %s <file.ti>\n", argv[0]);
    exit(1);
  }

  char *contents = read_string_from_file(argv[1]);
  ti_run_string(contents);
  tracked_free(contents);
  return 0;
}
