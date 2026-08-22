#ifndef TOKEN_H
#define TOKEN_H

/* -------------------- Token Types & Structure -------------------- */

typedef struct TOKEN_STRUCT {
  enum {
        /* 1. IDENTIFIERS & LITERALS */
        TOKEN_ID,           // x, my_var, foo
        TOKEN_INT,          // 10, 42
        TOKEN_FLOAT,        // 3.14
        TOKEN_STRING,       // "hello"
        TOKEN_BOOL,         // true, false 

        /* 2. KEYWORDS */
        TOKEN_KW_INT,       // int
        TOKEN_KW_FLOAT,     // float
        TOKEN_KW_STRING,    // string
        TOKEN_KW_IF,        // if
        TOKEN_KW_ELSE,      // else
        TOKEN_KW_WHILE,     // while
        TOKEN_KW_RETURN,    // return
        TOKEN_KW_BREAK,

        /* 3. OPERATORS & DELIMITERS */
        TOKEN_EQUALS,       // =
        TOKEN_DEQUALS,      // ==
        TOKEN_NOT,          // !
        TOKEN_NOT_EQUALS,   // !=
        TOKEN_PLUS,         // +
        TOKEN_MINUS,        // -
        TOKEN_STAR,         // *
        TOKEN_SLASH,        // /
        TOKEN_SEMI,         // ;
        TOKEN_LPAREN,       // (
        TOKEN_RPAREN,       // )
        TOKEN_LBRACE,       // {
        TOKEN_RBRACE,       // }
        TOKEN_LBRACKET,     // [
        TOKEN_RBRACKET,     // ]
        TOKEN_COMMA,        // ,
        TOKEN_LT,           // <
        TOKEN_LTE,          // <= 
        TOKEN_GT,           // >
        TOKEN_GTE,          // >=
        TOKEN_EOF           // End of file
  } type;
  char *value;
} token_t;

/* -------------------- Public Functions -------------------- */

/**
 * @brief Allocate and initialize a new token.
 * @param type Token type enum.
 * @param value String payload of the token (or NULL).
 * @return Pointer to newly allocated token_t.
 */
token_t *init_token(int type, char *value);

#endif // !TOKEN_H
