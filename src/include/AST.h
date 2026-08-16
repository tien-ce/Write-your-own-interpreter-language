#ifndef AST_H
#define AST_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef struct AST_STRUCT {
  enum {
    /* 1. LITERALS & IDENTIFIERS */
    AST_INT_LITERAL,          // 10
    AST_FLOAT_LITERAL,        // 3.14
    AST_STRING_LITERAL,       // "hello"
    AST_BOOLEAN,                 // bool 
    AST_IDENTIFIER,           // x, my_var (variable/function reference)

    /* 2. EXPRESSIONS */
    AST_BINARY_EXPR,          // x + y, a == b
    AST_UNARY_EXPR,           // -x, !flag
    AST_FUNCTION_CALL,        // mylove("van anh")
    AST_ARRAY_ACCESS,         // arr[0]

    /* 3. STATEMENTS */
    AST_COMPOUND,             // Block statement { ... }
    AST_IF_STATEMENT,         // if (cond) { ... } else { ... }
    AST_WHILE_STATEMENT,      // while (cond) { ... }
    AST_FOR_STATEMENT,        // for (init; cond; post) { ... }
    AST_RETURN_STATEMENT,     // return expr;
    AST_VARIABLE_DEFINITION,  // int x = 5;
    AST_ASSIGNMENT,      // x = 10

    AST_NOOP,

    /* 5. ROOT / PROGRAM */
    AST_PROGRAM, 
  }type;              // Root node containing all file statements

  union {
    /* 1. LITERALS and IDENTIFIERS */
    int int_value;
    double float_value;
    bool bool_value;
    char *string_value;
    char *identifier; // Name of variable

    /* 2.EXPRESSIONS */ 
    struct {
        enum {
          OP_ADD,             // +
          OP_SUB,             // -
          OP_MUL,             // *
          OP_DIV,             // /
          OP_MOD,             // %
          OP_EQ,              // ==
          OP_NEQ,             // !=
          OP_LT,              // <
          OP_GT,              // >
          OP_LTE,             // <=
          OP_GTE,             // >=
          OP_LOGICAL_AND,     // &&
          OP_LOGICAL_OR,      // ||
         }op; 
        struct AST_STRUCT *left; 
        struct AST_STRUCT *right;
    } binary_expr;

    struct {
      enum {
        OP_NOT,   // !
        OP_NEG,   // - (unary minus)
        OP_POS,   // + (unary plus)
        OP_BNOT   // ~ (bitwise not)
      } op;
      struct AST_STRUCT *operand;
    } unary_expr;    /* variable definition */ 

    struct {
      char *id;
      struct AST_STRUCT *index_expr;
    } array_access;

    /* Variable difinition statement */
    struct {
      enum {
        VAR_TYPE_INT,
        VAR_TYPE_FLOAT,
        VAR_TYPE_STRING,
      } variable_type;
      char *variable_name;
      struct AST_STRUCT *value;
    } variable_definition;

    /* Compound */ 
    struct {
      struct AST_STRUCT **compound_value; // Array contains ast_statements
      int compound_size;
    }compound;
  
    /* While statement */
    struct {
      struct AST_STRUCT *condition; // Point to expression node
      struct AST_STRUCT *body;  // Point to compound node
    }while_statement;

    /* Function call */
    struct {
      char *func; // Point to fuction name (id)
      struct AST_STRUCT **args; // An array constains pointers point to args (exprs)
      int num_arg;
    }function_call;

    struct {
      struct AST_STRUCT *id;
      struct AST_STRUCT *value; // Expression
    }assignment;

  }value;
}ast_t;  // Abstract syntax tree

ast_t *init_ast(int type);
void free_ast(ast_t *ast);
#endif // !AST_H
