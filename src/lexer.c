#include "include/lexer.h"
#include "include/token.h"
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
    uint32_t length = strlen(lexer->contents); 
    if (lexer->c != '\0' && lexer->i < length) {
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
    char *str = tracked_calloc(2, sizeof(char));
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
    char *value = tracked_calloc(1, sizeof(char));
    value[0] = '\0';

    while (lexer->c != '"' && lexer->c != '\0') {
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
        value = tracked_realloc(value, strlen(value) + strlen(s) + 1);
        strcat(value, s);
        tracked_free(s);
        lexer_advance(lexer);
    }

    if (lexer->c == '\0') {
        ti_log("[Lexer Error] Missing close quote at line %d\n", lexer->line_num);
        ti_log_line(lexer->line);
        ti_fatal();
    }
    lexer_advance(lexer); // Skip close quote
    return token_init(TOKEN_STRING, value);
}

/**
 * @brief Collect an identifier or keyword token.
 * @param lexer Pointer to active lexer.
 * @return Newly allocated identifier or keyword token.
 */
static token_t *lexer_collect_id(lexer_t *lexer)
{
    char *value = tracked_calloc(1, sizeof(char));
    while (isalnum(lexer->c) || lexer->c == '_') {
        char *s = lexer_get_current_char_as_string(lexer);
        value = tracked_realloc(value, strlen(value) + strlen(s) + 1);
        strcat(value, s);
        tracked_free(s);
        lexer_advance(lexer);
    }

    /* Check KEYWORDS (type alone tells us the value, so don't keep it) */
    if (strcmp(value, "int") == 0)    { tracked_free(value); return token_init(TOKEN_KW_INT, NULL); }
    if (strcmp(value, "float") == 0)  { tracked_free(value); return token_init(TOKEN_KW_FLOAT, NULL); }
    if (strcmp(value, "string") == 0) { tracked_free(value); return token_init(TOKEN_KW_STRING, NULL); }
    if (strcmp(value, "bool") == 0)   { tracked_free(value); return token_init(TOKEN_KW_BOOL, NULL); }
    if (strcmp(value, "void") == 0)   { tracked_free(value); return token_init(TOKEN_KW_VOID, NULL); }

    if (strcmp(value, "if") == 0)       { tracked_free(value); return token_init(TOKEN_KW_IF, NULL); }
    if (strcmp(value, "else") == 0)     { tracked_free(value); return token_init(TOKEN_KW_ELSE, NULL); }
    if (strcmp(value, "while") == 0)    { tracked_free(value); return token_init(TOKEN_KW_WHILE, NULL); }
    if (strcmp(value, "return") == 0)   { tracked_free(value); return token_init(TOKEN_KW_RETURN, NULL); }
    if (strcmp(value, "break") == 0)    { tracked_free(value); return token_init(TOKEN_KW_BREAK, NULL); }
    if (strcmp(value, "continue") == 0) { tracked_free(value); return token_init(TOKEN_KW_CONTINUE, NULL); }

    /* Boolean literals */
    if (strcmp(value, "true") == 0) {
        return token_init(TOKEN_BOOL, value);
    }
    if (strcmp(value, "false") == 0) {
        return token_init(TOKEN_BOOL, value);
    }

    return token_init(TOKEN_ID, value);
}

/**
 * @brief Collect an integer or float literal token.
 * @param lexer Pointer to active lexer.
 * @return Newly allocated integer or float token.
 */
static token_t *lexer_collect_number(lexer_t *lexer)
{
    char *value = tracked_calloc(1, sizeof(char));
    while (isdigit(lexer->c)) {
        char *s = lexer_get_current_char_as_string(lexer);
        value = tracked_realloc(value, strlen(value) + strlen(s) + 1);
        strcat(value, s);
        tracked_free(s);
        lexer_advance(lexer);
    }

    if (lexer->c == '.') {
        char *dot = lexer_get_current_char_as_string(lexer);
        value = tracked_realloc(value, strlen(value) + strlen(dot) + 1);
        strcat(value, dot);
        tracked_free(dot);
        lexer_advance(lexer);

        /* Fractional part */
        while (isdigit(lexer->c)) {
            char *s = lexer_get_current_char_as_string(lexer);
            value = tracked_realloc(value, strlen(value) + strlen(s) + 1);
            strcat(value, s);
            tracked_free(s);
            lexer_advance(lexer);
        }

        if (isalpha(lexer->c) || lexer->c == '_') {
            ti_log("[Lexer Error] Invalid suffix '%c' on float constant '%s' at line %d\n", lexer->c, value, lexer->line_num);
            ti_log_line(lexer->line);
            ti_fatal();
        }
        return token_init(TOKEN_FLOAT, value);
    }

    /* Integer number */
    if (isalpha(lexer->c) || lexer->c == '_') {
        ti_log("[Lexer Error] Invalid suffix '%c' on integer constant '%s' at line %d\n", lexer->c, value, lexer->line_num);
        ti_log_line(lexer->line);
        ti_fatal();
    } 
    return token_init(TOKEN_INT, value);
}

