#include "include/ti_build_parser.h"
#include "include/ti_type_ast.h"
#include "include/ti_build_lexer.h"
#include "include/ti_build_token.h"
#include "include/tracked_memory.h"
#include "TienInterpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------- Static Function Prototypes -------------------- */

static int token_type_to_op(parser_t *parser, int token_type);
static token_t *parser_peek(parser_t *parser);
static void parser_eat(parser_t *parser, int expected_type);
static ast_t *parser_parse_statement(parser_t *parser);
static ast_t *parser_parse_statements(parser_t *parser);
static ast_t *parser_parse_main_program(parser_t *parser);
static ast_t *parser_parse_definition(parser_t *parser);
static ast_t *parser_parse_param(parser_t *parser);
static ast_t *parser_parse_function_definition(parser_t *parser);
static ast_t *parser_parse_variable_definition(parser_t *parser);
static ast_t *parser_parse_assignment(parser_t *parser, ast_t *target);
static ast_t *parser_parse_function_call(parser_t *parser, char *func_name);
static ast_t *parser_parse_while_statement(parser_t *parser);
static ast_t *parser_parse_if_statement(parser_t *parser);
static ast_t *parser_parse_return_statement(parser_t *parser);
static ast_t *parser_parse_break_statement(parser_t *parser);
static ast_t *parser_parse_continue_statement(parser_t *parser);
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
static int token_type_to_op(parser_t *parser, int token_type)
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
        ti_log("[Parser Error] Unknown operator token %s at line %d\n",
               token_to_str(token_type), parser->lexer->line_num);
        ti_log_line(parser->lexer->line);
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
    /* Duplicate lexer state to perform lookahead without advancing active lexer */
    lexer_t *temp_lexer = lexer_copy(parser->lexer);

    /* Discard current token on copied stream to reach the lookahead token */
    token_t *discared = lexer_get_next_token(temp_lexer);
    if (discared->value != NULL) {
        tracked_free(parser->alloc_list, discared->value);
        discared->value = NULL;
    }
    tracked_free(parser->alloc_list, discared);

    /* Fetch and return the lookahead token, then free temporary lexer clone */
    token_t *next_token = lexer_get_next_token(temp_lexer);
    tracked_free(parser->alloc_list, (void *)temp_lexer);
    return next_token;
}

/**
 * @brief Parse a single typed function parameter.
 * @param parser Pointer to parser.
 * @return AST parameter node.
 */
/* <type> <param_name> */
static ast_t *parser_parse_param(parser_t *parser)
{
    val_type_t param_type;
    switch (parser->current_token->type) {
    case TOKEN_KW_INT:    param_type = VAL_INT;    break;
    case TOKEN_KW_FLOAT:  param_type = VAL_FLOAT;  break;
    case TOKEN_KW_STRING: param_type = VAL_STRING; break;
    case TOKEN_KW_BOOL:   param_type = VAL_BOOL;   break;
    default:
        ti_log("[Parser Error] Unexpected type %s for parameter, at line %d\n",
               token_to_str(parser->current_token->type), parser->lexer->line_num);
        ti_log_line(parser->lexer->line);
        ti_fatal();
        break;
    }
    /* Consume parameter type keyword */
    parser_eat(parser, parser->current_token->type); // Eat <param_type>

    /* Transfer ownership of parameter identifier string from token to AST */
    char *param_name = parser->current_token->value;
    parser->current_token->value = NULL; // Change the owner to ast instead of token
    parser_eat(parser, TOKEN_ID); // Eat param_name

    /* Construct AST parameter node */
    ast_t *param_node = ast_init(parser->alloc_list, AST_PARAM, parser->lexer->line_num);
    param_node->value.param.param_type = param_type;
    param_node->value.param.param_name = param_name;
    return param_node;
}

/**
 * @brief Parse a function definition statement: type func_name(param1, param2, ...) { body }.
 * @param parser Pointer to parser.
 * @return AST function definition node.
 */
