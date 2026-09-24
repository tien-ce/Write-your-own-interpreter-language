#include "include/ti_build_lexer.h"
#include "include/ti_build_token.h"
#include "include/tracked_memory.h"
#include "TienInterpreter.h"
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* -------------------- Static Function Prototypes -------------------- */

static token_t *lexer_advance_with_token(lexer_t *lexer, token_t *token);
static void lexer_advance(lexer_t *lexer);
static void lexer_go_back(lexer_t *lexer);
static void lexer_skip_whitespace(lexer_t *lexer);
static char *lexer_get_current_char_as_string(lexer_t *lexer);
static token_t *lexer_collect_string(lexer_t *lexer);
static token_t *lexer_collect_id(lexer_t *lexer);
static token_t *lexer_collect_number(lexer_t *lexer);

/* -------------------- Static Functions -------------------- */

/**
 * @brief Advance the lexer by one character and return the given token.
 * @param lexer Pointer to active lexer.
 * @param token Token to return.
 * @return The provided token pointer.
 */
static token_t *lexer_advance_with_token(lexer_t *lexer, token_t *token)
{
    lexer_advance(lexer);
    return token;
}

/**
 * @brief Step back the lexer by one character.
 * @param lexer Pointer to active lexer.
 */
static void lexer_go_back(lexer_t *lexer)
{
    if (lexer->i != 0) {
        lexer->i--;
        lexer->c = lexer->contents[lexer->i];
    }
}

/**
 * @brief Advance the lexer by one character.
 * @param lexer Pointer to active lexer.
 */
static void lexer_advance(lexer_t *lexer)
{
    /* Advance cursor to next character if not yet at end of source buffer */
    if (lexer->c != '\0') {
        lexer->i++;
        lexer->c = lexer->contents[lexer->i];
    }
}

/**
 * @brief Skip whitespace characters in the source and track line numbers.
 * @param lexer Pointer to active lexer.
 */
static void lexer_skip_whitespace(lexer_t *lexer)
{
    /* Skip spaces and newline characters, tracking line numbers and line start pointers */
    while (lexer->c == ' ' || lexer->c == '\n') {
        if (lexer->c == '\n') {
            lexer->line_num++;
            lexer->line = lexer->contents + lexer->i + 1; 
        }
        lexer_advance(lexer);
    }
}

/**
 * @brief Return current character as a newly allocated null-terminated string.
 * @param lexer Pointer to active lexer.
 * @return Tracked single-character string.
 */
static char *lexer_get_current_char_as_string(lexer_t *lexer)
{
    char *str = tracked_calloc(lexer->alloc_list, 2, sizeof(char));
    str[0] = lexer->c;
    str[1] = '\0';
    return str;
}

/**
 * @brief Collect a string literal enclosed in double quotes.
 * @param lexer Pointer to active lexer.
 * @return Newly allocated string token.
 */
static token_t *lexer_collect_string(lexer_t *lexer)
{        
    lexer_advance(lexer); // go through open quote
    char *value = tracked_calloc(lexer->alloc_list, 1, sizeof(char));
    value[0] = '\0';

    /* Accumulate characters until closing double quote or unexpected EOF */
    while (lexer->c != '"' && lexer->c != '\0') {
        /* Process escape sequences */
        if (lexer->c == '\\') {
            lexer_advance(lexer);
            switch (lexer->c) {
            case 'n':
                lexer->c = '\n';
                break;
            case 't':
                lexer->c = '\t';
                break;
            case 'r':
                lexer->c = '\r';
                break;
            }
        }
        char *s = lexer_get_current_char_as_string(lexer);
        value = tracked_realloc(lexer->alloc_list, value, strlen(value) + strlen(s) + 1);
        strcat(value, s);
        tracked_free(lexer->alloc_list, s);
        lexer_advance(lexer);
    }

    /* Check for unclosed string literal */
    if (lexer->c == '\0') {
        ti_log("[Lexer Error] Missing close quote at line %d\n", lexer->line_num);
        ti_log_line(lexer->line);
        ti_fatal();
    }
    lexer_advance(lexer); // Skip close quote
    return token_init(lexer->alloc_list, TOKEN_STRING, value);
}

