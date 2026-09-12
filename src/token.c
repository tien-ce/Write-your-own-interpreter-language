#include "include/token.h"
#include "include/tracked_memory.h"
#include <stdlib.h>

/* -------------------- Public Functions -------------------- */

/* Allocate and initialize a new token */
token_t *token_init(int type, char *value)
{
    token_t *token = tracked_calloc(1, sizeof(struct TOKEN_STRUCT));
    token->type = type;
    token->value = value;
    return token;
}

/* Convert token type to human-readable string */
const char *token_to_str(int token_type)
{
    switch (token_type) {
    case TOKEN_ID:          return "ID";
    case TOKEN_INT:         return "INT";
    case TOKEN_FLOAT:       return "FLOAT";
    case TOKEN_STRING:      return "STRING";
    case TOKEN_BOOL:        return "BOOL";
    case TOKEN_KW_VOID:     return "VOID";
    case TOKEN_KW_INT:      return "'int'";
    case TOKEN_KW_FLOAT:    return "'float'";
    case TOKEN_KW_STRING:   return "'string'";
    case TOKEN_KW_BOOL:     return "'bool'";
    case TOKEN_KW_IF:       return "'if'";
    case TOKEN_KW_ELSE:     return "'else'";
    case TOKEN_KW_WHILE:    return "'while'";
    case TOKEN_KW_RETURN:   return "'return'";
    case TOKEN_KW_BREAK:    return "'break'";
    case TOKEN_EQUALS:      return "'='";
    case TOKEN_DEQUALS:     return "'=='";
    case TOKEN_NOT:         return "'!'";
    case TOKEN_NOT_EQUALS:  return "'!='";
    case TOKEN_AND:         return "'&'";
    case TOKEN_LOGIC_AND:   return "'&&'";
    case TOKEN_OR:          return "'|'";
    case TOKEN_LOGIC_OR:    return "'||'";
    case TOKEN_PLUS:        return "'+'";
    case TOKEN_MINUS:       return "'-'";
    case TOKEN_STAR:        return "'*'";
    case TOKEN_SLASH:       return "'/'";
    case TOKEN_SEMI:        return "';'";
    case TOKEN_LPAREN:      return "'('";
    case TOKEN_RPAREN:      return "')'";
    case TOKEN_LBRACE:      return "'{'";
    case TOKEN_RBRACE:      return "'}'";
    case TOKEN_LBRACKET:    return "'['";
    case TOKEN_RBRACKET:    return "']'";
    case TOKEN_COMMA:       return "','";
    case TOKEN_LT:          return "'<'";
    case TOKEN_LTE:         return "'<='";
    case TOKEN_GT:          return "'>'";
    case TOKEN_GTE:         return "'>='";
    case TOKEN_EOF:         return "EOF";
    default:                return "UNKNOWN";
    }
}
