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
static type_spec_t parser_parse_type(parser_t *parser);
static int parser_hex_digit(char c);
static char *parser_take_identifier(parser_t *parser);
static void parser_eat(parser_t *parser, int expected_type);
static ast_t *parser_parse_statement(parser_t *parser);
static ast_t *parser_parse_statements(parser_t *parser);
static ast_t *parser_parse_main_program(parser_t *parser);
static ast_t *parser_parse_definition(parser_t *parser);
static ast_t *parser_parse_param(parser_t *parser);
static ast_t *parser_parse_function_definition(parser_t *parser, type_spec_t type, char *name);
static ast_t *parser_parse_variable_definition(parser_t *parser, type_spec_t type, char *name);
static ast_t *parser_parse_variable_declaration(parser_t *parser, type_spec_t type, char *name);
static val_type_t parser_parse_list_element_type(parser_t *parser);
static ast_t *parser_parse_list_literal(parser_t *parser);
static ast_t *parser_parse_assignment(parser_t *parser, ast_t *target);
static ast_t *parser_parse_assignment_expr(parser_t *parser, ast_t *target);
static bool parser_is_update_token(int token_type);
static ast_t *parser_parse_for_statement(parser_t *parser);
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
* @brief Convert one hex digit character to its value.
* @param c Character already validated by the lexer as a hex digit.
* @return Value 0..15.
*/
static int parser_hex_digit(char c)
{
  if (c >= '0' && c <= '9') {
      return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
  }
  return c - 'A' + 10;
}
    
/**
 * @brief Parse a type specifier: a type keyword, or 'list <elem_type>' for lists.
 * @param parser Pointer to parser.
 * @return The parsed type (element_type is VAL_NULL unless the type is VAL_LIST).
 */
/* <type> | list <elem_type> */
static type_spec_t parser_parse_type(parser_t *parser)
{
    type_spec_t spec = { VAL_NULL, VAL_NULL };

    /* Map the keyword to its value type */
    switch (parser->current_token->type) {
    case TOKEN_KW_INT:    spec.type = VAL_INT;    break;
    case TOKEN_KW_FLOAT:  spec.type = VAL_FLOAT;  break;
    case TOKEN_KW_STRING: spec.type = VAL_STRING; break;
    case TOKEN_KW_BOOL:   spec.type = VAL_BOOL;   break;
    case TOKEN_KW_VOID:   spec.type = VAL_VOID;   break;
    case TOKEN_KW_DICT:   spec.type = VAL_DICT;   break;
    case TOKEN_KW_LIST:   spec.type = VAL_LIST;   break;
    case TOKEN_KW_BYTES:  spec.type = VAL_BYTES;  break;
    default:
        ti_log("[Parser Error] Unexpected type %s, at line %d\n",
               token_to_str(parser->current_token->type), parser->lexer->line_num);
        ti_log_line(parser->lexer->line);
        ti_fatal();
        return spec;
    }
    parser_eat(parser, parser->current_token->type); // Eat <type>

    /* 'list' carries its element type as a second keyword */
    if (spec.type == VAL_LIST) {
        spec.element_type = parser_parse_list_element_type(parser);
    }
    return spec;
}

/**
 * @brief Consume an identifier token and take ownership of its name string.
 * @param parser Pointer to parser.
 * @return The identifier name (owned by the caller, normally handed to an AST node).
 */
static char *parser_take_identifier(parser_t *parser)
{
    /* Move the string out of the token so parser_eat does not free it */
    char *name = parser->current_token->value;
    parser->current_token->value = NULL;
    parser_eat(parser, TOKEN_ID); // Eat <identifier>
    return name;
}

/**
 * @brief Parse a single typed function parameter.
 * @param parser Pointer to parser.
 * @return AST parameter node.
 */
/* <type> <param_name> */
static ast_t *parser_parse_param(parser_t *parser)
{
    type_spec_t type = parser_parse_type(parser);

    /* A parameter must hold a value, so void is rejected */
    if (type.type == VAL_VOID) {
        ti_log("[Parser Error] Parameter cannot have type void, at line %d\n", parser->lexer->line_num);
        ti_log_line(parser->lexer->line);
        ti_fatal();
    }
    char *param_name = parser_take_identifier(parser);

    /* Construct AST parameter node */
    ast_t *param_node = ast_init(parser->alloc_list, AST_PARAM, parser->lexer->line_num);
    param_node->value.param.param_type = type.type;
    param_node->value.param.element_type = type.element_type;
    param_node->value.param.param_name = param_name;
    return param_node;
}

