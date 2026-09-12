#include "include/debug.h"
#include "include/token.h"
#include "include/AST.h"

/* -------------------- Public Functions -------------------- */

/* Convert token type to readable debug string */
const char *token_type_to_str(int type)
{
    switch (type) {
    case TOKEN_ID:        return "TOKEN_ID";
    case TOKEN_INT:       return "TOKEN_INT";
    case TOKEN_FLOAT:     return "TOKEN_FLOAT";
    case TOKEN_STRING:    return "TOKEN_STRING";
    case TOKEN_BOOL:      return "TOKEN_BOOL";
    case TOKEN_KW_VOID:   return "TOKEN_KW_VOID";
    case TOKEN_KW_INT:    return "TOKEN_KW_INT";
    case TOKEN_KW_FLOAT:  return "TOKEN_KW_FLOAT";
    case TOKEN_KW_STRING: return "TOKEN_KW_STRING";
    case TOKEN_KW_BOOL:   return "TOKEN_KW_BOOL";
    case TOKEN_KW_IF:     return "TOKEN_KW_IF";
    case TOKEN_KW_ELSE:   return "TOKEN_KW_ELSE";
    case TOKEN_KW_WHILE:  return "TOKEN_KW_WHILE";
    case TOKEN_KW_RETURN: return "TOKEN_KW_RETURN";
    case TOKEN_KW_BREAK:  return "TOKEN_KW_BREAK";
    case TOKEN_EQUALS:    return "TOKEN_EQUALS";
    case TOKEN_DEQUALS:   return "TOKEN_DEQUALS";
    case TOKEN_NOT:       return "TOKEN_NOT";
    case TOKEN_NOT_EQUALS: return "TOKEN_NOT_EQUALS";
    case TOKEN_AND:       return "TOKEN_AND";
    case TOKEN_LOGIC_AND: return "TOKEN_LOGIC_AND";
    case TOKEN_OR:        return "TOKEN_OR";
    case TOKEN_LOGIC_OR:  return "TOKEN_LOGIC_OR";
    case TOKEN_PLUS:      return "TOKEN_PLUS";
    case TOKEN_MINUS:     return "TOKEN_MINUS";
    case TOKEN_STAR:      return "TOKEN_STAR";
    case TOKEN_SLASH:     return "TOKEN_SLASH";
    case TOKEN_SEMI:      return "TOKEN_SEMI";
    case TOKEN_LPAREN:    return "TOKEN_LPAREN";
    case TOKEN_RPAREN:    return "TOKEN_RPAREN";
    case TOKEN_LBRACE:    return "TOKEN_LBRACE";
    case TOKEN_RBRACE:    return "TOKEN_RBRACE";
    case TOKEN_LBRACKET:  return "TOKEN_LBRACKET";
    case TOKEN_RBRACKET:  return "TOKEN_RBRACKET";
    case TOKEN_COMMA:     return "TOKEN_COMMA";
    case TOKEN_LT:        return "TOKEN_LT";
    case TOKEN_LTE:       return "TOKEN_LTE";
    case TOKEN_GT:        return "TOKEN_GT";
    case TOKEN_GTE:       return "TOKEN_GTE";
    case TOKEN_EOF:       return "TOKEN_EOF";
    default:              return "TOKEN_UNKNOWN";
    }
}

/* Convert AST node type to readable string */
const char *ast_type_to_str(int type)
{
    switch (type) {
    case AST_INT_LITERAL:         return "AST_INT_LITERAL";
    case AST_FLOAT_LITERAL:       return "AST_FLOAT_LITERAL";
    case AST_STRING_LITERAL:      return "AST_STRING_LITERAL";
    case AST_BOOLEAN:             return "AST_BOOLEAN";
    case AST_IDENTIFIER:          return "AST_IDENTIFIER";
    case AST_BINARY_EXPR:         return "AST_BINARY_EXPR";
    case AST_UNARY_EXPR:          return "AST_UNARY_EXPR";
    case AST_FUNCTION_CALL:       return "AST_FUNCTION_CALL";
    case AST_FUNCTION_DEFINITION: return "AST_FUNCTION_DEFINITION";
    case AST_ARRAY_ACCESS:        return "AST_ARRAY_ACCESS";
    case AST_COMPOUND:            return "AST_COMPOUND";
    case AST_IF_STATEMENT:        return "AST_IF_STATEMENT";
    case AST_WHILE_STATEMENT:     return "AST_WHILE_STATEMENT";
    case AST_FOR_STATEMENT:       return "AST_FOR_STATEMENT";
    case AST_RETURN_STATEMENT:    return "AST_RETURN_STATEMENT";
    case AST_VARIABLE_DEFINITION: return "AST_VARIABLE_DEFINITION";
    case AST_ASSIGNMENT:          return "AST_ASSIGNMENT";
    case AST_PROGRAM:             return "AST_PROGRAM";
    default:                      return "AST_UNKNOWN";
    }
}

/* Convert binary operator enum to string symbol */
const char *binary_op_to_str(int op)
{
    switch (op) {
    case OP_ADD:         return "+";
    case OP_SUB:         return "-";
    case OP_MUL:         return "*";
    case OP_DIV:         return "/";
    case OP_MOD:         return "%";
    case OP_DEQ:         return "==";
    case OP_NEQ:         return "!=";
    case OP_LT:          return "<";
    case OP_GT:          return ">";
    case OP_LTE:         return "<=";
    case OP_GTE:         return ">=";
    case OP_LOGICAL_AND: return "&&";
    case OP_LOGICAL_OR:  return "||";
    default:             return "?";
    }
}

/* Convert unary operator enum to string symbol */
const char *unary_op_to_str(int op)
{
    switch (op) {
    case OP_NOT:  return "!";
    case OP_NEG:  return "-";
    case OP_POS:  return "+";
    case OP_BNOT: return "~";
    default:      return "?";
    }
}

/* Convert variable type enum to type keyword string */
const char *var_type_to_str(int type)
{
    switch (type) {
    case VAR_TYPE_INT:    return "int";
    case VAR_TYPE_FLOAT:  return "float";
    case VAR_TYPE_STRING: return "string";
    case VAR_TYPE_BOOL:   return "bool";
    case VAR_TYPE_VOID:   return "void";
    default:              return "?";
    }
}