/* <return_type> func_name(<type> param1, <type> param2, ...) { <compound> } */
static ast_t *parser_parse_function_definition(parser_t *parser)
{
    val_type_t return_type;
    switch (parser->current_token->type) {
    case TOKEN_KW_INT:    return_type = VAL_INT;    break;
    case TOKEN_KW_FLOAT:  return_type = VAL_FLOAT;  break;
    case TOKEN_KW_STRING: return_type = VAL_STRING; break;
    case TOKEN_KW_BOOL:   return_type = VAL_BOOL;   break;
    case TOKEN_KW_VOID:   return_type = VAL_VOID;   break;
    default:
        ti_log("[Parser Error] Unexpected type %s in function definition, at line %d\n",
               token_to_str(parser->current_token->type), parser->lexer->line_num);
        ti_log_line(parser->lexer->line);
        ti_fatal();
        break;
    }
    /* Consume return type keyword */
    parser_eat(parser, parser->current_token->type); // Eat <return_type>

    /* Transfer ownership of function name string from token to AST */
    char *func_name = parser->current_token->value;
    parser->current_token->value = NULL; // Change the owner to ast instead of token
    parser_eat(parser, TOKEN_ID); // Eat func_name

    parser_eat(parser, TOKEN_LPAREN); // Eat '('
    ast_t **params = NULL;
    int param_count = 0;

    /* Parse parameter list: <type> param1, <type> param2, ... */
    if (parser->current_token->type != TOKEN_RPAREN) {
        params = tracked_calloc(parser->alloc_list, 1, sizeof(struct AST_STRUCT *));
        ast_t *param_node = parser_parse_param(parser);
        params[param_count] = param_node;
        param_count++;

        while (parser->current_token->type == TOKEN_COMMA) {
            parser_eat(parser, TOKEN_COMMA); // Eat ','
            params = tracked_realloc(parser->alloc_list, params, (param_count + 1) * sizeof(struct AST_STRUCT *));
            ast_t *next_param_node = parser_parse_param(parser);
            params[param_count] = next_param_node;
            param_count++;
        }
    }
    parser_eat(parser, TOKEN_RPAREN); // Eat ')'

    /* Parse function body compound block */
    ast_t *statements = parser_parse_statements(parser); // Parse '{' ... '}' compound body

    /* Construct AST function definition node */
    ast_t *func_def_node = ast_init(parser->alloc_list, AST_FUNCTION_DEFINITION, parser->lexer->line_num);
    func_def_node->value.function_definition.return_type = return_type;
    func_def_node->value.function_definition.func_name = func_name;
    func_def_node->value.function_definition.param_count = param_count;
    func_def_node->value.function_definition.params = params;
    func_def_node->value.function_definition.body = statements;
    return func_def_node;
}

/**
 * @brief Dispatch between variable and function definitions based on lookahead token.
 * @param parser Pointer to parser.
 * @return AST definition node.
 */
