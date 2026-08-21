#ifndef DEBUG_H
#define DEBUG_H

/* -------------------- Public String Conversion API -------------------- */

/**
 * @brief Convert token type enum to readable string name.
 */
const char *token_type_to_str(int type);

/**
 * @brief Convert AST node type enum to readable string name.
 */
const char *ast_type_to_str(int type);

/**
 * @brief Convert binary operator enum to string symbol (e.g. "+", "==").
 */
const char *binary_op_to_str(int op);

/**
 * @brief Convert unary operator enum to string symbol (e.g. "!", "-").
 */
const char *unary_op_to_str(int op);

/**
 * @brief Convert variable type enum to type keyword string (e.g. "int").
 */
const char *var_type_to_str(int type);

#endif // !DEBUG_H
