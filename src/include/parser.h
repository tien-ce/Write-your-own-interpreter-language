#ifndef PARSER_H
#define PARSER_H

#include "token.h"
#include "AST.h"
#include "lexer.h"

/* -------------------- Parser Structure -------------------- */

typedef struct PARSER_STRUCT {
  lexer_t *lexer;
  token_t *current_token;
} parser_t;

/* -------------------- Public Functions -------------------- */

/**
 * @brief Initialize a new parser using the given lexer.
 * @param lexer Pointer to initialized lexer.
 * @return Pointer to newly allocated parser_t.
 */
parser_t *init_parser(lexer_t *lexer);

/**
 * @brief Consume expected token type or trigger fatal syntax error.
 * @param parser Parser instance.
 * @param expected_type Expected token type.
 */
void parser_eat(parser_t *parser, int expected_type);

/**
 * @brief Parse the entire source code into a program AST.
 * @return Root compound AST node.
 */
ast_t *parser_parse(parser_t *parser);

/**
 * @brief Parse a single statement.
 */
ast_t *parser_parse_statement(parser_t *parser);

/**
 * @brief Parse a block/compound statement enclosed in braces.
 */
ast_t *parser_parse_statements(parser_t *parser);

/**
 * @brief Parse top-level program statements.
 */
ast_t *parser_parse_main_program(parser_t *parser);

/* Expressions */

/**
 * @brief Parse primary expressions (literals, identifiers, groupings, function calls).
 */
ast_t *parser_parse_primary(parser_t *parser);

/**
 * @brief Parse multiplicative expressions (*, /).
 */
ast_t *parser_parse_term(parser_t *parser);

/**
 * @brief Parse additive expressions (+, -).
 */
ast_t *parser_parse_additive(parser_t *parser);

/**
 * @brief Parse comparison/relational expressions (==, !=, <, <=, >, >=).
 */
ast_t *parser_parse_comparison(parser_t *parser);

/**
 * @brief Parse logical expressions (||, &&).
 */
ast_t *parser_parse_expr(parser_t *parser);

/* Statements needing semicolon */

/**
 * @brief Parse variable definition statement (e.g. int x = 5;).
 */
ast_t *parser_parse_variable_definition(parser_t *parser);

/**
 * @brief Parse function call expression/statement.
 * @param func_name Callee function name.
 */
ast_t *parser_parse_function_call(parser_t *parser, char *func_name);

/**
 * @brief Parse variable assignment statement (e.g. x = 10;).
 * @param target Target AST node for assignment.
 */
ast_t *parser_parse_assignment(parser_t *parser, ast_t *target);

/* Statements not needing semicolon */

/**
 * @brief Parse while loop statement.
 */
ast_t *parser_parse_while_statement(parser_t *parser);

/**
 * @brief Parse if/else statement.
 */
ast_t *parser_parse_if_statement(parser_t *parser);

#endif // !PARSER_H