/**
 * @brief Parse the rest of a function definition after '<return_type> <func_name>'.
 * @param parser Pointer to parser (current token is '(').
 * @param type Declared return type.
 * @param name Function name (ownership passes to the AST node).
 * @return AST function definition node.
 */
/* (<type> param1, <type> param2, ...) { <compound> } */
static ast_t *parser_parse_function_definition(parser_t *parser, type_spec_t type, char *name)
{
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
    func_def_node->value.function_definition.return_type = type.type;
    func_def_node->value.function_definition.return_element_type = type.element_type;
    func_def_node->value.function_definition.func_name = name;
    func_def_node->value.function_definition.param_count = param_count;
    func_def_node->value.function_definition.params = params;
    func_def_node->value.function_definition.body = statements;
    return func_def_node;
}

/**
 * @brief Parse a definition: '<type> <identifier>' followed by '(' (function) or '=' (variable).
 * @param parser Pointer to parser.
 * @return AST definition node.
 */
static ast_t *parser_parse_definition(parser_t *parser)
{
    /* The common prefix is parsed once, so no lookahead is needed to tell the two forms apart */
    type_spec_t type = parser_parse_type(parser);
    char *name = parser_take_identifier(parser);

    switch (parser->current_token->type) {
    case TOKEN_LPAREN:
        return parser_parse_function_definition(parser, type, name);
    case TOKEN_EQUALS:
        return parser_parse_variable_definition(parser, type, name);
    default:
        ti_log("[Parser Error] Unexpected token %s in definition at line %d\n",
               token_to_str(parser->current_token->type), parser->lexer->line_num);
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
    case TOKEN_KW_LIST:
    case TOKEN_KW_BYTES:
        return parser_parse_definition(parser);
    case TOKEN_ID: {
        /* Parse expression starting with identifier; distinguish assignment from call */
        ast_t *expr = parser_parse_expr(parser);
        if (parser_is_update_token(parser->current_token->type)) {
            return parser_parse_assignment(parser, expr);
        }
        /* Consume terminating semicolon for expression statement */
        parser_eat(parser, TOKEN_SEMI);
        return expr;
    }
    case TOKEN_KW_FOR:
        return parser_parse_for_statement(parser);
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
    case TOKEN_LBRACKET:
        /* List literal: [expr, expr, ...] */
        return parser_parse_list_literal(parser);
    case TOKEN_BYTES:
    {
        /* The lexer guarantees an even number of valid hex digits */
        const char *hex = parser->current_token->value; // "1A024A..."
        int length = (int)(strlen(hex) / 2);
        if (length > TI_MAX_BYTES_LEN) {
          ti_log("[Parser Error] Bytes literal exceeds %d bytes at line %d\n", TI_MAX_BYTES_LEN, parser->lexer->line_num);
          ti_log_line(parser->lexer->line);
          ti_fatal();
        }

        ast_t *bytes_node = ast_init(parser->alloc_list, AST_BYTES_LITERAL, parser->lexer->line_num);
        bytes_node->value.bytes_literal.length = length;
        bytes_node->value.bytes_literal.data = NULL;

        /* Decode 2 digits per bytes: high nibble first */
        if (length > 0)
        {
            /* Allocate length bytes of data */
            uint8_t *data = tracked_calloc(parser->alloc_list, length, sizeof(uint8_t));
            for (int i = 0; i < length; i++)
            {
                /* Combine high nibble and low nibble into a single byte */
                data[i] = (uint8_t)((parser_hex_digit(hex[2 * i]) << 4) | parser_hex_digit(hex[2 * i + 1]));
            }
            bytes_node->value.bytes_literal.data = data;
        }
        parser_eat(parser,TOKEN_BYTES); // Eat the literal (frees the token's hex string)
        return bytes_node;
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
 * @brief Parse the element type keyword that follows 'list' in a type specifier.
 * @param parser Pointer to parser.
 * @return Element value type (VAL_INT, VAL_FLOAT, VAL_STRING or VAL_BOOL).
 */
/* list <elem_type> */
static val_type_t parser_parse_list_element_type(parser_t *parser)
{
    val_type_t element_type = VAL_NULL;
    switch (parser->current_token->type) {
    case TOKEN_KW_INT:    element_type = VAL_INT;    break;
    case TOKEN_KW_FLOAT:  element_type = VAL_FLOAT;  break;
    case TOKEN_KW_STRING: element_type = VAL_STRING; break;
    case TOKEN_KW_BOOL:   element_type = VAL_BOOL;   break;
    default:
        /* Containers are rejected as elements: refcounting cannot reclaim reference cycles */
        ti_log("[Parser Error] List element type must be int, float, string or bool, but got %s at line %d\n",
               token_to_str(parser->current_token->type), parser->lexer->line_num);
        ti_log_line(parser->lexer->line);
        ti_fatal();
        return VAL_NULL;
    }
    parser_eat(parser, parser->current_token->type); // Eat <elem_type>
    return element_type;
}

/**
 * @brief Parse a list literal: [expr, expr, ...] (empty list and trailing comma allowed).
 * @param parser Pointer to parser.
 * @return AST list literal node.
 */
/* [<expr>, <expr>, ...] */
static ast_t *parser_parse_list_literal(parser_t *parser)
{
    /* Record the opening line so runtime errors point at the start of the literal */
    int line = parser->lexer->line_num;
    parser_eat(parser, TOKEN_LBRACKET); // Eat '['

    ast_t **elements = NULL;
    int element_count = 0;

    /* Parse comma-separated element expressions until the closing bracket */
    while (parser->current_token->type != TOKEN_RBRACKET) {
        /* Nested list literals are rejected: elements must be scalar or string */
        if (parser->current_token->type == TOKEN_LBRACKET) {
            ti_log("[Parser Error] Nested list literals are not supported at line %d\n", parser->lexer->line_num);
            ti_log_line(parser->lexer->line);
            ti_fatal();
        }

        /* Reject literals that could never fit in a list at runtime */
        if (element_count >= TI_MAX_LIST_ITEMS) {
            ti_log("[Parser Error] List literal exceeds %d elements at line %d\n", TI_MAX_LIST_ITEMS, parser->lexer->line_num);
            ti_log_line(parser->lexer->line);
            ti_fatal();
        }

        /* Elements are arbitrary expressions, evaluated and type-checked at runtime */
        ast_t *element_node = parser_parse_expr(parser);
        elements = tracked_realloc(parser->alloc_list, elements, (element_count + 1) * sizeof(ast_t *));
        elements[element_count] = element_node;
        element_count++;

        /* Handle comma separator (optional before the closing bracket) */
        if (parser->current_token->type == TOKEN_COMMA) {
            parser_eat(parser, TOKEN_COMMA);
        } else if (parser->current_token->type != TOKEN_RBRACKET) {
            ti_log("[Parser Error] Expected ',' or ']' in list literal at line %d\n", parser->lexer->line_num);
            ti_log_line(parser->lexer->line);
            ti_fatal();
        }
    }
    parser_eat(parser, TOKEN_RBRACKET); // Eat ']'

    /* Construct AST list literal node */
    ast_t *list_node = ast_init(parser->alloc_list, AST_LIST_LITERAL, line);
    list_node->value.list_literal.elements = elements;
    list_node->value.list_literal.element_count = element_count;
    return list_node;
}

/**
 * @brief Parse the rest of a variable declaration after '<type> <name>': '= expr', no ';'.
 * @param parser Pointer to parser (current token is '=').
 * @param type Declared type (with element type for lists).
 * @param name Variable name (ownership passes to the AST node).
 * @return AST variable definition node.
 */
/* = <expr> */
static ast_t *parser_parse_variable_declaration(parser_t *parser, type_spec_t type, char *name)
{
    /* Parse assignment operator and initialization expression */
    parser_eat(parser, TOKEN_EQUALS); // Eat '='
    ast_t *value = parser_parse_expr(parser);

    /* A list literal takes its element type from the declaration (needed for empty lists) */
    if (type.type == VAL_LIST && value->type == AST_LIST_LITERAL) {
        value->value.list_literal.element_type = type.element_type;
    }

    /* Construct AST variable definition node */
    ast_t *var_def_node = ast_init(parser->alloc_list, AST_VARIABLE_DEFINITION, parser->lexer->line_num);
    var_def_node->value.variable_definition.variable_type = type.type;
    var_def_node->value.variable_definition.element_type = type.element_type;
    var_def_node->value.variable_definition.variable_name = name;
    var_def_node->value.variable_definition.value = value;
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
 * @brief Parse an assignment-like statement without the terminating semicolon.
 * 'target = e' is a plain assignment. 'target += e' (and -=, *=, /=) and 'target++' (and --) are
 * rewritten to 'target = target <op> e' (e is the literal 1 for ++ and --), so the evaluators
 * only ever see AST_ASSIGNMENT. The target is cloned because it is both the destination and the
 * left operand and each node must have exactly one owner.
 * @param parser Pointer to parser.
 * @param target Target AST node already parsed (identifier or array access).
 * @return AST assignment node.
 */
/* <target> (= | += | -= | *= | /=) <expr>   |   <target> (++ | --) */
static ast_t *parser_parse_assignment_expr(parser_t *parser, ast_t *target)
{
    int token_type = parser->current_token->type;
    int line = parser->lexer->line_num;
    ast_t *value = NULL;

    if (token_type == TOKEN_EQUALS) {
        /* Plain assignment: the right-hand side is used as is */
        parser_eat(parser, TOKEN_EQUALS); // Eat '='
        value = parser_parse_expr(parser);
    } else {
        /* Map the operator token to the binary operator it stands for */
        int op = OP_ADD;
        switch (token_type) {
        case TOKEN_PLUS_EQUALS:
        case TOKEN_PLUS_PLUS:
            op = OP_ADD;
            break;
        case TOKEN_MINUS_EQUALS:
        case TOKEN_MINUS_MINUS:
            op = OP_SUB;
            break;
        case TOKEN_STAR_EQUALS:
            op = OP_MUL;
            break;
        case TOKEN_SLASH_EQUALS:
            op = OP_DIV;
            break;
        default:
            ti_log("[Parser Error] Unexpected token %s in assignment at line %d\n",
                   token_to_str(token_type), parser->lexer->line_num);
            ti_log_line(parser->lexer->line);
            ti_fatal();
            return NULL;
        }
        parser_eat(parser, token_type); // Eat the operator token

        /* Right operand: the literal 1 for ++/--, otherwise the parsed expression */
        ast_t *right = NULL;
        if (token_type == TOKEN_PLUS_PLUS || token_type == TOKEN_MINUS_MINUS) {
            right = ast_init(parser->alloc_list, AST_INT_LITERAL, line);
            right->value.int_value = 1;
        } else {
            right = parser_parse_expr(parser);
        }

        /* The target is reused as the left operand, so it needs its own copy */
        ast_t *left = ast_clone(parser->alloc_list, target);
        if (left == NULL) {
            ti_log("[Parser Error] Unsupported target for compound assignment at line %d\n", line);
            ti_log_line(parser->lexer->line);
            ti_fatal();
            return NULL;
        }

        value = ast_init(parser->alloc_list, AST_BINARY_EXPR, line);
        value->value.binary_expr.op = op;
        value->value.binary_expr.left = left;
        value->value.binary_expr.right = right;
    }

    /* Construct AST assignment node (the caller consumes the terminator) */
    ast_t *assignment_node = ast_init(parser->alloc_list, AST_ASSIGNMENT, parser->lexer->line_num);
    assignment_node->value.assignment.target = target;
    assignment_node->value.assignment.value = value;
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

/**
 * @brief Parse the rest of a variable definition after '<type> <name>': = expr;.
 * @param parser Pointer to parser (current token is '=').
 * @param type Declared type.
 * @param name Variable name (ownership passes to the AST node).
 * @return AST variable definition node.
 */
static ast_t *parser_parse_variable_definition(parser_t *parser, type_spec_t type, char *name)
{
    ast_t *var_def_node = parser_parse_variable_declaration(parser, type, name);
    parser_eat(parser, TOKEN_SEMI); // Eat ';'
    return var_def_node;
}

/**
 * @brief Check whether a token starts the right part of an assignment-like statement.
 * @param token_type Token type enum value.
 * @return true for '=', '+=', '-=', '*=', '/=', '++' and '--'.
 */
static bool parser_is_update_token(int token_type)
{
    return token_type == TOKEN_EQUALS || token_type == TOKEN_PLUS_EQUALS ||
           token_type == TOKEN_MINUS_EQUALS || token_type == TOKEN_STAR_EQUALS ||
           token_type == TOKEN_SLASH_EQUALS || token_type == TOKEN_PLUS_PLUS ||
           token_type == TOKEN_MINUS_MINUS;
}

/**
 * @brief Parse variable assignment statement (target = expr;).
 * @param parser Pointer to parser.
 * @param target Target AST node for assignment.
 * @return AST assignment node.
 */
static ast_t *parser_parse_assignment(parser_t *parser, ast_t *target)
{
    ast_t *assignment_node = parser_parse_assignment_expr(parser, target);
    parser_eat(parser, TOKEN_SEMI);   // Eat ';'
    return assignment_node;
}

/**
 * @brief Parse for loop statement: for (init; condition; step) { body }.
 * Each header part may be omitted. init is a variable definition or an assignment, condition is
 * an expression, step is an assignment (assignment is a statement, so there is no i++).
 * @param parser Pointer to parser.
 * @return AST for node.
 */
/* for ([<definition> | <assignment>]; [<expr>]; [<assignment>]) { <compound> } */
static ast_t *parser_parse_for_statement(parser_t *parser)
{
    int line = parser->lexer->line_num;
    ast_t *init = NULL;
    ast_t *condition = NULL;
    ast_t *step = NULL;

    parser_eat(parser, TOKEN_KW_FOR); // Eat 'for'
    parser_eat(parser, TOKEN_LPAREN); // Eat '('

    /* Init part: a declaration or an assignment, followed by the first separator */
    switch (parser->current_token->type) {
    case TOKEN_SEMI:
        break; /* omitted */
    case TOKEN_KW_INT:
    case TOKEN_KW_FLOAT:
    case TOKEN_KW_STRING:
    case TOKEN_KW_BOOL:
    case TOKEN_KW_DICT:
    case TOKEN_KW_LIST:
    case TOKEN_KW_BYTES: {
        type_spec_t init_type = parser_parse_type(parser);
        char *init_name = parser_take_identifier(parser);
        init = parser_parse_variable_declaration(parser, init_type, init_name);
        break;
    }
    case TOKEN_ID: {
        ast_t *target = parser_parse_expr(parser);
        if (!parser_is_update_token(parser->current_token->type)) {
            ti_log("[Parser Error] for-loop init must be a definition or an assignment at line %d\n", parser->lexer->line_num);
            ti_log_line(parser->lexer->line);
            ti_fatal();
        }
        init = parser_parse_assignment_expr(parser, target);
        break;
    }
    default:
        ti_log("[Parser Error] Unexpected token %s in for-loop init at line %d\n",
               token_to_str(parser->current_token->type), parser->lexer->line_num);
        ti_log_line(parser->lexer->line);
        ti_fatal();
    }
    parser_eat(parser, TOKEN_SEMI); // Eat first ';'

    /* Condition part: an expression (omitted means always true) */
    if (parser->current_token->type != TOKEN_SEMI) {
        condition = parser_parse_expr(parser);
    }
    parser_eat(parser, TOKEN_SEMI); // Eat second ';'

    /* Step part: an assignment (omitted is allowed) */
    if (parser->current_token->type != TOKEN_RPAREN) {
        if (parser->current_token->type != TOKEN_ID) {
            ti_log("[Parser Error] for-loop step must be an assignment at line %d\n", parser->lexer->line_num);
            ti_log_line(parser->lexer->line);
            ti_fatal();
        }
        /* Current token is TOKEN_ID, so the AST returned from parser_expr should be identifier or array access*/
        ast_t *target = parser_parse_expr(parser);
        if (!parser_is_update_token(parser->current_token->type)) {
            ti_log("[Parser Error] for-loop step must be an assignment at line %d\n", parser->lexer->line_num);
            ti_log_line(parser->lexer->line);
            ti_fatal();
        }
        step = parser_parse_assignment_expr(parser, target);
    }
    parser_eat(parser, TOKEN_RPAREN); // Eat ')'

    /* Loop body is always a compound block */
    ast_t *body = parser_parse_statements(parser);

    ast_t *for_node = ast_init(parser->alloc_list, AST_FOR_STATEMENT, line);
    for_node->value.for_statement.init = init;
    for_node->value.for_statement.condition = condition;
    for_node->value.for_statement.step = step;
    for_node->value.for_statement.body = body;
    return for_node;
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
