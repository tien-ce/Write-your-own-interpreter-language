#include <stdio.h>
#include <stdlib.h>
#include "include/lexer.h"
#include "include/parser.h"
#include "include/visitor.h"
#include "include/tracked_memory.h"

extern void ast_draw(ast_t *node);

static char *read_string_from_file(const char *path);

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
  lexer_t *lexer = init_lexer(contents);
  parser_t *parser = init_parser(lexer);
  ast_t *root = parser_parse(parser);
  context_t *context = init_interpreter_context();
  visitor_visit(context,root);
  //ast_draw(root);
  tracked_free(parser);
  free_context(context);
  tracked_free((void*)lexer);
  free_ast(root);
  tracked_free(contents);
	return 0;
}
