#include "include/parser.h"
#include "include/AST.h"
#include "include/lexer.h"
#include "include/token.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
parser_t *init_parser(lexer_t *lexer)
{
  parser_t *parser = calloc(1, sizeof(struct PARSER_STRUCT));
  parser->lexer = lexer;
  parser->current_token= lexer_get_next_token(parser->lexer);
  return parser;
}
void parser_eat(parser_t *parser, int expected_type)
{
  if (parser->current_token->type == expected_type)
  {
    parser->current_token = lexer_get_next_token(parser->lexer);
  }
  else
  {
      printf(
          "Unexpected value %s with type %d",
          parser->current_token->value,
          parser->current_token->type
      );
      exit(1);
  }
}

ast_t *parser_parse(parser_t *parser)
{
  return parser_parse_statements(parser);
}

ast_t *parser_parse_statement(parser_t *parser)
{
  switch(parser->current_token->type)
  {
    case TOKEN_KW_INT:
    case TOKEN_KW_FLOAT:
    case TOKEN_KW_STRING:
      return parser_parse_variable_definition(parser);
    case TOKEN_ID:
    {
      ast_t *expr = parser_parse_expr(parser);
      if (parser->current_token->type == TOKEN_EQUALS)
        return parser_parse_assignment(parser, expr);
      return expr;
    }
    case TOKEN_KW_WHILE:
      return parser_parse_while_statement(parser);
    default:
      return NULL;
  }
}

ast_t *parser_parse_statements(parser_t *parser)
{
  ast_t **compound_value = calloc(1, sizeof(struct AST_STRUCT*));
  ast_t *compound = init_ast(AST_COMPOUND);
  compound->value.compound.compound_value = compound_value;
  // Parse first statement
  ast_t *statement = parser_parse_statement(parser);
  compound->value.compound.compound_value[0] = statement; 
  compound->value.compound.compound_size = 1;
  
  // When still end with semicolon 
  while(parser->current_token->type == TOKEN_SEMI)
  {
    parser_eat(parser, TOKEN_SEMI); // Eat ;
    ast_t *statement = parser_parse_statement(parser);
    // Might be end file 
    if (!statement)
      break;
    int size = compound->value.compound.compound_size;
    // Add new statement 
    compound->value.compound.compound_value = realloc(
     compound->value.compound.compound_value,
     (size + 1) * sizeof(struct AST_STRUCT)
    );
    compound->value.compound.compound_value[size] = statement; 
    compound->value.compound.compound_size += 1;
  }
  return compound;
}

ast_t *parser_parse_expr(parser_t *parser)
{
  switch(parser->current_token->type)
  {
    case TOKEN_INT:
    {
      ast_t *int_node = init_ast(AST_INT_LITERAL);
      int_node->value.int_value = atoi(parser->current_token->value);
      parser_eat(parser,TOKEN_INT);
      return int_node;
    }
    case TOKEN_STRING:
    {
      ast_t *string_node = init_ast(AST_STRING_LITERAL);
      string_node->value.string_value = parser->current_token->value;
      parser_eat(parser,TOKEN_STRING);
      return string_node;
    }
    case TOKEN_ID:
    {
      char *id_value = parser->current_token->value;
      parser_eat(parser, TOKEN_ID);
      // '('
      if (parser->current_token->type == TOKEN_LPAREN)
      {
        return parser_parse_function_call(parser, id_value);
      }
      ast_t *id_node = init_ast(AST_IDENTIFIER);
      id_node->value.identifier = id_value;
      return id_node;
    }
  }

}

ast_t *parser_parse_variable_definition(parser_t *parser)
{
  /* int a = (expression); */
  //int
  char *variable_type = parser->current_token->value;
  parser_eat(parser, parser->current_token->type);
  // a
  char *variable_name = parser->current_token->value;
  parser_eat(parser, TOKEN_ID);
  // = 
  parser_eat(parser, TOKEN_EQUALS);
  // expression 
  ast_t *value = parser_parse_expr(parser);
  ast_t *var_def_node = init_ast(AST_VARIABLE_DEFINITION);
  var_def_node->value.variable_definition.variable_type = variable_type;
  var_def_node->value.variable_definition.variable_name = variable_name;
  var_def_node->value.variable_definition.value = value;
  return var_def_node;
}

ast_t *parser_parse_while_statement(parser_t *parser)
{
  /* while(expression) { compound } */
  // while
  parser_eat(parser, TOKEN_KW_WHILE);
  // ( left parenthesis
  parser_eat(parser, TOKEN_LPAREN);
  // expression
  ast_t *condition = parser_parse_expr(parser);
  // right parenthesis
  parser_eat(parser, TOKEN_RPAREN);
  // { left brace 
  parser_eat(parser, TOKEN_LBRACE);
  // compound 
  ast_t *body = parser_parse_statements(parser);
  // } Right brace
  parser_eat(parser, TOKEN_RBRACE);
  ast_t * while_node = init_ast(AST_WHILE_STATEMENT);
  while_node->value.while_statement.condition = condition;
  while_node->value.while_statement.body = body;
  return while_node;
}

ast_t *parser_parse_function_call(parser_t *parser, char *func_name)
{
  // ( 
  parser_eat(parser, TOKEN_LPAREN);
  ast_t **args = NULL;
  int num_arg = 0;
  ast_t *func = init_ast(AST_IDENTIFIER);
  func->value.identifier = func_name;
  /* fuc_name(arg1,arg2,...) */
  if(parser->current_token->type != TOKEN_RPAREN)
  {
    args = calloc(1, sizeof(struct AST_STRUCT*));
    ast_t *arg_node = parser_parse_expr(parser);
    args[num_arg] = arg_node;
    num_arg++;
  }
  while(parser->current_token->type == TOKEN_COMMA)
  {
    //','
    parser_eat(parser,TOKEN_COMMA);
    printf("%s\t", parser->current_token->value);
    args = realloc(args, (num_arg + 1) * sizeof(struct AST_STRUCT*));
    ast_t *arg_node = parser_parse_expr(parser);
    args[num_arg] = arg_node;
    num_arg++;
  }
  //')'
  parser_eat(parser,TOKEN_RPAREN);
  ast_t *func_call_node = init_ast(AST_FUNCTION_CALL);
  func_call_node->value.function_call.func = func;
  func_call_node->value.function_call.args = args;
  func_call_node->value.function_call.num_arg = num_arg;
  return func_call_node;
}

ast_t *parser_parse_assignment(parser_t *parser, ast_t *target)
{
  /* target = expr */
  // =
  parser_eat(parser, TOKEN_EQUALS);
  // expression
  ast_t *value = parser_parse_expr(parser);
  ast_t *assignment_node = init_ast(AST_ASSIGNMENT);
  assignment_node->value.assignment.id = target;
  assignment_node->value.assignment.value = value;
  return assignment_node;
}
