#ifndef TOKEN_H
#define TOKEN_H
typedef  struct TOKEN_STRUCT{
  enum {
        /* 1. IDENTIFIERS & LITERALS */
        TOKEN_ID,           // x, my_var, foo
        TOKEN_INT,          // 10, 42
        TOKEN_FLOAT,        // 3.14
        TOKEN_STRING,       // "hello"

        /* 2. KEYWORDS */
        TOKEN_KW_INT,       // int
        TOKEN_KW_FLOAT,     // float
        TOKEN_KW_STRING,    // string
        TOKEN_KW_IF,        // if
        TOKEN_KW_ELSE,      // else
        TOKEN_KW_WHILE,     // while
        TOKEN_KW_RETURN,    // return

        /* 3. OPERATORS & DELIMITERS */
        TOKEN_EQUALS,       // =
        TOKEN_PLUS,         // +
        TOKEN_MINUS,        // -
        TOKEN_STAR,         // *
        TOKEN_SLASH,        // /
        TOKEN_SEMI,         // ;
        TOKEN_LPAREN,       // (
        TOKEN_RPAREN,       // )
        TOKEN_LBRACE,       // {
        TOKEN_RBRACE,       // }
        TOKEN_COMMA,         // ,
        
        TOKEN_EOF           // End of file
  } type;    char *value;
} token_t;

token_t *init_token(int type, char* value);
#endif // !TOKEN_H
