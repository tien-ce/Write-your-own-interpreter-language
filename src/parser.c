#include "include/parser.h"
#include "include/AST.h"
#include "include/lexer.h"
#include "include/token.h"
#include "include/tracked_memory.h"
#include "TienInterpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------- Static Function Prototypes -------------------- */

static int token_type_to_op(int token_type);
static token_t *parser_peek(parser_t *parser);
static void parser_eat(parser_t *parser, int expected_type);
static ast_t *parser_parse_statement(parser_t *parser);
static ast_t *parser_parse_statements(parser_t *parser);
static ast_t *parser_parse_main_program(parser_t *parser);
static ast_t *parser_parse_definition(parser_t *parser);
static ast_t *parser_parse_function_definition(parser_t *parser);
static ast_t *parser_parse_variable_definition(parser_t *parser);
static ast_t *parser_parse_assignment(parser_t *parser, ast_t *target);
static ast_t *parser_parse_function_call(parser_t *parser, char *func_name);
static ast_t *parser_parse_while_statement(parser_t *parser);
static ast_t *parser_parse_if_statement(parser_t *parser);
static ast_t *parser_parse_expr(parser_t *parser);
static ast_t *parser_parse_comparison(parser_t *parser);
static ast_t *parser_parse_additive(parser_t *parser);
static ast_t *parser_parse_term(parser_t *parser);
static ast_t *parser_parse_primary(parser_t *parser);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Convert token type to binary operator enum.
 * @param token_type Token type enum value.
 * @return Corresponding binary operator enum value.
 */
static int token_type_to_op(int token_type)
{
    switch (token_type) {
    case TOKEN_PLUS:        return OP_ADD;
    case TOKEN_MINUS:       return OP_SUB;
    case TOKEN_STAR:        return OP_MUL;
    case TOKEN_SLASH:       return OP_DIV;
    case TOKEN_DEQUALS:     return OP_DEQ;
    case TOKEN_NOT_EQUALS:  return OP_NEQ;
    case TOKEN_LT:          return OP_LT;
    case TOKEN_GT:          return OP_GT;
    case TOKEN_LTE:         return OP_LTE;
    case TOKEN_GTE:         return OP_GTE;
    case TOKEN_LOGIC_AND:   return OP_LOGICAL_AND;
    case TOKEN_LOGIC_OR:    return OP_LOGICAL_OR;
    default:
        ti_log("Unknown operator token type: %d\n", token_type);
        ti_fatal();
        return -1;
    }
}

/**
 * @brief Peek at the token following the current token without consuming it.
 * @param parser Pointer to parser.
 * @return Next token pointer.
 */
static token_t *parser_peek(parser_t *parser)
{
    lexer_t *temp_lexer = lexer_copy(parser->lexer);
    (void)lexer_get_next_token(temp_lexer);
    token_t *next_token = lexer_get_next_token(temp_lexer);
    tracked_free((void *)temp_lexer);
    return next_token;
}

/**
 * @brief Parse a function definition statement: type func_name(args) { body }.
 * @param parser Pointer to parser.
 * @return AST function definition node.
 */
static ast_t *parser_parse_function_definition(parser_t *parser)
{
    int type;
    switch (parser->current_token->type) {
    case TOKEN_KW_INT:    type = VAR_TYPE_INT;    break;
    case TOKEN_KW_FLOAT:  type = VAR_TYPE_FLOAT;  break;
    case TOKEN_KW_STRING: type = VAR_TYPE_STRING; break;
    case TOKEN_KW_BOOL:   type = VAR_TYPE_BOOL;   break;
    case TOKEN_KW_VOID:   type = VAR_TYPE_VOID;   break;
    default:
        ti_log("[Parser Error] Unexpected type %s in function definition, at line %d\n",
               token_to_str(parser->current_token->type), parser->lexer->line_num);
        ti_log_line(parser->lexer->line);
        ti_fatal();
        break;
    }
    parser_eat(parser, parser->current_token->type);

    char *func_name = parser->current_token->value;
    parser_eat(parser, TOKEN_ID);

    parser_eat(parser, TOKEN_LPAREN);
    ast_t **args = NULL;
    int num_arg = 0;
    if (parser->current_token->type != TOKEN_RPAREN) {
        args = tracked_calloc(1, sizeof(struct AST_STRUCT *));
        ast_t *arg_node = parser_parse_expr(parser);
        args[num_arg] = arg_node;
        num_arg++;
    }
    while (parser->current_token->type == TOKEN_COMMA) {
        parser_eat(parser, TOKEN_COMMA);
        args = tracked_realloc(args, (num_arg + 1) * sizeof(struct AST_STRUCT *));
        ast_t *arg_node = parser_parse_expr(parser);
        args[num_arg] = arg_node;
        num_arg++;
    }
    parser_eat(parser, TOKEN_RPAREN);

    ast_t *statements = parser_parse_statements(parser);
    ast_t *var_def_node = ast_init(AST_FUNCTION_DEFINITION);
    var_def_node->value.function_definition.func_type = type;
    var_def_node->value.function_definition.func_name = func_name;
    var_def_node->value.function_definition.num_args = num_arg;
    var_def_node->value.function_definition.args = args;
    var_def_node->value.function_definition.body = statements;
    return var_def_node;
}