/* -------------------- Public Functions -------------------- */

/* Initialize a new lexer for the given source string */
lexer_t *lexer_init(char *str) 
{
    lexer_t *lexer = tracked_calloc(1, sizeof(struct LEXER_STRUCT));
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
    lexer_t *new_lexer = tracked_calloc(1, sizeof(struct LEXER_STRUCT));
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
        if (lexer->c == ' ' || lexer->c == '\n') {
            lexer_skip_whitespace(lexer);
            continue;
        }

        if (lexer->c == '"') {
            return lexer_collect_string(lexer);
        }

        if (isdigit(lexer->c)) {
            return lexer_collect_number(lexer);
        }

        if (isalpha(lexer->c)) {
            return lexer_collect_id(lexer);
        }

        switch (lexer->c) {
        case '(': return lexer_advance_with_token(lexer, token_init(TOKEN_LPAREN, NULL));
        case ')': return lexer_advance_with_token(lexer, token_init(TOKEN_RPAREN, NULL));
        case ';': return lexer_advance_with_token(lexer, token_init(TOKEN_SEMI, NULL));
        case '+': return lexer_advance_with_token(lexer, token_init(TOKEN_PLUS, NULL));
        case '-': return lexer_advance_with_token(lexer, token_init(TOKEN_MINUS, NULL));
        case ',': return lexer_advance_with_token(lexer, token_init(TOKEN_COMMA, NULL));
        case '&': {
            lexer_advance(lexer);
            if (lexer->c == '&') {
                return lexer_advance_with_token(lexer, token_init(TOKEN_LOGIC_AND, NULL));
            }
            lexer_go_back(lexer);
            return lexer_advance_with_token(lexer, token_init(TOKEN_AND, NULL));
        }
        case '|': {
            lexer_advance(lexer);
            if (lexer->c == '|') {
                return lexer_advance_with_token(lexer, token_init(TOKEN_LOGIC_OR, NULL));
            }
            lexer_go_back(lexer);
            return lexer_advance_with_token(lexer, token_init(TOKEN_OR, NULL));
        }
        case '=': {
            lexer_advance(lexer);
            if (lexer->c == '=') {
                return lexer_advance_with_token(lexer, token_init(TOKEN_DEQUALS, NULL));
            }
            lexer_go_back(lexer);
            return lexer_advance_with_token(lexer, token_init(TOKEN_EQUALS, NULL));
        }
        case '!': {
            lexer_advance(lexer);
            if (lexer->c == '=') {
                return lexer_advance_with_token(lexer, token_init(TOKEN_NOT_EQUALS, NULL));
            }
            lexer_go_back(lexer);
            return lexer_advance_with_token(lexer, token_init(TOKEN_NOT, NULL));
        }
        case '<': {
            lexer_advance(lexer);
            if (lexer->c == '=') {
                return lexer_advance_with_token(lexer, token_init(TOKEN_LTE, NULL));
            }
            lexer_go_back(lexer);
            return lexer_advance_with_token(lexer, token_init(TOKEN_LT, NULL));
        }
        case '>': {
            lexer_advance(lexer);
            if (lexer->c == '=') {
                return lexer_advance_with_token(lexer, token_init(TOKEN_GTE, NULL));
            }
            lexer_go_back(lexer);
            return lexer_advance_with_token(lexer, token_init(TOKEN_GT, NULL));
        }
        case '{': return lexer_advance_with_token(lexer, token_init(TOKEN_LBRACE, NULL));
        case '}': return lexer_advance_with_token(lexer, token_init(TOKEN_RBRACE, NULL));
        default:
            ti_log("[Lexer Error] Unexpected character %c, at line %d\n", lexer->c, lexer->line_num);
            ti_log_line(lexer->line);
            ti_fatal();
        }
    }
    return token_init(TOKEN_EOF, NULL);
}