/**
 * @brief Collect an identifier or keyword token.
 * @param lexer Pointer to active lexer.
 * @return Newly allocated identifier or keyword token.
 */
static token_t *lexer_collect_id(lexer_t *lexer)
{
    /* Accumulate alphanumeric characters and underscores */
    char *value = tracked_calloc(lexer->alloc_list, 1, sizeof(char));
    while (isalnum(lexer->c) || lexer->c == '_') {
        char *s = lexer_get_current_char_as_string(lexer);
        value = tracked_realloc(lexer->alloc_list, value, strlen(value) + strlen(s) + 1);
        strcat(value, s);
        tracked_free(lexer->alloc_list, s);
        lexer_advance(lexer);
    }

    /* Match type keywords (token type alone identifies the keyword, free raw string) */
    if (strcmp(value, "int") == 0)    { tracked_free(lexer->alloc_list, value); return token_init(lexer->alloc_list, TOKEN_KW_INT, NULL); }
    if (strcmp(value, "float") == 0)  { tracked_free(lexer->alloc_list, value); return token_init(lexer->alloc_list, TOKEN_KW_FLOAT, NULL); }
    if (strcmp(value, "string") == 0) { tracked_free(lexer->alloc_list, value); return token_init(lexer->alloc_list, TOKEN_KW_STRING, NULL); }
    if (strcmp(value, "bool") == 0)   { tracked_free(lexer->alloc_list, value); return token_init(lexer->alloc_list, TOKEN_KW_BOOL, NULL); }
    if (strcmp(value, "dict") == 0)   { tracked_free(lexer->alloc_list, value); return token_init(lexer->alloc_list, TOKEN_KW_DICT, NULL); }
    if (strcmp(value, "void") == 0)   { tracked_free(lexer->alloc_list, value); return token_init(lexer->alloc_list, TOKEN_KW_VOID, NULL); }

    /* Match control flow keywords */
    if (strcmp(value, "if") == 0)       { tracked_free(lexer->alloc_list, value); return token_init(lexer->alloc_list, TOKEN_KW_IF, NULL); }
    if (strcmp(value, "else") == 0)     { tracked_free(lexer->alloc_list, value); return token_init(lexer->alloc_list, TOKEN_KW_ELSE, NULL); }
    if (strcmp(value, "while") == 0)    { tracked_free(lexer->alloc_list, value); return token_init(lexer->alloc_list, TOKEN_KW_WHILE, NULL); }
    if (strcmp(value, "return") == 0)   { tracked_free(lexer->alloc_list, value); return token_init(lexer->alloc_list, TOKEN_KW_RETURN, NULL); }
    if (strcmp(value, "break") == 0)    { tracked_free(lexer->alloc_list, value); return token_init(lexer->alloc_list, TOKEN_KW_BREAK, NULL); }
    if (strcmp(value, "continue") == 0) { tracked_free(lexer->alloc_list, value); return token_init(lexer->alloc_list, TOKEN_KW_CONTINUE, NULL); }

    /* Match boolean literals */
    if (strcmp(value, "true") == 0) {
        return token_init(lexer->alloc_list, TOKEN_BOOL, value);
    }
    if (strcmp(value, "false") == 0) {
        return token_init(lexer->alloc_list, TOKEN_BOOL, value);
    }

    /* Fall through to generic user identifier */
    return token_init(lexer->alloc_list, TOKEN_ID, value);
}

/**
 * @brief Collect an integer or float literal token.
 * @param lexer Pointer to active lexer.
 * @return Newly allocated integer or float token.
 */
