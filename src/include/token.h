#ifndef TOKEN_H
#define TOKEN_H

/* -------------------- Token Types & Structure -------------------- */

typedef enum token_type {
    /* 1. IDENTIFIERS & LITERALS */
    TOKEN_ID,           // x, my_var, foo
    TOKEN_INT,          // 10, 42
    TOKEN_FLOAT,        // 3.14
    TOKEN_STRING,       // "hello"
    TOKEN_BOOL,         // true, false 

    /* 2. KEYWORDS */
    TOKEN_KW_VOID,
    TOKEN_KW_INT,       // int
    TOKEN_KW_FLOAT,     // float
    TOKEN_KW_STRING,    // string
    TOKEN_KW_BOOL,      // bool 
    TOKEN_KW_IF,        // if
    TOKEN_KW_ELSE,      // else
    TOKEN_KW_WHILE,     // while
    TOKEN_KW_RETURN,    // return
    TOKEN_KW_BREAK,     // break
    TOKEN_KW_CONTINUE,  // continue

    /* 3. OPERATORS & DELIMITERS */
    TOKEN_EQUALS,       // =
    TOKEN_DEQUALS,      // ==
    TOKEN_NOT,          // !
    TOKEN_NOT_EQUALS,   // !=
    TOKEN_AND,
    TOKEN_LOGIC_AND,
    TOKEN_OR,
    TOKEN_LOGIC_OR,
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
} token_type_t;

typedef struct TOKEN_STRUCT {
    token_type_t type;
    char *value;
} token_t;

/* -------------------- Public Functions -------------------- */

/**
 * @brief Allocate and initialize a new token.
 * @param type Token type enum.
 * @param value String payload of the token (or NULL).
 * @return Pointer to newly allocated token_t.
 */
token_t *token_init(int type, char *value);

/**
 * @brief Convert token type enum to human-readable string.
 * @param token_type Token type enum value.
 * @return Static string name of the token type.
 */
const char *token_to_str(int token_type);

#endif /* !TOKEN_H */