/**
 * @brief Dispatch between variable and function definitions based on lookahead token.
 * @param parser Pointer to parser.
 * @return AST definition node.
 */
static ast_t *parser_parse_definition(parser_t *parser)
{
    token_t *next_token = parser_peek(parser);
    switch ((int)next_token->type) {
    case TOKEN_LPAREN:
        return parser_parse_function_definition(parser);
    case TOKEN_EQUALS:
        return parser_parse_variable_definition(parser);
    default:
        ti_log("Definition: Unexpected token %s at line %d\n",
               token_to_str(next_token->type), parser->lexer->line_num);
        ti_log_line(parser->lexer->line);
        ti_fatal();
        return NULL;
    }
}

/**
 * @brief Consume expected token type or trigger fatal syntax error.
 * @param parser Parser instance.
 * @param expected_type Expected token type enum.
 */
static void parser_eat(parser_t *parser, int expected_type)
{
    if ((int)parser->current_token->type == expected_type) {
        token_t *old_token = parser->current_token;
        parser->current_token = lexer_get_next_token(parser->lexer);
        tracked_free(old_token);
        old_token = NULL;
    } else {
        ti_log("Unexpected value %s, at line %d\n",
               parser->current_token->value ? parser->current_token->value : "<eof>",
               parser->lexer->line_num);
        ti_log_line(parser->lexer->line);
        ti_log("\n");
        ti_log("Expected token %s, but received %s\n",
               token_to_str(expected_type),
               token_to_str(parser->current_token->type));
        ti_fatal();
    }
}

/**
 * @brief Parse a single statement.
 * @param parser Pointer to parser.
 * @return AST node for parsed statement.
 */
static ast_t *parser_parse_statement(parser_t *parser)
{
    switch (parser->current_token->type) {
    case TOKEN_KW_INT:
    case TOKEN_KW_FLOAT:
    case TOKEN_KW_STRING:
    case TOKEN_KW_BOOL:
    case TOKEN_KW_VOID:
        return parser_parse_definition(parser);
    case TOKEN_ID: {
        ast_t *expr = parser_parse_expr(parser);
        if (parser->current_token->type == TOKEN_EQUALS) {
            return parser_parse_assignment(parser, expr);
        }
        /* Function call statement */
        parser_eat(parser, TOKEN_SEMI);
        return expr;
    }
    case TOKEN_KW_WHILE:
        return parser_parse_while_statement(parser);
    case TOKEN_KW_IF:
        return parser_parse_if_statement(parser);
    default:
        ti_log("[Parser] Unexpected statement starting with token type %d ('%s')\n",
               parser->current_token->type,
               parser->current_token->value ? parser->current_token->value : "");
        ti_fatal();
        return NULL;
    }
}

/**
 * @brief Parse a compound block of statements enclosed in braces { ... }.
 * @param parser Pointer to parser.
 * @return AST compound node.
 */
static ast_t *parser_parse_statements(parser_t *parser)
{
    parser_eat(parser, TOKEN_LBRACE);
    ast_t *compound = ast_init(AST_COMPOUND);
    compound->value.compound.compound_value = NULL;
    compound->value.compound.compound_size = 0;

    while (parser->current_token->type != TOKEN_RBRACE) {
        ast_t *statement = parser_parse_statement(parser);
        int size = compound->value.compound.compound_size;
        if (compound->value.compound.compound_value == NULL) {
            compound->value.compound.compound_value = tracked_calloc(1, sizeof(struct AST_STRUCT *));
        } else {
            compound->value.compound.compound_value = tracked_realloc(
                compound->value.compound.compound_value,
                (size + 1) * sizeof(struct AST_STRUCT *)
            );
        }
        compound->value.compound.compound_value[size] = statement;
        compound->value.compound.compound_size += 1;
    }
    parser_eat(parser, TOKEN_RBRACE);
    return compound;
}

/**
 * @brief Parse top-level program statements until EOF.
 * @param parser Pointer to parser.
 * @return Root compound AST node.
 */
