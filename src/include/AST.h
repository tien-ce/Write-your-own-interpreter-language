#ifndef AST_H
#define AST_H
#include <stdint.h>
#include <stddef.h>
typedef struct AST_STRUCT {
  enum {
    /* 1. LITERALS & IDENTIFIERS */
    AST_INT_LITERAL,          // 10
    AST_FLOAT_LITERAL,        // 3.14
    AST_STRING_LITERAL,       // "hello"
    AST_IDENTIFIER,           // x, my_var (variable/function reference)

    /* 2. EXPRESSIONS */
    AST_BINARY_EXPR,          // x + y, a == b
    AST_UNARY_EXPR,           // -x, !flag
    AST_ASSIGNMENT_EXPR,      // x = 10
    AST_FUNCTION_CALL,        // foo(a, b)
    AST_ARRAY_ACCESS,         // arr[0]

    /* 3. STATEMENTS */
    AST_COMPOUND,             // Block statement { ... }
    AST_EXPRESSION_STATEMENT, // Standalone expression statement (e.g., x + 1;)
    AST_IF_STATEMENT,         // if (cond) { ... } else { ... }
    AST_WHILE_STATEMENT,      // while (cond) { ... }
    AST_FOR_STATEMENT,        // for (init; cond; post) { ... }
    AST_RETURN_STATEMENT,     // return expr;

    /* 4. DECLARATIONS & DEFINITIONS */
    AST_VARIABLE_DEFINITION,  // int x = 5;
    AST_FUNCTION_DEFINITION,  // int add(int a, int b) { ... }
    AST_PARAM_DECLARATION,    // int a (function parameter)

    /* 5. ROOT / PROGRAM */
    AST_PROGRAM               // Root node containing all file statements
  } ast_t;

  union {
    /* 1. LITERALS and IDENTIFIERS */
    int int_value;
    double float_value;
    char *string_value;
    char *identifier; // Name of variable

    /* 2.EXPRESSIONS */ 
    struct {
        enum op{
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
         }; 
        struct ast_t *left; 
        struct ast_t *right;
    } binary_expr;

    struct {                        // AST_UNARY_EXPR
        int op;                     // Operator type (e.g., '-', '!')
        struct AST_STRUCT* operand;
    } unary_expr;

    struct {                        // AST_ASSIGNMENT_EXPR
        char* name;
        struct AST_STRUCT* value;
    } assignment_expr;

    struct {                        // AST_FUNCTION_CALL
        char* name;
        struct AST_STRUCT** args;
        size_t args_size;
    } function_call;

    struct {                        // AST_ARRAY_ACCESS
        struct AST_STRUCT* array;   // Identifier or expression
        struct AST_STRUCT* index;   // Expression inside []
    } array_access;

    /* 3. STATEMENTS */
    struct {                        // AST_COMPOUND / AST_PROGRAM
        struct AST_STRUCT** statements;
        size_t statements_size;
    } compound;

    struct {                        // AST_EXPRESSION_STATEMENT
        struct AST_STRUCT* expression;
    } expression_statement;

    struct {                        // AST_IF_STATEMENT
        struct AST_STRUCT* condition;
        struct AST_STRUCT* then_branch;
        struct AST_STRUCT* else_branch; // Can be NULL
    } if_statement;

    struct {                        // AST_WHILE_STATEMENT
        struct AST_STRUCT* condition;
        struct AST_STRUCT* body;
    } while_statement;

    struct {                        // AST_FOR_STATEMENT
        struct AST_STRUCT* init;
        struct AST_STRUCT* condition;
        struct AST_STRUCT* post;
        struct AST_STRUCT* body;
    } for_statement;

    struct {                        // AST_RETURN_STATEMENT
        struct AST_STRUCT* value;   // Can be NULL for void returns
    } return_statement;

    /* 4. DECLARATIONS & DEFINITIONS */
    struct {                        // AST_VARIABLE_DEFINITION
        char* type_name;            // "int", "float", etc.
        char* name;
        struct AST_STRUCT* value;   // Can be NULL
    } variable_definition;

    struct {                        // AST_PARAM_DECLARATION
        char* type_name;
        char* name;
    } param_declaration;

    struct {                        // AST_FUNCTION_DEFINITION
        char* return_type;
        char* name;
        struct AST_STRUCT** params;
        size_t params_size;
        struct AST_STRUCT* body;    // Points to AST_COMPOUND
    } function_definition;
  }value;
} ast_t;  // Abstract syntax tree

ast_t *init_ast(int type);
#endif // !AST_H
