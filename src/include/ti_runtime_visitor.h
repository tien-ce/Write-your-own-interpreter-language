#ifndef TI_RUNTIME_VISITOR_H
#define TI_RUNTIME_VISITOR_H

#include "ti_type_ast.h"
#include "ti_type.h"
#include "ti_type_value.h"
#include "ti_runtime_context.h"
#include "ti_type_func.h"
#include "ti_runtime.h"
#include <inttypes.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------- AST Evaluator / Visitor Dispatcher -------------------- */

/**
 * @brief Core AST recursive evaluator and dispatcher.
 * Routes each AST node to its respective statement/expression evaluator.
 *
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context scope.
 * @param node Pointer to AST node to evaluate.
 * @return Pointer to evaluated result value_t (or NULL).
 */
value_t *visitor_visit(ti_runtime_t *rt, context_t *ctx, ast_t *node);

/* -------------------- Expression Evaluators -------------------- */

/**
 * @brief Evaluate a binary expression node (+, -, *, /, ==, <, etc.).
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context scope.
 * @param node Pointer to binary expression AST node.
 * @return Evaluated result value_t.
 */
value_t *eval_binary_expr(ti_runtime_t *rt, context_t *ctx, ast_t *node);

/**
 * @brief Evaluate a unary expression node (!, -, +).
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context scope.
 * @param node Pointer to unary expression AST node.
 * @return Evaluated result value_t.
 */
value_t *eval_unary_expr(ti_runtime_t *rt, context_t *ctx, ast_t *node);

/* -------------------- Statement / Control Flow Evaluators -------------------- */

/**
 * @brief Evaluate an if statement node (including else branch).
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context scope.
 * @param node Pointer to if statement AST node.
 * @return Always NULL.
 */
value_t *eval_if_statement(ti_runtime_t *rt, context_t *ctx, ast_t *node);

/**
 * @brief Evaluate a while loop statement node.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context scope.
 * @param node Pointer to while statement AST node.
 * @return Always NULL.
 */
value_t *eval_while_statement(ti_runtime_t *rt, context_t *ctx, ast_t *node);

/**
 * @brief Evaluate a for loop statement node (stub).
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context scope.
 * @param node Pointer to for statement AST node.
 * @return Always NULL.
 */
value_t *eval_for_statement(ti_runtime_t *rt, context_t *ctx, ast_t *node);

/**
 * @brief Evaluate a compound statement block node.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context scope.
 * @param node Pointer to compound AST node.
 * @return Always NULL.
 */
value_t *eval_compound_statement(ti_runtime_t *rt, context_t *ctx, ast_t *node);

/**
 * @brief Evaluate a return statement node and set FLOW_RETURN in context.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context scope.
 * @param node Pointer to return statement AST node.
 * @return Always NULL.
 */
value_t *eval_return_statement(ti_runtime_t *rt, context_t *ctx, ast_t *node);

/**
 * @brief Evaluate a break statement node and set FLOW_BREAK in context.
 * @param ctx Pointer to active execution context scope.
 * @param node Pointer to break statement AST node.
 * @return Always NULL.
 */
value_t *eval_break_statement(context_t *ctx, ast_t *node);

/**
 * @brief Evaluate a continue statement node and set FLOW_CONTINUE in context.
 * @param ctx Pointer to active execution context scope.
 * @param node Pointer to continue statement AST node.
 * @return Always NULL.
 */
value_t *eval_continue_statement(context_t *ctx, ast_t *node);

/* -------------------- Variable & Identifier Evaluators -------------------- */

/**
 * @brief Evaluate a variable definition node and register into context.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context scope.
 * @param node Variable definition AST node.
 * @return Always NULL.
 */
value_t *eval_variable_definition(ti_runtime_t *rt, context_t *ctx, ast_t *node);

/**
 * @brief Evaluate an assignment statement node.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context scope.
 * @param node Assignment AST node.
 * @return Always NULL.
 */
value_t *eval_assignment(ti_runtime_t *rt, context_t *ctx, ast_t *node);

/**
 * @brief Look up and evaluate an identifier node.
 * @param ctx Pointer to active execution context scope.
 * @param node Identifier AST node.
 * @return Evaluated value_t pointer.
 */
value_t *eval_identifier(context_t *ctx, ast_t *node);
value_t *eval_array_access(ti_runtime_t *rt, context_t *ctx, ast_t *node);

/* -------------------- Literal Evaluators -------------------- */

/**
 * @brief Evaluate a string literal node.
 * @param ctx Pointer to active execution context scope.
 * @param node String literal AST node.
 * @return Newly allocated string value_t.
 */
value_t *eval_string_literal(context_t *ctx, ast_t *node);

/**
 * @brief Evaluate an integer literal node.
 * @param ctx Pointer to active execution context scope.
 * @param node Integer literal AST node.
 * @return Newly allocated integer value_t.
 */
value_t *eval_int_literal(context_t *ctx, ast_t *node);

/**
 * @brief Evaluate a float literal node.
 * @param ctx Pointer to active execution context scope.
 * @param node Float literal AST node.
 * @return Newly allocated float value_t.
 */
value_t *eval_float_literal(context_t *ctx, ast_t *node);

/**
 * @brief Evaluate a boolean literal node.
 * @param ctx Pointer to active execution context scope.
 * @param node Boolean literal AST node.
 * @return Newly allocated boolean value_t.
 */
value_t *eval_boolean_literal(context_t *ctx, ast_t *node);

value_t *eval_dict_literal(context_t *ctx, ast_t *node);
/* -------------------- Function Call & Definition Evaluators -------------------- */

/**
 * @brief Evaluate a function call node and dispatch to native builtin or Ti function.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context scope.
 * @param node Function call AST node.
 * @return Returned value_t from function callback.
 */
value_t *eval_function_call(ti_runtime_t *rt, context_t *ctx, ast_t *node);

/**
 * @brief Evaluate a function definition node and register it into the runtime function table.
 * @param rt Pointer to active runtime instance.
 * @param ctx Pointer to active execution context scope.
 * @param node Function definition AST node.
 * @return Always NULL.
 */
value_t *eval_function_definition(ti_runtime_t *rt, context_t *ctx, ast_t *node);

#ifdef __cplusplus
}
#endif

#endif /* !TI_RUNTIME_VISITOR_H */