static ast_t *parser_parse_main_program(parser_t *parser)
{
    ast_t *compound = ast_init(AST_COMPOUND);
    compound->value.compound.compound_value = NULL;
    compound->value.compound.compound_size = 0;

    while (parser->current_token->type != TOKEN_EOF) {
        ast_t *statement = parser_parse_statement(parser);
        int size = compound->value.compound.compound_size;
        if (compound->value.compound.compound_value == NULL) {
            compound->value.compound.compound_value = tracked_calloc(1, sizeof(struct AST_STRUCT *));
        } else {
            compound->value.compound.compound_value = tracked_realloc(
                compound->value.compound.compound_value,
                (size + 1) * sizeof(struct AST_STRUCT *)
            );
        }
        compound->value.compound.compound_value[size] = statement;
        compound->value.compound.compound_size += 1;
    }
    return compound;
}

/**
 * @brief Parse logical expressions (||, &&).
 * @param parser Pointer to parser.
 * @return AST expression node.
 */
static ast_t *parser_parse_expr(parser_t *parser)
{
    ast_t *left = parser_parse_comparison(parser);
    while (parser->current_token->type == TOKEN_LOGIC_AND ||
           parser->current_token->type == TOKEN_LOGIC_OR) {
        int op = parser->current_token->type;
        parser_eat(parser, op);
        ast_t *right = parser_parse_comparison(parser);
        ast_t *binary_node = ast_init(AST_BINARY_EXPR);
        binary_node->value.binary_expr.op = token_type_to_op(op);
        binary_node->value.binary_expr.left = left;
        binary_node->value.binary_expr.right = right;
        left = binary_node;
    }
    return left;
}

/**
 * @brief Parse comparison expressions (==, !=, <, <=, >, >=).
 * @param parser Pointer to parser.
 * @return AST expression node.
 */
static ast_t *parser_parse_comparison(parser_t *parser)
{
    ast_t *left = parser_parse_additive(parser);
    while (parser->current_token->type == TOKEN_DEQUALS ||
           parser->current_token->type == TOKEN_NOT_EQUALS ||
           parser->current_token->type == TOKEN_LT ||
           parser->current_token->type == TOKEN_LTE ||
           parser->current_token->type == TOKEN_GT ||
           parser->current_token->type == TOKEN_GTE) {
        int op = parser->current_token->type;
        parser_eat(parser, op);
        ast_t *right = parser_parse_additive(parser);
        ast_t *binary_node = ast_init(AST_BINARY_EXPR);
        binary_node->value.binary_expr.op = token_type_to_op(op);
        binary_node->value.binary_expr.left = left;
        binary_node->value.binary_expr.right = right;
        left = binary_node;
    }
    return left;
}

/**
 * @brief Parse additive expressions (+, -).
 * @param parser Pointer to parser.
 * @return AST expression node.
 */
static ast_t *parser_parse_additive(parser_t *parser)
{
    ast_t *left = parser_parse_term(parser);
    while (parser->current_token->type == TOKEN_PLUS ||
           parser->current_token->type == TOKEN_MINUS) {
        int op = parser->current_token->type;
        parser_eat(parser, op);
        ast_t *right = parser_parse_term(parser);
        ast_t *binary_node = ast_init(AST_BINARY_EXPR);
        binary_node->value.binary_expr.op = token_type_to_op(op);
        binary_node->value.binary_expr.left = left;
        binary_node->value.binary_expr.right = right;
        left = binary_node;
    }
    return left;
}

/**
 * @brief Parse multiplicative expressions (*, /).
 * @param parser Pointer to parser.
 * @return AST expression node.
 */
static ast_t *parser_parse_term(parser_t *parser)
{
    ast_t *left = parser_parse_primary(parser);
    while (parser->current_token->type == TOKEN_STAR ||
           parser->current_token->type == TOKEN_SLASH) {
        int op = parser->current_token->type;
        parser_eat(parser, op);
        ast_t *right = parser_parse_primary(parser);
        ast_t *binary_node = ast_init(AST_BINARY_EXPR);
        binary_node->value.binary_expr.op = token_type_to_op(op);
        binary_node->value.binary_expr.left = left;
        binary_node->value.binary_expr.right = right;
        left = binary_node;
    }
    return left;
}

/**
 * @brief Parse primary expressions (literals, identifiers, groupings, function calls).
 * @param parser Pointer to parser.
 * @return AST expression node.
 */
