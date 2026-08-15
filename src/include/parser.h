#ifndef PARSER_H
#define PARSER_H
#include "token.h"
#include "AST.h"
#include "lexer.h"
typedef struct PARSER_STRUCT {
  lexer_t *lexer;
  token_t *current_token;
} parser_t;

parser_t *init_parser(lexer_t *lexer);
void parser_eat(parser_t *parser, int expected_type);
ast_t *parser_parse(parser_t *parser); // return whole ast tree of source code
ast_t *parser_parse_statement(parser_t *parser);
ast_t *parser_parse_statements(parser_t *parser);
ast_t *parser_parse_variable_definition(parser_t *parser);
ast_t *parser_parse_function_call(parser_t *parser, char *func_name);
ast_t *parser_parse_assignment(parser_t *parser, ast_t *target);
ast_t *parser_parse_while_statement(parser_t *parser);

ast_t *parser_parse_primary(parser_t *parser); // Litterals,identifier,function_call, grouping
ast_t *parser_parse_term(parser_t *parser); // Multiplicative expression (*,/)
ast_t *parser_parse_expr(parser_t *parser); // Additive Expressions (+,-)
#endif // !PARSER_H
