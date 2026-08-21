#include "TienInterpreter.h"

void ti_run_string(const char *source_code)
{
  if (!source_code)
  {
    return;
  }

  char *contents = tracked_strdup(source_code);
  if (!contents)
  {
    ti_fatal();
    return;
  }

  lexer_t *lexer = init_lexer(contents);
  parser_t *parser = init_parser(lexer);
  ast_t *root = parser_parse(parser);
  context_t *context = init_interpreter_context();

  visitor_visit(context, root);

  tracked_free(parser);
  free_context(context);
  tracked_free((void*)lexer);
  free_ast(root);
  tracked_free(contents);
}