static ast_t *parser_parse_primary(parser_t *parser)
{
    switch (parser->current_token->type) {
    case TOKEN_INT: {
        ast_t *int_node = ast_init(AST_INT_LITERAL);
        int_node->value.int_value = atoi(parser->current_token->value);
        parser_eat(parser, TOKEN_INT);
        return int_node;
    }
    case TOKEN_FLOAT: {
        ast_t *float_node = ast_init(AST_FLOAT_LITERAL);
        float_node->value.float_value = atof(parser->current_token->value);
        parser_eat(parser, TOKEN_FLOAT);
        return float_node;
    }
    case TOKEN_STRING: {
        ast_t *string_node = ast_init(AST_STRING_LITERAL);
        string_node->value.string_value = parser->current_token->value;
        parser_eat(parser, TOKEN_STRING);
        return string_node;
    }
    case TOKEN_BOOL: {
        ast_t *bool_node = ast_init(AST_BOOLEAN);
        if (strcmp(parser->current_token->value, "true") == 0 || 
            strcmp(parser->current_token->value, "1") == 0) {
            bool_node->value.bool_value = 1;
        } else {
            bool_node->value.bool_value = 0;
        } 
        parser_eat(parser, TOKEN_BOOL);
        return bool_node;
    }
    case TOKEN_NOT:
    case TOKEN_PLUS:
    case TOKEN_MINUS: {
        int token_type = parser->current_token->type;
        parser_eat(parser, token_type);

        ast_t *unary_node = ast_init(AST_UNARY_EXPR);
        if (token_type == TOKEN_NOT) {
            unary_node->value.unary_expr.op = OP_NOT;
        } else if (token_type == TOKEN_PLUS) {
            unary_node->value.unary_expr.op = OP_POS;
        } else if (token_type == TOKEN_MINUS) {
            unary_node->value.unary_expr.op = OP_NEG;
        }

        unary_node->value.unary_expr.operand = parser_parse_primary(parser);
        return unary_node;
    }    
    case TOKEN_ID: {
        char *id_name = parser->current_token->value;
        parser_eat(parser, TOKEN_ID);

        /* Function call: id(...) */
        if (parser->current_token->type == TOKEN_LPAREN) {
            return parser_parse_function_call(parser, id_name);
        }

        /* Array index: id[expr] */
        if (parser->current_token->type == TOKEN_LBRACKET) {
            parser_eat(parser, TOKEN_LBRACKET);
            ast_t *index_expr = parser_parse_expr(parser);
            parser_eat(parser, TOKEN_RBRACKET);
            ast_t *array_access_node = ast_init(AST_ARRAY_ACCESS);
            array_access_node->value.array_access.id = id_name;
            array_access_node->value.array_access.index_expr = index_expr;
            return array_access_node;
        }

        /* Simple identifier reference */
        ast_t *variable_node = ast_init(AST_IDENTIFIER);
        variable_node->value.identifier = id_name;
        return variable_node;
    }
    case TOKEN_LPAREN: {
        parser_eat(parser, TOKEN_LPAREN);
        ast_t *expr = parser_parse_expr(parser);
        parser_eat(parser, TOKEN_RPAREN);
        return expr;
    }
    default:
        ti_log("Unexpected value %s, at line %d\n",
               token_to_str(parser->current_token->type),
               parser->lexer->line_num);
        ti_log_line(parser->lexer->line);
        ti_fatal();
        return NULL;
    }
}

/**
 * @brief Parse variable definition statement (e.g. int x = 5;).
 * @param parser Pointer to parser.
 * @return AST variable definition node.
 */
static ast_t *parser_parse_variable_definition(parser_t *parser)
{
    int variable_type;
    switch (parser->current_token->type) {
    case TOKEN_KW_INT:    variable_type = VAR_TYPE_INT;    break;
    case TOKEN_KW_FLOAT:  variable_type = VAR_TYPE_FLOAT;  break;
    case TOKEN_KW_STRING: variable_type = VAR_TYPE_STRING; break;
    case TOKEN_KW_BOOL:   variable_type = VAR_TYPE_BOOL;   break;
    case TOKEN_KW_VOID:   variable_type = VAR_TYPE_VOID;   break;
    default:
        ti_log("[Parser Error] Unexpected type keyword with type %d ('%s') in variable definition\n",
               parser->current_token->type,
               parser->current_token->value ? parser->current_token->value : "");
        ti_fatal();
        break;
    }
    parser_eat(parser, parser->current_token->type);

    char *variable_name = parser->current_token->value;
    parser_eat(parser, TOKEN_ID);

    parser_eat(parser, TOKEN_EQUALS);

    ast_t *value = parser_parse_expr(parser);
    ast_t *var_def_node = ast_init(AST_VARIABLE_DEFINITION);
    var_def_node->value.variable_definition.variable_type = variable_type;
    var_def_node->value.variable_definition.variable_name = variable_name;
    var_def_node->value.variable_definition.value = value;
    parser_eat(parser, TOKEN_SEMI);
    return var_def_node;
}