static token_t *lexer_collect_number(lexer_t *lexer)
{
    /* Accumulate integer digit sequence */
    char *value = tracked_calloc(lexer->alloc_list, 1, sizeof(char));
    while (isdigit(lexer->c)) {
        char *s = lexer_get_current_char_as_string(lexer);
        value = tracked_realloc(lexer->alloc_list, value, strlen(value) + strlen(s) + 1);
        strcat(value, s);
        tracked_free(lexer->alloc_list, s);
        lexer_advance(lexer);
    }

    /* Check for decimal point to process floating-point number */
    if (lexer->c == '.') {
        char *dot = lexer_get_current_char_as_string(lexer);
        value = tracked_realloc(lexer->alloc_list, value, strlen(value) + strlen(dot) + 1);
        strcat(value, dot);
        tracked_free(lexer->alloc_list, dot);
        lexer_advance(lexer);

        /* Accumulate fractional digits */
        while (isdigit(lexer->c)) {
            char *s = lexer_get_current_char_as_string(lexer);
            value = tracked_realloc(lexer->alloc_list, value, strlen(value) + strlen(s) + 1);
            strcat(value, s);
            tracked_free(lexer->alloc_list, s);
            lexer_advance(lexer);
        }

        /* Reject invalid alphanumeric suffixes on float constants */
        if (isalpha(lexer->c) || lexer->c == '_') {
            ti_log("[Lexer Error] Invalid suffix '%c' on float constant '%s' at line %d\n", lexer->c, value, lexer->line_num);
            ti_log_line(lexer->line);
            ti_fatal();
        }
        return token_init(lexer->alloc_list, TOKEN_FLOAT, value);
    }

    /* Reject invalid alphanumeric suffixes on integer constants */
    if (isalpha(lexer->c) || lexer->c == '_') {
        ti_log("[Lexer Error] Invalid suffix '%c' on integer constant '%s' at line %d\n", lexer->c, value, lexer->line_num);
        ti_log_line(lexer->line);
        ti_fatal();
    } 
    return token_init(lexer->alloc_list, TOKEN_INT, value);
}

/* -------------------- Public Functions -------------------- */

/* Initialize a new lexer for the given source string */
lexer_t *lexer_init(alloc_hdr_t **list, char *str) 
{
    lexer_t *lexer = tracked_calloc(list, 1, sizeof(struct LEXER_STRUCT));
    if (!lexer) {
        return NULL;
    }
    lexer->alloc_list = list;
    lexer->contents = str;
    lexer->i = 0;
    lexer->c = lexer->contents[lexer->i];
    lexer->line_num = 1;
    lexer->line = lexer->contents;
    return lexer;
}

/* Create a shallow copy of the given lexer state */
lexer_t *lexer_copy(lexer_t *lexer)
{
    alloc_hdr_t **list = lexer ? lexer->alloc_list : NULL;
    lexer_t *new_lexer = tracked_calloc(list, 1, sizeof(struct LEXER_STRUCT));
    if (!new_lexer || !lexer) {
        return new_lexer;
    }
    new_lexer->alloc_list = lexer->alloc_list;
    new_lexer->contents = lexer->contents;
    new_lexer->i = lexer->i;
    new_lexer->c = lexer->c;
    new_lexer->line_num = lexer->line_num;
    new_lexer->line = lexer->line;
    return new_lexer;
}

