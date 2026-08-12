#ifndef TOKEN_H
#define TOKEN_H
typedef  struct TOKEN_STRUCT{
    enum {
        TOKEN_ID,
        TOKEN_EQUALS,
        TOKEN_STRING,
        TOKEN_SEMI, // semicolon ;
        TOKEN_LPAREN, // left parenthesys (
        TOKEN_RPAREN, // right parenthesys )
    } type;
    char *value;
} token_t;

token_t *init_token(int type, char* value);
#endif // !TOKEN_H
