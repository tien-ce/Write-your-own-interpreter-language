#ifndef VISITOR_H
#define VISITOR_H

#include "value.h"
#include "context.h"
#include "AST.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Core AST recursive evaluator and dispatcher.
 * @param ctx Pointer to active execution context scope.
 * @param node Pointer to AST node to evaluate.
 * @return Pointer to evaluated result value_t (or NULL).
 */
value_t *visitor_visit(context_t *ctx, ast_t *node);

#ifdef __cplusplus
}
#endif

#endif /* !VISITOR_H */