/**
 * @brief Parse while loop statement: while (condition) { body }.
 * @param parser Pointer to parser.
 * @return AST while node.
 */
static ast_t *parser_parse_while_statement(parser_t *parser)
{
    parser_eat(parser, TOKEN_KW_WHILE);
    parser_eat(parser, TOKEN_LPAREN);
    ast_t *condition = parser_parse_expr(parser);
    parser_eat(parser, TOKEN_RPAREN);

    ast_t *body = parser_parse_statements(parser);
    ast_t *while_node = ast_init(AST_WHILE_STATEMENT);
    while_node->value.while_statement.condition = condition;
    while_node->value.while_statement.body = body;
    return while_node;
}

/**
 * @brief Parse if/else statement: if (condition) body [else body].
 * @param parser Pointer to parser.
 * @return AST if node.
 */
static ast_t *parser_parse_if_statement(parser_t *parser)
{
    parser_eat(parser, TOKEN_KW_IF);
    parser_eat(parser, TOKEN_LPAREN);
    ast_t *condition = parser_parse_expr(parser);
    parser_eat(parser, TOKEN_RPAREN);

    ast_t *body = NULL;
    if (parser->current_token->type == TOKEN_LBRACE) {
        body = parser_parse_statements(parser);
    } else {
        body = parser_parse_statement(parser);
    }

    ast_t *if_node = ast_init(AST_IF_STATEMENT);
    if_node->value.if_statement.condition = condition;
    if_node->value.if_statement.body = body;
    if_node->value.if_statement.else_body = NULL;

    if (parser->current_token->type == TOKEN_KW_ELSE) {
        parser_eat(parser, TOKEN_KW_ELSE);
        if (parser->current_token->type == TOKEN_LBRACE) {
            if_node->value.if_statement.else_body = parser_parse_statements(parser);
        } else {
            if_node->value.if_statement.else_body = parser_parse_statement(parser);
        }
    }
    return if_node;
}

/**
 * @brief Parse function call expression: func(arg1, arg2, ...).
 * @param parser Pointer to parser.
 * @param func_name Name of function being called.
 * @return AST function call node.
 */
static ast_t *parser_parse_function_call(parser_t *parser, char *func_name)
{
    parser_eat(parser, TOKEN_LPAREN);
    ast_t **args = NULL;
    int num_arg = 0;
    char *func = func_name;

    if (parser->current_token->type != TOKEN_RPAREN) {
        args = tracked_calloc(1, sizeof(struct AST_STRUCT *));
        ast_t *arg_node = parser_parse_expr(parser);
        args[num_arg] = arg_node;
        num_arg++;
    }
    while (parser->current_token->type == TOKEN_COMMA) {
        parser_eat(parser, TOKEN_COMMA);
        args = tracked_realloc(args, (num_arg + 1) * sizeof(struct AST_STRUCT *));
        ast_t *arg_node = parser_parse_expr(parser);
        args[num_arg] = arg_node;
        num_arg++;
    }
    parser_eat(parser, TOKEN_RPAREN);

    ast_t *func_call_node = ast_init(AST_FUNCTION_CALL);
    func_call_node->value.function_call.func = func;
    func_call_node->value.function_call.args = args;
    func_call_node->value.function_call.num_arg = num_arg;
    return func_call_node;
}

/**
 * @brief Parse variable assignment statement (target = expr;).
 * @param parser Pointer to parser.
 * @param target Target AST node for assignment.
 * @return AST assignment node.
 */
static ast_t *parser_parse_assignment(parser_t *parser, ast_t *target)
{
    parser_eat(parser, TOKEN_EQUALS);
    ast_t *value = parser_parse_expr(parser);
    ast_t *assignment_node = ast_init(AST_ASSIGNMENT);
    assignment_node->value.assignment.id = target;
    assignment_node->value.assignment.value = value;
    parser_eat(parser, TOKEN_SEMI);
    return assignment_node;
}

/* -------------------- Public Functions -------------------- */

/* Initialize a new parser using the given lexer */
parser_t *parser_init(lexer_t *lexer)
{
    parser_t *parser = tracked_calloc(1, sizeof(struct PARSER_STRUCT));
    parser->lexer = lexer;
    parser->current_token = lexer_get_next_token(parser->lexer);
    return parser;
}

/* Parse the entire source code into a program AST */
ast_t *parser_parse(parser_t *parser)
{
    return parser_parse_main_program(parser);
}
