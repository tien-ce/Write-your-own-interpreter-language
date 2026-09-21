#ifndef DEBUG_H
#define DEBUG_H

#include "ti_type_ast.h"

/* -------------------- Public String Conversion & Debug API -------------------- */

/**
 * @brief Convert token type enum to readable string name (e.g. "TOKEN_INT").
 * @param type Token type enum value.
 * @return Static string constant name.
 */
const char *token_type_to_str(int type);

/**
 * @brief Convert AST node type enum to readable string name.
 * @param type AST node type enum value.
 * @return Static string constant name.
 */
const char *ast_type_to_str(int type);

/**
 * @brief Convert binary operator enum to string symbol (e.g. "+", "==").
 * @param op Binary operator enum value.
 * @return Operator symbol string.
 */
const char *binary_op_to_str(int op);

/**
 * @brief Convert unary operator enum to string symbol (e.g. "!", "-").
 * @param op Unary operator enum value.
 * @return Operator symbol string.
 */
const char *unary_op_to_str(int op);

/**
 * @brief Convert value/variable type enum to type keyword string (e.g. "int").
 * @param type Value/variable type enum value.
 * @return Type keyword string.
 */
const char *val_type_to_str(val_type_t type);

/**
 * @brief Convert variable type enum to type keyword string (e.g. "int").
 * @param type Variable type enum value.
 * @return Type keyword string.
 */
const char *var_type_to_str(int type);

/**
 * @brief Render an ASCII tree representation of an AST hierarchy to stdout.
 * @param root Root AST node to draw.
 */
void ast_draw(ast_t *root);

#endif /* !DEBUG_H */