static ast_t *parser_parse_definition(parser_t *parser)
{
    /* Peek ahead to distinguish between function declaration ('(') and variable declaration ('=') */
    token_t *next_token = parser_peek(parser);
    int next_type = (int)next_token->type;
    if (next_token->value != NULL) {
        tracked_free(parser->alloc_list, next_token->value);
        next_token->value = NULL;
    }
    tracked_free(parser->alloc_list, next_token);
    switch (next_type) {
    case TOKEN_LPAREN:
        return parser_parse_function_definition(parser);
    case TOKEN_EQUALS:
        return parser_parse_variable_definition(parser);
    default:
        ti_log("[Parser Error] Unexpected token %s in definition at line %d\n",
               token_to_str(next_type), parser->lexer->line_num);
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
    /* Verify token matches expectation, advance lexer, and free consumed token */
    if ((int)parser->current_token->type == expected_type) {
        token_t *old_token = parser->current_token;
        parser->current_token = lexer_get_next_token(parser->lexer);
        if (old_token->value != NULL) {
            tracked_free(parser->alloc_list, old_token->value); // Free the value of old token
            old_token->value = NULL;
        }
        tracked_free(parser->alloc_list, old_token);
        old_token = NULL;
    } else {
        /* Format and log syntax error with offending source line */
        ti_log("[Parser Error] Unexpected token %s ('%s') at line %d\n",
               token_to_str(parser->current_token->type),
               parser->current_token->value ? parser->current_token->value : "<eof>",
               parser->lexer->line_num);
        ti_log_line(parser->lexer->line);
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
    case TOKEN_KW_DICT:
        return parser_parse_definition(parser);
    case TOKEN_ID: {
        /* Parse expression starting with identifier; distinguish assignment from call */
        ast_t *expr = parser_parse_expr(parser);
        if (parser->current_token->type == TOKEN_EQUALS) {
            return parser_parse_assignment(parser, expr);
        }
        /* Consume terminating semicolon for expression statement */
        parser_eat(parser, TOKEN_SEMI);
        return expr;
    }
    case TOKEN_KW_WHILE:
        return parser_parse_while_statement(parser);
    case TOKEN_KW_IF:
        return parser_parse_if_statement(parser);
    case TOKEN_KW_RETURN:
        return parser_parse_return_statement(parser);
    case TOKEN_KW_BREAK:
        return parser_parse_break_statement(parser);
    case TOKEN_KW_CONTINUE:
        return parser_parse_continue_statement(parser);
    default:
        ti_log("[Parser Error] Unexpected statement starting with %s ('%s') at line %d\n",
               token_to_str(parser->current_token->type),
               parser->current_token->value ? parser->current_token->value : "",
               parser->lexer->line_num);
        ti_log_line(parser->lexer->line);
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
    /* Consume opening brace and initialize compound statement node */
    parser_eat(parser, TOKEN_LBRACE);
    ast_t *compound = ast_init(parser->alloc_list, AST_COMPOUND, parser->lexer->line_num);
    compound->value.compound.statements = NULL;
    compound->value.compound.statement_count = 0;

    /* Parse statements sequentially until closing brace */
    while (parser->current_token->type != TOKEN_RBRACE) {
        ast_t *statement = parser_parse_statement(parser);
        int count = compound->value.compound.statement_count;
        if (compound->value.compound.statements == NULL) {
            compound->value.compound.statements = tracked_calloc(parser->alloc_list, 1, sizeof(struct AST_STRUCT *));
        } else {
            compound->value.compound.statements = tracked_realloc(
                parser->alloc_list,
                compound->value.compound.statements,
                (count + 1) * sizeof(struct AST_STRUCT *)
            );
        }
        compound->value.compound.statements[count] = statement;
        compound->value.compound.statement_count += 1;
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
    /* Initialize root compound container for the program */
    ast_t *compound = ast_init(parser->alloc_list, AST_COMPOUND, parser->lexer->line_num);
    compound->value.compound.statements = NULL;
    compound->value.compound.statement_count = 0;

    /* Parse top-level statements until end of input stream */
    while (parser->current_token->type != TOKEN_EOF) {
        ast_t *statement = parser_parse_statement(parser);
        int count = compound->value.compound.statement_count;
        if (compound->value.compound.statements == NULL) {
            compound->value.compound.statements = tracked_calloc(parser->alloc_list, 1, sizeof(struct AST_STRUCT *));
        } else {
            compound->value.compound.statements = tracked_realloc(
                parser->alloc_list,
                compound->value.compound.statements,
                (count + 1) * sizeof(struct AST_STRUCT *)
            );
        }
        compound->value.compound.statements[count] = statement;
        compound->value.compound.statement_count += 1;
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
    /* Parse higher precedence comparison expression first */
    ast_t *left = parser_parse_comparison(parser);

    /* Left-associatively chain logical AND/OR operations */
    while (parser->current_token->type == TOKEN_LOGIC_AND ||
           parser->current_token->type == TOKEN_LOGIC_OR) {
        int op = parser->current_token->type;
        parser_eat(parser, op);
        ast_t *right = parser_parse_comparison(parser);
        ast_t *binary_node = ast_init(parser->alloc_list, AST_BINARY_EXPR, parser->lexer->line_num);
        binary_node->value.binary_expr.op = token_type_to_op(parser, op);
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
    /* Parse higher precedence additive expression first */
    ast_t *left = parser_parse_additive(parser);

    /* Left-associatively chain comparison operations */
    while (parser->current_token->type == TOKEN_DEQUALS ||
           parser->current_token->type == TOKEN_NOT_EQUALS ||
           parser->current_token->type == TOKEN_LT ||
           parser->current_token->type == TOKEN_LTE ||
           parser->current_token->type == TOKEN_GT ||
           parser->current_token->type == TOKEN_GTE) {
        int op = parser->current_token->type;
        parser_eat(parser, op);
        ast_t *right = parser_parse_additive(parser);
        ast_t *binary_node = ast_init(parser->alloc_list, AST_BINARY_EXPR, parser->lexer->line_num);
        binary_node->value.binary_expr.op = token_type_to_op(parser, op);
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
    /* Parse higher precedence multiplicative term first */
    ast_t *left = parser_parse_term(parser);

    /* Left-associatively chain addition and subtraction operations */
    while (parser->current_token->type == TOKEN_PLUS ||
           parser->current_token->type == TOKEN_MINUS) {
        int op = parser->current_token->type;
        parser_eat(parser, op);
        ast_t *right = parser_parse_term(parser);
        ast_t *binary_node = ast_init(parser->alloc_list, AST_BINARY_EXPR, parser->lexer->line_num);
        binary_node->value.binary_expr.op = token_type_to_op(parser, op);
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
    /* Parse highest precedence primary atom first */
    ast_t *left = parser_parse_primary(parser);

    /* Left-associatively chain multiplication and division operations */
    while (parser->current_token->type == TOKEN_STAR ||
           parser->current_token->type == TOKEN_SLASH) {
        int op = parser->current_token->type;
        parser_eat(parser, op);
        ast_t *right = parser_parse_primary(parser);
        ast_t *binary_node = ast_init(parser->alloc_list, AST_BINARY_EXPR, parser->lexer->line_num);
        binary_node->value.binary_expr.op = token_type_to_op(parser, op);
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
        /* Parse integer literal and convert string representation to integer value */
        ast_t *int_node = ast_init(parser->alloc_list, AST_INT_LITERAL, parser->lexer->line_num);
        int_node->value.int_value = atoi(parser->current_token->value);
        parser_eat(parser, TOKEN_INT);
        return int_node;
    }
    case TOKEN_FLOAT: {
        /* Parse floating-point literal and convert string representation to double */
        ast_t *float_node = ast_init(parser->alloc_list, AST_FLOAT_LITERAL, parser->lexer->line_num);
        float_node->value.float_value = atof(parser->current_token->value);
        parser_eat(parser, TOKEN_FLOAT);
        return float_node;
    }
    case TOKEN_STRING: {
        /* Parse string literal; transfer ownership of allocated string buffer to AST */
        ast_t *string_node = ast_init(parser->alloc_list, AST_STRING_LITERAL, parser->lexer->line_num);
        string_node->value.string_value = parser->current_token->value;
        parser->current_token->value = NULL; // Change the owner to ast instead of token
        parser_eat(parser, TOKEN_STRING);
        return string_node;
    }
    case TOKEN_BOOL: {
        /* Parse boolean literal (true/false) */
        ast_t *bool_node = ast_init(parser->alloc_list, AST_BOOLEAN, parser->lexer->line_num);
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
        /* Parse unary prefix operator (!, +, -) and recursively evaluate primary operand */
        int token_type = parser->current_token->type;
        parser_eat(parser, token_type);

        ast_t *unary_node = ast_init(parser->alloc_list, AST_UNARY_EXPR, parser->lexer->line_num);
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
        /* Disambiguate identifier reference between function call, array index, or variable */
        char *id_name = parser->current_token->value;
        parser->current_token->value = NULL; // Change the owner to ast instead of token
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
            ast_t *array_access_node = ast_init(parser->alloc_list, AST_ARRAY_ACCESS, parser->lexer->line_num);
            array_access_node->value.array_access.id = id_name;
            array_access_node->value.array_access.index_expr = index_expr;
            return array_access_node;
        }

        /* Simple identifier variable reference */
        ast_t *variable_node = ast_init(parser->alloc_list, AST_IDENTIFIER, parser->lexer->line_num);
        variable_node->value.identifier = id_name;
        return variable_node;
    }
    case TOKEN_LBRACE: {
        /* Parse Dictionary Literal: { "key": value, ... } */
        parser_eat(parser, TOKEN_LBRACE); // Eat '{'
        
        char **keys = NULL;
        ast_t **values = NULL;
        int pair_count = 0;
        
        /* Loop until we hit the closing brace */
        while (parser->current_token->type != TOKEN_RBRACE) {
            /* 1. Strict Validation: Key MUST be a string literal */
            if (parser->current_token->type != TOKEN_STRING) {
                ti_log("[Parser Error] Dictionary key must be a string literal at line %d\n", parser->lexer->line_num);
                ti_log_line(parser->lexer->line);
                ti_fatal();
            }
            
            /* Extract string and transfer ownership to AST */
            char *key_str = parser->current_token->value;
            parser->current_token->value = NULL;
            parser_eat(parser, TOKEN_STRING);
            
            /* 2. Strict Validation: Expect Colon ':' */
            if (parser->current_token->type != TOKEN_COLON) {
                ti_log("[Parser Error] Expected ':' after dictionary key at line %d\n", parser->lexer->line_num);
                ti_log_line(parser->lexer->line);
                ti_fatal();
            }
            parser_eat(parser, TOKEN_COLON);
            
            /* 3. Strict Validation: Value MUST be a primitive literal (Int, Float, String, Bool) */
            int v_type = parser->current_token->type;
            if (v_type != TOKEN_INT && v_type != TOKEN_FLOAT && v_type != TOKEN_STRING && v_type != TOKEN_BOOL) {
                ti_log("[Parser Error] Dictionary value must be a primitive literal at line %d\n", parser->lexer->line_num);
                ti_log_line(parser->lexer->line);
                ti_fatal();
            }
            /* Parse the literal value into an AST node */
            ast_t *val_node = parser_parse_primary(parser);
            
            /* 4. Store the pair dynamically */
            keys = tracked_realloc(parser->alloc_list, keys, (pair_count + 1) * sizeof(char *));
            values = tracked_realloc(parser->alloc_list, values, (pair_count + 1) * sizeof(ast_t *));
            keys[pair_count] = key_str;
            values[pair_count] = val_node;
            pair_count++;
            
            /* Handle optional comma separator */
            if (parser->current_token->type == TOKEN_COMMA) {
                parser_eat(parser, TOKEN_COMMA);
            } else if (parser->current_token->type != TOKEN_RBRACE) {
                /* If not a comma and not a right brace, it's a syntax error */
                ti_log("[Parser Error] Expected ',' or '}' in dictionary literal at line %d\n", parser->lexer->line_num);
                ti_log_line(parser->lexer->line);
                ti_fatal();
            }
        }
        parser_eat(parser, TOKEN_RBRACE); // Eat '}'
        
        /* Construct and return the AST_DICT_LITERAL node */
        ast_t *dict_node = ast_init(parser->alloc_list, AST_DICT_LITERAL, parser->lexer->line_num);
        dict_node->value.dict_literal.keys = keys;
        dict_node->value.dict_literal.values = values;
        dict_node->value.dict_literal.pair_count = pair_count;
        return dict_node;
    }
    case TOKEN_LPAREN: {
        /* Parenthesized grouped subexpression (expr) */
        parser_eat(parser, TOKEN_LPAREN);
        ast_t *expr = parser_parse_expr(parser);
        parser_eat(parser, TOKEN_RPAREN);
        return expr;
    }
    default:
        ti_log("[Parser Error] Unexpected token %s ('%s') in expression at line %d\n",
               token_to_str(parser->current_token->type),
               parser->current_token->value ? parser->current_token->value : "",
               parser->lexer->line_num);
        ti_log_line(parser->lexer->line);
        ti_fatal();
        return NULL;
    }
}

/**
 * @brief Parse a variable definition statement: type var_name = expr;.
 * @param parser Pointer to parser.
 * @return AST variable definition node.
 */
/* <type> <variable_name> = <expr>; */
static ast_t *parser_parse_variable_definition(parser_t *parser)
{
    val_type_t variable_type;
    switch (parser->current_token->type) {
    case TOKEN_KW_INT:    variable_type = VAL_INT;    break;
    case TOKEN_KW_FLOAT:  variable_type = VAL_FLOAT;  break;
    case TOKEN_KW_STRING: variable_type = VAL_STRING; break;
    case TOKEN_KW_BOOL:   variable_type = VAL_BOOL;   break;
    case TOKEN_KW_VOID:   variable_type = VAL_VOID;   break;
    case TOKEN_KW_DICT:   variable_type = VAL_DICT;   break;
    default:
        ti_log("[Parser Error] Unexpected type %s in variable definition, at line %d\n",
               token_to_str(parser->current_token->type), parser->lexer->line_num);
        ti_log_line(parser->lexer->line);
        ti_fatal();
        return NULL;
    }
    /* Consume variable type keyword */
    parser_eat(parser, parser->current_token->type); // Eat <variable_type>

    /* Transfer ownership of variable name identifier to AST */
    char *variable_name = parser->current_token->value;
    parser->current_token->value = NULL; // Give the owner to AST
    parser_eat(parser, TOKEN_ID); // Eat variable_name

    /* Parse assignment operator and initialization expression */
    parser_eat(parser, TOKEN_EQUALS); // Eat '='
    ast_t *value = parser_parse_expr(parser);

    /* Construct AST variable definition node */
    ast_t *var_def_node = ast_init(parser->alloc_list, AST_VARIABLE_DEFINITION, parser->lexer->line_num);
    var_def_node->value.variable_definition.variable_type = variable_type;
    var_def_node->value.variable_definition.variable_name = variable_name;
    var_def_node->value.variable_definition.value = value;
    parser_eat(parser, TOKEN_SEMI); // Eat ';'
    return var_def_node;
}

/**
 * @brief Parse while loop statement: while (condition) { body }.
 * @param parser Pointer to parser.
 * @return AST while node.
 */
/* while (<condition>) { <compound> } */
static ast_t *parser_parse_while_statement(parser_t *parser)
{
    /* Consume 'while' keyword and condition enclosed in parentheses */
    parser_eat(parser, TOKEN_KW_WHILE); // Eat 'while'
    parser_eat(parser, TOKEN_LPAREN);   // Eat '('
    ast_t *condition = parser_parse_expr(parser);
    parser_eat(parser, TOKEN_RPAREN);   // Eat ')'

    /* Parse loop body statements */
    ast_t *body = parser_parse_statements(parser);
    ast_t *while_node = ast_init(parser->alloc_list, AST_WHILE_STATEMENT, parser->lexer->line_num);
    while_node->value.while_statement.condition = condition;
    while_node->value.while_statement.body = body;
    return while_node;
}

/**
 * @brief Parse if/else statement: if (condition) body [else body].
 * @param parser Pointer to parser.
 * @return AST if node.
 */
/* if (<condition>) <compound> [else <compound>] */
static ast_t *parser_parse_if_statement(parser_t *parser)
{
    /* Consume 'if' keyword and condition expression enclosed in parentheses */
    parser_eat(parser, TOKEN_KW_IF);   // Eat 'if'
    parser_eat(parser, TOKEN_LPAREN);  // Eat '('
    ast_t *condition = parser_parse_expr(parser);
    parser_eat(parser, TOKEN_RPAREN);  // Eat ')'

    /* Parse true branch body (either compound block or single statement) */
    ast_t *body = NULL;
    if (parser->current_token->type == TOKEN_LBRACE) {
        body = parser_parse_statements(parser);
    } else {
        body = parser_parse_statement(parser);
    }

    ast_t *if_node = ast_init(parser->alloc_list, AST_IF_STATEMENT, parser->lexer->line_num);
    if_node->value.if_statement.condition = condition;
    if_node->value.if_statement.body = body;
    if_node->value.if_statement.else_body = NULL;

    /* Parse optional 'else' branch if present */
    if (parser->current_token->type == TOKEN_KW_ELSE) {
        parser_eat(parser, TOKEN_KW_ELSE); // Eat 'else'
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
/* <func_name>(<arg1>, <arg2>, ...) */
static ast_t *parser_parse_function_call(parser_t *parser, char *func_name)
{
    parser_eat(parser, TOKEN_LPAREN); // Eat '('
    ast_t **args = NULL;
    int arg_count = 0;

    /* Parse comma-separated argument expression list */
    if (parser->current_token->type != TOKEN_RPAREN) {
        args = tracked_calloc(parser->alloc_list, 1, sizeof(struct AST_STRUCT *));
        ast_t *arg_node = parser_parse_expr(parser);
        args[arg_count] = arg_node;
        arg_count++;
    }
    while (parser->current_token->type == TOKEN_COMMA) {
        parser_eat(parser, TOKEN_COMMA); // Eat ','
        args = tracked_realloc(parser->alloc_list, args, (arg_count + 1) * sizeof(struct AST_STRUCT *));
        ast_t *arg_node = parser_parse_expr(parser);
        args[arg_count] = arg_node;
        arg_count++;
    }
    parser_eat(parser, TOKEN_RPAREN); // Eat ')'

    /* Construct AST function call node */
    ast_t *func_call_node = ast_init(parser->alloc_list, AST_FUNCTION_CALL, parser->lexer->line_num);
    func_call_node->value.function_call.func_name = func_name;
    func_call_node->value.function_call.args = args;
    func_call_node->value.function_call.arg_count = arg_count;
    return func_call_node;
}

/**
 * @brief Parse variable assignment statement (target = expr;).
 * @param parser Pointer to parser.
 * @param target Target AST node for assignment.
 * @return AST assignment node.
 */
/* <target> = <expr>; */
static ast_t *parser_parse_assignment(parser_t *parser, ast_t *target)
{
    /* Consume assignment operator '=' and parse right-hand side expression */
    parser_eat(parser, TOKEN_EQUALS); // Eat '='
    ast_t *value = parser_parse_expr(parser);

    /* Construct AST assignment node and consume terminating semicolon */
    ast_t *assignment_node = ast_init(parser->alloc_list, AST_ASSIGNMENT, parser->lexer->line_num);
    assignment_node->value.assignment.target = target;
    assignment_node->value.assignment.value = value;
    parser_eat(parser, TOKEN_SEMI);   // Eat ';'
    return assignment_node;
}

/**
 * @brief Parse return statement: return [expr];.
 * @param parser Pointer to parser.
 * @return AST return statement node.
 */
/* return [<expr>]; */
static ast_t *parser_parse_return_statement(parser_t *parser)
{
    /* Consume 'return' keyword */
    parser_eat(parser, TOKEN_KW_RETURN); // Eat 'return'

    ast_t *return_node = ast_init(parser->alloc_list, AST_RETURN_STATEMENT, parser->lexer->line_num);
    return_node->value.return_statement.value = NULL;

    /* Parse optional return expression if semicolon does not follow immediately */
    if (parser->current_token->type != TOKEN_SEMI) {
        return_node->value.return_statement.value = parser_parse_expr(parser);
    }

    parser_eat(parser, TOKEN_SEMI); // Eat ';'
    return return_node;
}

/**
 * @brief Parse break statement: break;.
 * @param parser Pointer to parser.
 * @return AST break statement node.
 */
/* break; */
static ast_t *parser_parse_break_statement(parser_t *parser)
{
    parser_eat(parser, TOKEN_KW_BREAK); // Eat 'break'
    parser_eat(parser, TOKEN_SEMI);     // Eat ';'

    return ast_init(parser->alloc_list, AST_BREAK_STATEMENT, parser->lexer->line_num);
}

/**
 * @brief Parse continue statement: continue;.
 * @param parser Pointer to parser.
 * @return AST continue statement node.
 */
/* continue; */
static ast_t *parser_parse_continue_statement(parser_t *parser)
{
    parser_eat(parser, TOKEN_KW_CONTINUE); // Eat 'continue'
    parser_eat(parser, TOKEN_SEMI);         // Eat ';'

    return ast_init(parser->alloc_list, AST_CONTINUE_STATEMENT, parser->lexer->line_num);
}

/* -------------------- Public Functions -------------------- */

/* Initialize a new parser using the given lexer */
parser_t *parser_init(alloc_hdr_t **list, lexer_t *lexer)
{
    parser_t *parser = tracked_calloc(list, 1, sizeof(struct PARSER_STRUCT));
    if (!parser) {
        return NULL;
    }
    parser->alloc_list = list;
    parser->lexer = lexer;
    parser->current_token = lexer_get_next_token(parser->lexer);
    return parser;
}

/* Parse the entire source code into a program AST */
ast_t *parser_parse(parser_t *parser)
{
    return parser_parse_main_program(parser);
}