/* Tokenize and return the next token from source */
token_t *lexer_get_next_token(lexer_t *lexer)
{
    while (lexer->c != '\0' && lexer->c != EOF && lexer->c != '\000') {
        /* Skip leading whitespace and blank lines */
        if (lexer->c == ' ' || lexer->c == '\n') {
            lexer_skip_whitespace(lexer);
            continue;
        }

        /* String literal: starts with double quote */
        if (lexer->c == '"') {
            return lexer_collect_string(lexer);
        }

        /* Numeric literal: starts with a digit */
        if (isdigit(lexer->c)) {
            return lexer_collect_number(lexer);
        }

        /* Identifier or keyword: starts with an alphabetic character */
        if (isalpha(lexer->c)) {
            return lexer_collect_id(lexer);
        }

        /* Single-character and two-character operators/delimiters */
        switch (lexer->c) {
        case '(': return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_LPAREN, NULL));
        case ')': return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_RPAREN, NULL));
        case '[': return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_LBRACKET, NULL));
        case ']': return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_RBRACKET, NULL));
        case '{': return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_LBRACE, NULL));
        case '}': return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_RBRACE, NULL));
        case ';': return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_SEMI, NULL));
        case ':': return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_COLON, NULL));
        case '+': return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_PLUS, NULL));
        case '-': return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_MINUS, NULL));
        case ',': return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_COMMA, NULL));
        case '*': return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_STAR, NULL));
        case '/': {
            /* Check for single-line (//) or multi-line comments vs division operator (/) */
            lexer_advance(lexer);
            if (lexer->c == '/') {
                /* Consume rest of the line for single-line comment */
                while (lexer->c != '\n' && lexer->c != '\0' && lexer->c != EOF) {
                    lexer_advance(lexer);
                }
                continue; /* Skip to next token */
            } else if (lexer->c == '*') {
                /* Consume multi-line comment block */
                lexer_advance(lexer);
                while (lexer->c != '\0' && lexer->c != EOF) {
                    if (lexer->c == '*') {
                        lexer_advance(lexer);
                        if (lexer->c == '/') {
                            lexer_advance(lexer);
                            break; /* End of multi-line comment */
                        }
                    } else {
                        if (lexer->c == '\n') {
                            lexer->line_num++;
                            lexer->line = lexer->contents + lexer->i + 1;
                        }
                        lexer_advance(lexer);
                    }
                }
                continue; /* Skip to next token */
            }
            /* Not a comment, rollback lookahead and return slash operator */
            lexer_go_back(lexer);
            return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_SLASH, NULL));
        }
        case '&': {
            /* Check for logical AND (&&) vs bitwise AND (&) */
            lexer_advance(lexer);
            if (lexer->c == '&') {
                return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_LOGIC_AND, NULL));
            }
            lexer_go_back(lexer);
            return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_AND, NULL));
        }
        case '|': {
            /* Check for logical OR (||) vs bitwise OR (|) */
            lexer_advance(lexer);
            if (lexer->c == '|') {
                return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_LOGIC_OR, NULL));
            }
            lexer_go_back(lexer);
            return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_OR, NULL));
        }
        case '=': {
            /* Check for equality operator (==) vs assignment operator (=) */
            lexer_advance(lexer);
            if (lexer->c == '=') {
                return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_DEQUALS, NULL));
            }
            lexer_go_back(lexer);
            return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_EQUALS, NULL));
        }
        case '!': {
            /* Check for inequality (!=) vs logical NOT (!) */
            lexer_advance(lexer);
            if (lexer->c == '=') {
                return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_NOT_EQUALS, NULL));
            }
            lexer_go_back(lexer);
            return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_NOT, NULL));
        }
        case '<': {
            /* Check for less-than-or-equal (<=) vs less-than (<) */
            lexer_advance(lexer);
            if (lexer->c == '=') {
                return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_LTE, NULL));
            }
            lexer_go_back(lexer);
            return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_LT, NULL));
        }
        case '>': {
            /* Check for greater-than-or-equal (>=) vs greater-than (>) */
            lexer_advance(lexer);
            if (lexer->c == '=') {
                return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_GTE, NULL));
            }
            lexer_go_back(lexer);
            return lexer_advance_with_token(lexer, token_init(lexer->alloc_list, TOKEN_GT, NULL));
        }
        default:
            ti_log("[Lexer Error] Unexpected character %c, at line %d\n", lexer->c, lexer->line_num);
            ti_log_line(lexer->line);
            ti_fatal();
        }
    }
    /* End of source text reached */
    return token_init(lexer->alloc_list, TOKEN_EOF, NULL);
}
